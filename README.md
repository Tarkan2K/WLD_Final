# WLD_Final
# HFT Market Data Engine

**WLD FINAL** is a high-performance market data ingestion and visualization system designed for the **WLD/USDT** perpetual contract on Bybit.

It features a **Dual-Mode Architecture** running on a single binary, allowing it to serve as both a "Black Box" recorder on AWS and a "Tactical Radar" on local machines, sharing the same core C++ processing logic.

## Key Features

### 1. Hybrid Architecture (IPC)
* **Ingestion (Python):** Handles WebSocket connection management, subscription to multiple streams (`Trade`, `Depth50`, `Liquidation`, `Ticker`), and data normalization.
* **Processing (C++17):** Ingests data via **Standard Input Pipe (`|`)** to minimize latency overhead.
* **Concurrency:** Implements a **Lock-Free Ring Buffer (SPSC)** to decouple the ingestion thread from the processing/writing thread.

### 2. Dual-Mode Operation
The system behavior changes based on runtime flags, optimizing for the environment:

* **Headless Mode (AWS/Server):**
    * **Goal:** Maximum throughput, zero data loss.
    * **Action:** Silently records all market events to binary files.
    * **Rotation:** Automatic file rotation every 60 minutes to manage storage efficiency.
    * **Output:** `data/history/market_data_YYYYMMDD_HHMM.bin`
    
* **Visual Mode (Local/Desktop):**
    * **Goal:** Real-time tactical analysis.
    * **Action:** Disables disk writes (protects SSD).
    * **Visualization:** Renders an **Inverse Liquidation Heatmap** in the terminal.
    * **Telemetry:** Displays Real-Time Open Interest, Funding Rates, and Mark Price.

### 3. Inverse Liquidation Logic
The engine calculates "Pain Zones" by analyzing Taker Flow in real-time:
* **Taker Buy:** Opens a Long -> Projected Liquidation is **BELOW** price.
* **Taker Sell:** Opens a Short -> Projected Liquidation is **ABOVE** price.
* **Confirmation:** Real liquidation events from the exchange validate and boost the heat zones.

### 4. Binary Protocol
Data is stored using a custom packed binary struct (`#pragma pack(1)`) to minimize I/O and disk usage.
* **Trades:** 25 bytes/record.
* **Depth Snapshots:** Compact fixed arrays.
* **Precision:** All math uses `int64_t` Fixed-Point arithmetic (E8 Scale) to avoid floating-point errors.

---

##**Installation & Build**

### Prerequisites
* **C++ Compiler:** `g++` (C++17 standard)
* **Python:** 3.8+ with `websocket-client`

### Compilation
```bash
cd src_cpp
make
# Output binary: ../bin/recorder
