#include "LiquidationEngine.hpp"
#include "protocol.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

// --- CONFIGURATION ---
static constexpr size_t RING_BUFFER_SIZE = 65536 * 4; // Increased for Depth50
static constexpr size_t WRITE_BUFFER_SIZE =
    1024 * 1024; // 1MB Chunks for disk write

// --- LOCK-FREE RING BUFFER ---
template <typename T, size_t Size> class RingBuffer {
private:
  std::array<T, Size> buffer;
  alignas(64) std::atomic<size_t> head{0};
  alignas(64) std::atomic<size_t> tail{0};

public:
  bool push(const T &item) {
    size_t current_head = head.load(std::memory_order_relaxed);
    size_t next_head = (current_head + 1) % Size;
    if (next_head == tail.load(std::memory_order_acquire))
      return false;
    buffer[current_head] = item;
    head.store(next_head, std::memory_order_release);
    return true;
  }

  bool pop(T &item) {
    size_t current_tail = tail.load(std::memory_order_relaxed);
    if (current_tail == head.load(std::memory_order_acquire))
      return false;
    item = buffer[current_tail];
    tail.store((current_tail + 1) % Size, std::memory_order_release);
    return true;
  }
};

RingBuffer<MarketMsg, RING_BUFFER_SIZE> ringBuffer;
std::atomic<bool> running{true};
std::string FILE_PREFIX = "market_data";

// MODES
bool MODE_HEADLESS = false;
bool MODE_VISUAL = false;

// Engine for Visual Mode
LiquidationEngine visualEngine;

// --- HELPER: E8 Conversion ---
int64_t toFixedE8(const std::string &val_str) {
  try {
    double v = std::stod(val_str);
    return static_cast<int64_t>(v * 100000000.0);
  } catch (...) {
    return 0;
  }
}

// Writer Class with Rotation
class FileWriter {
  std::ofstream file;
  char *buffer;
  size_t bufPos = 0;
  std::string currentSuffix;
  std::chrono::system_clock::time_point lastRotation;

public:
  FileWriter() {
    // Allocate heap for large buffer
    buffer = new char[WRITE_BUFFER_SIZE];
  }
  ~FileWriter() { delete[] buffer; }

  void checkRotation() {
    auto now = std::chrono::system_clock::now();
    // Rotate every 60 minutes
    if (std::chrono::duration_cast<std::chrono::minutes>(now - lastRotation)
                .count() >= 60 ||
        !file.is_open()) {
      rotate();
    }
  }

  void rotate() {
    if (file.is_open()) {
      flush(true);
      file.close();
    }

    auto now = std::chrono::system_clock::now();
    lastRotation = now;
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time_t);
    std::stringstream ss;
    ss << "data/history/" << FILE_PREFIX << "_"
       << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".bin";

    std::string path = ss.str();
    std::cerr << "[REC] Rotating to: " << path << std::endl;

    file.open(path, std::ios::binary | std::ios::app);
  }

  void write(const char *data, size_t len) {
    if (!file.is_open())
      return;

    if (bufPos + len > WRITE_BUFFER_SIZE)
      flush();
    std::memcpy(&buffer[bufPos], data, len);
    bufPos += len;
  }

  void flush(bool force = false) {
    if (bufPos > 0 && file.is_open()) {
      file.write(buffer, bufPos);
      bufPos = 0;
      if (force)
        file.flush();
    }
  }
};

void consumerThreadFunc() {
  FileWriter writer;
  if (MODE_HEADLESS) {
    writer.rotate(); // Initial Open
  }

  MarketMsg msg;
  auto lastFlush = std::chrono::steady_clock::now();
  auto lastDash = std::chrono::steady_clock::now(); // For Visual Update

  // Temp buffer for packet serialization
  // Depth50 is large: 8 + (8*50) + (8*50) + (8*50) + (8*50) = 8 + 400 + 400 +
  // 400 + 400 = 1608 bytes + struct overhead.
  char packBuf[4096];

  while (running.load(std::memory_order_relaxed) || true) {
    bool popped = ringBuffer.pop(msg);
    if (popped) {

      // --- VISUAL MODE LOGIC ---
      if (MODE_VISUAL) {
        if (msg.type == TYPE_TRADE) {
          double px = (double)msg.payload.trade.price / 100000000.0;
          double qty = (double)msg.payload.trade.qty / 100000000.0;
          visualEngine.onTrade(
              px, qty,
              msg.payload.trade.is_buyer_maker); // Stored Char/Bool check
        } else if (msg.type == TYPE_LIQ) {
          double px = (double)msg.payload.liq.price / 100000000.0;
          double qty = (double)msg.payload.liq.qty / 100000000.0;
          visualEngine.onLiquidation(px, qty, msg.payload.liq.side);
        } else if (msg.type == TYPE_TICKER) {
          visualEngine.onTicker(msg.payload.ticker.open_interest,
                                msg.payload.ticker.funding_rate,
                                msg.payload.ticker.mark_price);
        }
      }

      // --- HEADLESS MODE LOGIC (WRITE TO DISK) ---
      if (MODE_HEADLESS) {
        writer.checkRotation();

        size_t packLen = 0;
        packBuf[0] = msg.type;
        packBuf[1] = msg.symbol_id;
        packLen = 2;

        if (msg.type == TYPE_TRADE) {
          std::memcpy(&packBuf[packLen], &msg.payload.trade,
                      sizeof(TradePayload));
          packLen += sizeof(TradePayload);
        } else if (msg.type == TYPE_DEPTH_SNAPSHOT) {
          std::memcpy(&packBuf[packLen], &msg.payload.snapshot,
                      sizeof(SnapshotPayload));
          packLen += sizeof(SnapshotPayload);
        } else if (msg.type == TYPE_LIQ) {
          std::memcpy(&packBuf[packLen], &msg.payload.liq,
                      sizeof(LiquidationPayload));
          packLen += sizeof(LiquidationPayload);
        } else if (msg.type == TYPE_TICKER) {
          std::memcpy(&packBuf[packLen], &msg.payload.ticker,
                      sizeof(TickerPayload));
          packLen += sizeof(TickerPayload);
        }

        writer.write(packBuf, packLen);
      }

    } else {
      if (!running.load(std::memory_order_relaxed))
        break;
      std::this_thread::yield();
    }

    auto now = std::chrono::steady_clock::now();

    // Disk Flush (1s)
    if (MODE_HEADLESS &&
        std::chrono::duration_cast<std::chrono::seconds>(now - lastFlush)
                .count() >= 1) {
      writer.flush(true);
      lastFlush = now;
    }

    // Visual Refresh (100ms)
    if (MODE_VISUAL &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDash)
                .count() >= 100) {
      visualEngine.printDashboard();
      lastDash = now;
    }
  }
}

int main(int argc, char *argv[]) {
  // Argument Parsing
  if (argc > 1) {
    std::string arg1 = argv[1];
    if (arg1 == "--headless") {
      MODE_HEADLESS = true;
      std::cerr << ">>> STARTING IN HEADLESS MODE (RECORDING ONLY) <<<"
                << std::endl;
    } else if (arg1 == "--visual-only") {
      MODE_VISUAL = true;
      std::cerr << ">>> STARTING IN VISUAL MODE (NO DISK WRITE) <<<"
                << std::endl;
    } else {
      // Default fallback or help
      std::cerr << "Usage: ./recorder [--headless | --visual-only]"
                << std::endl;
      return 1;
    }
  } else {
    std::cerr << "Usage: ./recorder [--headless | --visual-only]" << std::endl;
    return 1;
  }

  std::ios_base::sync_with_stdio(false);
  std::cin.tie(NULL);

  std::thread consumer(consumerThreadFunc);

  std::string line;
  line.reserve(65536); // Reserve big for Depth lines

  while (std::getline(std::cin, line)) {
    if (line.empty())
      continue;

    try {
      std::stringstream ss(line);
      std::string segment;
      std::vector<std::string> parts;
      // Optimization: Custom Split might be faster but stringstream is safer
      // for now
      while (std::getline(ss, segment, '|'))
        parts.push_back(segment);

      if (parts.empty())
        continue;
      std::string type = parts[0];

      if (type == "TRADE" && parts.size() >= 6) {
        // TRADE|ts|WLDUSDT|side|price|qty
        MarketMsg msg;
        msg.type = TYPE_TRADE;
        msg.symbol_id = ID_WLDUSDT;
        msg.payload.trade.timestamp = std::stoll(parts[1]);
        msg.payload.trade.price = toFixedE8(parts[4]);
        msg.payload.trade.qty = toFixedE8(parts[5]);
        // logic: is_buyer_maker. If Side == "SELL", is_buyer_maker = true.
        msg.payload.trade.is_buyer_maker = (parts[3] == "SELL");

        ringBuffer.push(msg);

      } else if (type == "DEPTH" && parts.size() >= 5) {
        // DEPTH|ts|WLDUSDT|bids|asks
        if (MODE_VISUAL)
          continue; // Skip Depth parsing in Visual Mode

        MarketMsg msg;
        msg.type = TYPE_DEPTH_SNAPSHOT;
        msg.symbol_id = ID_WLDUSDT;
        msg.payload.snapshot.timestamp = std::stoll(parts[1]);

        // Parse Bids (Top 50)
        auto parseStr = [&](const std::string &s, int64_t *px, int64_t *qty) {
          std::stringstream ss2(s);
          std::string p;
          int i = 0;
          while (std::getline(ss2, p, ',') && i < 50) {
            size_t c = p.find(':');
            if (c != std::string::npos) {
              px[i] = toFixedE8(p.substr(0, c));
              qty[i] = toFixedE8(p.substr(c + 1));
            }
            i++;
          }
          // Fill remainder with 0? Already struct init? Not strictly, array is
          // junk. Should memset 0 if needed. But we just save it. Protocol is
          // fixed size. Ideally we should zero out rest.
          for (; i < 50; i++) {
            px[i] = 0;
            qty[i] = 0;
          }
        };

        parseStr(parts[3], msg.payload.snapshot.bid_px,
                 msg.payload.snapshot.bid_qty);
        parseStr(parts[4], msg.payload.snapshot.ask_px,
                 msg.payload.snapshot.ask_qty);

        ringBuffer.push(msg);

      } else if (type == "LIQ" && parts.size() >= 6) {
        // LIQ|ts|sym|side|px|qty
        MarketMsg msg;
        msg.type = TYPE_LIQ;
        msg.symbol_id = ID_WLDUSDT;
        msg.payload.liq.timestamp = std::stoll(parts[1]);
        msg.payload.liq.price = toFixedE8(parts[4]);
        msg.payload.liq.qty = toFixedE8(parts[5]);
        msg.payload.liq.side = parts[3][0]; // First char of "Buy"/"Sell"

        ringBuffer.push(msg);

      } else if (type == "TICKER" && parts.size() >= 6) {
        // TICKER|ts|sym|oi|funding|mark
        MarketMsg msg;
        msg.type = TYPE_TICKER;
        msg.symbol_id = ID_WLDUSDT;
        // Note: Py feed sends floats for OI/Funding usually?
        // "fundingRate": "0.0001", "markPrice": "..."
        // "openInterest": "23232.23"
        msg.payload.ticker.timestamp = std::stoll(parts[1]);
        msg.payload.ticker.open_interest = toFixedE8(parts[3]);
        msg.payload.ticker.funding_rate = toFixedE8(parts[4]);
        msg.payload.ticker.mark_price = toFixedE8(parts[5]);

        ringBuffer.push(msg);
      }

    } catch (...) {
      // Parse error, ignore line
    }
  }

  running.store(false);
  consumer.join();
  return 0;
}
