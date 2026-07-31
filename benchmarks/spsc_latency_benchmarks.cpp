// Latency counterpart to spsc_benchmarks.cpp.
//
// Throughput asks how fast the queue drains a backlog; latency asks how long
// one item takes to cross when there is no backlog at all. They are different
// questions and they stress different code: the throughput harness keeps the
// ring full, this one keeps it empty, so every pop here misses and has to go
// look at the producer's index. The cached-peer-index trick that wins the
// throughput benchmark does nothing on an empty ring — both queues reload the
// shared line on every single hop — so this is the workload where the two are
// expected to land close together.
//
// Measured as a ping-pong round trip over two queues: ping stamps the clock,
// pushes a token, spins for the echo, stamps again. Both stamps are taken on
// the same thread, so nothing depends on the two cores' clocks agreeing. One-way
// latency is about half the reported round trip.

#include "bench_common.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#define SPSC_HAS_RDTSC 1
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#define SPSC_HAS_RDTSC 1
#endif

namespace {

using bench::Value;

// ---------------------------------------------------------------- timestamps

// A round trip is tens of nanoseconds, and steady_clock resolves to ~100 ns on
// Windows (QPC ticks at 10 MHz), so per-sample steady_clock readings would be
// mostly quantisation. Samples are taken with rdtsc instead — invariant on any
// x86 of the last decade, i.e. ticking at a fixed rate regardless of turbo or
// C-state — and steady_clock is consulted once, over a 200 ms window, purely to
// convert ticks into nanoseconds. Elsewhere this falls back to steady_clock; if
// that clock is coarse on the target, read the mean and ignore the percentiles.
inline std::uint64_t now_ticks() noexcept {
#if SPSC_HAS_RDTSC
    // rdtsc is not ordered against surrounding loads, so without the fences the
    // reads can drift into or out of the region they are supposed to bracket.
    _mm_lfence();
    const std::uint64_t t = __rdtsc();
    _mm_lfence();
    return t;
#else
    return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
#endif
}

double ns_per_tick() {
    static const double value = [] {
#if SPSC_HAS_RDTSC
        using clock = std::chrono::steady_clock;
        // spin rather than sleep: a sleep can be charged to another core and
        // the wake-up latency lands inside the calibration window
        const auto wall_begin  = clock::now();
        const std::uint64_t t0 = now_ticks();
        while (clock::now() - wall_begin < std::chrono::milliseconds(200)) {
        }
        const std::uint64_t t1 = now_ticks();
        const auto wall_end    = clock::now();

        const double ns = std::chrono::duration<double, std::nano>(wall_end - wall_begin).count();
        return ns / static_cast<double>(t1 - t0);
#else
        return 1.0; // the fallback already counts in nanoseconds
#endif
    }();
    return value;
}

// Cost of the two now_ticks() calls bracketing a sample — the floor of what
// this harness can resolve. Reported next to the results so a 60 ns round trip
// can be read against its own measurement error instead of taken at face value.
double timer_overhead_ticks() {
    static const double value = [] {
        constexpr int kTrials = 4096;
        std::vector<std::uint64_t> deltas(kTrials);
        for (int i = 0; i < kTrials; ++i) {
            const std::uint64_t a = now_ticks();
            const std::uint64_t b = now_ticks();
            deltas[static_cast<std::size_t>(i)] = b - a;
        }
        std::sort(deltas.begin(), deltas.end());
        return static_cast<double>(deltas[kTrials / 2]);
    }();
    return value;
}

void spin_ticks(std::uint64_t ticks) noexcept {
    const std::uint64_t deadline = now_ticks() + ticks;
    while (now_ticks() < deadline) {
    }
}

// nearest-rank, on an already sorted sample set
std::uint64_t percentile(const std::vector<std::uint64_t>& sorted, double p) {
    if (sorted.empty()) {
        return 0;
    }
    const auto rank = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1) + 0.5);
    return sorted[rank];
}

// ---------------------------------------------------------------- the harness

// Only ever one token in flight, so capacity is irrelevant to what is measured;
// it just needs to be big enough that neither side can observe a full ring.
constexpr std::size_t kCapacity = 1024;

// Untimed round trips before recording, to fault in both rings and let the two
// spin loops reach steady state.
constexpr int kWarmupRoundTrips = 50'000;

// Fixed sample count instead of a min-time target: percentiles are only
// comparable across rows if every row is a percentile of the same n, and it
// keeps the pre-sized sample buffer honest.
constexpr int64_t kSamples = 200'000;

template <typename Queue>
void BM_RoundTrip(benchmark::State& state) {
    const double tick_ns = ns_per_tick(); // calibrate before anything is timed
    const auto idle_ticks =
            static_cast<std::uint64_t>(static_cast<double>(state.range(0)) / tick_ns);

    Queue to_pong(kCapacity);
    Queue to_ping(kCapacity);

    std::atomic<bool> stop{false};
    std::atomic<bool> pong_ready{false};

    // The echo thread: pure poll, exactly what a latency-sensitive consumer
    // does. Note it is spinning on the shared index the whole time, including
    // while ping is idling between round trips.
    std::thread pong([&] {
        bench::pin_to_cpu(bench::kConsumerCpu);
        pong_ready.store(true, std::memory_order_release);

        Value v = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (to_pong.try_pop(v)) {
                while (!to_ping.try_push(v)) {
                }
            }
        }
    });

    bench::pin_to_cpu(bench::kProducerCpu);
    while (!pong_ready.load(std::memory_order_acquire)) {
    }

    Value token              = 0;
    std::uint64_t mismatches = 0;

    // returns the round trip in ticks; checking the echo also stops the copy
    // out of the ring from being optimised away
    auto round_trip = [&]() noexcept -> std::uint64_t {
        const std::uint64_t t0 = now_ticks();
        while (!to_pong.try_push(token)) {
        }
        Value echo = 0;
        while (!to_ping.try_pop(echo)) {
        }
        const std::uint64_t t1 = now_ticks();

        mismatches += (echo != token);
        ++token;
        return t1 - t0;
    };

    for (int i = 0; i < kWarmupRoundTrips; ++i) {
        round_trip();
    }

    std::vector<std::uint64_t> samples;
    samples.reserve(static_cast<std::size_t>(state.max_iterations));

    for (auto _ : state) {
        const std::uint64_t ticks = round_trip();
        // recorded outside the stamped region, so the bookkeeping is not timed
        samples.push_back(ticks);
        state.SetIterationTime(static_cast<double>(ticks) * tick_ns * 1e-9);

        if (idle_ticks != 0) {
            spin_ticks(idle_ticks);
        }
    }

    stop.store(true, std::memory_order_relaxed);
    pong.join();

    if (mismatches != 0) {
        state.SkipWithError("round-trip payload mismatch");
        return;
    }
    if (samples.empty()) {
        return;
    }

    // The mean is what google-benchmark prints as Time; for a latency
    // distribution the tail is the part that decides whether a system meets its
    // deadline, so the percentiles are the interesting columns. max_ns is the
    // exception — at hundreds of microseconds it is the scheduler preempting a
    // pinned thread, which says nothing about either queue.
    std::sort(samples.begin(), samples.end());
    state.counters["p50_ns"]   = static_cast<double>(percentile(samples, 0.50)) * tick_ns;
    state.counters["p99_ns"]   = static_cast<double>(percentile(samples, 0.99)) * tick_ns;
    state.counters["p999_ns"]  = static_cast<double>(percentile(samples, 0.999)) * tick_ns;
    state.counters["max_ns"]   = static_cast<double>(samples.back()) * tick_ns;
    state.counters["timer_ns"] = timer_overhead_ticks() * tick_ns;
}

// The idle sweep is to latency what the capacity sweep is to throughput. At 0
// the two threads hand off back to back and every predictor and cache line is
// hot — the best case, and the one most benchmarks stop at. Pausing between
// round trips is closer to how a real feed behaves and shows whether the queue
// stays predictable when the hand-off is not already in flight.
void configure(benchmark::internal::Benchmark* b) {
    b->ArgName("idle_ns")
     ->Arg(0)
     ->Arg(1'000)
     ->Arg(10'000)
     ->Iterations(kSamples)
     ->UseManualTime()
     ->Unit(benchmark::kNanosecond);
}

} // namespace

BENCHMARK_TEMPLATE(BM_RoundTrip, bench::Mine)->Name("SpscRingBuffer")->Apply(configure);

#if SPSC_HAS_BOOST
BENCHMARK_TEMPLATE(BM_RoundTrip, bench::Boost)->Name("boost::lockfree::spsc_queue")->Apply(configure);
#endif

BENCHMARK_MAIN();
