#pragma once

#include "wld_live_lab/order_book.hpp"
#include "wld_live_lab/telemetry.hpp"
#include <cstdint>

namespace wld_live_lab {

struct QuoteDecision {
    int64_t bid_px{0};
    int64_t ask_px{0};
    bool    bid_active{false};
    bool    ask_active{false};
};

class IStrategy {
public:
    virtual ~IStrategy() = default;

    [[nodiscard]] virtual QuoteDecision evaluate(
        const OrderBook& book,
        const TelemetryEngine& telemetry,
        int64_t inventory_e8) const noexcept = 0;
};

class AvellanedaStoikovDemo final : public IStrategy {
private:
    int64_t half_spread_e8_;
    int64_t risk_aversion_bps_;

public:
    explicit AvellanedaStoikovDemo(
        int64_t half_spread_e8 = 20'000LL,
        int64_t risk_aversion_bps = 100LL) noexcept
        : half_spread_e8_(half_spread_e8),
          risk_aversion_bps_(risk_aversion_bps) {}

    [[nodiscard]] QuoteDecision evaluate(
        const OrderBook& book,
        const TelemetryEngine& telemetry,
        int64_t inventory_e8) const noexcept override {
        QuoteDecision decision{};

        if (telemetry.is_stale()) {
            return decision;
        }

        const int64_t micro_px = book.get_micro_price();
        if (micro_px <= 0) {
            return decision;
        }

        const int64_t inventory_units = inventory_e8 / PRICE_SCALE;
        const int64_t skew_offset     = inventory_units * risk_aversion_bps_;
        const int64_t reservation_px  = micro_px - skew_offset;

        int64_t bid_px = reservation_px - half_spread_e8_;
        int64_t ask_px = reservation_px + half_spread_e8_;

        if (bid_px >= ask_px) {
            const int64_t mid = (bid_px + ask_px) / 2;
            bid_px = mid - half_spread_e8_;
            ask_px = mid + half_spread_e8_;
        }

        decision.bid_px     = bid_px;
        decision.ask_px     = ask_px;
        decision.bid_active = true;
        decision.ask_active = true;
        return decision;
    }
};

}
