#include "SpscRingBuffer.hpp"

#include <benchmark/benchmark.h>
#include <atomic>
#include <thread>
#include <cstdint>

static void BM_SpscThroughput(benchmark::State& state) {
    const std::size_t capacity = 1 << 16;
    const std::size_t N = state.range(0);

    for (auto _ : state) {
        SpscRingBuffer<std::uint64_t> q(capacity);

        std::atomic<bool> start_flag{false};
        std::atomic<bool> ready_producer{false};
        std::atomic<bool> ready_consumer{false};

        std::thread producer([&] {
            ready_producer.store(true, std::memory_order_release);
            while (!start_flag.load(std::memory_order_acquire)) {}

            std::uint64_t value = 0;
            std::size_t pushed = 0;
            while (pushed < N) {
                if (q.try_push(value)) {
                    ++value;
                    ++pushed;
                }
            }
        });

        std::thread consumer([&] {
            ready_consumer.store(true, std::memory_order_release);
            while (!start_flag.load(std::memory_order_acquire)) {}

            std::uint64_t value;
            std::size_t popped = 0;
            while (popped < N) {
                if (q.try_pop(value)) {
                    ++popped;
                }
            }
        });

        while (!ready_producer.load(std::memory_order_acquire)
               || !ready_consumer.load(std::memory_order_acquire)) {}

        start_flag.store(true, std::memory_order_release);
        producer.join();
        consumer.join();

    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(N));
}

BENCHMARK(BM_SpscThroughput)->Arg(10'000'000)->UseRealTime(); // Using wall time ins of CPU time, as using CPU time gives 0

BENCHMARK_MAIN();
