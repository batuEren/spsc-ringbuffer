#pragma once

// Scaffolding shared by the benchmark binaries: thread pinning and the adapters
// that give both queues one interface. Keeping it in one place means the
// throughput and latency harnesses drive literally the same two objects.

#include "SpscRingBuffer.hpp"

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

namespace bench {

using Value = std::uint64_t;

// Two distinct physical cores on an SMT machine (logical ids are interleaved,
// so 2 and 4 are siblings of different cores). Left unpinned, the scheduler is
// free to co-locate the threads on one core's sibling hyperthreads — which
// share L1 and make every queue look fast — or to migrate them mid-run. That
// placement noise is larger than the gap between the two implementations.
constexpr unsigned kProducerCpu = 2;
constexpr unsigned kConsumerCpu = 4;

inline void pin_to_cpu(unsigned cpu) {
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

// Adapters give both queues the same try_push/try_pop shape, so the harnesses
// compile to the same code for each and the implementation is the only variable.
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

} // namespace bench
