# 📘 WLD_FINAL System Bluebook

**System Name**: Cortex Gen 3 / WLD Market Data Recorder
**Target Asset**: WLD/USDT (Bybit Linear Perpetual)
**Version**: Gen 3.1 (Dual-Mode: Recorder & Visualizer)

---

## 1. System Overview

This project has been overhauled into a professional **Dual-Mode System** capable of both high-fidelity data recording and real-time market visualization.

### Modes of Operation
1.  **Headless Recorder (AWS Mode)**:
    -   Optimized for server environments.
    -   Captures **Trades**, **Orderbook (Depth 50)**, **Liquidations**, and **Tickers**.
    -   Writes to binary files with automatic rotation every 60 minutes.
    -   No console output (silent operation).

2.  **Visualizer (Local Mode)**:
    -   Real-time **Inverse Liquidation Heatmap**.
    -   Displays live telemetry (Price, OI, Funding Rate).
    -   Visualizes estimated vs. real liquidation zones.
    -   **No disk writes** (Protects local SSD).

### Directory Structure

```
WLD_FINAL/
├── bin/                  # Compiled executables (recorder)
├── data/
│   └── history/          # Recorded binary market data (*.bin)
├── logs/                 # System logs
├── src_cpp/              # C++ Source Code
│   ├── recorder.cpp      # Core Event Loop & Mode Logic
│   ├── LiquidationEngine.hpp # Visualization Logic
│   ├── protocol.h        # Binary Protocol Definitions
│   └── ...
├── src_py/               # Python Source Code
│   ├── bybit_feed.py     # WebSocket Feed (Trades, Depth, Liq, Ticker)
│   └── ...
├── run_recorder.sh       # Unified startup script
└── Makefile              # Build configuration
```

---

## 2. Components Detail

### A. Data Feed (`src_py/bybit_feed.py`)
-   **Source**: Bybit V5 Public WebSocket.
-   **Channels**:
    -   `publicTrade.WLDUSDT`
    -   `orderbook.50.WLDUSDT` (Top 50 Levels)
    -   `liquidation.WLDUSDT` (Real liquidation events)
    -   `tickers.WLDUSDT` (Open Interest, Funding, Mark Price)
-   **Output**: Pipe-delimited stream to STDOUT.

### B. Core Engine (`src_cpp/recorder.cpp`)
-   **Dual-Mode Logic**: Parses command line arguments to switch between Recording and Visualization.
-   **Ring Buffer**: Lock-free buffering for high-throughput ingestion.
-   **File Writer**: Handles binary recording with hourly rotation (Headless Mode only).

### C. Visualization Engine (`src_cpp/LiquidationEngine.hpp`)
-   **Inverse Liquidation Logic**: Estimates liquidation levels based on Taker flow.
    -   *Taker Buy* -> Potential Long Liquidation (Price - 4%).
    -   *Taker Sell* -> Potential Short Liquidation (Price + 4%).
-   **Real Liquidation Confirmation**: Flashes/Boosts zones when real liquidation events occur.
-   **Dashboard**: Renders a color-coded text UI with telemetry.

---

## 3. Usage

### Prerequisites
-   Linux Environment
-   `g++` (supporting C++17)
-   Python 3 with `websocket-client`

### Build
```bash
cd src_cpp
make
# Output: ../bin/recorder
```

### Run
The system is controlled via `run_recorder.sh` with flags:

**1. AWS / Server Mode (Recording Only)**
```bash
./run_recorder.sh --headless
```
*   Silent operation.
*   Writes to `data/history/market_data_YYYYMMDD_HHMMSS.bin`.
*   Rotates files every hour.

**2. Local / Visual Mode (Dashboard Only)**
```bash
./run_recorder.sh --visual-only
```
*   Displays **Inverse Liquidation Heatmap**.
*   Shows Real-Time Ticker Info (OI, Funding).
*   **Does NOT write to disk.**

---

## 4. Data Formats

### Binary Protocol (`protocol.h`)
The recorder saves data in a packed binary format.

**Header**:
```cpp
struct MarketMsg {
  uint8_t type;      // 0x01=TRADE, 0x03=DEPTH, 0x04=LIQ, 0x05=TICKER
  uint8_t symbol_id; // 0 = WLDUSDT
  union {
    TradePayload trade;
    SnapshotPayload snapshot;
    LiquidationPayload liq;
    TickerPayload ticker;
  } payload;
};
```

**Payloads**:
-   **Trade**: `ts`, `price`, `qty`, `is_buyer_maker`
-   **Snapshot**: `ts`, `bid_px[50]`, `bid_qty[50]`, `ask_px[50]`, `ask_qty[50]`
-   **Liquidation**: `ts`, `price`, `qty`, `side` ('B'/'S')
-   **Ticker**: `ts`, `open_interest`, `funding_rate`, `mark_price`

### Pipe Protocol (IPC)
Format: `TYPE|timestamp|symbol|...`

-   **TRADE**: `TRADE|ts|WLDUSDT|SIDE|px|qty`
-   **DEPTH**: `DEPTH|ts|WLDUSDT|bid1:q1,bid2:q2...|ask1:q1,ask2:q2...`
-   **LIQ**: `LIQ|ts|WLDUSDT|SIDE|px|qty`
-   **TICKER**: `TICKER|ts|WLDUSDT|oi|funding|mark_price`

---
