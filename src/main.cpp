#include "file_writer.hpp"
#include "wld_live_lab/order_book.hpp"
#include "wld_live_lab/protocol.hpp"
#include "wld_live_lab/ring_buffer.hpp"
#include "wld_live_lab/strategy_interface.hpp"
#include "wld_live_lab/telemetry.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr size_t RING_BUFFER_CAPACITY = 262'144;

wld_live_lab::RingBuffer<wld_live_lab::MarketMsg, RING_BUFFER_CAPACITY> g_ring_buffer;
std::atomic<bool> g_running{true};

void signal_handler(int) noexcept {
    g_running.store(false, std::memory_order_release);
}

[[nodiscard]] inline std::string_view next_token(std::string_view& sv, char delimiter = '|') noexcept {
    const size_t pos = sv.find(delimiter);
    if (pos == std::string_view::npos) {
        const std::string_view token = sv;
        sv = std::string_view{};
        return token;
    }
    const std::string_view token = sv.substr(0, pos);
    sv.remove_prefix(pos + 1);
    return token;
}

[[nodiscard]] inline int64_t parse_int64(std::string_view sv) noexcept {
    int64_t val = 0;
    std::from_chars(sv.data(), sv.data() + sv.size(), val);
    return val;
}

[[nodiscard]] int64_t parse_fixed_e8(std::string_view sv) noexcept {
    if (sv.empty()) {
        return 0;
    }

    bool negative = false;
    if (sv.front() == '-') {
        negative = true;
        sv.remove_prefix(1);
    } else if (sv.front() == '+') {
        sv.remove_prefix(1);
    }

    const size_t dot_pos = sv.find('.');
    const std::string_view int_part = (dot_pos == std::string_view::npos) ? sv : sv.substr(0, dot_pos);
    const std::string_view frac_part =
        (dot_pos == std::string_view::npos) ? std::string_view{} : sv.substr(dot_pos + 1);

    int64_t integral = 0;
    if (!int_part.empty()) {
        std::from_chars(int_part.data(), int_part.data() + int_part.size(), integral);
    }

    int64_t fractional = 0;
    if (!frac_part.empty()) {
        const size_t digits = std::min<size_t>(8, frac_part.size());
        std::from_chars(frac_part.data(), frac_part.data() + digits, fractional);
        for (size_t i = digits; i < 8; ++i) {
            fractional *= 10;
        }
    }

    const int64_t scaled = (integral * wld_live_lab::PRICE_SCALE) + fractional;
    return negative ? -scaled : scaled;
}

void parse_depth_side(std::string_view side_sv, int64_t* px_arr, int64_t* qty_arr) noexcept {
    int idx = 0;
    while (!side_sv.empty() && idx < wld_live_lab::SNAPSHOT_DEPTH) {
        const std::string_view pair = next_token(side_sv, ',');
        const size_t colon = pair.find(':');
        if (colon != std::string_view::npos) {
            px_arr[idx]  = parse_fixed_e8(pair.substr(0, colon));
            qty_arr[idx] = parse_fixed_e8(pair.substr(colon + 1));
            ++idx;
        }
    }
    for (; idx < wld_live_lab::SNAPSHOT_DEPTH; ++idx) {
        px_arr[idx]  = 0;
        qty_arr[idx] = 0;
    }
}

void consumer_thread_loop(bool headless_mode, bool visual_mode) {
    wld_live_lab::FileWriter            writer;
    wld_live_lab::OrderBook             book;
    wld_live_lab::TelemetryEngine       telemetry;
    wld_live_lab::AvellanedaStoikovDemo strategy;

    if (headless_mode) {
        writer.rotate();
    }

    char pack_buf[2048];
    auto last_flush = std::chrono::steady_clock::now();
    auto last_ui    = std::chrono::steady_clock::now();

    int64_t last_oi_e8      = 0;
    int64_t last_funding_e8 = 0;
    int64_t last_mark_e8    = 0;

    wld_live_lab::MarketMsg msg{};

    while (g_running.load(std::memory_order_acquire) || !g_ring_buffer.empty()) {
        if (g_ring_buffer.pop(msg)) {
            const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();

            if (msg.type == wld_live_lab::MSG_TYPE_TRADE) {
                wld_live_lab::MarketUpdate mu{};
                mu.timestamp_exchange = msg.payload.trade.timestamp * 1'000'000LL;
                mu.timestamp_local    = now_ns;
                mu.price              = msg.payload.trade.price;
                mu.size               = msg.payload.trade.qty;
                mu.side               = msg.payload.trade.is_buyer_maker ? 'A' : 'B';
                mu.type               = 'T';
                telemetry.add_event(mu);
            } else if (msg.type == wld_live_lab::MSG_TYPE_DEPTH_SNAPSHOT) {
                book.load_snapshot(msg.payload.snapshot);
            } else if (msg.type == wld_live_lab::MSG_TYPE_TICKER) {
                last_oi_e8      = msg.payload.ticker.open_interest;
                last_funding_e8 = msg.payload.ticker.funding_rate;
                last_mark_e8    = msg.payload.ticker.mark_price;
            }

            if (headless_mode) {
                writer.check_rotation();
                pack_buf[0] = static_cast<char>(msg.type);
                pack_buf[1] = static_cast<char>(msg.symbol_id);
                size_t pack_len = 2;

                if (msg.type == wld_live_lab::MSG_TYPE_TRADE) {
                    std::memcpy(&pack_buf[pack_len], &msg.payload.trade, sizeof(wld_live_lab::TradePayload));
                    pack_len += sizeof(wld_live_lab::TradePayload);
                } else if (msg.type == wld_live_lab::MSG_TYPE_DEPTH_SNAPSHOT) {
                    std::memcpy(&pack_buf[pack_len], &msg.payload.snapshot, sizeof(wld_live_lab::SnapshotPayload));
                    pack_len += sizeof(wld_live_lab::SnapshotPayload);
                } else if (msg.type == wld_live_lab::MSG_TYPE_LIQUIDATION) {
                    std::memcpy(&pack_buf[pack_len], &msg.payload.liq, sizeof(wld_live_lab::LiquidationPayload));
                    pack_len += sizeof(wld_live_lab::LiquidationPayload);
                } else if (msg.type == wld_live_lab::MSG_TYPE_TICKER) {
                    std::memcpy(&pack_buf[pack_len], &msg.payload.ticker, sizeof(wld_live_lab::TickerPayload));
                    pack_len += sizeof(wld_live_lab::TickerPayload);
                }

                writer.write(pack_buf, pack_len);
            }
        } else {
            std::this_thread::yield();
        }

        const auto now = std::chrono::steady_clock::now();

        if (headless_mode &&
            std::chrono::duration_cast<std::chrono::seconds>(now - last_flush).count() >= 1) {
            writer.flush(true);
            last_flush = now;
        }

        if (visual_mode &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ui).count() >= 200) {
            const auto quotes = strategy.evaluate(book, telemetry, 0);
            std::cout << "\033[2J\033[H"
                      << "==============================================================\n"
                      << "  WLD LIVE LAB | C++20 ZERO-ALLOCATION TELEMETRY\n"
                      << "==============================================================\n"
                      << std::fixed << std::setprecision(4)
                      << " Micro-Price : " << wld_live_lab::OrderBook::to_double(book.get_micro_price()) << " USDT\n"
                      << " Mark Price  : " << wld_live_lab::OrderBook::to_double(last_mark_e8) << " USDT\n"
                      << " Imbalance   : " << std::setprecision(2)
                      << (wld_live_lab::OrderBook::to_double(book.get_imbalance()) * 100.0) << "%\n"
                      << " VPIN Proxy  : " << (wld_live_lab::OrderBook::to_double(telemetry.get_vpin()) * 100.0) << "%\n"
                      << " Velocity    : " << telemetry.get_trade_velocity() << " trades/sec\n"
                      << " Open Int.   : " << std::setprecision(0)
                      << wld_live_lab::OrderBook::to_double(last_oi_e8) << " WLD\n"
                      << " Funding     : " << std::setprecision(6)
                      << wld_live_lab::OrderBook::to_double(last_funding_e8) << "\n"
                      << "--------------------------------------------------------------\n"
                      << " Avellaneda-Stoikov Demo Quotes:\n"
                      << std::setprecision(4)
                      << "   Bid: " << (quotes.bid_active ? wld_live_lab::OrderBook::to_double(quotes.bid_px) : 0.0)
                      << " | Ask: " << (quotes.ask_active ? wld_live_lab::OrderBook::to_double(quotes.ask_px) : 0.0) << "\n"
                      << "==============================================================\n"
                      << std::flush;
            last_ui = now;
        }
    }

    if (headless_mode) {
        writer.close();
    }
}

}

int main(int argc, char* argv[]) {
    bool headless_mode = false;
    bool visual_mode   = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--headless") {
            headless_mode = true;
        } else if (arg == "--visual-only") {
            visual_mode = true;
        }
    }

    if (!headless_mode && !visual_mode) {
        std::cerr << "Usage: wld_live_lab_engine [--headless | --visual-only]\n";
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::thread consumer(consumer_thread_loop, headless_mode, visual_mode);

    std::string line;
    line.reserve(65536);

    while (g_running.load(std::memory_order_relaxed) && std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }

        std::string_view sv(line);
        if (!sv.empty() && sv.back() == '\r') {
            sv.remove_suffix(1);
        }

        const std::string_view type = next_token(sv);
        const std::string_view ts_sv = next_token(sv);
        const std::string_view sym_sv = next_token(sv);
        (void)sym_sv;

        if (ts_sv.empty()) {
            continue;
        }

        wld_live_lab::MarketMsg msg{};
        msg.symbol_id = wld_live_lab::SYMBOL_ID_WLDUSDT;

        if (type == "TRADE") {
            const std::string_view side_sv = next_token(sv);
            const std::string_view px_sv   = next_token(sv);
            const std::string_view qty_sv  = next_token(sv);
            if (qty_sv.empty()) {
                continue;
            }

            msg.type                         = wld_live_lab::MSG_TYPE_TRADE;
            msg.payload.trade.timestamp      = parse_int64(ts_sv);
            msg.payload.trade.price          = parse_fixed_e8(px_sv);
            msg.payload.trade.qty            = parse_fixed_e8(qty_sv);
            msg.payload.trade.is_buyer_maker = (side_sv == "SELL");

            while (!g_ring_buffer.push(msg) && g_running.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }
        } else if (type == "DEPTH") {
            const std::string_view bids_sv = next_token(sv);
            const std::string_view asks_sv = next_token(sv);

            msg.type                       = wld_live_lab::MSG_TYPE_DEPTH_SNAPSHOT;
            msg.payload.snapshot.timestamp = parse_int64(ts_sv);
            parse_depth_side(bids_sv, msg.payload.snapshot.bid_px, msg.payload.snapshot.bid_qty);
            parse_depth_side(asks_sv, msg.payload.snapshot.ask_px, msg.payload.snapshot.ask_qty);

            while (!g_ring_buffer.push(msg) && g_running.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }
        } else if (type == "LIQ") {
            const std::string_view side_sv = next_token(sv);
            const std::string_view px_sv   = next_token(sv);
            const std::string_view qty_sv  = next_token(sv);
            if (side_sv.empty() || qty_sv.empty()) {
                continue;
            }

            msg.type                  = wld_live_lab::MSG_TYPE_LIQUIDATION;
            msg.payload.liq.timestamp = parse_int64(ts_sv);
            msg.payload.liq.price     = parse_fixed_e8(px_sv);
            msg.payload.liq.qty       = parse_fixed_e8(qty_sv);
            msg.payload.liq.side      = side_sv.front();

            while (!g_ring_buffer.push(msg) && g_running.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }
        } else if (type == "TICKER") {
            const std::string_view oi_sv   = next_token(sv);
            const std::string_view fund_sv = next_token(sv);
            const std::string_view mark_sv = next_token(sv);
            if (mark_sv.empty()) {
                continue;
            }

            msg.type                         = wld_live_lab::MSG_TYPE_TICKER;
            msg.payload.ticker.timestamp     = parse_int64(ts_sv);
            msg.payload.ticker.open_interest = parse_fixed_e8(oi_sv);
            msg.payload.ticker.funding_rate  = parse_fixed_e8(fund_sv);
            msg.payload.ticker.mark_price    = parse_fixed_e8(mark_sv);

            while (!g_ring_buffer.push(msg) && g_running.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }
        }
    }

    g_running.store(false, std::memory_order_release);
    consumer.join();
    return 0;
}
