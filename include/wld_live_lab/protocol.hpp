#pragma once

#include <cstdint>

namespace wld_live_lab {

inline constexpr int64_t PRICE_SCALE = 100'000'000LL;

inline constexpr uint8_t MSG_TYPE_TRADE          = 0x01;
inline constexpr uint8_t MSG_TYPE_DEPTH_SNAPSHOT = 0x03;
inline constexpr uint8_t MSG_TYPE_LIQUIDATION    = 0x04;
inline constexpr uint8_t MSG_TYPE_TICKER         = 0x05;

inline constexpr uint8_t SYMBOL_ID_WLDUSDT = 0x00;

inline constexpr int32_t SNAPSHOT_DEPTH = 50;

#pragma pack(push, 1)

struct TradePayload {
    int64_t timestamp;
    int64_t price;
    int64_t qty;
    bool    is_buyer_maker;
};
static_assert(sizeof(TradePayload) == 25, "TradePayload must be 25 bytes packed");

struct SnapshotPayload {
    int64_t timestamp;
    int64_t bid_px[SNAPSHOT_DEPTH];
    int64_t bid_qty[SNAPSHOT_DEPTH];
    int64_t ask_px[SNAPSHOT_DEPTH];
    int64_t ask_qty[SNAPSHOT_DEPTH];
};
static_assert(sizeof(SnapshotPayload) == 1608, "SnapshotPayload must be 1608 bytes packed");

struct LiquidationPayload {
    int64_t timestamp;
    int64_t price;
    int64_t qty;
    char    side;
};
static_assert(sizeof(LiquidationPayload) == 25, "LiquidationPayload must be 25 bytes packed");

struct TickerPayload {
    int64_t timestamp;
    int64_t open_interest;
    int64_t funding_rate;
    int64_t mark_price;
};
static_assert(sizeof(TickerPayload) == 32, "TickerPayload must be 32 bytes packed");

struct MarketMsg {
    uint8_t type;
    uint8_t symbol_id;
    union {
        TradePayload       trade;
        SnapshotPayload    snapshot;
        LiquidationPayload liq;
        TickerPayload      ticker;
    } payload;
};

#pragma pack(pop)

}
