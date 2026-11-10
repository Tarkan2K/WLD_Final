#pragma once

#include <cstdint>

namespace velox {

/** @brief Fixed-point scaling factor (10^8) for lossless monetary representation. */
inline constexpr int64_t PRICE_SCALE = 100'000'000LL;

/** @brief Wire message type discriminators. */
inline constexpr uint8_t MSG_TYPE_TRADE          = 0x01;
inline constexpr uint8_t MSG_TYPE_DEPTH_SNAPSHOT = 0x03;
inline constexpr uint8_t MSG_TYPE_LIQUIDATION    = 0x04;
inline constexpr uint8_t MSG_TYPE_TICKER         = 0x05;

/** @brief Instrument identifier mapping. */
inline constexpr uint8_t SYMBOL_ID_WLDUSDT = 0x00;

/** @brief Maximum order book depth serialized per snapshot frame. */
inline constexpr int32_t SNAPSHOT_DEPTH = 50;

#pragma pack(push, 1)

/**
 * @brief Packed trade execution record (25 bytes).
 */
struct TradePayload {
    int64_t timestamp;       ///< Exchange epoch timestamp (ms)
    int64_t price;           ///< Execution price scaled by 1e8
    int64_t qty;             ///< Executed quantity scaled by 1e8
    bool    is_buyer_maker;  ///< true: passive bid filled (taker sell); false: passive ask filled (taker buy)
};
static_assert(sizeof(TradePayload) == 25, "TradePayload must be 25 bytes packed");

/**
 * @brief Packed top-50 L2 depth snapshot record (1608 bytes).
 */
struct SnapshotPayload {
    int64_t timestamp;                 ///< Exchange epoch timestamp (ms)
    int64_t bid_px[SNAPSHOT_DEPTH];    ///< Descending bid prices (E8)
    int64_t bid_qty[SNAPSHOT_DEPTH];   ///< Corresponding bid quantities (E8)
    int64_t ask_px[SNAPSHOT_DEPTH];    ///< Ascending ask prices (E8)
    int64_t ask_qty[SNAPSHOT_DEPTH];   ///< Corresponding ask quantities (E8)
};
static_assert(sizeof(SnapshotPayload) == 1608, "SnapshotPayload must be 1608 bytes packed");

/**
 * @brief Packed forced liquidation record (25 bytes).
 */
struct LiquidationPayload {
    int64_t timestamp;  ///< Exchange epoch timestamp (ms)
    int64_t price;      ///< Bankruptcy/fill price scaled by 1e8
    int64_t qty;        ///< Liquidated quantity scaled by 1e8
    char    side;       ///< 'B' (Buy to cover short) or 'S' (Sell to liquidate long)
};
static_assert(sizeof(LiquidationPayload) == 25, "LiquidationPayload must be 25 bytes packed");

/**
 * @brief Packed derivatives telemetry record (32 bytes).
 */
struct TickerPayload {
    int64_t timestamp;      ///< Exchange epoch timestamp (ms)
    int64_t open_interest;  ///< Open interest scaled by 1e8
    int64_t funding_rate;   ///< Funding rate scaled by 1e8
    int64_t mark_price;     ///< Mark price scaled by 1e8
};
static_assert(sizeof(TickerPayload) == 32, "TickerPayload must be 32 bytes packed");

/**
 * @brief Tagged union envelope for IPC and disk serialization.
 */
struct MarketMsg {
    uint8_t type;       ///< Discriminator (MSG_TYPE_*)
    uint8_t symbol_id;  ///< Instrument identifier
    union {
        TradePayload       trade;
        SnapshotPayload    snapshot;
        LiquidationPayload liq;
        TickerPayload      ticker;
    } payload;
};

#pragma pack(pop)

} // namespace velox
