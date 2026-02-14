#pragma once

#include "OrderBookL3.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

/**
 * @brief SignalEngine Gen 3 "Omni-Directional" (Master Prompt Compliance)
 *
 * SENSORS:
 * 1. TradeVelocity: Trades/sec (Momentum).
 * 2. LatencyGuard: Stale Data Protection (> 500ms).
 * 3. MarketRegime: Vacuum vs Absorption.
 */
class SignalEngine {
private:
  // Circular Buffer for Trade History (1000 items)
  static constexpr size_t WINDOW_SIZE = 1000;
  static constexpr int64_t PRICE_SCALE = 100000000;
  static constexpr int64_t MAX_LATENCY_NS = 500000000; // 500ms

  struct TradeRecord {
    int64_t price;
    int64_t size;
    char side;         // 'B'uy or 'S'ell (Taker)
    int64_t timestamp; // Exchange Timestamp
  };

  std::array<TradeRecord, WINDOW_SIZE> history;
  size_t head = 0;
  size_t count = 0;

  // Running Totals for VPIN
  int64_t runningBuyVol = 0;
  int64_t runningSellVol = 0;

  // Telemetry
  int64_t currentLatency = 0;
  bool isStale = false;

  // Gen 3 Thresholds (Master Prompt)
  // Vacuum: Liquidity in next 5 levels < Critical Threshold
  // Critical Threshold = 0.5 BTC equivalent? Let's assume 100000000 is 1.0
  // Unit. We'll set a conservative "Vacuum" threshold of 0.1 Units total in 5
  // levels.
  static constexpr int64_t VACUUM_THRESHOLD = 50000000; // 0.5 Units (E8)

  // Absorption: High Volume at Best detected via VPIN or heavy walls
  static constexpr int64_t WALL_THRESHOLD = 500000000; // 5.0 Units (Big Wall)

public:
  enum class State {
    NORMAL,
    VACUUM_DETECTED,    // Slippage Risk / Momentum Opportunity
    ABSORPTION_DETECTED // Wall Reversion Opportunity
  };

  SignalEngine() = default;

  /**
   * @brief Ingests Market Update
   * LatencyGuard: Checks packet age vs system time.
   */
  void addEvent(const MarketUpdate &mu) {
    // 1. Latency Guard
    currentLatency = mu.timestamp_local - mu.timestamp_exchange;
    if (currentLatency > MAX_LATENCY_NS) {
      isStale = true;
    } else {
      isStale = false;
    }

    if (mu.type != 'T')
      return;

    char side = mu.side; // 'B'uy Taker or 'A'sk Taker
    TradeRecord rec{mu.price, mu.size, side, mu.timestamp_exchange};

    if (count == WINDOW_SIZE) {
      const auto &tail = history[head];
      if (tail.side == 'B')
        runningBuyVol -= tail.size;
      else
        runningSellVol -= tail.size;
    } else {
      count++;
    }

    history[head] = rec;
    if (side == 'B')
      runningBuyVol += rec.size;
    else
      runningSellVol += rec.size;

    head = (head + 1) % WINDOW_SIZE;
  }

  [[nodiscard]] bool isSignalStale() const { return isStale; }
  [[nodiscard]] int64_t getLatency() const { return currentLatency; }

  /**
   * @brief Trade Velocity (Trades per Second)
   */
  [[nodiscard]] double getTradeVelocity() const {
    if (count < 2)
      return 0.0;
    size_t tailIdx = (head + WINDOW_SIZE - count) % WINDOW_SIZE;
    size_t recentIdx = (head + WINDOW_SIZE - 1) % WINDOW_SIZE;
    int64_t durationNs =
        history[recentIdx].timestamp - history[tailIdx].timestamp;
    if (durationNs <= 0)
      return 0.0;
    return (double)count / (durationNs / 1e9);
  }

  /**
   * @brief Market Regime Detection (Master Prompt Strict)
   */
  [[nodiscard]] State checkIntegrity(const OrderBookL3 &book) const {
    if (isStale)
      return State::NORMAL; // Safety

    // 1. VACUUM DETECTION
    // "Liquidez acumulada en los siguientes 5 niveles es < Umbral Crítico"
    int64_t bidLiq5 = 0;
    int64_t askLiq5 = 0;

    size_t d = std::min((size_t)5, book.bids.size());
    for (size_t i = 0; i < d; ++i)
      bidLiq5 += book.bids[i].size;

    d = std::min((size_t)5, book.asks.size());
    for (size_t i = 0; i < d; ++i)
      askLiq5 += book.asks[i].size;

    // Strict Vacuum: If ANY side has < Threshold in 5 levels
    if (bidLiq5 < VACUUM_THRESHOLD || askLiq5 < VACUUM_THRESHOLD) {
      return State::VACUUM_DETECTED;
    }

    // 2. ABSORPTION DETECTION
    // "Alto volumen tradeado en Best Bid/Ask pero el precio NO se mueve."
    // Proxy: Check for huge walls at L1 > 5x avg, or hardcoded massive size.
    if ((!book.bids.empty() && book.bids[0].size > WALL_THRESHOLD) ||
        (!book.asks.empty() && book.asks[0].size > WALL_THRESHOLD)) {
      return State::ABSORPTION_DETECTED;
    }

    return State::NORMAL;
  }

  // --- Telemetry Helpers ---
  [[nodiscard]] int64_t getVPIN() const {
    int64_t total = runningBuyVol + runningSellVol;
    if (total == 0)
      return 0;
    int64_t diff = runningBuyVol - runningSellVol;
    // (diff * SCALE) / total
    return (std::abs(diff) * PRICE_SCALE) / total * (diff < 0 ? -1 : 1);
  }

  // Re-used for Trap Logic in Strategy
  [[nodiscard]] int64_t getToxicity() const { return std::abs(getVPIN()); }

  // Basic Trap Signal (Max/Min price within window vs Flow)
  [[nodiscard]] int getTrapSignal() const {
    if (count < 50)
      return 0;
    int64_t maxPx = 0;
    int64_t minPx = std::numeric_limits<int64_t>::max();
    int64_t lastPx = history[(head + WINDOW_SIZE - 1) % WINDOW_SIZE].price;

    for (size_t i = 0; i < count; ++i) {
      int64_t px = history[(head + WINDOW_SIZE - 1 - i) % WINDOW_SIZE].price;
      if (px > maxPx)
        maxPx = px;
      if (px < minPx)
        minPx = px;
    }

    int64_t vpin = getVPIN();
    // Bull Trap: Buying but Price Stalled below Max
    if (vpin > 30000000 && lastPx < maxPx - 50000)
      return 1;
    // Bear Trap: Selling but Price Stalled above Min
    if (vpin < -30000000 && lastPx > minPx + 50000)
      return -1;

    return 0;
  }

  // Helper for ROCKET logic: Depth of Vacuum in Units?
  // We return the average volume of top 5 levels
  int64_t getVacuumDepth(const OrderBookL3 &book) const {
    int64_t liq = 0;
    for (size_t i = 0; i < std::min((size_t)5, book.bids.size()); ++i)
      liq += book.bids[i].size;
    for (size_t i = 0; i < std::min((size_t)5, book.asks.size()); ++i)
      liq += book.asks[i].size;
    return liq / 10; // Avg per side/level rough approx
  }
};
