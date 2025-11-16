# Lock-Free SPSC Ring Buffer (C++)

A wait-free **single-producer, single-consumer ring buffer** implemented in modern C++ with a focus on **predictable low-latency**, **cache efficiency**, and **minimal synchronization overhead**.  
The design is *loosely based on* the ideas from **[Single Producer Single Consumer Lock-free FIFO From the Ground Up – Charles Frasch (CppCon 2023)](https://www.youtube.com/watch?v=K3P_Lmq6pw0)**.

## Features & Optimizations

- **Wait-free SPSC design** with strictly bounded O(1) push/pop operations.
- **Cache-line padding** to eliminate false sharing between producer and consumer indices.
- **Power-of-two capacity** with fast bitmask indexing instead of modulo.
- **Allocator-aware memory management** via `std::allocator_traits`.
- **Optimized atomic operations** using `memory_order_relaxed` and acquire/release where appropriate.
- **Predictable performance** with no dynamic allocations or locks in the hot path.

## Benchmarks

Throughput benchmarking is included, and **comparative tests against `boost::lockfree::spsc_queue` will be added soon**.
