/**
 * @file spsc_ring.h
 * @brief Lock-free single-producer / single-consumer ring (pure C++, host-testable).
 *
 * Used to hand frame pointers between pipeline stages. Exactly one task pushes and
 * exactly one task pops, so two atomics with acquire/release ordering suffice: no
 * mutex, no priority inversion, O(1) push/pop that never blocks. Blocking/wakeup is
 * done outside the ring (FreeRTOS task notifications), which keeps this class free of
 * RTOS dependencies.
 *
 * Capacity is exactly N items (head/tail are free-running counters, so "full" and
 * "empty" are distinguishable without wasting a slot). N must be a power of two.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mic {

template <typename T, size_t N>
class SpscRing {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two >= 2");

public:
    /// Producer side. Returns false (and does not block) when full.
    bool push(const T& v) {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= N) return false;
        buf_[head & (N - 1)] = v;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Consumer side. Returns false (and does not block) when empty.
    bool pop(T& out) {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        const uint32_t head = head_.load(std::memory_order_acquire);
        if (head == tail) return false;
        out = buf_[tail & (N - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    size_t size() const {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }
    bool empty() const { return size() == 0; }
    static constexpr size_t capacity() { return N; }

private:
    T buf_[N] = {};
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> tail_{0};
};

}  // namespace mic
