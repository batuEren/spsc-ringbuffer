#include "SpscRingBuffer.hpp"

#include <chrono>
#include <iostream>
#include <thread>
#include <atomic>

using Clock = std::chrono::steady_clock;

void basic_correctness_test() {
    SpscRingBuffer<int> q(8);

    int x = 0;

    // empty pop
    if (q.try_pop(x)) {
        std::cerr << "Error: pop from empty succeeded\n";
    }

    // push some items
    for (int i = 0; i < 4; ++i) {
        if (!q.try_push(i)) {
            std::cerr << "Error: push failed at " << i << "\n";
        }
    }

    // pop them back
    for (int i = 0; i < 4; ++i) {
        if (!q.try_pop(x)) {
            std::cerr << "Error: pop failed at " << i << "\n";
        } else if (x != i) {
            std::cerr << "Error: expected " << i << " got " << x << "\n";
        }
    }

    std::cout << "Basic correctness test done.\n";
}

double benchmark_spsc(std::size_t N, std::size_t capacity) {
    SpscRingBuffer<std::uint64_t> q(capacity);

    std::atomic<bool> start_flag{false};
    std::atomic<bool> ready_producer{false};
    std::atomic<bool> ready_consumer{false};

    std::thread producer([&] {
        ready_producer.store(true, std::memory_order_release);
        while (!start_flag.load(std::memory_order_acquire)) {
            // spin
        }

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
        while (!start_flag.load(std::memory_order_acquire)) {
            // spin
        }

        std::uint64_t value;
        std::size_t popped = 0;
        while (popped < N) {
            if (q.try_pop(value)) {
                ++popped;
            }
        }
    });

    // wait for both threads to be ready
    while (!ready_producer.load(std::memory_order_acquire)
           || !ready_consumer.load(std::memory_order_acquire)) {
    }

    auto t0 = Clock::now();
    start_flag.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    auto t1 = Clock::now();
    std::chrono::duration<double> elapsed = t1 - t0;
    return static_cast<double>(N) / elapsed.count(); // ops per second
}

int main() {
    basic_correctness_test();

    const std::size_t N = 10'000'000;       // 10M operations
    const std::size_t capacity = 1 << 16;   // 65536 entries

    double ops_per_sec = benchmark_spsc(N, capacity);
    std::cout << "Throughput: " << (ops_per_sec / 1e6) << " M ops/s\n";

    return 0;
}
