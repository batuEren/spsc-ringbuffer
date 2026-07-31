// Latency counterpart to spsc_throughput_benchmark.cpp.
//
// Throughput asks how fast the queue drains a backlog; latency asks how long
// one item takes to cross when there is no backlog at all. They are different
// questions and they stress different code: the throughput harness keeps the
// ring full, this one keeps it empty, so every pop here misses and has to go
// look at the producer's index.
//
// Prediction, stated up front so the result either confirms it or flags a
// surprise: the empty ring defeats the cached-index trick on the pop side
// only — there both queues reload the producer's index on every attempt. The
// push side still differs. boost::lockfree::spsc_queue loads read_index_ on
// every push to test fullness, and the consumer has advanced that index since
// the previous push, so that load is a coherence miss every single time. Ours
// tests fullness against cached_tail_, which with one token in flight is only
// refreshed once per kCapacity pushes, so 1023 pushes out of 1024 skip the
// shared load entirely. A round trip contains two pushes, one per direction,
// both inside the stamped window — so the expectation is not a tie but ours
// ahead by roughly two cache-line transfers per round trip. A tie would mean
// the transfer is hidden by something else, and would itself need explaining.
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

// For the cold rows, a short second warmup with the eviction walk in place, so
// the recorded loop starts in its steady trip-then-walk rhythm without paying
// the walk 50'000 times.
constexpr int kEvictWarmupRoundTrips = 256;

// Fixed sample count instead of a min-time target: percentiles are only
// comparable across rows if every row is a percentile of the same n, and it
// keeps the pre-sized sample buffer honest. 100k rather than more because the
// cold rows pay a scratch walk per sample and the whole table runs 9 times;
// each repetition still puts 100 samples beyond its p999.
constexpr int64_t kSamples = 100'000;

template <typename Queue>
void BM_RoundTrip(benchmark::State& state) {
    if (!bench::require_cross_core_pair(state)) {
        return;
    }

    const double tick_ns = ns_per_tick(); // calibrate before anything is timed

    // Scratch for the cold rows, sized by the row argument and filled here so
    // its pages are faulted in before anything is recorded. One read per cache
    // line displaces the line; the running sum keeps the loads alive.
    constexpr std::size_t kWordsPerLine = 64 / sizeof(std::uint64_t);
    std::vector<std::uint64_t> scratch(
            static_cast<std::size_t>(state.range(0)) * 1024 / sizeof(std::uint64_t), 1);
    std::uint64_t evict_sink = 0;
    auto evict = [&]() noexcept {
        for (std::size_t i = 0; i < scratch.size(); i += kWordsPerLine) {
            evict_sink += scratch[i];
        }
    };

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

    // Sized then cleared, not reserved: writing the zeroes first-touches every
    // page here, so no push_back below takes a page fault mid-run. reserve()
    // alone leaves the pages unmapped and the faults land between stamped
    // regions, where they can drag a scheduling event into the tail.
    std::vector<std::uint64_t> samples(static_cast<std::size_t>(state.max_iterations), 0);
    samples.clear();

    for (int i = 0; i < kWarmupRoundTrips; ++i) {
        round_trip();
    }
    if (!scratch.empty()) {
        for (int i = 0; i < kEvictWarmupRoundTrips; ++i) {
            round_trip();
            evict();
        }
    }

    for (auto _ : state) {
        const std::uint64_t ticks = round_trip();
        // recorded outside the stamped region, so the bookkeeping is not timed
        samples.push_back(ticks);
        state.SetIterationTime(static_cast<double>(ticks) * tick_ns * 1e-9);

        if (!scratch.empty()) {
            evict();
        }
    }

    stop.store(true, std::memory_order_relaxed);
    pong.join();
    benchmark::DoNotOptimize(evict_sink);

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

// The cold sweep is to latency what the capacity sweep is to throughput. At 0
// the two threads hand off back to back and every predictor and cache line is
// hot — the best case, and the one most benchmarks stop at. A real producer
// does other work between events, and that work evicts the queue's lines.
//
// Note that merely *waiting* between round trips cannot produce that state:
// with both threads spinning through the pause, nothing leaves L1 and no core
// drops frequency, so a timed-idle sweep just measures the hot case again at
// three times the runtime. Cold has to be manufactured — between round trips
// ping walks `evict_kb` kilobytes of scratch (outside the stamped region),
// displacing its copies of the ring, the indices, and their TLB entries.
// 256 KB clears L1; 4 MB, twice a typical L2, sends the producer-side lines
// back to L3 or memory. Pong keeps polling and stays warm throughout — the
// scenario is a consumer waiting hot while the producer arrives cold with the
// event, which is the shape of an event-driven system between events. The 4 MB
// walk is what bounds this binary's runtime: roughly 100–200 us per trip, a
// couple of minutes per queue across the repetitions.
//
// 9 repetitions (odd, so the median is an observation) put spread across the
// whole table; interleaving — see main — decides what that spread is charged to.
void configure(benchmark::internal::Benchmark* b) {
    b->ArgName("evict_kb")
     ->Arg(0)
     ->Arg(256)
     ->Arg(4096)
     ->Iterations(kSamples)
     ->Repetitions(9)
     // console shows mean/median/stddev/cv over the repetitions only;
     // --benchmark_out still records every individual repetition
     ->DisplayAggregatesOnly(true)
     ->UseManualTime()
     ->Unit(benchmark::kNanosecond);
}

} // namespace

BENCHMARK_TEMPLATE(BM_RoundTrip, bench::Mine)->Name("SpscRingBuffer")->Apply(configure);

#if SPSC_HAS_BOOST
BENCHMARK_TEMPLATE(BM_RoundTrip, bench::Boost)->Name("boost::lockfree::spsc_queue")->Apply(configure);
#endif

// Not BENCHMARK_MAIN(): by default google-benchmark finishes every repetition
// of one benchmark before starting the next, so thermal and turbo drift over
// the run lands entirely on whichever queue ran second and is confounded with
// queue identity. Random interleaving shuffles all repetitions together so
// drift is charged to both queues equally. The flag is injected ahead of the
// real argv, so an explicit --benchmark_enable_random_interleaving=false on
// the command line still wins.
int main(int argc, char** argv) {
    std::vector<char*> args;
    args.reserve(static_cast<std::size_t>(argc) + 1);
    args.push_back(argv[0]);
    static char interleave[] = "--benchmark_enable_random_interleaving=true";
    args.push_back(interleave);
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    int argn = static_cast<int>(args.size());
    benchmark::Initialize(&argn, args.data());
    if (benchmark::ReportUnrecognizedArguments(argn, args.data())) {
        return 1;
    }

    bench::add_cpu_pair_context();

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
