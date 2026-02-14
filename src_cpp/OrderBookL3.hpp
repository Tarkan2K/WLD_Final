#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

// Configuration
constexpr int64_t PRICE_SCALE =
    100000000; // 1e8 for Fixed Point (Satoshi style)
constexpr size_t MAX_ORDER_BOOK_DEPTH = 10000; // Pre-allocation size

// Zero-Latency Market Event (Cache Line Aligned - 64 Bytes)
// This structure fits exactly into a standard 64-byte Cache Line.
// This prevents 'false sharing' across CPU cores.
struct alignas(64) MarketUpdate {
  int64_t timestamp_exchange; // 8 bytes: Engine time (ns)
  int64_t timestamp_local;    // 8 bytes: NIC arrival time (ns)
  int64_t order_id;           // 8 bytes: Unique Order ID (L3)
  int64_t price;              // 8 bytes: Fixed Point (E8)
  int64_t size;               // 8 bytes: Fixed Point (E8)
  char side;                  // 1 byte: 'B' (Bid) or 'A' (Ask)
  char type;                  // 1 byte: 'A' (Add), 'C' (Cancel), 'T' (Trade)
  char padding[22];           // 22 bytes: Explicit padding to reach 64 bytes
};

// Compile-time check to ensure infrastructure alignment
static_assert(sizeof(MarketUpdate) == 64,
              "MarketUpdate must be exactly 64 bytes to match Cache Line");

class OrderBookL3 {
private:
  // Contiguous memory layout for Cache Locality.
  // Unlike std::list or std::map, vector keeps data close in RAM.
  std::vector<MarketUpdate> bids;
  std::vector<MarketUpdate> asks;

  // Reserve memory ONCE at startup to achieve Zero-Allocation during runtime
  void reserve() {
    bids.reserve(MAX_ORDER_BOOK_DEPTH);
    asks.reserve(MAX_ORDER_BOOK_DEPTH);
  }

  friend class SignalEngine;

public:
  OrderBookL3() { reserve(); }

  void clear() {
    bids.clear();
    asks.clear();
  }

  // --- Fixed Point Converters ---

  // Convert Double to Fixed Point (E8)
  // Example: 0.4550 -> 45,500,000
  static int64_t toFixed(double val) {
    return static_cast<int64_t>(val * PRICE_SCALE);
  }

  // Convert Fixed Point to Double (Only for Logging/UI)
  static double toDouble(int64_t val) {
    return static_cast<double>(val) / static_cast<double>(PRICE_SCALE);
  }

  // --- Signal Engine: Micro-Structures ---

  // Calculates Micro-Price (Volume Weighted Fair Price)
  // Formula: ((BidPx * AskVol) + (AskPx * BidVol)) / (BidVol + AskVol)
  // Uses 128-bit arithmetic internally to prevent overflow during Price*Vol
  // multiplication.
  int64_t getMicroPrice() const {
    if (bids.empty() || asks.empty())
      return 0;

    int64_t bestBidPx = bids[0].price; // Assumes sorted list (TODO: Sort Logic)
    int64_t bestAskPx = asks[0].price;

    // Using Top 1 Level Volume for nanosecond calculation speed
    // David Gross Optimization: Don't iterate deep levels if speed is priority
    int64_t bidVol = bids[0].size;
    int64_t askVol = asks[0].size;

    if (bidVol + askVol == 0)
      return (bestBidPx + bestAskPx) / 2;

    // 128-bit Math for safety
    unsigned __int128 num =
        (static_cast<unsigned __int128>(bestBidPx) * askVol) +
        (static_cast<unsigned __int128>(bestAskPx) * bidVol);
    int64_t den = bidVol + askVol;

    return static_cast<int64_t>(num / den);
  }

  // Calculates Order Book Imbalance (OBI)
  // Range: -1.0 to 1.0 (Scaled to E8, so -100M to +100M)
  // > 0.3 (30M) means Bullish Pressure
  int64_t getImbalance() const {
    if (bids.empty() || asks.empty())
      return 0;

    int64_t bidVol = 0;
    int64_t askVol = 0;

    // Sum top 5 levels for robust Imbalance
    size_t depthB = std::min((size_t)5, bids.size());
    for (size_t i = 0; i < depthB; ++i)
      bidVol += bids[i].size;

    size_t depthA = std::min((size_t)5, asks.size());
    for (size_t i = 0; i < depthA; ++i)
      askVol += asks[i].size;

    if (bidVol + askVol == 0)
      return 0;

    // (Bid - Ask) / (Bid + Ask)
    int64_t diff = bidVol - askVol;
    int64_t total = bidVol + askVol;

    // Use 128-bit math to prevent overflow: (Diff * Scale) / Total
    unsigned __int128 num =
        static_cast<unsigned __int128>(std::abs(diff)) * PRICE_SCALE;
    int64_t result = static_cast<int64_t>(num / total);
    return (diff < 0) ? -result : result;
  }

  // Add Order (Placeholder for sorted insertion)
  void addOrder(const MarketUpdate &update) {
    // In real L3, we would insert sorted by Price, then Time.
    // For PoC compilation test:
    if (update.side == 'B')
      bids.push_back(update);
    else
      asks.push_back(update);
  }
};
