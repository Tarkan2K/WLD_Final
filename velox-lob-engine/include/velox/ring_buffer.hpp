#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

namespace velox {

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

    [[nodiscard]] bool pop(T& out_item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);

        if (current_tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        out_item = buffer_[current_tail];
        tail_.store((current_tail + 1) & MASK, std::memory_order_release);
        return true;
    }

    [[nodiscard]] size_t size() const noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t t = tail_.load(std::memory_order_relaxed);
        return (h - t) & MASK;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return Capacity - 1;
    }
};

}
