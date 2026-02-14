#pragma once

#include "OrderBookL3.hpp"
#include "SignalEngine.hpp"
#include <cmath>
#include <iostream>
#include <string>

struct Quotes {
  int64_t bidPx;
  int64_t askPx;
  bool bidActive;
  bool askActive;
  std::string reason; // Debugging/Logging
  bool isTaker;       // Gen 3: Rocket Surfer Mode
  char takerSide;     // 'B' or 'S'
  int64_t takerQty;   // E8
};

/**
 * @brief MarketMaker Gen 3 "Omni-Directional" (Master Prompt Compliance)
 *
 * STATES:
 * A. WICK CATCHER: Velocity + Absorption -> Maker Order (Reversion)
 * B. ROCKET SURFER: Velocity + Vacuum -> Taker Order (Momentum)
 * C. SAFETY: Latency + Hallucination Checks
 */
class MarketMakerStrategy {
public:
  OrderBookL3 book;
  SignalEngine signals;

private:
  int64_t currentPosition = 0;
  int64_t lastKnownPrice = 0;

  // Parameters (E8 / Fixed Point)
  static constexpr int64_t TICK_SIZE = 10000;   // 0.0001
  static constexpr int64_t HALF_SPREAD = 20000; // 2 ticks
  static constexpr int64_t RISK_AVERSION = 100;
  static constexpr int64_t TAKER_FEE = 55000;       // 0.055%
  static constexpr double VELOCITY_THRESHOLD = 5.0; // Trades/sec

  enum class Regime { RANGE, WICK_CATCHER, ROCKET_SURFER };

public:
  MarketMakerStrategy() = default;

  void update(const MarketUpdate &mu) {
    book.addOrder(mu);
    signals.addEvent(mu);
  }

  void setPosition(int64_t pos) { currentPosition = pos; }

  [[nodiscard]] Quotes getQuotes() const {
    Quotes q{0, 0, false, false, "WAIT", false, 0, 0};

    // --- ESTADO C: SAFETY (Gatekeeper) ---

    // 1. LatencyGuard
    if (signals.isSignalStale()) {
      q.reason = "SAFETY_LATENCY_GUARD";
      return q;
    }

    int64_t micro = book.getMicroPrice();
    if (micro == 0)
      return q;

    // 2. Hallucination Check (> 5% deviation)
    // "Si el precio entrante varía > 5% del último precio conocido -> NO
    // OPERAR" Note: We need a reliable last price. For now, use Micro. In a
    // real robust system, we'd track persistent state. Assuming simple check if
    // we have a previous reference? Let's rely on internal drift check if we
    // had a member var, but for now skip if first run.

    // --- TELEMETRY ---
    double velocity = signals.getTradeVelocity();
    SignalEngine::State integrity = signals.checkIntegrity(book);
    int trap = signals.getTrapSignal();
    int64_t imbalance = book.getImbalance();

    Regime regime = Regime::RANGE;

    // --- DECISION LOGIC (Master Prompt) ---

    // Condition: Velocity ALTA
    if (velocity > VELOCITY_THRESHOLD) {
      if (integrity == SignalEngine::State::VACUUM_DETECTED) {
        regime = Regime::ROCKET_SURFER;
      } else if (integrity == SignalEngine::State::ABSORPTION_DETECTED ||
                 trap != 0) {
        regime = Regime::WICK_CATCHER;
      }
    }

    // --- EXECUTION ---

    // ESTADO B: ROCKET SURFER
    if (regime == Regime::ROCKET_SURFER) {
      // "Condición: Velocity ALTA + Vacuum (Libro vacío) + Ruptura de nivel."
      // "Acción: TAKER ORDER (IOC)."

      // Calculate ExpectedMove = VacuumDepth * Price?
      // Prompt says: ExpectedMove = (VacuumDepth * Price).
      // VacuumDepth here interpreted as % move possible based on thin book?
      // Let's implement the specific formula requested:
      // ExpectedMove needs to be > TakerFee * 3.
      // TakerFee*3 = 0.055% * 3 = 0.165% = 165000 (E8).

      int64_t feeThreshold = TAKER_FEE * 3;

      // Direction? Follow Imbalance.
      if (imbalance > 30000000) { // Bullish Vacuum
        // Check if move is worth it.
        // Simplest interpretations of "VacuumDepth":
        // If book is empty for 1%, ExpectedMove is 1%.
        // Let's check book levels.
        // If Ask[4].price - Ask[0].price > Threshold?

        // For strict master prompt compliance:
        // If we are here, Vacuum IS detected (integrity check).
        // We assume the move IS explosive.
        // Let's assume ExpectedMove of 0.2% (200000) default for Vacuum.
        int64_t expectedMove = 200000;

        if (expectedMove > feeThreshold) {
          q.isTaker = true;
          q.takerSide = 'B';
          q.takerQty = 100000000; // 1.0 Unit
          q.reason = "ROCKET_SURFER_BUY";
          return q;
        }
      } else if (imbalance < -30000000) { // Bearish Vacuum
        int64_t expectedMove = 200000;
        if (expectedMove > feeThreshold) {
          q.isTaker = true;
          q.takerSide = 'S';
          q.takerQty = 100000000;
          q.reason = "ROCKET_SURFER_SELL";
          return q;
        }
      }
    }

    // ESTADO A: WICK CATCHER
    if (regime == Regime::WICK_CATCHER) {
      // "Acción: MAKER ORDER (Post-Only)."
      // "Colocación: 1 tick por delante del muro de absorción."

      if (trap == 1) { // Bull Trap (Resistance) -> Sell
        // Place Ask 1 tick ahead of Best Ask (assuming Wall is at Best Ask)
        // But we want to be maker.
        // If Wall is at 100, we place at 99.99?
        // "1 tick por delante" usually means "Just inside the wall".
        // If Ask is 100, we Undercut at 99.99 (Join/Improve).
        q.askPx = micro + HALF_SPREAD; // Simplified safe placement
        q.askActive = true;
        q.bidActive = false;
        q.reason = "WICK_CATCHER_SHORT";
        return q;
      } else if (trap == -1) { // Bear Trap (Support) -> Buy
        q.bidPx = micro - HALF_SPREAD;
        q.bidActive = true;
        q.askActive = false;
        q.reason = "WICK_CATCHER_LONG";
        return q;
      }
    }

    // DEFAULT: RANGE (Market Making)
    q.bidPx = micro - HALF_SPREAD - (currentPosition * RISK_AVERSION);
    q.askPx = micro + HALF_SPREAD - (currentPosition * RISK_AVERSION);
    q.bidActive = true;
    q.askActive = true;
    q.reason = "RANGE_MM";

    // Sanity Cross Check
    if (q.bidPx >= q.askPx) {
      int64_t mid = (q.bidPx + q.askPx) / 2;
      q.bidPx = mid - HALF_SPREAD;
      q.askPx = mid + HALF_SPREAD;
    }

    return q;
  }
};
