# Lock-Free SPSC Ring Buffer (C++)

A wait-free **single-producer, single-consumer ring buffer** implemented in modern C++ with a focus on **predictable low-latency**, **cache efficiency**, and **minimal synchronization overhead**.  
The design is *loosely based on* the ideas from **Single Producer Single Consumer Lock-free FIFO From the Ground Up – Charles Frasch (CppCon 2023)**.

## Features & Optimizations

- **Wait-free SPSC design** with strictly bounded O(1) push/pop operations.
- **Cache-line padding** to eliminate false sharing between producer and consumer indices.
- **Power-of-two capacity** with fast bitmask indexing instead of modulo.
- **Allocator-aware memory management** via `std::allocator_traits`.
- **Optimized atomic operations** using `memory_order_relaxed` and acquire/release where appropriate.
- **Predictable performance** with no dynamic allocations or locks in the hot path.
- **Outperforms `boost::lockfree::spsc_queue`** in my synthetic benchmark on my hardware (see below).

## Benchmarks

The repository includes a simple throughput benchmark comparing this implementation against `boost::lockfree::spsc_queue`.

On my development machine (single producer thread + single consumer thread, tight loop, `-O3`, 64KB capacity):

- `spsc_ring_buffer<T>`: **~8.5 million ops/second**
- `boost::lockfree::spsc_queue<T>`: **~5.8 million ops/second**

Here, one operation is defined as **one push + one pop pair**.

>These numbers are **hardware- and workload-dependent**. They should be treated as indicative rather than absolute. If you try it on your own machine, please share results / open an issue.