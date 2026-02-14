// ... (Headers remain same) ...
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
static constexpr size_t RING_BUFFER_SIZE = 65536;
static constexpr size_t WRITE_BUFFER_SIZE = 65536; // 64KB Chunks

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

// --- HELPER: E8 Conversion ---
int64_t toFixedE8(const std::string &val_str) {
  int64_t val = 0;
  int64_t frac = 0;
  int frac_cnt = 0;
  bool dec = false;
  for (char c : val_str) {
    if (c == '.') {
      dec = true;
      continue;
    }
    if (dec) {
      if (frac_cnt < 8) {
        frac = (frac * 10) + (c - '0');
        frac_cnt++;
      }
    } else {
      val = (val * 10) + (c - '0');
    }
  }
  while (frac_cnt < 8) {
    frac *= 10;
    frac_cnt++;
  }
  return (val * 100000000LL) + frac;
}

std::string generateFilename(const std::string &suffix) {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm = *std::localtime(&time_t);
  std::stringstream ss;
  ss << "data/history/" << FILE_PREFIX << "_" << suffix << "_"
     << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".bin";
  return ss.str();
}

// Writer Class handles one file buffer
class FileWriter {
  std::ofstream file;
  char buffer[WRITE_BUFFER_SIZE];
  size_t bufPos = 0;

public:
  void open(const std::string &path) {
    file.open(path, std::ios::binary | std::ios::app);
    if (!file.is_open())
      std::cerr << "[ERROR] Cannot open " << path << std::endl;
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
  void close() {
    flush(true);
    if (file.is_open())
      file.close();
  }
};

void writerThreadFunc() {
  std::string fn_master = generateFilename("MASTER");

  std::cerr << "[REC] Master: " << fn_master << std::endl;

  FileWriter fMaster;
  fMaster.open(fn_master);

  MarketMsg msg;
  auto lastFlush = std::chrono::steady_clock::now();

  // Temp buffer for packet serialization
  char packBuf[1024];

  while (running.load(std::memory_order_relaxed) || true) {
    bool popped = ringBuffer.pop(msg);
    if (popped) {
      // Serialize Packet
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
      }

      // Write to Master (Only WLD)
      if (msg.symbol_id == ID_WLDUSDT) {
        fMaster.write(packBuf, packLen);
      }

    } else {
      if (!running.load(std::memory_order_relaxed))
        break;
      std::this_thread::yield();
    }

    // Time Flush (1s)
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastFlush)
            .count() >= 1) {
      fMaster.flush(true);
      lastFlush = now;
    }
  }

  fMaster.close();
}

int main(int argc, char *argv[]) {
  if (argc > 1) {
    FILE_PREFIX = argv[1];
  }

  std::ios_base::sync_with_stdio(false);
  std::cin.tie(NULL);

  std::thread writer(writerThreadFunc);
  std::string line;
  line.reserve(4096);

  while (std::getline(std::cin, line)) {
    if (line.empty())
      continue;

    // Line Format: TYPE|TS|SYMBOL|...

    if (line.compare(0, 6, "TRADE|") == 0) {
      // TRADE|162...|WLDUSDT|SELL|0.402|100
      std::stringstream ss(line);
      std::string seg;
      std::vector<std::string> parts;
      while (std::getline(ss, seg, '|'))
        parts.push_back(seg);

      if (parts.size() < 6)
        continue;

      try {
        MarketMsg msg;
        msg.type = TYPE_TRADE;

        // Parse Symbol
        std::string sym = parts[2]; // Index 2
        if (sym == "WLDUSDT")
          msg.symbol_id = ID_WLDUSDT;
        else
          continue; // Unknown symbol

        msg.payload.trade.timestamp = std::stoll(parts[1]);
        msg.payload.trade.is_buyer_maker = (parts[3] == "SELL");
        msg.payload.trade.price = toFixedE8(parts[4]);
        msg.payload.trade.qty = toFixedE8(parts[5]);

        ringBuffer.push(msg);
      } catch (...) {
      }

    } else if (line.compare(0, 6, "DEPTH|") == 0) {
      // DEPTH|162...|WLDUSDT|BIDS|ASKS
      std::stringstream ss(line);
      std::string seg;
      std::vector<std::string> parts;
      while (std::getline(ss, seg, '|'))
        parts.push_back(seg);

      if (parts.size() < 5)
        continue;

      try {
        MarketMsg msg;
        msg.type = TYPE_DEPTH_SNAPSHOT;

        std::string sym = parts[2];
        if (sym == "WLDUSDT")
          msg.symbol_id = ID_WLDUSDT;
        else
          continue;

        msg.payload.snapshot.timestamp = std::stoll(parts[1]);

        // Parse Bids (Part 3) and Asks (Part 4)
        // Helper lambda to parse "px:qty,px:qty"
        auto parseBook = [&](const std::string &s, int64_t *px_arr,
                             int64_t *qty_arr) {
          int idx = 0;
          std::stringstream ss2(s);
          std::string pair;
          while (std::getline(ss2, pair, ',') && idx < 10) {
            size_t c = pair.find(':');
            if (c != std::string::npos) {
              px_arr[idx] = toFixedE8(pair.substr(0, c));
              qty_arr[idx] = toFixedE8(pair.substr(c + 1));
            }
            idx++;
          }
        };

        parseBook(parts[3], msg.payload.snapshot.bid_px,
                  msg.payload.snapshot.bid_qty);
        parseBook(parts[4], msg.payload.snapshot.ask_px,
                  msg.payload.snapshot.ask_qty);

        ringBuffer.push(msg);
      } catch (...) {
      }
    }
  }

  running.store(false);
  writer.join();
  return 0;
}
