#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// Visualization Colors
#define ANSI_RED "\033[38;2;255;50;50m"
#define ANSI_GREEN "\033[38;2;50;255;50m"
#define ANSI_YELLOW "\033[38;2;255;255;50m"
#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"

class LiquidationEngine {
private:
  // Heatmap Buckets: Price (E8) -> Score (Volume/Intensity)
  std::map<int64_t, int64_t> liquidationMap;
  std::mutex mapMutex;

  // Telemetry
  int64_t currentOI = 0;
  int64_t currentFunding = 0;
  int64_t currentMarkPrice = 0;
  double lastTradePrice = 0.0;

  // Constants
  static constexpr double INV_LIQ_FACTOR = 0.04; // 1/25 = 4%
  static constexpr int64_t BUCKET_SIZE =
      1000000; // 0.01 USDT (since E8, 1000000 = 0.01)
  // Wait, E8 means 1.00000000 is 100,000,000.
  // WLD Price ~2.00. 1 Tick is usually 0.0001?
  // User instruction said: "Bucket size: 100,000 E8".
  // 100,000 E8 = 100,000 * 10^-8 = 0.00100000 = 0.001.
  // Let's stick to the User Instruction: 100,000 E8.

  static constexpr int64_t BUCKET_STEP = 100000;

public:
  void onTrade(double price, double qty, bool isBuyerMaker) {
    // isBuyerMaker = true -> SELL (Taker Sell)
    // isBuyerMaker = false -> BUY (Taker Buy)

    // Inverse Logic:
    // Taker BUY (Longs entering?) -> They get rekt if price drops.
    // Liq Price ~= Price - (Price / 25)

    // Taker SELL (Shorts entering?) -> They get rekt if price rises.
    // Liq Price ~= Price + (Price / 25)

    double estLiqPrice = 0;
    if (isBuyerMaker) {
      // Taker SELL -> Short -> Rekt UP
      estLiqPrice = price * (1.0 + INV_LIQ_FACTOR);
    } else {
      // Taker BUY -> Long -> Rekt DOWN
      estLiqPrice = price * (1.0 - INV_LIQ_FACTOR);
    }

    int64_t liqPxE8 = static_cast<int64_t>(estLiqPrice * 100000000LL);
    int64_t bucket = roundToBucket(liqPxE8);
    int64_t qtyE8 = static_cast<int64_t>(qty * 100000000LL);

    std::lock_guard<std::mutex> lock(mapMutex);
    liquidationMap[bucket] += qtyE8;
    lastTradePrice = price;
  }

  void onLiquidation(double price, double qty, char side) {
    // Real Liquidation Event!
    // Highlight this zone / Validate.
    // We can boost the score of this bucket to show confirmation.

    int64_t liqPxE8 = static_cast<int64_t>(price * 100000000LL);
    int64_t bucket = roundToBucket(liqPxE8);
    int64_t qtyE8 = static_cast<int64_t>(qty * 100000000LL);

    std::lock_guard<std::mutex> lock(mapMutex);
    // Huge boost for real liquidations to make them pop
    liquidationMap[bucket] += (qtyE8 * 10);
  }

  void onTicker(int64_t oi, int64_t funding, int64_t mark) {
    currentOI = oi;
    currentFunding = funding;
    currentMarkPrice = mark;
  }

  void printDashboard() {
    std::cout << "\033[2J\033[H"; // Clear Screen

    std::lock_guard<std::mutex> lock(mapMutex);

    std::cout << "=========================================================="
              << std::endl;
    std::cout << "  🔥 INVERSE LIQUIDATION HEATMAP | CORTEX VISUALIZER  "
              << std::endl;
    std::cout << "=========================================================="
              << std::endl;

    // 1. Telemetry
    double oi = currentOI / 100000000.0; // E8? Usually OI from Bybit is plain
                                         // float value, output from Py.
    // In protocol.h we stored it as int64. In Py feed we printed raw value.
    // If Py feed sent float string -> we need to assume how we parsed it.
    // Wait, in `recorder.cpp` we will parse `toE8`.
    // So currentOI is E8.

    double oiVal = (double)currentOI / 100000000.0;
    double fundVal =
        (double)currentFunding / 100000000.0; // Typically 0.0001 = 0.01%
    double markVal = (double)currentMarkPrice / 100000000.0;

    std::cout << " [REAL-TIME TELEMETRY]" << std::endl;
    std::cout << " PRICE: " << ANSI_BOLD << lastTradePrice << ANSI_RESET
              << " USDT" << std::endl;
    std::cout << " MARK:  " << markVal << std::endl;
    std::cout << " OI:    " << std::fixed << std::setprecision(0) << oiVal
              << " WLD" << std::endl;
    std::cout << " FUND:  " << std::setprecision(6) << fundVal << " ("
              << (fundVal * 100) << "%)" << std::endl;
    std::cout << "----------------------------------------------------------"
              << std::endl;

    // 2. Heatmap
    // Sort buckets by intensity
    struct Zone {
      int64_t price;
      int64_t score;
    };
    std::vector<Zone> zones;
    for (auto const &[px, score] : liquidationMap) {
      zones.push_back({px, score});
    }

    // Sort Descending by Score
    std::sort(zones.begin(), zones.end(),
              [](const Zone &a, const Zone &b) { return a.score > b.score; });

    std::cout << " TOP 15 LIQUIDATION ZONES (Estimated & Real)" << std::endl;
    std::cout << " PRICE      | INTENSITY " << std::endl;

    int count = 0;
    double maxScore = (zones.empty()) ? 1.0 : (double)zones[0].score;

    for (const auto &z : zones) {
      if (count++ >= 15)
        break;

      double pxDouble = (double)z.price / 100000000.0;
      double intensity = (double)z.score / maxScore;
      int barLen = static_cast<int>(intensity * 30);

      std::string color = ANSI_YELLOW;
      if (pxDouble > lastTradePrice)
        color = ANSI_RED; // Overhead resistance (Short Liqs)
      if (pxDouble < lastTradePrice)
        color = ANSI_GREEN; // Support beneath (Long Liqs)

      std::cout << " " << std::setw(8) << std::fixed << std::setprecision(4)
                << pxDouble << " | " << color;

      for (int i = 0; i < barLen; ++i)
        std::cout << "█";
      std::cout << ANSI_RESET << std::endl;
    }
    std::cout << "=========================================================="
              << std::endl;
  }

private:
  int64_t roundToBucket(int64_t pxE8) {
    return (pxE8 / BUCKET_STEP) * BUCKET_STEP;
  }
};
