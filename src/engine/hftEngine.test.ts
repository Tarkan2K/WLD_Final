import test from 'node:test';
import assert from 'node:assert/strict';
import {
  parseFixedE8,
  toFixedE8,
  toDoubleE8,
  updateBookSide,
  calculateMicroPriceE8,
  calculateImbalanceE8,
  evaluateAvellanedaStoikov,
  runPipelineBenchmark,
  type PriceLevel,
} from './hftEngine';

test('parseFixedE8 parses decimal strings into E8 integers with zero floating-point drift', () => {
  assert.equal(parseFixedE8('2.1540'), 215_400_000);
  assert.equal(parseFixedE8('0.000125'), 12_500);
  assert.equal(parseFixedE8('42850120.5'), 4_285_012_050_000_000);
  assert.equal(parseFixedE8('-0.0001'), -10_000);
});

test('updateBookSide maintains sorted order, updates in-place, and erases when sizeE8 == 0', () => {
  let bids: PriceLevel[] = [];
  bids = updateBookSide(bids, 'B', toFixedE8(2.151), toFixedE8(100));
  bids = updateBookSide(bids, 'B', toFixedE8(2.153), toFixedE8(200));
  bids = updateBookSide(bids, 'B', toFixedE8(2.152), toFixedE8(150));

  // Strictly descending for bids
  assert.equal(bids.length, 3);
  assert.equal(bids[0].priceE8, 215_300_000);
  assert.equal(bids[1].priceE8, 215_200_000);
  assert.equal(bids[2].priceE8, 215_100_000);

  // Update in-place
  bids = updateBookSide(bids, 'B', toFixedE8(2.152), toFixedE8(350));
  assert.equal(bids[1].sizeE8, toFixedE8(350));

  // Erase when sizeE8 == 0
  bids = updateBookSide(bids, 'B', toFixedE8(2.153), 0);
  assert.equal(bids.length, 2);
  assert.equal(bids[0].priceE8, 215_200_000);
});

test('OrderBook::get_micro_price and get_imbalance compute 128-bit E8 microstructure metrics', () => {
  let bids: PriceLevel[] = [];
  let asks: PriceLevel[] = [];
  bids = updateBookSide(bids, 'B', toFixedE8(2.0), toFixedE8(300));
  asks = updateBookSide(asks, 'A', toFixedE8(2.001), toFixedE8(100));

  assert.equal(calculateMicroPriceE8(bids, asks), 200_075_000);
  assert.equal(calculateImbalanceE8(bids, asks), 50_000_000); // (300 - 100) / 400 = +0.50 -> 50,000,000 E8
});

test('AvellanedaStoikovDemo::evaluate computes inventory-skewed reservation quotes and respects LatencyGuard', () => {
  const quoteNeutral = evaluateAvellanedaStoikov(215_400_000, false, 0, 20_000, 100);
  assert.equal(quoteNeutral.bidActive, true);
  assert.equal(quoteNeutral.bidPxE8, 215_380_000);
  assert.equal(quoteNeutral.askPxE8, 215_420_000);

  // Positive inventory skews reservation price downward to encourage selling
  const quoteLong = evaluateAvellanedaStoikov(215_400_000, false, 50, 20_000, 100);
  assert.equal(quoteLong.reservationPxE8, 215_395_000);
  assert.ok(quoteLong.askPxE8 < quoteNeutral.askPxE8);

  // Stale feed pulls quotes
  const quoteStale = evaluateAvellanedaStoikov(215_400_000, true, 0);
  assert.equal(quoteStale.bidActive, false);
  assert.equal(quoteStale.askActive, false);
});

test('runPipelineBenchmark processes events and returns valid throughput and latency metrics', () => {
  const report = runPipelineBenchmark(50_000);
  assert.equal(report.eventsProcessed, 50_000);
  assert.ok(report.throughputMops > 0);
  assert.ok(report.avgLatencyNs > 0);
  assert.ok(toDoubleE8(toFixedE8(report.finalMicroPrice)) > 2.0);
});
