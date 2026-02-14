#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <cstdint>

static constexpr uint8_t TYPE_TRADE = 0x01;
static constexpr uint8_t TYPE_DEPTH_SNAPSHOT = 0x03;
static constexpr uint8_t TYPE_LIQ = 0x04;
static constexpr uint8_t TYPE_TICKER = 0x05;

static constexpr uint8_t ID_WLDUSDT = 0;

#pragma pack(push, 1)

struct TradePayload {
  int64_t timestamp;
  int64_t price;
  int64_t qty;
  bool is_buyer_maker; // true = Sell order filled (Taker Sell), false = Buy
                       // order filled (Taker Buy) ... Wait, standard convention
                       // is: isBuyerMaker=true -> SELL, isBuyerMaker=false ->
                       // BUY.
  // Actually, let's stick to simple CHAR for side to avoid confusion or bool
  // packing issues if not careful. But `is_buyer_maker` is standard in
  // Binance/Bybit. Let's use `char side` specifically for Liq to be safe?
  // Re-reading User Req: "LiquidationPayload ... char side".
  // Re-reading TradePayload: User said "Keep existing TradePayload".
  // Existing TradePayload had `bool is_buyer_maker`. We keep it.
};

struct SnapshotPayload {
  int64_t timestamp;
  int64_t bid_px[50]; // Increased to 50 as per goal "Orderbook (Depth 50)"
  int64_t bid_qty[50];
  int64_t ask_px[50];
  int64_t ask_qty[50];
};

struct LiquidationPayload {
  int64_t timestamp;
  int64_t price;
  int64_t qty;
  char side; // 'B' or 'S' (The side of the LIQUIDATION ORDER, e.g., Buy to
             // close Short)
};

struct TickerPayload {
  int64_t timestamp;
  int64_t open_interest;
  int64_t funding_rate;
  int64_t mark_price;
};

struct MarketMsg {
  uint8_t type;
  uint8_t symbol_id;
  union {
    TradePayload trade;
    SnapshotPayload snapshot;
    LiquidationPayload liq;
    TickerPayload ticker;
  } payload;
};

#pragma pack(pop)
#endif
