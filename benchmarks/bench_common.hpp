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
#include <vector>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <fstream>
#include <string>
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

// The id a thread actually lands on: the constants above wrap on machines with
// fewer logical CPUs, and on a small enough machine both can fold onto the
// same one. Exposed so a harness can report — or refuse — the pair it got.
inline unsigned effective_cpu(unsigned cpu) {
    const unsigned n = std::thread::hardware_concurrency();
    return n != 0 ? cpu % n : cpu;
}

inline void pin_to_cpu(unsigned cpu) {
    cpu = effective_cpu(cpu);
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

// Whether two logical CPUs are SMT siblings of one physical core: 1 yes,
// 0 no, -1 can't tell. Siblings share L1 and the store buffers, so a hand-off
// between them never crosses the core boundary — any result taken on such a
// pair is a measurement of the core, not of the queue. The comment on the
// constants above assumes ids 2 and 4 are distinct cores; this checks the
// assumption against the actual topology instead of trusting the enumeration
// order.
inline int cpu_pair_shares_core(unsigned a, unsigned b) {
#if defined(_WIN32)
    // ids past the width of one group-relative affinity mask belong to another
    // processor group, which is outside what pin_to_cpu handles anyway
    constexpr unsigned kMaskBits = sizeof(KAFFINITY) * 8;
    if (a >= kMaskBits || b >= kMaskBits) {
        return -1;
    }
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0) {
        return -1;
    }
    std::vector<unsigned char> buf(len);
    auto* base = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data());
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, base, &len)) {
        return -1;
    }
    int core_a = -1;
    int core_b = -1;
    int core   = 0;
    for (DWORD off = 0; off < len; ++core) {
        const auto* info =
                reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + off);
        const KAFFINITY mask = info->Processor.GroupMask[0].Mask;
        if ((mask >> a) & 1u) {
            core_a = core;
        }
        if ((mask >> b) & 1u) {
            core_b = core;
        }
        off += info->Size;
    }
    if (core_a < 0 || core_b < 0) {
        return -1;
    }
    return core_a == core_b ? 1 : 0;
#elif defined(__linux__)
    const auto topology_id = [](unsigned cpu, const char* leaf) -> long {
        std::ifstream f("/sys/devices/system/cpu/cpu" + std::to_string(cpu)
                        + "/topology/" + leaf);
        long v = -1;
        f >> v;
        return f ? v : -1;
    };
    const long pkg_a  = topology_id(a, "physical_package_id");
    const long pkg_b  = topology_id(b, "physical_package_id");
    const long core_a = topology_id(a, "core_id");
    const long core_b = topology_id(b, "core_id");
    if (pkg_a < 0 || pkg_b < 0 || core_a < 0 || core_b < 0) {
        return -1;
    }
    return (pkg_a == pkg_b && core_a == core_b) ? 1 : 0;
#else
    (void)a;
    (void)b;
    return -1;
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
