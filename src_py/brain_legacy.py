import sys
import os
import time
import logging
from pybit.unified_trading import HTTP
from dotenv import load_dotenv

load_dotenv()

# --- LOGGING SETUP ---
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s | TITAN-PY | %(message)s',
    handlers=[logging.StreamHandler(sys.stderr)] # Logs a stderr para no ensuciar el pipe
)
logger = logging.getLogger()

# --- CONFIGURACIÓN DE GUERRA ---
API_KEY = os.getenv("BYBIT_API_KEY")
API_SECRET = os.getenv("BYBIT_API_SECRET")
SYMBOL = "WLDUSDT"
TESTNET = os.getenv("BYBIT_TESTNET", "false").lower() == "true"

# PARAMETROS DE LA ESTRATEGIA "TITAN SMART TRAP HUNTER"
LEVERAGE = 50
RISK_FACTOR = 0.90      # Usar el 90% del saldo disponible
TP_TICKS = 0.0009       # Target: 9 Ticks (Fijo)
SL_TICKS = 0.0006       # Stop: 6 Ticks (Fijo)
COOLDOWN_SECONDS = 900  # 15 Minutos de descanso obligatorio

# --- BYBIT SESSION ---
try:
    session = HTTP(testnet=TESTNET, api_key=API_KEY, api_secret=API_SECRET)
    logger.info("CONEXIÓN CON BYBIT ESTABLECIDA.")
except Exception as e:
    logger.error(f"ERROR DE CONEXIÓN: {e}")
    sys.exit(1)

# Configurar Apalancamiento al iniciar
def set_leverage():
    try:
        session.set_leverage(category="linear", symbol=SYMBOL, buyLeverage=str(LEVERAGE), sellLeverage=str(LEVERAGE))
        logger.info(f"APALANCAMIENTO FORZADO A x{LEVERAGE}")
    except Exception as e:
        # Si ya está seteado, dará error, lo ignoramos pero logueamos info
        logger.info(f"Leverage check: {e}")

set_leverage()

def get_wallet_balance():
    """Obtiene el saldo USDT disponible en la cuenta de Derivados Unificada"""
    try:
        response = session.get_wallet_balance(accountType="UNIFIED", coin="USDT")
        # Navegar la respuesta de Bybit (puede variar según tipo de cuenta, ajustar si es necesario)
        balance = float(response['result']['list'][0]['coin'][0]['walletBalance'])
        return balance
    except Exception as e:
        logger.error(f"ERROR LEYENDO BALANCE: {e}")
        return 0.0

def has_open_position():
    """Verifica si ya existe una posición abierta en el símbolo"""
    try:
        response = session.get_positions(category="linear", symbol=SYMBOL)
        for pos in response['result']['list']:
            if float(pos['size']) > 0:
                logger.info(f"POSICIÓN DETECTADA: {pos['side']} {pos['size']} WLD. Bloqueando nueva entrada.")
                return True
        return False
    except Exception as e:
        logger.error(f"ERROR VERIFICANDO POSICIONES: {e}")
        return True # Ante duda, bloqueamos (Fail-Safe)

def execute_order(side, price):
    # 0. POSITION GUARD (Evitar doble entrada)
    if has_open_position():
        logger.warning("ORDEN RECHAZADA: Ya existe una posición abierta.")
        return

    # 1. LEER SALDO ACTUAL (Para el Interés Compuesto)
    balance = get_wallet_balance()
    if balance < 1.0:
        logger.error("SALDO INSUFICIENTE PARA OPERAR.")
        return

    # 2. CALCULAR TAMAÑO DE POSICIÓN (All-In 90%)
    # Qty = (Saldo * 0.90 * 50x) / Precio
    notional_value = balance * RISK_FACTOR * LEVERAGE
    qty = notional_value / price
    
    # Redondeo para WLD (Normalmente 1 decimal, o entero. Usamos int para asegurar no exceder margen)
    qty = int(qty) 
    
    if qty <= 0:
        logger.warning(f"CANTIDAD CALCULADA ES 0. Balance: {balance}")
        return

    # 3. CALCULAR TP/SL (EN TICKS, NO PORCENTAJE)
    if side == "Buy":
        tp_price = price + TP_TICKS
        sl_price = price - SL_TICKS
    else: # Por si algún día activas Shorts
        tp_price = price - TP_TICKS
        sl_price = price + SL_TICKS

    # Redondear precios a 4 decimales (WLD standard)
    tp_price = round(tp_price, 4)
    sl_price = round(sl_price, 4)

    logger.info(f"DISPARANDO {side} | Balance: ${balance:.2f} | Qty: {qty} WLD | TP: {tp_price} | SL: {sl_price}")

    # 4. ENVIAR ORDEN (Inicial con TP/SL estimados)
    try:
        session.place_order(
            category="linear",
            symbol=SYMBOL,
            side=side,
            orderType="Market",
            qty=str(qty),
            stopLoss=str(sl_price),
            takeProfit=str(tp_price),
            slTriggerBy="LastPrice",
            tpTriggerBy="LastPrice"
        )
        sys.stderr.write(f"[EXECUTED] {side} {qty} WLD @ {price} (Bal: {balance:.2f})\n")
        sys.stderr.flush()
        
        # 5. AJUSTE DINÁMICO DE TP/SL (Para garantizar ticks reales desde Entry Price)
        time.sleep(1) # Esperar 1s para asegurar fill y actualización de API
        
        try:
            pos_data = session.get_positions(category="linear", symbol=SYMBOL)['result']['list']
            for pos in pos_data:
                size = float(pos['size'])
                if size > 0:
                    avg_entry = float(pos['avgPrice'])
                    
                    # Recalcular TP/SL exactos desde la entrada real
                    if side == "Buy":
                        real_tp = avg_entry + TP_TICKS
                        real_sl = avg_entry - SL_TICKS
                    else: # Sell
                        real_tp = avg_entry - TP_TICKS
                        real_sl = avg_entry + SL_TICKS
                        
                    real_tp = round(real_tp, 4)
                    real_sl = round(real_sl, 4)
                    
                    session.set_trading_stop(
                        category="linear",
                        symbol=SYMBOL,
                        takeProfit=str(real_tp),
                        stopLoss=str(real_sl),
                        tpTriggerBy="LastPrice",
                        slTriggerBy="LastPrice",
                        positionIdx=0 # Modo One-Way
                    )
                    logger.info(f"TP/SL ACTUALIZADO A ENTRADA REAL ({avg_entry}): TP {real_tp} | SL {real_sl}")
                    break
        except Exception as update_err:
             logger.error(f"FALLO ACTUALIZANDO TP/SL: {update_err}")

        # 6. ESPERAR CIERRE DE POSICIÓN
        logger.info("ESPERANDO CIERRE DE POSICIÓN PARA INICIAR COOLDOWN...")
        while has_open_position():
            time.sleep(1)
            
        # 7. COOLDOWN OBLIGATORIO POST-CIERRE
        logger.info(f"POSICIÓN CERRADA. ENTRANDO EN COOLDOWN DE {COOLDOWN_SECONDS} SEGUNDOS...")
        time.sleep(COOLDOWN_SECONDS)
        logger.info("COOLDOWN FINALIZADO. LISTO PARA NUEVA OPERACIÓN.")
        
    except Exception as e:
        logger.error(f"FALLO DE EJECUCIÓN BYBIT: {e}")

def main():
    logger.info("NERVE GATEWAY LISTENING (STDIN)... ESPERANDO SEÑALES DE TITAN...")
    
    while True:
        try:
            # Leer línea del C++
            line = sys.stdin.readline()
            if not line: break
            line = line.strip()
            
            # FILTRO DE SEÑAL
            # Formato esperado del C++ corregido: SIGNAL|SYMBOL|SIDE|PRICE|TYPE
            if line.startswith("SIGNAL"):
                parts = line.split("|")
                # parts[0]=SIGNAL, [1]=WLDUSDT, [2]=BUY, [3]=0.3915, [4]=MARKET
                
                if len(parts) < 4: continue
                
                cmd_sym = parts[1]
                side = parts[2] # "BUY"
                price = float(parts[3])
                
                if cmd_sym != SYMBOL: continue
                
                # Bybit espera "Buy" o "Sell" (Capitalized)
                side_formatted = side.capitalize() 
                
                execute_order(side_formatted, price)
                
        except KeyboardInterrupt:
            logger.info("APAGANDO NERVE...")
            break
        except Exception as e:
            logger.error(f"LOOP ERROR: {e}")

if __name__ == "__main__":
    main()