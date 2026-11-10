#pragma once

#include "velox/order_book.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace velox {

class TelemetryEngine {
public:
    static constexpr size_t  WINDOW_SIZE    = 1000;
    static constexpr int64_t MAX_LATENCY_NS = 500'000'000LL;

private:
    struct TradeRecord {
        int64_t price{0};
        int64_t size{0};
        int64_t timestamp{0};
        char    side{'B'};
    };

    std::array<TradeRecord, WINDOW_SIZE> history_{};
    size_t  head_{0};
    size_t  count_{0};
    int64_t running_buy_vol_{0};
    int64_t running_sell_vol_{0};
    int64_t current_latency_ns_{0};
    bool    is_stale_{false};

public:
    TelemetryEngine() = default;

    void add_event(const MarketUpdate& mu) noexcept {
        current_latency_ns_ = mu.timestamp_local - mu.timestamp_exchange;
        is_stale_ = (current_latency_ns_ > MAX_LATENCY_NS);

        if (mu.type != 'T') {
            return;
        }

        if (count_ == WINDOW_SIZE) {
            const TradeRecord& evicted = history_[head_];
            if (evicted.side == 'B') {
                running_buy_vol_ -= evicted.size;
            } else {
                running_sell_vol_ -= evicted.size;
            }
        } else {
            ++count_;
        }

        history_[head_] = TradeRecord{mu.price, mu.size, mu.timestamp_exchange, mu.side};
        if (mu.side == 'B') {
            running_buy_vol_ += mu.size;
        } else {
            running_sell_vol_ += mu.size;
        }

        head_ = (head_ + 1) % WINDOW_SIZE;
    }

    [[nodiscard]] bool is_stale() const noexcept { return is_stale_; }

    [[nodiscard]] int64_t get_latency_ns() const noexcept { return current_latency_ns_; }

    [[nodiscard]] double get_trade_velocity() const noexcept {
        if (count_ < 2) {
            return 0.0;
        }

        const size_t oldest_idx = (head_ + WINDOW_SIZE - count_) % WINDOW_SIZE;
        const size_t newest_idx = (head_ + WINDOW_SIZE - 1) % WINDOW_SIZE;
        const int64_t duration_ns = history_[newest_idx].timestamp - history_[oldest_idx].timestamp;

        if (duration_ns <= 0) {
            return 0.0;
        }

        return static_cast<double>(count_) / (static_cast<double>(duration_ns) * 1e-9);
    }

    [[nodiscard]] int64_t get_vpin() const noexcept {
        const int64_t total = running_buy_vol_ + running_sell_vol_;
        if (total <= 0) {
            return 0;
        }

        const int64_t diff = running_buy_vol_ - running_sell_vol_;
        const int64_t abs_diff = diff < 0 ? -diff : diff;
        const unsigned __int128 num =
            static_cast<unsigned __int128>(abs_diff) * static_cast<unsigned __int128>(PRICE_SCALE);
        const int64_t mag = static_cast<int64_t>(num / static_cast<unsigned __int128>(total));

        return (diff < 0) ? -mag : mag;
    }
};

}
