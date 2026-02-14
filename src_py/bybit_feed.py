import websocket
import json
import time

SYMBOL = "WLDUSDT"

def on_message(ws, message):
    try:
        data = json.loads(message)
    except:
        return

    if "topic" not in data:
        return
    
    topic = data["topic"]

    # 1. TRADES
    if topic == f"publicTrade.{SYMBOL}":
        for trade in data["data"]:
            ts = int(trade["T"]) 
            price = trade["p"]
            qty = trade["v"]
            side = trade["S"].upper()
            # TRADE|ts|sym|side|px|qty
            print(f"TRADE|{ts}|{SYMBOL}|{side}|{price}|{qty}", flush=True)

    # 2. DEPTH (Orderbook 50)
    elif topic == f"orderbook.50.{SYMBOL}":
        # type: snapshot or delta
        type_ = data["type"]
        ts = data["ts"]
        
        # We only care about the updates, but for simplicity in this architecture
        # we might just stream everything. The recorder/engine will handle it.
        # But wait, the standard usually sends snapshots first then deltas. 
        # For simplicity in this 'Recorder' focused task, we pass what we get.
        # However, the user request implied "Flatten top 50". 
        # If it's a delta, we might not have the full book here unless we maintain state.
        # BUT, `orderbook.50` pushes snapshots? No, it pushes delta.
        # actually, for "Recorder", we usually want raw updates to replay.
        # But the User Requirement says: "Flatten top 50 levels: p:v,p:v...". 
        # This implies we need to maintain a local book if we want to output a full snapshot every time,
        # OR the C++ side expects Delta? 
        # Reviewing text: "Visualize ... Orderbook (Depth 50)".
        # "Output Formats ... bids_str|asks_str (Flatten top 50 levels)".
        # PROPOSAL: To keep Python lightweight, we just forward what we get if it's a snapshot, 
        # but `orderbook.50` is delta headers. 
        # Actually, Bybit V5 `orderbook.50` sends a snapshot first, then deltas.
        # If the user wants "Flatten top 50" on EVERY output, Python must maintain the book.
        # Let's implement a simple local book maintenance here.
        
        process_depth(data)

    # 3. LIQUIDATIONS
    elif topic == f"liquidation.{SYMBOL}":
        # data format: { "symbol":..., "side": "Buy" (liq order side), "size":..., "price":... }
        liq = data["data"]
        ts = data["ts"]
        # LIQ|ts|sym|side|px|qty
        # Note: 'side' in liquidation message is the side of the LIQUIDATED position? 
        # Or the order executed? Bybit docs: "side": "Buy" means a Buy order was executed to close a Short position.
        # User Req: "Real Liquidations (Confirmation)".
        # We pass exactly what Bybit sends.
        price = liq["price"]
        qty = liq["size"]
        side = liq["side"] 
        print(f"LIQ|{ts}|{SYMBOL}|{side}|{price}|{qty}", flush=True)

    # 4. TICKERS
    elif topic == f"tickers.{SYMBOL}":
        # "openInterest", "fundingRate", "markPrice"
        # Ticker data is usually a snapshot or delta? Bybit V5 Tickers are snapshots (push frequency).
        current = data["data"]
        ts = data["ts"]
        
        # We need specific fields.
        oi = current.get("openInterest", 0)
        fr = current.get("fundingRate", 0)
        mp = current.get("markPrice", 0)
        
        # TICKER|ts|sym|oi|funding|mark
        print(f"TICKER|{ts}|{SYMBOL}|{oi}|{fr}|{mp}", flush=True)

# --- Local Depth Maintenance for "Flattened" Output ---
bids = {}
asks = {}

def process_depth(data):
    global bids, asks
    type_ = data["type"]
    
    # Handle Snapshot
    if type_ == "snapshot":
        bids.clear()
        asks.clear()
        for b in data["data"]["b"]:
            bids[b[0]] = b[1]
        for a in data["data"]["a"]:
            asks[a[0]] = a[1]
            
    # Handle Delta
    elif type_ == "delta":
        for b in data["data"]["b"]:
            if b[1] == "0":
                if b[0] in bids: del bids[b[0]]
            else:
                bids[b[0]] = b[1]
        for a in data["data"]["a"]:
            if a[1] == "0":
                if a[0] in asks: del asks[a[0]]
            else:
                asks[a[0]] = a[1]
    
    # Format for Output (Top 50)
    # Sort Bids Descending, Asks Ascending
    sorted_bids = sorted(bids.items(), key=lambda x: float(x[0]), reverse=True)[:50]
    sorted_asks = sorted(asks.items(), key=lambda x: float(x[0]))[:50]
    
    bids_str = ",".join([f"{p}:{v}" for p, v in sorted_bids])
    asks_str = ",".join([f"{p}:{v}" for p, v in sorted_asks])
    
    ts = data["ts"]
    print(f"DEPTH|{ts}|{SYMBOL}|{bids_str}|{asks_str}", flush=True)

def on_error(ws, error):
    # Silently ignore to keep the pipe clean or print to stderr
    pass

def on_close(ws, close_status_code, close_msg):
    pass

def on_open(ws):
    # Subscribe to all 4 channels
    req = {
        "op": "subscribe",
        "args": [
            f"publicTrade.{SYMBOL}",
            f"orderbook.50.{SYMBOL}",
            f"liquidation.{SYMBOL}",
            f"tickers.{SYMBOL}"
        ]
    }
    ws.send(json.dumps(req))

if __name__ == "__main__":
    websocket.enableTrace(False)
    while True:
        try:
            ws = websocket.WebSocketApp("wss://stream.bybit.com/v5/public/linear",
                                      on_open=on_open,
                                      on_message=on_message,
                                      on_error=on_error,
                                      on_close=on_close)
            ws.run_forever()
        except:
            time.sleep(1)
