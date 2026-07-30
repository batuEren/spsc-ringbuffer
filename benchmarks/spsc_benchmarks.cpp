#include "SpscRingBuffer.hpp"

#include <benchmark/benchmark.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

#if SPSC_HAS_BOOST
#include <boost/lockfree/spsc_queue.hpp>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace {

using Value = std::uint64_t;

// Two distinct physical cores on an SMT machine (logical ids are interleaved,
// so 2 and 4 are siblings of different cores). Left unpinned, the scheduler is
// free to co-locate the threads on one core's sibling hyperthreads — which
// share L1 and make every queue look fast — or to migrate them mid-run. That
// placement noise is larger than the gap between the two implementations.
constexpr unsigned kProducerCpu = 2;
constexpr unsigned kConsumerCpu = 4;

void pin_to_cpu(unsigned cpu) {
    const unsigned n = std::thread::hardware_concurrency();
    if (n != 0) {
        cpu %= n;
    }
#if defined(_WIN32)
    SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(1) << cpu);
#elif defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#else
    (void)cpu;
#endif
}

// Adapters give both queues the same try_push/try_pop shape, so the harness
// below compiles to the same code for each and the implementation is the only
// variable.
struct Mine {
    explicit Mine(std::size_t capacity) : q(capacity) {}
    bool try_push(Value v) noexcept { return q.try_push(v); }
    bool try_pop(Value& v) noexcept { return q.try_pop(v); }

    SpscRingBuffer<Value> q;
};

#if SPSC_HAS_BOOST
// Same usable capacity as ours: boost allocates capacity+1 slots internally
// and keeps one empty to distinguish full from empty, so both queues hold
// exactly `capacity` elements for a given argument.
struct Boost {
    explicit Boost(std::size_t capacity) : q(capacity) {}
    bool try_push(Value v) noexcept { return q.push(v); }
    bool try_pop(Value& v) noexcept { return q.pop(v); }

    boost::lockfree::spsc_queue<Value> q;
};
#endif

template <typename Queue>
void BM_Throughput(benchmark::State& state) {
    const std::size_t N        = static_cast<std::size_t>(state.range(0));
    const std::size_t capacity = static_cast<std::size_t>(state.range(1));

    for (auto _ : state) {
        Queue q(capacity);

        std::atomic<bool> start_flag{false};
        std::atomic<bool> ready_producer{false};
        std::atomic<bool> ready_consumer{false};
        std::uint64_t order_errors = 1; // overwritten by the consumer

        std::thread producer([&] {
            pin_to_cpu(kProducerCpu);
            ready_producer.store(true, std::memory_order_release);
            while (!start_flag.load(std::memory_order_acquire)) {}

            std::uint64_t value = 0;
            while (value < N) {
                if (q.try_push(value)) {
                    ++value;
                }
            }
        });

        std::thread consumer([&] {
            pin_to_cpu(kConsumerCpu);
            ready_consumer.store(true, std::memory_order_release);
            while (!start_flag.load(std::memory_order_acquire)) {}

            // values arrive as 0,1,2,... — checking them both verifies FIFO
            // order and forces the copy out of the ring to actually happen
            std::uint64_t errors = 0;
            std::uint64_t value = 0;
            std::size_t popped = 0;
            while (popped < N) {
                if (q.try_pop(value)) {
                    errors += (value != popped);
                    ++popped;
                }
            }
            order_errors = errors;
        });

        while (!ready_producer.load(std::memory_order_acquire)
               || !ready_consumer.load(std::memory_order_acquire)) {}

        // time only the transfer, not thread creation/teardown
        const auto t0 = std::chrono::steady_clock::now();
        start_flag.store(true, std::memory_order_release);
        producer.join();
        consumer.join();
        const auto t1 = std::chrono::steady_clock::now();

        state.SetIterationTime(std::chrono::duration<double>(t1 - t0).count());

        if (order_errors != 0) {
            state.SkipWithError("FIFO order violated");
            break;
        }
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(N));
}

constexpr int64_t kItems = 10'000'000;

// The capacity sweep isolates the one real difference between the two queues.
// boost::lockfree::spsc_queue reloads the peer's index on every push and pop,
// so it pays a coherence miss per item and lands near the same rate whatever
// the capacity. This queue only reloads when it actually runs full or empty,
// so a larger ring means fewer forced reloads and a widening gap.
void configure(benchmark::internal::Benchmark* b) {
    b->ArgNames({"items", "capacity"})
     ->Args({kItems, 1 << 8})
     ->Args({kItems, 1 << 12})
     ->Args({kItems, 1 << 16})
     ->UseManualTime()
     ->Unit(benchmark::kMillisecond);
}

} // namespace

BENCHMARK_TEMPLATE(BM_Throughput, Mine)->Name("SpscRingBuffer")->Apply(configure);

#if SPSC_HAS_BOOST
BENCHMARK_TEMPLATE(BM_Throughput, Boost)->Name("boost::lockfree::spsc_queue")->Apply(configure);
#endif

BENCHMARK_MAIN();
