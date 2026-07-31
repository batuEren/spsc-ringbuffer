#include "bench_common.hpp"

#include <benchmark/benchmark.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace {

using bench::Value;

template <typename Queue>
void BM_Throughput(benchmark::State& state) {
    if (!bench::require_cross_core_pair(state)) {
        return;
    }

    const std::size_t N        = static_cast<std::size_t>(state.range(0));
    const std::size_t capacity = static_cast<std::size_t>(state.range(1));

    for (auto _ : state) {
        Queue q(capacity);

        std::atomic<bool> start_flag{false};
        std::atomic<bool> ready_producer{false};
        std::atomic<bool> ready_consumer{false};
        std::uint64_t order_errors = 1; // overwritten by the consumer

        std::thread producer([&] {
            bench::pin_to_cpu(bench::kProducerCpu);
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
            bench::pin_to_cpu(bench::kConsumerCpu);
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

// Two spinning threads handing off 10M items is a noisy workload: the pair can
// settle into batched hand-off or into item-at-a-time lockstep depending on how
// the loops happen to line up at startup, and a single unlabelled mean says
// nothing about which one you got. So each repetition is a full second of
// transfers, and the run is a sample of kRepetitions of those seconds — enough
// to quote a median and a spread rather than one number.
//
// An odd count so the median is an actual observation rather than the midpoint
// of two. Budget ~1 s per repetition per row: six rows, so a shade over 90 s.
constexpr double kSecondsPerRepetition = 1.0;
constexpr int    kRepetitions          = 15;

// Runs once per row, before the first recorded repetition, so page faults on a
// fresh ring and the spin loops reaching steady state land outside the sample
// set instead of skewing repetition 0.
constexpr double kWarmUpSeconds = 0.5;

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
     ->MinWarmUpTime(kWarmUpSeconds)
     ->MinTime(kSecondsPerRepetition)
     ->Repetitions(kRepetitions)
     // console shows the mean/median/stddev/cv rows only; --benchmark_out still
     // records every individual repetition for anyone who wants the distribution
     ->DisplayAggregatesOnly(true)
     ->Unit(benchmark::kMillisecond);
}

} // namespace

BENCHMARK_TEMPLATE(BM_Throughput, bench::Mine)->Name("SpscRingBuffer")->Apply(configure);

#if SPSC_HAS_BOOST
BENCHMARK_TEMPLATE(BM_Throughput, bench::Boost)->Name("boost::lockfree::spsc_queue")->Apply(configure);
#endif

// Not BENCHMARK_MAIN(): the run's CPU pair is recorded next to the numbers,
// because throughput across a socket boundary and throughput inside one CCX
// are not the same experiment.
int main(int argc, char** argv) {
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }

    bench::add_cpu_pair_context();

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
