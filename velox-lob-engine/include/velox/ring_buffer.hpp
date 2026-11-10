#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

namespace velox {

/**
 * @brief Single-Producer Single-Consumer (SPSC) lock-free ring buffer.
 *
 * Memory Layout & Concurrency Invariants:
 * - Capacity is constrained to a power of two at compile time, enabling
 *   single-cycle bitwise index wrapping (`index & MASK`).
 * - Storage is heap-allocated via `std::unique_ptr<T[]>` at construction to
 *   prevent multi-megabyte `.bss` segment inflation on large message types.
 * - `head_` and `tail_` atomic cursors are aligned to separate 64-byte cache
 *   lines (`alignas(64)`) to eliminate false sharing between producer and
 *   consumer cores.
 *
 * @tparam T        Element type (trivially copyable recommended).
 * @tparam Capacity Ring buffer slot count (must be a power of 2).
 */
template <typename T, size_t Capacity>
class RingBuffer {
    static_assert(Capacity >= 2 && ((Capacity & (Capacity - 1)) == 0),
                  "Capacity must be a power of 2");

private:
    static constexpr size_t MASK = Capacity - 1;

    std::unique_ptr<T[]> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};

public:
    RingBuffer() : buffer_(std::make_unique<T[]>(Capacity)) {}

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    /**
     * @brief Enqueues an item (Producer thread only).
     * @return true if enqueued, false if the buffer is full.
     */
    [[nodiscard]] bool push(const T& item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t next_head = (current_head + 1) & MASK;

        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[current_head] = item;
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    /**
     * @brief Dequeues an item (Consumer thread only).
     * @return true if dequeued into `out_item`, false if the buffer is empty.
     */
    [[nodiscard]] bool pop(T& out_item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);

        if (current_tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        out_item = buffer_[current_tail];
        tail_.store((current_tail + 1) & MASK, std::memory_order_release);
        return true;
    }

    /**
     * @brief Returns approximate queue occupancy in O(1) time.
     */
    [[nodiscard]] size_t size() const noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t t = tail_.load(std::memory_order_relaxed);
        return (h - t) & MASK;
    }

    /**
     * @brief Returns true if no elements are pending consumption.
     */
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    /**
     * @brief Returns usable slot capacity (Capacity - 1).
     */
    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return Capacity - 1;
    }
};

} // namespace velox
