#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <cstdint>
static constexpr uint8_t TYPE_TRADE = 0x01;
static constexpr uint8_t TYPE_DEPTH_SNAPSHOT = 0x03;
static constexpr uint8_t ID_WLDUSDT = 0;
#pragma pack(push, 1)
struct TradePayload {
  int64_t timestamp;
  int64_t price;
  int64_t qty;
  bool is_buyer_maker;
};
struct SnapshotPayload {
  int64_t timestamp;
  int64_t bid_px[10];
  int64_t bid_qty[10];
  int64_t ask_px[10];
  int64_t ask_qty[10];
};
struct MarketMsg {
  uint8_t type;
  uint8_t symbol_id;
  union {
    TradePayload trade;
    SnapshotPayload snapshot;
  } payload;
};
#pragma pack(pop)
#endif
