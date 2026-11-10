export const PRICE_SCALE = 100_000_000; // 1e8 Fixed-Point (E8)
export const HALF_SPREAD_E8 = 20_000; // 0.0002 USDT (2 ticks)
export const DEFAULT_RISK_AVERSION_BPS = 100;
export const MAX_LATENCY_NS = 500_000_000; // 500ms Stale Guard
export const MAX_BOOK_DEPTH = 50;

export interface PriceLevel {
  priceE8: number;
  sizeE8: number;
  price: number;
  size: number;
}

export interface MarketUpdate {
  timestampExchangeNs: number;
  timestampLocalNs: number;
  orderId: number;
  priceE8: number;
  sizeE8: number;
  side: 'B' | 'A';
  type: 'A' | 'C' | 'T';
}

export interface QuoteDecision {
  bidPxE8: number;
  askPxE8: number;
  bidPx: number;
  askPx: number;
  reservationPxE8: number;
  reservationPx: number;
  bidActive: boolean;
  askActive: boolean;
}

export interface TelemetrySnapshot {
  velocityTps: number;
  vpinE8: number;
  vpinPct: number;
  imbalanceE8: number;
  imbalancePct: number;
  microPriceE8: number;
  microPrice: number;
  latencyNs: number;
  isStale: boolean;
}

export interface BenchmarkReport {
  eventsProcessed: number;
  elapsedMs: number;
  throughputMops: number;
  avgLatencyNs: number;
  finalMicroPrice: number;
  finalImbalancePct: number;
  checksumHex: string;
}

/**
 * Pure integer fixed-point E8 parser equivalent to `parse_fixed_e8(std::string_view)` in `src/main.cpp`.
 * Zero floating-point conversions.
 */
export function parseFixedE8(raw: string): number {
  const s = raw.trim();
  if (!s) return 0;

  let negative = false;
  let idx = 0;
  if (s[0] === '-') {
    negative = true;
    idx = 1;
  } else if (s[0] === '+') {
    idx = 1;
  }

  const body = s.slice(idx);
  const dotPos = body.indexOf('.');
  const intPart = dotPos === -1 ? body : body.slice(0, dotPos);
  const fracPart = dotPos === -1 ? '' : body.slice(dotPos + 1);

  let integral = 0;
  for (let i = 0; i < intPart.length; i++) {
    const code = intPart.charCodeAt(i) - 48;
    if (code >= 0 && code <= 9) {
      integral = integral * 10 + code;
    }
  }

  let fractional = 0;
  const digits = Math.min(8, fracPart.length);
  for (let i = 0; i < digits; i++) {
    const code = fracPart.charCodeAt(i) - 48;
    if (code >= 0 && code <= 9) {
      fractional = fractional * 10 + code;
    }
  }
  for (let i = digits; i < 8; i++) {
    fractional *= 10;
  }

  const scaled = integral * PRICE_SCALE + fractional;
  return negative ? -scaled : scaled;
}

export function toFixedE8(val: number): number {
  if (!Number.isFinite(val)) return 0;
  return Math.round(val * PRICE_SCALE);
}

export function toDoubleE8(valE8: number): number {
  return valE8 / PRICE_SCALE;
}

/**
 * Updates a sorted contiguous L2 book side (`bids` descending, `asks` ascending).
 * Inserts, updates in-place, or erases when `sizeE8 <= 0`.
 */
export function updateBookSide(
  levels: PriceLevel[],
  side: 'B' | 'A',
  priceE8: number,
  sizeE8: number
): PriceLevel[] {
  const next = [...levels];
  const idx = next.findIndex((lvl) => lvl.priceE8 === priceE8);

  if (idx !== -1) {
    if (sizeE8 <= 0) {
      next.splice(idx, 1);
    } else {
      next[idx] = {
        priceE8,
        sizeE8,
        price: toDoubleE8(priceE8),
        size: toDoubleE8(sizeE8),
      };
    }
    return next;
  }

  if (sizeE8 <= 0) {
    return next;
  }

  const newEntry: PriceLevel = {
    priceE8,
    sizeE8,
    price: toDoubleE8(priceE8),
    size: toDoubleE8(sizeE8),
  };

  let insertAt = next.length;
  for (let i = 0; i < next.length; i++) {
    if (side === 'B' ? next[i].priceE8 < priceE8 : next[i].priceE8 > priceE8) {
      insertAt = i;
      break;
    }
  }

  next.splice(insertAt, 0, newEntry);
  if (next.length > MAX_BOOK_DEPTH) {
    next.pop();
  }
  return next;
}

/**
 * Computes L1 volume-weighted Micro-Price using 128-bit equivalent BigInt math.
 */
export function calculateMicroPriceE8(bids: PriceLevel[], asks: PriceLevel[]): number {
  if (bids.length === 0 || asks.length === 0) return 0;

  const bestBidPx = BigInt(bids[0].priceE8);
  const bestAskPx = BigInt(asks[0].priceE8);
  const bidVol = BigInt(bids[0].sizeE8);
  const askVol = BigInt(asks[0].sizeE8);

  const total = bidVol + askVol;
  if (total <= 0n) {
    return Number((bestBidPx + bestAskPx) / 2n);
  }

  const num = bestBidPx * askVol + bestAskPx * bidVol;
  return Number(num / total);
}

/**
 * Computes Top-5 level Order Book Imbalance (OBI) scaled to [-1e8, +1e8].
 */
export function calculateImbalanceE8(bids: PriceLevel[], asks: PriceLevel[]): number {
  if (bids.length === 0 || asks.length === 0) return 0;

  let bidVol = 0n;
  let askVol = 0n;

  const depthB = Math.min(5, bids.length);
  for (let i = 0; i < depthB; i++) bidVol += BigInt(bids[i].sizeE8);

  const depthA = Math.min(5, asks.length);
  for (let i = 0; i < depthA; i++) askVol += BigInt(asks[i].sizeE8);

  const total = bidVol + askVol;
  if (total <= 0n) return 0;

  const diff = bidVol - askVol;
  const absDiff = diff < 0n ? -diff : diff;
  const scaled = Number((absDiff * BigInt(PRICE_SCALE)) / total);
  return diff < 0n ? -scaled : scaled;
}

/**
 * Evaluates textbook Avellaneda-Stoikov reservation-price market maker quotes.
 */
export function evaluateAvellanedaStoikov(
  microPriceE8: number,
  isStale: boolean,
  inventoryUnits: number,
  halfSpreadE8: number = HALF_SPREAD_E8,
  riskAversionBps: number = DEFAULT_RISK_AVERSION_BPS
): QuoteDecision {
  if (isStale || microPriceE8 <= 0) {
    return {
      bidPxE8: 0,
      askPxE8: 0,
      bidPx: 0,
      askPx: 0,
      reservationPxE8: 0,
      reservationPx: 0,
      bidActive: false,
      askActive: false,
    };
  }

  const skewOffset = Math.round(inventoryUnits * riskAversionBps);
  const reservationPxE8 = microPriceE8 - skewOffset;

  let bidPxE8 = reservationPxE8 - halfSpreadE8;
  let askPxE8 = reservationPxE8 + halfSpreadE8;

  if (bidPxE8 >= askPxE8) {
    const mid = Math.trunc((bidPxE8 + askPxE8) / 2);
    bidPxE8 = mid - halfSpreadE8;
    askPxE8 = mid + halfSpreadE8;
  }

  return {
    bidPxE8,
    askPxE8,
    bidPx: toDoubleE8(bidPxE8),
    askPx: toDoubleE8(askPxE8),
    reservationPxE8,
    reservationPx: toDoubleE8(reservationPxE8),
    bidActive: true,
    askActive: true,
  };
}

/**
 * Executes a deterministic pipeline benchmark mirroring `benchmarks/bench_pipeline.cpp`.
 */
export function runPipelineBenchmark(numEvents: number = 100_000): BenchmarkReport {
  const basePx = 215_400_000; // 2.1540 USDT
  const bidPrices = new Int32Array(20);
  const bidSizes = new Float64Array(20);
  const askPrices = new Int32Array(20);
  const askSizes = new Float64Array(20);

  for (let i = 0; i < 20; i++) {
    bidPrices[i] = basePx - (i + 1) * 10_000;
    bidSizes[i] = (100 + (i + 1) * 5) * PRICE_SCALE;
    askPrices[i] = basePx + (i + 1) * 10_000;
    askSizes[i] = (100 + (i + 1) * 5) * PRICE_SCALE;
  }

  let buyVol = 1_000_000_000;
  let sellVol = 1_000_000_000;
  let checksum = 0;

  const t0 = performance.now();
  for (let i = 0; i < numEvents; i++) {
    const slot = i & 15;
    const isBid = (i & 1) === 0;
    const sz = (50 + (i & 63)) * PRICE_SCALE;

    if ((i & 3) === 0) {
      if (isBid) buyVol += sz;
      else sellVol += sz;
    } else {
      if (isBid) bidSizes[slot] = sz;
      else askSizes[slot] = sz;
    }

    const b0Px = bidPrices[0];
    const a0Px = askPrices[0];
    const b0Sz = bidSizes[0];
    const a0Sz = askSizes[0];
    const micro = Math.trunc((b0Px * a0Sz + a0Px * b0Sz) / (b0Sz + a0Sz));

    let sumB = 0;
    let sumA = 0;
    for (let k = 0; k < 5; k++) {
      sumB += bidSizes[k];
      sumA += askSizes[k];
    }
    const obi = Math.trunc(((sumB - sumA) * PRICE_SCALE) / (sumB + sumA));
    const vpin = Math.trunc(((buyVol - sellVol) * PRICE_SCALE) / (buyVol + sellVol));
    const bidQuote = micro - HALF_SPREAD_E8;

    checksum = (checksum ^ (micro + obi + bidQuote + vpin)) >>> 0;
  }
  const t1 = performance.now();

  const elapsedMs = Math.max(0.5, t1 - t0);
  const throughputMops = numEvents / (elapsedMs / 1000) / 1_000_000;
  const avgLatencyNs = (elapsedMs * 1_000_000) / numEvents;

  const finalMicro =
    (bidPrices[0] * askSizes[0] + askPrices[0] * bidSizes[0]) /
    (bidSizes[0] + askSizes[0]) /
    PRICE_SCALE;

  let sumB = 0;
  let sumA = 0;
  for (let k = 0; k < 5; k++) {
    sumB += bidSizes[k];
    sumA += askSizes[k];
  }
  const finalImbalancePct = ((sumB - sumA) / (sumB + sumA)) * 100;

  return {
    eventsProcessed: numEvents,
    elapsedMs,
    throughputMops,
    avgLatencyNs,
    finalMicroPrice: finalMicro,
    finalImbalancePct,
    checksumHex: `0x${checksum.toString(16).padStart(8, '0')}`,
  };
}
