#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <memory>
#include <stdexcept>

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
            , capacity_{round_up(capacity)}
            , mask_{capacity_ - 1}
            , ring_{allocator_traits::allocate(*this, capacity_)}
    {
        if (capacity_ < 2) {
            throw std::invalid_argument("capacity must be at least 2");
        }
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    ~SpscRingBuffer() {
        auto tail = tail_.load(std::memory_order_relaxed);
        auto head = head_.load(std::memory_order_relaxed);

        while (tail != head) {
            ++tail;
        }

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
        auto head = head_.load(std::memory_order_relaxed);
        auto tail = tail_.load(std::memory_order_acquire); // see what consumer has done

        if ((head - tail) == capacity_) {
            // full
            return false;
        }

        ring_[head & mask_] = item; // trivially copyable assumption
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // consumer thread only
    bool try_pop(T& out) noexcept {
        auto tail = tail_.load(std::memory_order_relaxed);
        auto head = head_.load(std::memory_order_acquire);

        if (tail == head) {
            // empty
            return false;
        }

        out = ring_[tail & mask_];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

private:
    static size_type round_up(size_type x) noexcept {
        if ((x && ((x & (x - 1)) == 0))) return x;

        size_type p = 1;
        while (p < x) {
            p <<= 1;
        }
        return p;
    }

    T* element(size_type cursor) noexcept {
        return &ring_[cursor & mask_];
    }

    // put head and tail on separate cache lines to avoid false sharing
    alignas(64) std::atomic<size_type> head_{0}; // producer ownd
    alignas(64) std::atomic<size_type> tail_{0}; // consumer ownd

    const size_type capacity_;
    const size_type mask_;
    T* ring_;
};
