#pragma once

#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>

template<typename T, typename Alloc = std::allocator<T>>
class SpscRingBuffer : private Alloc {
public:
    using value_type        = T;
    using allocator_type    = Alloc;
    using allocator_traits  = std::allocator_traits<allocator_type>;
    using size_type         = typename allocator_traits::size_type;

    static_assert(std::is_trivially_copyable_v<T>,
                  "SpscRingBuffer requires trivially copyable T");

    explicit SpscRingBuffer(size_type capacity,
                            allocator_type const& alloc = allocator_type{})
            : allocator_type{alloc}
            , capacity_{round_up(validate(capacity))}
            , mask_{capacity_ - 1}
            , ring_{allocator_traits::allocate(*this, capacity_)}
    {
    }

    ~SpscRingBuffer() {
        // no cleanup needed: T is trivially copyable, so trivially destructible
        allocator_traits::deallocate(*this, ring_, capacity_);
    }

    SpscRingBuffer(const SpscRingBuffer&)            = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    SpscRingBuffer(SpscRingBuffer&&)            = delete;
    SpscRingBuffer& operator=(SpscRingBuffer&&) = delete;

    size_type size() const noexcept {
        auto head = head_.load(std::memory_order_acquire);
        auto tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }

    size_type capacity() const noexcept { return capacity_; }

    bool empty() const noexcept {
        auto tail = tail_.load(std::memory_order_acquire);
        auto head = head_.load(std::memory_order_acquire);
        return head == tail;
    }

    bool full() const noexcept {
        auto head = head_.load(std::memory_order_acquire);
        auto tail = tail_.load(std::memory_order_acquire);
        return (head - tail) == capacity_;
    }

    // producer thread only
    bool try_push(const T& item) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);

        // cached_tail_ only ever lags the real tail_, so "looks full" against
        // the cache is conservative: refresh once, then decide for real.
        if ((head - cached_tail_) == capacity_) {
            cached_tail_ = tail_.load(std::memory_order_acquire); // see what consumer has done
            if ((head - cached_tail_) == capacity_) [[unlikely]] {
                return false;
            }
        }

        ring_[head & mask_] = item; // trivially copyable assumption
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // consumer thread only
    bool try_pop(T& out) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);

        // cached_head_ only ever lags the real head_, so "looks empty" against
        // the cache is conservative: refresh once, then decide for real.
        if (tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire); // see what producer has done
            if (tail == cached_head_) [[unlikely]] {
                return false;
            }
        }

        out = ring_[tail & mask_];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

private:
    static size_type validate(size_type x) {
        if (x < 2) {
            throw std::invalid_argument("capacity must be at least 2");
        }
        // round_up must not overflow past the top bit
        constexpr size_type max_capacity =
                size_type{1} << (std::numeric_limits<size_type>::digits - 1);
        if (x > max_capacity) {
            throw std::invalid_argument("capacity too large");
        }
        return x;
    }

    static size_type round_up(size_type x) noexcept {
        if ((x & (x - 1)) == 0) return x;

        size_type p = 1;
        while (p < x) {
            p <<= 1;
        }
        return p;
    }

    static constexpr std::size_t kCacheLine = 64;

    // read-only after construction; shared freely between both threads
    const size_type capacity_;
    const size_type mask_;
    T* ring_;

    // producer-owned line: producer writes head_ every push and keeps its
    // stale copy of tail_ here; consumer only reads head_ when it runs dry
    alignas(kCacheLine) std::atomic<size_type> head_{0};
    size_type cached_tail_{0};

    // consumer-owned line, mirror of the above
    alignas(kCacheLine) std::atomic<size_type> tail_{0};
    size_type cached_head_{0};
};
