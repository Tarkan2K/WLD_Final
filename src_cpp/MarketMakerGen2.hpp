#pragma once

#include "OrderBookL3.hpp"
#include "SignalEngine.hpp"
#include <algorithm>

struct Quotes {
  int64_t bidPx;
  int64_t askPx;
  bool bidActive;
  bool askActive;
};

/**
 * @brief MarketMaker Gen 2 "Sniper" (C++20 Zero-Alloc)
 *
 * Philosophy:
 * - Front of Book (2 ticks).
 * - Aggressive Trap Exploitation.
 * - Minimal Risk Aversion (Hold for profit).
 */
class MarketMakerGen2 {
public:
  OrderBookL3 book;
  SignalEngine signals;

private:
  // Calibration Constants (Hardcoded for Gen 2 Specification)
  static constexpr int64_t HALF_SPREAD = 20000;  // 2 ticks (0.02%)
  static constexpr int64_t RISK_AVERSION = 1000; // Increased 10x
  static constexpr int64_t TRAP_AGGRESSION =
      100000; // Reduced 20x (Was 2,000,000)
  static constexpr int64_t MAX_LATENCY = 50000000; // 50ms Guard

  // State
  int64_t currentPosition = 0;
  int64_t targetPosition = 0;

public:
  MarketMakerGen2() = default;

  void update(const MarketUpdate &mu) {
    book.addOrder(mu);
    signals.addEvent(mu);
  }

  void setPosition(int64_t pos) { currentPosition = pos; }

  [[nodiscard]] Quotes getQuotes() const {
    Quotes q{0, 0, false, false};

    // 1. Latency Circuit Breaker
    if (signals.getLatency() > MAX_LATENCY) {
      return q; // Pull quotes if lagged
    }

    // 2. Micro-Price Calculation (Fair Value)
    int64_t micro = book.getMicroPrice();
    if (micro == 0)
      return q;

    // 3. Inventory Skew Calculation
    // skew_offset = pos * RISK_AVERSION
    // Positive Pos (Long) -> Positive Skew -> Lowers Bid/Ask (to Sell)
    int64_t skewOffsets = currentPosition * RISK_AVERSION;

    // 4. Trap Logic (Signal Offset)
    int trap = signals.getTrapSignal();
    int64_t signalOffset = 0;

    if (trap == 1) {
      // BULL TRAP (Buyers saturated, Price stalling @ High)
      // Expect CRASH.
      // We want to be SHORT.
      // Action: Shift Quotes DOWN aggressively.
      // Why? To fill Asks (Sell) lower than market? Or to chase price down?
      // Prompt Spec: "Bull Trap: Desplazar precios hacia ABAJO
      // (-TRAP_AGGRESSION)" If we shift down, our Ask becomes cheaper -> We get
      // filled Short. Correct.
      signalOffset = -TRAP_AGGRESSION;
    } else if (trap == -1) {
      // BEAR TRAP (Sellers saturated, Price stalling @ Low)
      // Expect BOUNCE.
      // We want to be LONG.
      // Action: Shift Quotes UP aggressively.
      // If we shift up, our Bid becomes higher -> We get filled Long. Correct.
      signalOffset = TRAP_AGGRESSION;
    }

    // 5. Calculate Final Quotes
    // Formula: Micro +/- Spread - Skew + Signal
    q.bidPx = micro - HALF_SPREAD - skewOffsets + signalOffset;
    q.askPx = micro + HALF_SPREAD - skewOffsets + signalOffset;

    q.bidActive = true;
    q.askActive = true;

    // 6. Sanity Check: Crossed Book
    if (q.bidPx >= q.askPx) {
      int64_t mid = (q.bidPx + q.askPx) / 2;
      q.bidPx = mid - HALF_SPREAD;
      q.askPx = mid + HALF_SPREAD;
    }

    return q;
  }
};
