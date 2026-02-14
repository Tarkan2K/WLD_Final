#pragma once

#include <atomic>
#include <optional>
#include <vector>

// Single Producer Single Consumer (SPSC) Lock-Free Ring Buffer
template <typename T, size_t Capacity> class RingBuffer {
private:
  std::vector<T> buffer;
  std::atomic<size_t> head; // Write index
  std::atomic<size_t> tail; // Read index

public:
  RingBuffer() : buffer(Capacity + 1), head(0), tail(0) {}

  // Writer Thread Only
  bool push(const T &item) {
    size_t current_head = head.load(std::memory_order_relaxed);
    size_t next_head = (current_head + 1) % buffer.size();

    if (next_head == tail.load(std::memory_order_acquire)) {
      return false; // Full
    }

    buffer[current_head] = item;
    head.store(next_head, std::memory_order_release);
    return true;
  }

  // Reader Thread Only
  bool pop(T &out_item) {
    size_t current_tail = tail.load(std::memory_order_relaxed);

    if (current_tail == head.load(std::memory_order_acquire)) {
      return false; // Empty
    }

    out_item = buffer[current_tail];
    tail.store((current_tail + 1) % buffer.size(), std::memory_order_release);
    return true;
  }

  // Check occupancy (Approximate)
  size_t size() const {
    size_t h = head.load(std::memory_order_relaxed);
    size_t t = tail.load(std::memory_order_relaxed);
    if (h >= t)
      return h - t;
    return buffer.size() - (t - h);
  }
};
