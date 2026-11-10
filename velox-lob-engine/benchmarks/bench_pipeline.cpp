#include "velox/order_book.hpp"
#include "velox/protocol.hpp"
#include "velox/ring_buffer.hpp"
#include "velox/strategy_interface.hpp"
#include "velox/telemetry.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>

int main() {
    using namespace velox;

    constexpr size_t NUM_EVENTS    = 1'000'000;
    constexpr size_t RING_CAPACITY = 65'536;

    RingBuffer<MarketUpdate, RING_CAPACITY> ring;
    OrderBook                               book;
    TelemetryEngine                         telemetry;
    AvellanedaStoikovDemo                   strategy;

    // Seed initial 20 levels per side so book operations exercise realistic depth
    constexpr int64_t base_px = 215'400'000LL; // 2.1540 USDT
    for (int i = 1; i <= 20; ++i) {
        book.update_level('B', base_px - (i * 10'000LL), (100 + i * 5) * PRICE_SCALE);
        book.update_level('A', base_px + (i * 10'000LL), (100 + i * 5) * PRICE_SCALE);
    }

    int64_t checksum = 0;
    const auto t0 = std::chrono::steady_clock::now();

    for (size_t i = 0; i < NUM_EVENTS; ++i) {
        const int64_t offset_ticks = static_cast<int64_t>(i & 15) * 10'000LL;
        const bool    is_bid       = (i & 1) == 0;

        MarketUpdate ev{};
        ev.timestamp_exchange = static_cast<int64_t>(i * 1'000);
        ev.timestamp_local    = ev.timestamp_exchange + 12'000; // 12 us transit
        ev.order_id           = static_cast<int64_t>(i);
        ev.price              = is_bid ? (base_px - offset_ticks - 10'000LL)
                                       : (base_px + offset_ticks + 10'000LL);
        ev.size               = static_cast<int64_t>((50 + (i & 63)) * PRICE_SCALE);
        ev.side               = is_bid ? 'B' : 'A';
        ev.type               = ((i & 3) == 0) ? 'T' : 'A';

        (void)ring.push(ev);

        MarketUpdate out{};
        if (ring.pop(out)) {
            if (out.type == 'T') {
                telemetry.add_event(out);
            } else {
                book.update_level(out.side, out.price, out.size);
            }

            const int64_t micro = book.get_micro_price();
            const int64_t obi   = book.get_imbalance();
            const auto    quote = strategy.evaluate(book, telemetry, 0);

            checksum ^= (micro + obi + quote.bid_px + telemetry.get_vpin());
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed_ns =
        static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    const double elapsed_ms  = elapsed_ns / 1e6;
    const double throughput  = (static_cast<double>(NUM_EVENTS) / (elapsed_ns / 1e9)) / 1e6;
    const double ns_per_op   = elapsed_ns / static_cast<double>(NUM_EVENTS);

    std::cout << "==============================================================\n"
              << "  VELOX LOB ENGINE | PIPELINE BENCHMARK (1,000,000 EVENTS)\n"
              << "==============================================================\n"
              << std::fixed << std::setprecision(2)
              << " Events Processed  : " << NUM_EVENTS << "\n"
              << " Total Elapsed     : " << elapsed_ms << " ms\n"
              << " Throughput        : " << throughput << " M events/sec\n"
              << " Average Latency   : " << ns_per_op << " ns/event\n"
              << " Micro-Price (Final): " << std::setprecision(4)
              << OrderBook::to_double(book.get_micro_price()) << " USDT\n"
              << " OBI (Final)       : " << std::setprecision(2)
              << (OrderBook::to_double(book.get_imbalance()) * 100.0) << "%\n"
              << " Checksum Guard    : 0x" << std::hex << checksum << std::dec << "\n"
              << "==============================================================\n";

    return 0;
}
