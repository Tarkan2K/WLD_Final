# 📘 WLD_FINAL System Bluebook

**System Name**: Cortex Gen 3 / WLD Market Data Recorder
**Target Asset**: WLD/USDT (Bybit Linear Perpetual)
**Version**: Gen 3 (Development/Recording Phase)

---

## 1. System Overview

This project consists of two distinct subsystems:
1.  **Market Data Recorder**: A high-performance C++ recorder that ingests real-time Bybit WebSocket data and persists it to binary files for analysis/backtesting.
2.  **HFT Strategy Engine (Cortex Gen 3)**: A low-latency trading engine (headers only, currently dormant) designed to execute the "Sniper" and "Trap Hunter" strategies.

### Directory Structure

```
WLD_FINAL/
├── bin/                  # Compiled executables (recorder)
├── data/
│   └── history/          # Recorded binary market data (*.bin)
├── logs/                 # System logs
├── src_cpp/              # C++ Source Code (Recorder + Strategy Engine)
├── src_py/               # Python Source Code (Feed + Execution Gateway)
├── run_recorder.sh       # Startup script for the recorder
└── Makefile              # Build configuration
```

---

## 2. Components Detail

### A. Data Recorder (Active)
**Source**: `src_cpp/recorder.cpp`, `src_py/bybit_feed.py`
**Architecture**: Python Feed -> Pipe (`|`) -> C++ Recorder

1.  **Feed (`src_py/bybit_feed.py`)**:
    -   Connects to Bybit V5 Public WebSocket.
    -   Subscribes to `publicTrade.WLDUSDT`.
    -   Normalizes data and prints to STDOUT in a pipe-delimited format:
        `TRADE|timestamp|symbol|side|price|qty`

2.  **Recorder (`src_cpp/recorder.cpp`)**:
    -   Reads STDIN from the Python feed.
    -   Parses `TRADE` and `DEPTH` messages.
    -   Push messages to a **Lock-Free Ring Buffer** (64KB).
    -   **Writer Thread**: Pops from buffer and appends to binary files in `data/history/`.
    -   **Format**: Custom binary protocol defined in `src_cpp/protocol.h`.

### B. Cortex Gen 3 Strategy Engine (In Development)
**Source**: `src_cpp/LiveEngine.hpp`, `src_cpp/MarketMakerGen2.hpp`
**Status**: Implemented in headers but not linked in the current `Makefile` target.

-   **Classes**:
    -   `LiveEngine`: Main loop, manages `sqlite3` database for trade logging, prints specific dashboard.
    -   `MarketMakerGen2`: Implements "Sniper" strategy (Zero-Alloc, C++20).
    -   `OrderBookL3`: Internal order book representation.
    -   `SignalEngine`: Signal processing (Velocity, VPIN, Trap detection).
-   **Strategy Logic**:
    -   **Micro-Price** calculation.
    -   **Inventory Skew**: Adjusts quotes based on current position and risk aversion.
    -   **Trap Detection**: Identifies Bull/Bear traps to shift quotes aggressively.

### C. Nerve Gateway (Legacy/Standby)
**Source**: `src_py/brain_legacy.py`
**Purpose**: Execution gateway for the strategy.
-   Listens to STDIN for `SIGNAL` messages from the C++ engine.
-   Executes orders on Bybit using `pybit`.
-   Manages Position Guard, Risk checks, and dynamic TP/SL adjustments.

---

## 3. Usage

### Prerequisites
-   Linux Environment
-   `g++` (supporting C++17)
-   Python 3 with `websocket-client` and `pybit`
-   `sqlite3` (for Strategy Engine)

### Build
To compile the C++ recorder:
```bash
cd src_cpp
make
# Output: ../bin/recorder
```

### Run Recorder
To start recording market data:
```bash
./run_recorder.sh
```
*Note: This starts the Python feed and pipes it to the recorder binary.*

---

## 4. Data Formats

### Binary Protocol (`protocol.h`)
The recorder saves data in a packed binary format:
```cpp
struct MarketMsg {
  uint8_t type;      // 0x01 = TRADE, 0x03 = DEPTH
  uint8_t symbol_id; // 0 = WLDUSDT
  union {
    TradePayload trade;
    SnapshotPayload snapshot;
  } payload;
};
```

### CSV/Pipe Protocol
Used for Inter-Process Communication (Python -> C++):
-   **Trade**: `TRADE|timestamp_ms|WLDUSDT|SIDE|price|qty`
-   **Depth**: `DEPTH|timestamp_ms|WLDUSDT|bids_str|asks_str`

---

## 5. Deployment Notes
-   **Secrets**: API Keys are loaded from Environment Variables (`.env`) in `brain_legacy.py` and `run_recorder.sh` (if customized). The Recorder itself does not require API keys.
-   **Database**: The Strategy Engine writes to `hft_live.db` (SQLite) when active.

