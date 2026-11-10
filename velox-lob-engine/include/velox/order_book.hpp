#pragma once

#include "velox/protocol.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace velox {

/**
 * @brief Cache-line aligned normalized market event (64 bytes).
 *
 * Fits inside a single L1d cache line to prevent split loads and false sharing
 * when passed across thread boundaries.
 */
struct alignas(64) MarketUpdate {
    int64_t timestamp_exchange;  ///< Matching engine timestamp (ns)
    int64_t timestamp_local;     ///< Local ingress timestamp (ns)
    int64_t order_id;            ///< Order identifier (L3) or sequence ID
    int64_t price;               ///< Fixed-point price (E8)
    int64_t size;                ///< Fixed-point quantity (E8)
    char    side;                ///< 'B' (Bid) or 'A' (Ask)
    char    type;                ///< 'A' (Add/Update), 'C' (Cancel), 'T' (Trade)
    char    padding[22];         ///< Explicit padding to 64-byte boundary
};

static_assert(sizeof(MarketUpdate) == 64,
              "MarketUpdate must occupy exactly one 64-byte cache line");

/**
 * @brief Price-level aggregate entry for contiguous L2 order book storage.
 */
struct PriceLevel {
    int64_t price{0};  ///< Fixed-point price (E8)
    int64_t size{0};   ///< Aggregate quantity at price level (E8)
};

/**
 * @brief Contiguous, zero-allocation L2/L3 Order Book with 128-bit microstructure math.
 *
 * Algorithmic Complexity & Memory Layout:
 * - Price levels are stored in flat `std::vector<PriceLevel>` containers
 *   pre-allocated at startup (`MAX_BOOK_DEPTH`), guaranteeing L1/L2 hardware
 *   prefetcher friendliness over pointer-chasing `std::map` trees.
 * - `bids_` is maintained in strictly descending price order (`bids_[0]` = Best Bid).
 * - `asks_` is maintained in strictly ascending price order (`asks_[0]` = Best Ask).
 */
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

    /**
     * @brief Inserts, updates in-place, or erases a price level while preserving sort order.
     *
     * @param side     'B' for Bid, 'A' for Ask.
     * @param price_e8 Price scaled by 1e8.
     * @param size_e8  Aggregate quantity scaled by 1e8 (0 removes the level).
     */
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

    /**
     * @brief Bulk-loads a pre-sorted snapshot payload in O(N) without reallocations.
     */
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

    /**
     * @brief Computes L1 volume-weighted Micro-Price using 128-bit intermediate arithmetic.
     *
     * Formula:
     *   MicroPrice = (BestBidPx * BestAskQty + BestAskPx * BestBidQty) / (BestBidQty + BestAskQty)
     *
     * @return Fixed-point Micro-Price (E8), or 0 if either side of the book is empty.
     */
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

    /**
     * @brief Computes Top-5 level Order Book Imbalance (OBI) scaled to [-1e8, +1e8].
     *
     * Formula:
     *   OBI = ((SumBidVol_5 - SumAskVol_5) * PRICE_SCALE) / (SumBidVol_5 + SumAskVol_5)
     */
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

} // namespace velox
