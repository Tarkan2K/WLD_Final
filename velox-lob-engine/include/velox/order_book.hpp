#pragma once

#include "velox/protocol.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace velox {

struct alignas(64) MarketUpdate {
    int64_t timestamp_exchange;
    int64_t timestamp_local;
    int64_t order_id;
    int64_t price;
    int64_t size;
    char    side;
    char    type;
    char    padding[22];
};

static_assert(sizeof(MarketUpdate) == 64,
              "MarketUpdate must occupy exactly one 64-byte cache line");

struct PriceLevel {
    int64_t price{0};
    int64_t size{0};
};

class OrderBook {
public:
    static constexpr size_t MAX_BOOK_DEPTH = 64;

private:
    std::vector<PriceLevel> bids_;
    std::vector<PriceLevel> asks_;

public:
    OrderBook() {
        bids_.reserve(MAX_BOOK_DEPTH);
        asks_.reserve(MAX_BOOK_DEPTH);
    }

    void clear() noexcept {
        bids_.clear();
        asks_.clear();
    }

    [[nodiscard]] static constexpr int64_t to_fixed(double val) noexcept {
        return static_cast<int64_t>(val * static_cast<double>(PRICE_SCALE));
    }

    [[nodiscard]] static constexpr double to_double(int64_t val) noexcept {
        return static_cast<double>(val) / static_cast<double>(PRICE_SCALE);
    }

    void update_level(char side, int64_t price_e8, int64_t size_e8) noexcept {
        if (side == 'B') {
            auto it = std::lower_bound(
                bids_.begin(), bids_.end(), price_e8,
                [](const PriceLevel& lvl, int64_t px) { return lvl.price > px; });

            if (it != bids_.end() && it->price == price_e8) {
                if (size_e8 <= 0) {
                    bids_.erase(it);
                } else {
                    it->size = size_e8;
                }
            } else if (size_e8 > 0) {
                if (bids_.size() < MAX_BOOK_DEPTH) {
                    bids_.insert(it, PriceLevel{price_e8, size_e8});
                } else if (it != bids_.end()) {
                    bids_.pop_back();
                    bids_.insert(it, PriceLevel{price_e8, size_e8});
                }
            }
        } else {
            auto it = std::lower_bound(
                asks_.begin(), asks_.end(), price_e8,
                [](const PriceLevel& lvl, int64_t px) { return lvl.price < px; });

            if (it != asks_.end() && it->price == price_e8) {
                if (size_e8 <= 0) {
                    asks_.erase(it);
                } else {
                    it->size = size_e8;
                }
            } else if (size_e8 > 0) {
                if (asks_.size() < MAX_BOOK_DEPTH) {
                    asks_.insert(it, PriceLevel{price_e8, size_e8});
                } else if (it != asks_.end()) {
                    asks_.pop_back();
                    asks_.insert(it, PriceLevel{price_e8, size_e8});
                }
            }
        }
    }

    void load_snapshot(const SnapshotPayload& snap) noexcept {
        bids_.clear();
        asks_.clear();
        for (int i = 0; i < SNAPSHOT_DEPTH; ++i) {
            if (snap.bid_px[i] > 0 && snap.bid_qty[i] > 0) {
                bids_.push_back(PriceLevel{snap.bid_px[i], snap.bid_qty[i]});
            }
            if (snap.ask_px[i] > 0 && snap.ask_qty[i] > 0) {
                asks_.push_back(PriceLevel{snap.ask_px[i], snap.ask_qty[i]});
            }
        }
    }

    [[nodiscard]] int64_t get_micro_price() const noexcept {
        if (bids_.empty() || asks_.empty()) {
            return 0;
        }

        const int64_t best_bid_px = bids_[0].price;
        const int64_t best_ask_px = asks_[0].price;
        const int64_t bid_vol     = bids_[0].size;
        const int64_t ask_vol     = asks_[0].size;

        const int64_t total_vol = bid_vol + ask_vol;
        if (total_vol <= 0) {
            return (best_bid_px + best_ask_px) / 2;
        }

        const unsigned __int128 num =
            (static_cast<unsigned __int128>(best_bid_px) * static_cast<unsigned __int128>(ask_vol)) +
            (static_cast<unsigned __int128>(best_ask_px) * static_cast<unsigned __int128>(bid_vol));

        return static_cast<int64_t>(num / static_cast<unsigned __int128>(total_vol));
    }

    [[nodiscard]] int64_t get_imbalance() const noexcept {
        if (bids_.empty() || asks_.empty()) {
            return 0;
        }

        int64_t bid_vol = 0;
        int64_t ask_vol = 0;

        const size_t depth_b = std::min<size_t>(5, bids_.size());
        for (size_t i = 0; i < depth_b; ++i) {
            bid_vol += bids_[i].size;
        }

        const size_t depth_a = std::min<size_t>(5, asks_.size());
        for (size_t i = 0; i < depth_a; ++i) {
            ask_vol += asks_[i].size;
        }

        const int64_t total = bid_vol + ask_vol;
        if (total <= 0) {
            return 0;
        }

        const int64_t diff = bid_vol - ask_vol;
        const int64_t abs_diff = diff < 0 ? -diff : diff;
        const unsigned __int128 num =
            static_cast<unsigned __int128>(abs_diff) * static_cast<unsigned __int128>(PRICE_SCALE);
        const int64_t result = static_cast<int64_t>(num / static_cast<unsigned __int128>(total));

        return (diff < 0) ? -result : result;
    }

    [[nodiscard]] const std::vector<PriceLevel>& bids() const noexcept { return bids_; }
    [[nodiscard]] const std::vector<PriceLevel>& asks() const noexcept { return asks_; }
};

}
