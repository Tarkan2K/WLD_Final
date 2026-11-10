#!/usr/bin/env python3
"""
Asynchronous Bybit V5 Linear WebSocket ingestion gateway.

Maintains local L2 order book depth (Top 50) and persistent ticker state cache
to prevent partial delta zero-flicker, emitting pipe-delimited frames to STDOUT.
"""

from __future__ import annotations

import asyncio
import json
import signal
import sys
from typing import Any, Dict, List, Tuple

try:
    import websockets
except ImportError:
    websockets = None  # type: ignore[assignment]

BYBIT_Linear_WSS: str = "wss://stream.bybit.com/v5/public/linear"
DEFAULT_SYMBOL: str = "WLDUSDT"
DEPTH_LIMIT: int = 50


class BybitFeedHandler:
    """Stateful WebSocket feed normalizer for Bybit V5 linear perpetuals."""

    def __init__(self, symbol: str = DEFAULT_SYMBOL) -> None:
        self.symbol: str = symbol
        self.bids: Dict[str, str] = {}
        self.asks: Dict[str, str] = {}
        self.ticker_state: Dict[str, str] = {
            "openInterest": "0",
            "fundingRate": "0",
            "markPrice": "0",
        }
        self._stop_event: asyncio.Event = asyncio.Event()

    def request_stop(self) -> None:
        """Signals the ingestion loop to terminate cleanly."""
        self._stop_event.set()

    def handle_message(self, raw_msg: str) -> None:
        """Parses a raw JSON frame and writes normalized pipe records to STDOUT."""
        try:
            payload: Dict[str, Any] = json.loads(raw_msg)
        except json.JSONDecodeError:
            return

        topic: str | None = payload.get("topic")
        if not topic:
            return

        if topic == f"publicTrade.{self.symbol}":
            self._process_trades(payload.get("data", []))
        elif topic == f"orderbook.50.{self.symbol}":
            self._process_depth(payload)
        elif topic == f"liquidation.{self.symbol}":
            self._process_liquidation(payload)
        elif topic == f"tickers.{self.symbol}":
            self._process_ticker(payload)

    def _process_trades(self, trades: List[Dict[str, Any]]) -> None:
        out_lines: List[str] = []
        for trade in trades:
            ts = int(trade["T"])
            price = str(trade["p"])
            qty = str(trade["v"])
            side = str(trade["S"]).upper()
            out_lines.append(f"TRADE|{ts}|{self.symbol}|{side}|{price}|{qty}\n")
        if out_lines:
            sys.stdout.write("".join(out_lines))
            sys.stdout.flush()

    def _process_depth(self, payload: Dict[str, Any]) -> None:
        msg_type: str = str(payload.get("type", ""))
        data: Dict[str, Any] = payload.get("data", {})
        ts: int = int(payload.get("ts", 0))

        if msg_type == "snapshot":
            self.bids = {str(px): str(qty) for px, qty in data.get("b", [])}
            self.asks = {str(px): str(qty) for px, qty in data.get("a", [])}
        elif msg_type == "delta":
            for px, qty in data.get("b", []):
                px_s, qty_s = str(px), str(qty)
                if qty_s == "0" or float(qty_s) == 0.0:
                    self.bids.pop(px_s, None)
                else:
                    self.bids[px_s] = qty_s
            for px, qty in data.get("a", []):
                px_s, qty_s = str(px), str(qty)
                if qty_s == "0" or float(qty_s) == 0.0:
                    self.asks.pop(px_s, None)
                else:
                    self.asks[px_s] = qty_s
        else:
            return

        sorted_bids: List[Tuple[str, str]] = sorted(
            self.bids.items(), key=lambda item: float(item[0]), reverse=True
        )[:DEPTH_LIMIT]
        sorted_asks: List[Tuple[str, str]] = sorted(
            self.asks.items(), key=lambda item: float(item[0])
        )[:DEPTH_LIMIT]

        bids_str = ",".join(f"{p}:{q}" for p, q in sorted_bids)
        asks_str = ",".join(f"{p}:{q}" for p, q in sorted_asks)

        sys.stdout.write(f"DEPTH|{ts}|{self.symbol}|{bids_str}|{asks_str}\n")
        sys.stdout.flush()

    def _process_liquidation(self, payload: Dict[str, Any]) -> None:
        liq: Dict[str, Any] = payload.get("data", {})
        if not liq:
            return
        ts: int = int(payload.get("ts", 0))
        side: str = str(liq.get("side", "Buy"))
        price: str = str(liq.get("price", "0"))
        qty: str = str(liq.get("size", "0"))
        sys.stdout.write(f"LIQ|{ts}|{self.symbol}|{side}|{price}|{qty}\n")
        sys.stdout.flush()

    def _process_ticker(self, payload: Dict[str, Any]) -> None:
        current: Dict[str, Any] = payload.get("data", {})
        ts: int = int(payload.get("ts", 0))

        for key in ("openInterest", "fundingRate", "markPrice"):
            val = current.get(key)
            if val is not None and str(val) != "":
                self.ticker_state[key] = str(val)

        oi = self.ticker_state["openInterest"]
        fr = self.ticker_state["fundingRate"]
        mp = self.ticker_state["markPrice"]

        sys.stdout.write(f"TICKER|{ts}|{self.symbol}|{oi}|{fr}|{mp}\n")
        sys.stdout.flush()

    async def run(self) -> None:
        """Connects to Bybit V5 with exponential backoff and processes frames."""
        if websockets is None:
            sys.stderr.write("Error: 'websockets' package is required (pip install websockets)\n")
            sys.exit(1)

        backoff_sec: float = 1.0
        max_backoff_sec: float = 30.0

        subscribe_payload: str = json.dumps(
            {
                "op": "subscribe",
                "args": [
                    f"publicTrade.{self.symbol}",
                    f"orderbook.50.{self.symbol}",
                    f"liquidation.{self.symbol}",
                    f"tickers.{self.symbol}",
                ],
            }
        )

        while not self._stop_event.is_set():
            try:
                async with websockets.connect(
                    BYBIT_Linear_WSS,
                    ping_interval=20,
                    ping_timeout=10,
                    max_queue=4096,
                ) as ws:
                    await ws.send(subscribe_payload)
                    backoff_sec = 1.0

                    while not self._stop_event.is_set():
                        try:
                            raw_msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
                            if isinstance(raw_msg, str):
                                self.handle_message(raw_msg)
                        except asyncio.TimeoutError:
                            continue
            except Exception as exc:
                if self._stop_event.is_set():
                    break
                sys.stderr.write(f"[velox-feed] Connection lost ({exc}), retrying in {backoff_sec:.1f}s\n")
                await asyncio.sleep(backoff_sec)
                backoff_sec = min(max_backoff_sec, backoff_sec * 2.0)


def main() -> None:
    handler = BybitFeedHandler(symbol=DEFAULT_SYMBOL)
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, handler.request_stop)
        except NotImplementedError:
            pass

    try:
        loop.run_until_complete(handler.run())
    finally:
        loop.close()


if __name__ == "__main__":
    main()
