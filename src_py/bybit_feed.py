import websocket
import json
import time

def on_message(ws, message):
    data = json.loads(message)
    if "topic" in data and data["topic"] == "publicTrade.WLDUSDT":
        for trade in data["data"]:
            ts = int(trade["T"] * 1000)  # Microseconds if possible, else milliseconds
            price = trade["p"]
            qty = trade["v"]
            side = trade["S"].upper() # ensuring BUY/SELL
            print(f"TRADE|{ts}|WLDUSDT|{side}|{price}|{qty}", flush=True)

def on_error(ws, error):
    print(error)

def on_close(ws, close_status_code, close_msg):
    print("### closed ###")

def on_open(ws):
    print("Opened connection")
    ws.send(json.dumps({"op": "subscribe", "args": ["publicTrade.WLDUSDT"]}))

if __name__ == "__main__":
    websocket.enableTrace(False)
    ws = websocket.WebSocketApp("wss://stream.bybit.com/v5/public/linear",
                              on_open=on_open,
                              on_message=on_message,
                              on_error=on_error,
                              on_close=on_close)
    ws.run_forever()
