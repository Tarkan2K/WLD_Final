#pragma once

#include "MarketMakerStrategy.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sqlite3.h>
#include <sstream>
#include <string>
#include <vector>

// Simulated Live Order
struct LiveOrder {
  int id;
  char side; // 'B' or 'A'
  double price;
  double quantity;
  bool active;
  bool isExit;
  long long timestamp;
  std::string reason;
};

// --- LEGACY ORDERBOOK (Display only) ---
struct DisplayOrderBook {
  struct Level {
    double price;
    double qty;
  };
  std::vector<Level> bids;
  std::vector<Level> asks;
};
// ------------------------------------------------------------------------------------------

class LiveEngine {
private:
  // GEN 3 STRATEGY CORE
  MarketMakerStrategy strategy;

  // Account State
  double initialBalance;
  double balance;
  double position = 0.0;
  double entryPrice = 0.0;

  std::vector<LiveOrder> orders;
  int orderIdCounter = 0;

  // Config
  double minOrderValue = 25.0;
  double profitTargetAutoStop = 0.30;
  std::string sessionId;
  long long lastDashboardPrint = 0;

  // Black Box DB
  sqlite3 *db;
  bool dbReady = false;

  DisplayOrderBook displayBook; // For JSON dump only

public:
  LiveEngine() : initialBalance(1000.0), balance(1000.0) {
    generateSessionId();
    initDB();
    loadLastBalance();
  }

  void loadLastBalance() {
    if (!dbReady)
      return;
    // Simplified for now, usually we read from DB
    balance = 1000.0;
  }

  ~LiveEngine() {
    if (dbReady)
      sqlite3_close(db);
  }

  void run() {
    std::string line;
    while (std::getline(std::cin, line)) {
      parseLine(line);
      printDashboard();
      dumpOrderBook();
      dumpDashboardState();
      checkAutoStop();
    }
  }

private:
  void generateSessionId() {
    std::time_t now = std::time(nullptr);
    std::stringstream ss;
    ss << "GEN3-CORTEX-" << now;
    sessionId = ss.str();
  }

  void initDB() {
    int rc = sqlite3_open("hft_live.db", &db);
    if (rc)
      return;

    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", 0, 0, 0);

    const char *sql =
        "CREATE TABLE IF NOT EXISTS trade_log ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp_ns INTEGER,"
        "symbol TEXT,"
        "side TEXT,"
        "strategy_type TEXT,"   // WICK_CATCHER / ROCKET_SURFER / RANGE
        "entry_price INTEGER,"  // E8
        "exit_price INTEGER,"   // E8 (0 if open)
        "pnl_realized INTEGER," // E8
        "trigger_reason TEXT,"
        "telemetry_velocity INTEGER,"
        "telemetry_vpin INTEGER,"
        "session_id TEXT"
        ");";

    sqlite3_exec(db, sql, 0, 0, 0);

    const char *sql_legacy = "CREATE TABLE IF NOT EXISTS trades ("
                             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                             "timestamp INTEGER,"
                             "session_id TEXT,"
                             "side TEXT,"
                             "price REAL,"
                             "qty REAL,"
                             "pnl REAL,"
                             "balance REAL,"
                             "reason TEXT,"
                             "book_snapshot TEXT DEFAULT '{}');";
    sqlite3_exec(db, sql_legacy, 0, 0, 0);

    dbReady = true;
  }

  void logTrade(long long timestamp, const std::string &side, double price,
                double qty, double pnl, const std::string &reason) {
    if (!dbReady)
      return;

    // 1. Legacy
    std::stringstream ss;
    ss << "INSERT INTO trades (timestamp, session_id, side, price, qty, pnl, "
          "balance, reason, book_snapshot) VALUES ("
       << timestamp << ", '" << sessionId << "', '" << side << "', " << price
       << ", " << qty << ", " << pnl << ", " << balance << ", '" << reason
       << "', '{}');";
    sqlite3_exec(db, ss.str().c_str(), 0, 0, 0);

    // 2. Gen 3
    int64_t velocity = (int64_t)(strategy.signals.getTradeVelocity() * 100);
    int64_t vpin = strategy.signals.getVPIN();
    int64_t pxE8 = OrderBookL3::toFixed(price);
    int64_t pnlE8 = OrderBookL3::toFixed(pnl);

    std::stringstream ss2;
    ss2 << "INSERT INTO trade_log (timestamp_ns, symbol, side, strategy_type, "
           "entry_price, pnl_realized, trigger_reason, telemetry_velocity, "
           "telemetry_vpin, session_id) VALUES ("
        << timestamp * 1000000 << ", 'WLD/USDT', '" << side << "', '"
        << (reason.find("ROCKET") != std::string::npos ? "ROCKET_SURFER"
                                                       : "WICK_CATCHER")
        << "', " << pxE8 << ", " << pnlE8 << ", '" << reason << "', "
        << velocity << ", " << vpin << ", '" << sessionId << "');";

    sqlite3_exec(db, ss2.str().c_str(), 0, 0, 0);
  }

  void dumpOrderBook() {
    // Atomic Write: Write to .tmp then rename
    std::ofstream f("book_snapshot.tmp");
    f << "{\"bids\":[";
    for (size_t i = 0; i < std::min((size_t)5, displayBook.bids.size()); ++i) {
      f << "[" << displayBook.bids[i].price << "," << displayBook.bids[i].qty
        << "]"
        << (i < std::min((size_t)5, displayBook.bids.size()) - 1 ? "," : "");
    }
    f << "], \"asks\":[";
    for (size_t i = 0; i < std::min((size_t)5, displayBook.asks.size()); ++i) {
      f << "[" << displayBook.asks[i].price << "," << displayBook.asks[i].qty
        << "]"
        << (i < std::min((size_t)5, displayBook.asks.size()) - 1 ? "," : "");
    }
    f << "]}";
    f.close();
    std::rename("book_snapshot.tmp", "book_snapshot.json");
  }

  void dumpDashboardState() {
    double price = 0;
    if (!displayBook.bids.empty())
      price = displayBook.bids[0].price;

    std::ofstream jsonFile("dashboard.tmp");
    jsonFile << "{"
             << "\"session_id\": \"" << sessionId << "\","
             << "\"price\": " << price << ","
             << "\"velocity\": " << strategy.signals.getTradeVelocity() << ","
             << "\"position\": " << position << ","
             << "\"entry_price\": " << entryPrice << ","
             << "\"balance\": " << balance << ","
             << "\"orders\": [";

    bool first = true;
    for (const auto &o : orders) {
      if (!o.active)
        continue;
      if (!first)
        jsonFile << ",";
      jsonFile << "{\"id\":" << o.id << ",\"side\":\"" << o.side << "\""
               << ",\"price\":" << o.price << ",\"qty\":" << o.quantity
               << ",\"ts\":" << o.timestamp << ",\"type\":\"" << o.reason
               << "\"}";
      first = false;
    }
    jsonFile << "]}";
    jsonFile.close();
    std::rename("dashboard.tmp", "dashboard.json");
  }

  void parseLine(const std::string &line) {
    std::stringstream ss(line);
    std::string segment;
    std::vector<std::string> parts;
    while (std::getline(ss, segment, '|'))
      parts.push_back(segment);
    if (parts.empty())
      return;

    if (parts[0] == "DEPTH") {
      if (parts.size() < 4)
        return;
      onDepth(parts[2], parts[3]);
      updateStrategy();
    } else if (parts[0] == "TRADE") {
      if (parts.size() < 5)
        return;
      double price = std::stod(parts[3]);
      long long ts = std::stoll(parts[1]);

      MarketUpdate mu;
      mu.type = 'T';
      mu.timestamp_local = std::time(nullptr) * 1000000000LL;
      mu.timestamp_exchange = ts * 1000000;
      mu.price = OrderBookL3::toFixed(price);
      mu.size = OrderBookL3::toFixed(std::stod(parts[4]));
      mu.side = (parts[2] == "BUY" ? 'B' : 'A');
      strategy.update(mu);
      checkFills(price, ts);
    }
  }

  void onDepth(const std::string &bidsStr, const std::string &asksStr) {
    displayBook.bids.clear();
    displayBook.asks.clear();
    auto parse = [&](const std::string &s,
                     std::vector<DisplayOrderBook::Level> &out) {
      std::stringstream ss(s);
      std::string pair;
      while (std::getline(ss, pair, ',')) {
        size_t c = pair.find(':');
        if (c != std::string::npos)
          out.push_back(
              {std::stod(pair.substr(0, c)), std::stod(pair.substr(c + 1))});
      }
    };
    parse(bidsStr, displayBook.bids);
    parse(asksStr, displayBook.asks);
  }

  void updateStrategy() {
    strategy.book.clear();
    for (auto &x : displayBook.bids) {
      MarketUpdate mu;
      mu.side = 'B';
      mu.price = OrderBookL3::toFixed(x.price);
      mu.size = OrderBookL3::toFixed(x.qty);
      strategy.book.addOrder(mu);
    }
    for (auto &x : displayBook.asks) {
      MarketUpdate mu;
      mu.side = 'A';
      mu.price = OrderBookL3::toFixed(x.price);
      mu.size = OrderBookL3::toFixed(x.qty);
      strategy.book.addOrder(mu);
    }
    strategy.setPosition(static_cast<int64_t>(position));
    executeQuotes(strategy.getQuotes());
  }

  void executeQuotes(const Quotes &q) {
    if (q.reason == "SAFETY_LATENCY_GUARD")
      return;

    if (q.isTaker) {
      if (q.takerSide == 'B') {
        double p = (!displayBook.asks.empty()) ? displayBook.asks[0].price : 0;
        if (p > 0)
          placeOrder('B', p, minOrderValue / p, false, q.reason);
      } else {
        double p = (!displayBook.bids.empty()) ? displayBook.bids[0].price : 0;
        if (p > 0)
          placeOrder('A', p, minOrderValue / p, false, q.reason);
      }
      return;
    }

    // --- GEN 3/2: MAKER EXECUTION (WICK CATCHER / RANGE) ---
    // Minimal Maker implementation (re-adding logic snippet)
    // --- BID MANAGEMENT ---
    bool bidExists = false;
    for (auto &o : orders) {
      if (!o.active || o.side != 'B' || o.isExit)
        continue;
      if (!q.bidActive) {
        o.active = false;
      } else {
        double targetPx = OrderBookL3::toDouble(q.bidPx);
        if (std::abs(o.price - targetPx) > 1e-5) {
          o.active = false;
        } else {
          bidExists = true;
        }
      }
    }
    if (q.bidActive && !bidExists) {
      double px = OrderBookL3::toDouble(q.bidPx);
      if (px > 0)
        placeOrder('B', px, minOrderValue / px, false, q.reason);
    }

    // --- ASK MANAGEMENT ---
    bool askExists = false;
    for (auto &o : orders) {
      if (!o.active || o.side != 'A' || o.isExit)
        continue;
      if (!q.askActive) {
        o.active = false;
      } else {
        double targetPx = OrderBookL3::toDouble(q.askPx);
        if (std::abs(o.price - targetPx) > 1e-5) {
          o.active = false;
        } else {
          askExists = true;
        }
      }
    }
    if (q.askActive && !askExists) {
      double px = OrderBookL3::toDouble(q.askPx);
      if (px > 0)
        placeOrder('A', px, minOrderValue / px, false, q.reason);
    }
  }

  void placeOrder(char side, double price, double qty, bool isExit,
                  std::string reason = "MANUAL") {
    LiveOrder o;
    o.id = ++orderIdCounter;
    o.side = side;
    o.price = price;
    o.quantity = qty;
    o.active = true;
    o.isExit = isExit;
    o.timestamp = std::time(nullptr) * 1000;
    o.reason = reason;
    orders.push_back(o);
  }

  void checkFills(double tradePrice, long long ts) {
    for (auto &o : orders) {
      if (!o.active)
        continue;
      bool filled = (o.side == 'B' && tradePrice <= o.price) ||
                    (o.side == 'A' && tradePrice >= o.price);
      if (filled) {
        o.active = false;
        double pnl = 0;

        // PnL Logic Restore
        if (o.side == 'B') {   // BOUGHT
          if (position >= 0) { // Adding to Long
            double cost = position * entryPrice + o.quantity * o.price;
            position += o.quantity;
            entryPrice = cost / position;
            logTrade(ts, "BUY_LONG", o.price, o.quantity, 0, o.reason);
          } else { // Covering Short
            pnl = (entryPrice - o.price) * o.quantity;
            balance += pnl;
            position += o.quantity; // e.g. -10 + 10 = 0
            logTrade(ts, "BUY_COVER", o.price, o.quantity, pnl, o.reason);
            if (std::abs(position) < 1e-9) {
              position = 0;
              entryPrice = 0;
            }
          }
        } else {               // SOLD
          if (position <= 0) { // Adding to Short
            double cost =
                std::abs(position) * entryPrice + o.quantity * o.price;
            position -= o.quantity;
            entryPrice = cost / std::abs(position);
            logTrade(ts, "SELL_SHORT", o.price, o.quantity, 0, o.reason);
          } else { // Closing Long
            pnl = (o.price - entryPrice) * o.quantity;
            balance += pnl;
            position -= o.quantity;
            logTrade(ts, "SELL_CLOSE", o.price, o.quantity, pnl, o.reason);
            if (std::abs(position) < 1e-9) {
              position = 0;
              entryPrice = 0;
            }
          }
        }
      }
    }
  }

  // --- RICH DASHBOARD RESTORED ---
  void printDashboard() {
    long long now = std::time(nullptr);
    // Rate limit: 1 sec
    if (now - lastDashboardPrint < 1)
      return;
    lastDashboardPrint = now;

    // Clear Screen (ANSI)
    std::cout << "\033[2J\033[H";

    double spread = 0;
    double bestBid = 0, bestAsk = 0;
    if (!displayBook.bids.empty() && !displayBook.asks.empty()) {
      bestBid = displayBook.bids[0].price;
      bestAsk = displayBook.asks[0].price;
      spread = (bestAsk - bestBid) / bestBid * 100.0;
    }

    std::cout << "=========================================================="
              << std::endl;
    std::cout << "  🐺 CORTEX GEN 3 | OMNI-DIRECTIONAL | " << sessionId
              << std::endl;
    std::cout << "=========================================================="
              << std::endl;
    std::cout << " Status: RUNNING     | DB: Connected (hft_live.db)"
              << std::endl;
    std::cout << "----------------------------------------------------------"
              << std::endl;
    std::cout << " MARKET DATA" << std::endl;
    std::cout << " Price: " << std::fixed << std::setprecision(4) << bestAsk
              << " | Spread: " << std::setprecision(3) << spread << "%"
              << std::endl;
    std::cout << " Volty: " << strategy.signals.getTradeVelocity() << " tps"
              << " | VPIN: " << strategy.signals.getVPIN() << std::endl;
    std::cout << "----------------------------------------------------------"
              << std::endl;
    std::cout << " ACCOUNT" << std::endl;
    std::cout << " Balance: $" << std::setprecision(2) << balance
              << " | Position: " << position << " (" << (position * bestAsk)
              << " USD)" << std::endl;
    std::cout << " Avg Entry: " << std::setprecision(4) << entryPrice
              << std::endl;
    std::cout << "----------------------------------------------------------"
              << std::endl;
    std::cout << " ACTIVE ORDERS (" << orders.size() << ")" << std::endl;
    int shown = 0;
    for (auto &o : orders) {
      if (!o.active)
        continue;
      if (shown++ > 5) {
        std::cout << "... (+ more)" << std::endl;
        break;
      }
      std::cout << " [" << o.id << "] " << (o.side == 'B' ? "BID" : "ASK")
                << " " << o.quantity << " @ " << o.price << " [" << o.reason
                << "]" << std::endl;
    }
    std::cout << "=========================================================="
              << std::endl;
  }

  void checkAutoStop() {}
};
