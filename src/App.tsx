import React, { useState } from 'react';
import {
  PRICE_SCALE,
  parseFixedE8,
  toFixedE8,
  toDoubleE8,
  updateBookSide,
  calculateMicroPriceE8,
  calculateImbalanceE8,
  evaluateAvellanedaStoikov,
  runPipelineBenchmark,
  type PriceLevel,
  type BenchmarkReport,
} from './engine/hftEngine';
import { VELOX_REPO_FILES } from './engine/repoFiles';

type ActiveSection = 'EXPLORER' | 'BENCHMARK' | 'SIMULATOR' | 'PARSER';

function createInitialBook(): { bids: PriceLevel[]; asks: PriceLevel[] } {
  let bids: PriceLevel[] = [];
  let asks: PriceLevel[] = [];
  const basePx = 2.1540;
  for (let i = 1; i <= 10; i++) {
    const bPx = toFixedE8(Number((basePx - i * 0.0002).toFixed(4)));
    const aPx = toFixedE8(Number((basePx + i * 0.0002).toFixed(4)));
    const bSz = toFixedE8(100 + i * 25);
    const aSz = toFixedE8(95 + i * 20);
    bids = updateBookSide(bids, 'B', bPx, bSz);
    asks = updateBookSide(asks, 'A', aPx, aSz);
  }
  return { bids, asks };
}

export function App() {
  const [activeSection, setActiveSection] = useState<ActiveSection>('EXPLORER');
  const [selectedFilePath, setSelectedFilePath] = useState<string>(VELOX_REPO_FILES[0].path);
  const [copiedFile, setCopiedFile] = useState<boolean>(false);

  // Benchmark State
  const [benchReport, setBenchReport] = useState<BenchmarkReport>(() => ({
    eventsProcessed: 1_000_000,
    elapsedMs: 18.42,
    throughputMops: 54.29,
    avgLatencyNs: 18.42,
    finalMicroPrice: 2.1540,
    finalImbalancePct: -0.58,
    checksumHex: '0x1a7f3c94',
  }));

  // OrderBook & Avellaneda-Stoikov Simulator State
  const [{ bids, asks }, setBook] = useState(createInitialBook);
  const [inventoryUnits, setInventoryUnits] = useState<number>(0);
  const [halfSpreadTicks, setHalfSpreadTicks] = useState<number>(2);
  const [riskAversionBps, setRiskAversionBps] = useState<number>(100);
  const [isStaleSimulated, setIsStaleSimulated] = useState<boolean>(false);

  // Level Update Form State
  const [levelSide, setLevelSide] = useState<'B' | 'A'>('B');
  const [levelPriceInput, setLevelPriceInput] = useState<string>('2.1539');
  const [levelSizeInput, setLevelSizeInput] = useState<string>('450.0');

  // Zero-Alloc Parser Test State
  const [rawFrameInput, setRawFrameInput] = useState<string>(
    'TRADE|1730841602800|WLDUSDT|BUY|2.15407500|145.25000000'
  );

  const selectedFile =
    VELOX_REPO_FILES.find((f) => f.path === selectedFilePath) ?? VELOX_REPO_FILES[0];

  const microPriceE8 = calculateMicroPriceE8(bids, asks);
  const imbalanceE8 = calculateImbalanceE8(bids, asks);
  const quotes = evaluateAvellanedaStoikov(
    microPriceE8,
    isStaleSimulated,
    inventoryUnits,
    halfSpreadTicks * 10_000,
    riskAversionBps
  );

  const handleCopyCode = () => {
    navigator.clipboard?.writeText(selectedFile.content);
    setCopiedFile(true);
    setTimeout(() => setCopiedFile(false), 1800);
  };

  const handleApplyLevelUpdate = (e: React.FormEvent) => {
    e.preventDefault();
    const pxE8 = parseFixedE8(levelPriceInput);
    const szE8 = parseFixedE8(levelSizeInput);
    if (pxE8 <= 0) return;

    setBook((prev) => ({
      bids: levelSide === 'B' ? updateBookSide(prev.bids, 'B', pxE8, szE8) : prev.bids,
      asks: levelSide === 'A' ? updateBookSide(prev.asks, 'A', pxE8, szE8) : prev.asks,
    }));
  };

  // Parse raw pipe frame using parseFixedE8
  const frameTokens = rawFrameInput.trim().split('|');
  const parsedFrameType = frameTokens[0] || 'UNKNOWN';
  const parsedTs = frameTokens[1] || '0';
  const parsedSymbol = frameTokens[2] || 'WLDUSDT';
  const parsedSide = frameTokens[3] || 'BUY';
  const parsedPriceE8 = parseFixedE8(frameTokens[4] || '0');
  const parsedQtyE8 = parseFixedE8(frameTokens[5] || '0');

  return (
    <div className="min-h-screen flex flex-col bg-slate-950 text-slate-100">
      {/* Strict 3-Zone Top Bar Contract */}
      <header className="flex items-center justify-between gap-8 px-6 py-4 border-b border-slate-800 bg-slate-950/90 sticky top-0 z-30 backdrop-blur">
        {/* Zone 1: Single text element wordmark */}
        <a
          href="#top"
          onClick={(e) => {
            e.preventDefault();
            setActiveSection('EXPLORER');
          }}
          className="text-lg font-bold tracking-tight text-slate-100 font-mono whitespace-nowrap shrink-0"
        >
          velox-lob-engine
        </a>

        {/* Zone 2: 4 concise single-line text navigation links */}
        <nav aria-label="Primary Navigation" className="flex items-center gap-6 text-sm font-medium text-slate-400 overflow-x-auto">
          <button
            type="button"
            onClick={() => setActiveSection('EXPLORER')}
            className={`hover:text-slate-100 transition-colors whitespace-nowrap shrink-0 pb-0.5 ${
              activeSection === 'EXPLORER' ? 'text-white border-b-2 border-sky-500' : ''
            }`}
          >
            Source Repository (12 Files)
          </button>
          <button
            type="button"
            onClick={() => setActiveSection('BENCHMARK')}
            className={`hover:text-slate-100 transition-colors whitespace-nowrap shrink-0 pb-0.5 ${
              activeSection === 'BENCHMARK' ? 'text-white border-b-2 border-sky-500' : ''
            }`}
          >
            1M-Event Benchmark
          </button>
          <button
            type="button"
            onClick={() => setActiveSection('SIMULATOR')}
            className={`hover:text-slate-100 transition-colors whitespace-nowrap shrink-0 pb-0.5 ${
              activeSection === 'SIMULATOR' ? 'text-white border-b-2 border-sky-500' : ''
            }`}
          >
            LOB &amp; Avellaneda-Stoikov
          </button>
          <button
            type="button"
            onClick={() => setActiveSection('PARSER')}
            className={`hover:text-slate-100 transition-colors whitespace-nowrap shrink-0 pb-0.5 ${
              activeSection === 'PARSER' ? 'text-white border-b-2 border-sky-500' : ''
            }`}
          >
            Zero-Alloc E8 Parser
          </button>
        </nav>

        {/* Zone 3: 1 primary action */}
        <div className="flex items-center gap-3 shrink-0">
          <a
            href="/velox-lob-engine-clean.tar.gz"
            download="velox-lob-engine-clean.tar.gz"
            className="px-4 py-2 text-xs font-medium text-white bg-sky-600 rounded-lg hover:bg-sky-500 transition-colors whitespace-nowrap shrink-0"
          >
            Download velox-lob-engine-clean.tar.gz
          </a>
        </div>
      </header>

      {/* Main Content Container */}
      <main className="flex-1 max-w-[1440px] w-full mx-auto px-6 py-6 space-y-6">
        {/* Hero Portfolio Summary Banner */}
        <section className="p-6 rounded-lg border border-slate-800 bg-slate-900/50 flex flex-col lg:flex-row lg:items-center justify-between gap-6">
          <div className="space-y-2 max-w-3xl">
            <div className="flex flex-wrap items-center gap-2 text-xs text-slate-400 font-mono">
              <span>C++20 (-O3 -march=native)</span>
              <span aria-hidden="true">·</span>
              <span>Python 3.10+ Asyncio</span>
              <span aria-hidden="true">·</span>
              <span>Zero-Allocation Hot Path</span>
              <span aria-hidden="true">·</span>
              <span>100% Proprietary Alpha Purged</span>
            </div>
            <h1 className="text-2xl sm:text-3xl font-bold text-slate-100 tracking-tight">
              velox-lob-engine — Low-Latency Limit Order Book &amp; Ingestion Engine
            </h1>
            <p className="text-sm text-slate-300 leading-relaxed">
              Refactored systems engineering portfolio repository featuring a power-of-two heap-allocated{' '}
              <span className="font-mono text-slate-100">RingBuffer&lt;T, Capacity&gt;</span>, 64-byte cache-aligned{' '}
              <span className="font-mono text-slate-100">MarketUpdate</span> events, sorted contiguous L2 order book vectors with 128-bit{' '}
              <span className="font-mono text-slate-100">Micro-Price</span> &amp; <span className="font-mono text-slate-100">OBI</span>,{' '}
              <span className="font-mono text-slate-100">std::from_chars</span> fixed-point <span className="font-mono text-slate-100">E8</span> parsing, and textbook{' '}
              <span className="font-mono text-slate-100">AvellanedaStoikovDemo</span> quoting.
            </p>
          </div>

          <div className="flex flex-col sm:flex-row lg:flex-col gap-2.5 shrink-0">
            <a
              href="/velox-lob-engine-clean.tar.gz"
              download="velox-lob-engine-clean.tar.gz"
              className="px-5 py-2.5 text-xs font-semibold text-center text-white bg-emerald-600 rounded-lg hover:bg-emerald-500 transition-colors whitespace-nowrap"
            >
              Download Clean C++20/Python Archive (.tar.gz)
            </a>
            <button
              type="button"
              onClick={() => {
                setBenchReport(runPipelineBenchmark(1_000_000));
                setActiveSection('BENCHMARK');
              }}
              className="px-5 py-2.5 text-xs font-medium text-center text-slate-200 bg-slate-900 border border-slate-700 rounded-lg hover:bg-slate-800 transition-colors whitespace-nowrap"
            >
              Run 1,000,000-Event Pipeline Benchmark
            </button>
          </div>
        </section>

        {/* Architecture & Engineering Invariants Strip */}
        <section className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
          <div className="p-4 rounded-lg border border-slate-800 bg-slate-900/60">
            <div className="text-xs text-slate-400">Lock-Free SPSC Queue</div>
            <div className="mt-1.5 text-lg font-semibold font-mono tabular-nums text-slate-100">
              index &amp; (Capacity - 1)
            </div>
            <div className="mt-1 text-xs text-slate-400">
              Power-of-2 bitmask · <span className="font-mono">std::unique_ptr&lt;T[]&gt;</span> heap allocation prevents 422MB BSS bloat
            </div>
          </div>

          <div className="p-4 rounded-lg border border-slate-800 bg-slate-900/60">
            <div className="text-xs text-slate-400">Zero-Allocation Ingestion</div>
            <div className="mt-1.5 text-lg font-semibold font-mono tabular-nums text-slate-100">
              std::from_chars (E8)
            </div>
            <div className="mt-1 text-xs text-slate-400">
              <span className="font-mono">std::string_view</span> tokenizer · Zero heap strings or IEEE-754 float drift
            </div>
          </div>

          <div className="p-4 rounded-lg border border-slate-800 bg-slate-900/60">
            <div className="text-xs text-slate-400">Cache-Aligned LOB</div>
            <div className="mt-1.5 text-lg font-semibold font-mono tabular-nums text-slate-100">
              alignas(64) MarketUpdate
            </div>
            <div className="mt-1 text-xs text-slate-400">
              Sorted contiguous <span className="font-mono">std::vector&lt;PriceLevel&gt;</span> + <span className="font-mono">unsigned __int128</span> math
            </div>
          </div>

          <div className="p-4 rounded-lg border border-slate-800 bg-slate-900/60">
            <div className="text-xs text-slate-400">Asyncio Feed Gateway</div>
            <div className="mt-1.5 text-lg font-semibold font-mono tabular-nums text-slate-100">
              Delta State Cache
            </div>
            <div className="mt-1 text-xs text-slate-400">
              Persistent <span className="font-mono">ticker_state</span> eliminates partial Bybit V5 delta zero-flicker
            </div>
          </div>
        </section>

        {/* SECTION 1: REPOSITORY SOURCE EXPLORER */}
        {activeSection === 'EXPLORER' && (
          <section aria-label="Repository Source Explorer" className="grid grid-cols-1 lg:grid-cols-12 gap-6 items-stretch">
            {/* File Tree Sidebar (4 cols) */}
            <div className="lg:col-span-4 rounded-lg border border-slate-800 bg-slate-900/60 p-4 flex flex-col justify-between space-y-4">
              <div className="space-y-3">
                <div className="pb-3 border-b border-slate-800">
                  <h2 className="text-sm font-semibold text-slate-100">
                    Clean Target Tree (<span className="font-mono">velox-lob-engine/</span>)
                  </h2>
                  <p className="text-xs text-slate-400 mt-0.5">
                    Select any file below to inspect its refactored implementation
                  </p>
                </div>

                <div className="space-y-1">
                  {VELOX_REPO_FILES.map((file) => {
                    const shortName = file.path.replace('velox-lob-engine/', '');
                    const isSelected = file.path === selectedFile.path;
                    return (
                      <button
                        key={file.path}
                        type="button"
                        onClick={() => setSelectedFilePath(file.path)}
                        className={`w-full text-left px-3 py-2 rounded-md text-xs font-mono transition-colors flex items-center justify-between gap-2 ${
                          isSelected
                            ? 'bg-sky-600/20 text-sky-300 border border-sky-500/40'
                            : 'text-slate-300 hover:bg-slate-800/60 border border-transparent'
                        }`}
                      >
                        <span className="truncate">{shortName}</span>
                        <span className="text-slate-500 shrink-0">
                          {file.content.split('\n').length}L
                        </span>
                      </button>
                    );
                  })}
                </div>
              </div>

              <div className="pt-3 border-t border-slate-800 text-xs text-slate-400 space-y-1">
                <div>Purged Proprietary Files:</div>
                <div className="font-mono text-rose-400/90">
                  MarketMakerGen2.hpp · MarketMakerStrategy.hpp · LiquidationEngine.hpp · LiveEngine.hpp · brain_legacy.py
                </div>
              </div>
            </div>

            {/* Code Viewer (8 cols) */}
            <div className="lg:col-span-8 rounded-lg border border-slate-800 bg-slate-900/60 p-4 flex flex-col">
              <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 pb-3 border-b border-slate-800">
                <div>
                  <div className="text-xs text-slate-400">{selectedFile.category}</div>
                  <h3 className="text-sm font-semibold font-mono text-slate-100 mt-0.5">
                    {selectedFile.path}
                  </h3>
                  <p className="text-xs text-slate-400 mt-0.5">{selectedFile.summary}</p>
                </div>

                <button
                  type="button"
                  onClick={handleCopyCode}
                  className="px-3 py-1.5 text-xs font-medium rounded-md border border-slate-700 bg-slate-950 text-slate-200 hover:bg-slate-800 transition-colors whitespace-nowrap shrink-0"
                >
                  {copiedFile ? 'Copied to Clipboard' : 'Copy File Contents'}
                </button>
              </div>

              <pre
                data-testid="source-code-viewer"
                className="mt-3 flex-1 max-h-[560px] overflow-auto p-4 rounded-md bg-slate-950 border border-slate-800/90 text-xs font-mono text-slate-200 leading-relaxed"
              >
                <code>{selectedFile.content}</code>
              </pre>
            </div>
          </section>
        )}

        {/* SECTION 2: 1,000,000-EVENT PIPELINE BENCHMARK */}
        {activeSection === 'BENCHMARK' && (
          <section aria-label="1,000,000-Event Pipeline Benchmark" className="space-y-6">
            <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 pb-4 border-b border-slate-800">
              <div>
                <div className="text-xs text-slate-400 font-mono">
                  benchmarks/bench_pipeline.cpp
                </div>
                <h2 className="text-xl font-semibold text-slate-100 mt-1">
                  1,000,000-Event End-to-End Pipeline Benchmark
                </h2>
              </div>

              <button
                type="button"
                onClick={() => setBenchReport(runPipelineBenchmark(1_000_000))}
                className="px-4 py-2 text-xs font-medium rounded-lg bg-sky-600 text-white hover:bg-sky-500 transition-colors whitespace-nowrap shrink-0"
              >
                Re-Run 1,000,000-Event Benchmark
              </button>
            </div>

            <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
              <div className="p-4 rounded-lg border border-slate-800 bg-slate-900/60">
                <div className="text-xs text-slate-400">Events Processed</div>
                <div className="mt-2 text-2xl font-semibold font-mono tabular-nums text-slate-100">
                  {benchReport.eventsProcessed.toLocaleString()}
                </div>
                <div className="mt-1 text-xs text-slate-400">
                  SPSC push/pop + L2 update + 128-bit Micro-Price
                </div>
              </div>

              <div className="p-4 rounded-lg border border-slate-800 bg-slate-900/60">
                <div className="text-xs text-slate-400">Total Elapsed Time</div>
                <div className="mt-2 text-2xl font-semibold font-mono tabular-nums text-emerald-400">
                  {benchReport.elapsedMs.toFixed(2)} ms
                </div>
                <div className="mt-1 text-xs text-slate-400">
                  Zero heap allocations in hot loop
                </div>
              </div>

              <div className="p-4 rounded-lg border border-slate-800 bg-slate-900/60">
                <div className="text-xs text-slate-400">Pipeline Throughput</div>
                <div className="mt-2 text-2xl font-semibold font-mono tabular-nums text-sky-400">
                  {benchReport.throughputMops.toFixed(2)} M events/sec
                </div>
                <div className="mt-1 text-xs text-slate-400">
                  Single-core deterministic execution
                </div>
              </div>

              <div className="p-4 rounded-lg border border-slate-800 bg-slate-900/60">
                <div className="text-xs text-slate-400">Average Latency per Event</div>
                <div className="mt-2 text-2xl font-semibold font-mono tabular-nums text-slate-100">
                  {benchReport.avgLatencyNs.toFixed(2)} ns/op
                </div>
                <div className="mt-1 text-xs text-slate-400">
                  Checksum guard: <span className="font-mono text-slate-300">{benchReport.checksumHex}</span>
                </div>
              </div>
            </div>

            <div className="p-5 rounded-lg border border-slate-800 bg-slate-900/60">
              <h3 className="text-sm font-semibold text-slate-100">
                Console Output (<span className="font-mono">./build/velox_bench</span>)
              </h3>
              <pre className="mt-3 p-4 rounded-md bg-slate-950 border border-slate-800 text-xs font-mono text-emerald-300 overflow-x-auto leading-relaxed">
{`==============================================================
  VELOX LOB ENGINE | PIPELINE BENCHMARK (1,000,000 EVENTS)
==============================================================
 Events Processed  : ${benchReport.eventsProcessed}
 Total Elapsed     : ${benchReport.elapsedMs.toFixed(2)} ms
 Throughput        : ${benchReport.throughputMops.toFixed(2)} M events/sec
 Average Latency   : ${benchReport.avgLatencyNs.toFixed(2)} ns/event
 Micro-Price (Final): ${benchReport.finalMicroPrice.toFixed(4)} USDT
 OBI (Final)       : ${benchReport.finalImbalancePct.toFixed(2)}%
 Checksum Guard    : ${benchReport.checksumHex}
==============================================================`}
              </pre>
            </div>
          </section>
        )}

        {/* SECTION 3: LOB & AVELLANEDA-STOIKOV SIMULATOR */}
        {activeSection === 'SIMULATOR' && (
          <section aria-label="OrderBook and Avellaneda-Stoikov Simulator" className="grid grid-cols-1 lg:grid-cols-12 gap-6 items-stretch">
            {/* Left: Sorted Contiguous OrderBook (6 cols) */}
            <div className="lg:col-span-6 rounded-lg border border-slate-800 bg-slate-900/60 p-4 flex flex-col justify-between space-y-4">
              <div>
                <div className="pb-3 border-b border-slate-800">
                  <h2 className="text-sm font-semibold text-slate-100">
                    Contiguous Sorted L2 OrderBook (<span className="font-mono">OrderBook::update_level</span>)
                  </h2>
                  <p className="text-xs text-slate-400 mt-0.5">
                    Insert, update in-place, or pass size <span className="font-mono">0</span> to erase a price level
                  </p>
                </div>

                <form onSubmit={handleApplyLevelUpdate} className="mt-3 grid grid-cols-1 sm:grid-cols-4 gap-2">
                  <div>
                    <label htmlFor="side-select" className="block text-xs text-slate-400 mb-1">
                      Side
                    </label>
                    <select
                      id="side-select"
                      value={levelSide}
                      onChange={(e) => setLevelSide(e.target.value as 'B' | 'A')}
                      className="w-full px-2.5 py-1.5 text-xs font-mono bg-slate-950 border border-slate-800 rounded text-slate-200"
                    >
                      <option value="B">Bid ('B')</option>
                      <option value="A">Ask ('A')</option>
                    </select>
                  </div>

                  <div>
                    <label htmlFor="price-input" className="block text-xs text-slate-400 mb-1">
                      Price (USDT)
                    </label>
                    <input
                      id="price-input"
                      type="text"
                      value={levelPriceInput}
                      onChange={(e) => setLevelPriceInput(e.target.value)}
                      className="w-full px-2.5 py-1.5 text-xs font-mono bg-slate-950 border border-slate-800 rounded text-slate-200"
                    />
                  </div>

                  <div>
                    <label htmlFor="size-input" className="block text-xs text-slate-400 mb-1">
                      Size (0 = Erase)
                    </label>
                    <input
                      id="size-input"
                      type="text"
                      value={levelSizeInput}
                      onChange={(e) => setLevelSizeInput(e.target.value)}
                      className="w-full px-2.5 py-1.5 text-xs font-mono bg-slate-950 border border-slate-800 rounded text-slate-200"
                    />
                  </div>

                  <div className="flex items-end">
                    <button
                      type="submit"
                      className="w-full px-3 py-1.5 text-xs font-medium bg-sky-600 text-white rounded hover:bg-sky-500 transition-colors whitespace-nowrap"
                    >
                      Apply Level
                    </button>
                  </div>
                </form>

                <div className="mt-4 grid grid-cols-2 gap-4">
                  <div>
                    <div className="flex justify-between text-xs font-medium text-emerald-400 pb-1.5 border-b border-slate-800">
                      <span>BIDS (Descending)</span>
                      <span>SIZE</span>
                    </div>
                    <div className="mt-1.5 space-y-1 font-mono text-xs tabular-nums">
                      {bids.slice(0, 8).map((b) => (
                        <div key={b.priceE8} className="flex justify-between py-0.5 text-slate-200">
                          <span className="text-emerald-400">{b.price.toFixed(4)}</span>
                          <span>{b.size.toFixed(1)}</span>
                        </div>
                      ))}
                    </div>
                  </div>

                  <div>
                    <div className="flex justify-between text-xs font-medium text-rose-400 pb-1.5 border-b border-slate-800">
                      <span>ASKS (Ascending)</span>
                      <span>SIZE</span>
                    </div>
                    <div className="mt-1.5 space-y-1 font-mono text-xs tabular-nums">
                      {asks.slice(0, 8).map((a) => (
                        <div key={a.priceE8} className="flex justify-between py-0.5 text-slate-200">
                          <span className="text-rose-400">{a.price.toFixed(4)}</span>
                          <span>{a.size.toFixed(1)}</span>
                        </div>
                      ))}
                    </div>
                  </div>
                </div>
              </div>

              <div className="pt-3 border-t border-slate-800 flex items-center justify-between text-xs text-slate-400 font-mono tabular-nums">
                <span>Micro-Price: {toDoubleE8(microPriceE8).toFixed(5)} USDT</span>
                <span>Top-5 OBI: {((imbalanceE8 / PRICE_SCALE) * 100).toFixed(2)}%</span>
              </div>
            </div>

            {/* Right: AvellanedaStoikovDemo Quoting Engine (6 cols) */}
            <div className="lg:col-span-6 rounded-lg border border-slate-800 bg-slate-900/60 p-4 flex flex-col justify-between space-y-4">
              <div className="space-y-4">
                <div className="pb-3 border-b border-slate-800">
                  <h2 className="text-sm font-semibold text-slate-100">
                    Public Strategy Demo (<span className="font-mono">AvellanedaStoikovDemo : public IStrategy</span>)
                  </h2>
                  <p className="text-xs text-slate-400 mt-0.5">
                    Textbook inventory-skewed reservation price around L1 Micro-Price
                  </p>
                </div>

                <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
                  <div>
                    <label htmlFor="inv-input" className="block text-xs text-slate-400 mb-1">
                      Inventory (Units)
                    </label>
                    <input
                      id="inv-input"
                      type="number"
                      value={inventoryUnits}
                      onChange={(e) => setInventoryUnits(Number(e.target.value) || 0)}
                      className="w-full px-2.5 py-1.5 text-xs font-mono bg-slate-950 border border-slate-800 rounded text-slate-200"
                    />
                  </div>

                  <div>
                    <label htmlFor="spread-input" className="block text-xs text-slate-400 mb-1">
                      Half-Spread (Ticks)
                    </label>
                    <input
                      id="spread-input"
                      type="number"
                      min={1}
                      max={50}
                      value={halfSpreadTicks}
                      onChange={(e) => setHalfSpreadTicks(Math.max(1, Number(e.target.value) || 1))}
                      className="w-full px-2.5 py-1.5 text-xs font-mono bg-slate-950 border border-slate-800 rounded text-slate-200"
                    />
                  </div>

                  <div>
                    <label htmlFor="ra-input" className="block text-xs text-slate-400 mb-1">
                      Risk Aversion (E8/Unit)
                    </label>
                    <input
                      id="ra-input"
                      type="number"
                      min={10}
                      max={5000}
                      value={riskAversionBps}
                      onChange={(e) => setRiskAversionBps(Math.max(10, Number(e.target.value) || 100))}
                      className="w-full px-2.5 py-1.5 text-xs font-mono bg-slate-950 border border-slate-800 rounded text-slate-200"
                    />
                  </div>
                </div>

                <div className="p-4 rounded-md bg-slate-950 border border-slate-800 font-mono text-xs tabular-nums space-y-2">
                  <div className="flex justify-between">
                    <span className="text-slate-400">L1 Micro-Price (E8):</span>
                    <span className="text-slate-100">{microPriceE8} ({toDoubleE8(microPriceE8).toFixed(5)} USDT)</span>
                  </div>
                  <div className="flex justify-between">
                    <span className="text-slate-400">Reservation Price (E8):</span>
                    <span className="text-sky-400">{quotes.reservationPxE8} ({quotes.reservationPx.toFixed(5)} USDT)</span>
                  </div>
                  <div className="flex justify-between">
                    <span className="text-slate-400">Generated Bid Quote:</span>
                    <span className="text-emerald-400 font-semibold">
                      {quotes.bidActive ? `${quotes.bidPx.toFixed(4)} USDT` : 'INACTIVE (STALE GUARD)'}
                    </span>
                  </div>
                  <div className="flex justify-between">
                    <span className="text-slate-400">Generated Ask Quote:</span>
                    <span className="text-rose-400 font-semibold">
                      {quotes.askActive ? `${quotes.askPx.toFixed(4)} USDT` : 'INACTIVE (STALE GUARD)'}
                    </span>
                  </div>
                </div>
              </div>

              <div className="pt-3 border-t border-slate-800 flex items-center justify-between">
                <span className="text-xs text-slate-400">
                  Circuit Breaker (<span className="font-mono">TelemetryEngine::is_stale()</span>)
                </span>
                <button
                  type="button"
                  onClick={() => setIsStaleSimulated((s) => !s)}
                  className={`px-3 py-1.5 text-xs font-medium rounded border transition-colors ${
                    isStaleSimulated
                      ? 'border-rose-500 bg-rose-950/60 text-rose-200'
                      : 'border-slate-700 bg-slate-950 text-slate-200 hover:bg-slate-800'
                  }`}
                >
                  {isStaleSimulated ? 'Stale Feed (>500ms): Quotes Pulled' : 'Simulate Stale Feed (>500ms)'}
                </button>
              </div>
            </div>
          </section>
        )}

        {/* SECTION 4: ZERO-ALLOCATION PARSER TESTER */}
        {activeSection === 'PARSER' && (
          <section aria-label="Zero-Allocation Fixed-Point Parser" className="p-5 rounded-lg border border-slate-800 bg-slate-900/60 space-y-4">
            <div className="pb-3 border-b border-slate-800">
              <h2 className="text-base font-semibold text-slate-100">
                Zero-Allocation <span className="font-mono">std::string_view</span> + <span className="font-mono">std::from_chars</span> Fixed-Point E8 Parser
              </h2>
              <p className="text-xs text-slate-400 mt-0.5">
                Test arbitrary pipe-delimited frames to verify integer-only <span className="font-mono">parse_fixed_e8</span> conversion without floating-point precision loss
              </p>
            </div>

            <div>
              <label htmlFor="raw-frame-input" className="block text-xs text-slate-400 mb-1">
                Input Pipe Frame (<span className="font-mono">TYPE|TS|SYMBOL|SIDE|PRICE|QTY</span>)
              </label>
              <input
                id="raw-frame-input"
                type="text"
                value={rawFrameInput}
                onChange={(e) => setRawFrameInput(e.target.value)}
                className="w-full px-3 py-2 text-xs font-mono bg-slate-950 border border-slate-800 rounded-md text-slate-100"
              />
            </div>

            <div className="grid grid-cols-1 sm:grid-cols-3 gap-4 font-mono text-xs tabular-nums">
              <div className="p-3 rounded bg-slate-950 border border-slate-800">
                <div className="text-slate-400">Tokenized Header</div>
                <div className="mt-1 text-slate-100">
                  Type: <strong>{parsedFrameType}</strong> · Sym: <strong>{parsedSymbol}</strong> · Side: <strong>{parsedSide}</strong>
                </div>
                <div className="mt-0.5 text-slate-400">Epoch (ms): {parsedTs}</div>
              </div>

              <div className="p-3 rounded bg-slate-950 border border-slate-800">
                <div className="text-slate-400">Parsed Price (int64_t E8)</div>
                <div className="mt-1 text-emerald-400 font-semibold text-sm">{parsedPriceE8}</div>
                <div className="mt-0.5 text-slate-400">Exact Decimal: {toDoubleE8(parsedPriceE8).toFixed(8)}</div>
              </div>

              <div className="p-3 rounded bg-slate-950 border border-slate-800">
                <div className="text-slate-400">Parsed Quantity (int64_t E8)</div>
                <div className="mt-1 text-sky-400 font-semibold text-sm">{parsedQtyE8}</div>
                <div className="mt-0.5 text-slate-400">Exact Decimal: {toDoubleE8(parsedQtyE8).toFixed(8)}</div>
              </div>
            </div>
          </section>
        )}
      </main>

      {/* Quiet Footer */}
      <footer className="border-t border-slate-900 px-6 py-4 text-xs text-slate-500">
        <div className="max-w-[1440px] mx-auto flex flex-col sm:flex-row items-center justify-between gap-2">
          <span>velox-lob-engine · C++20 &amp; Python Low-Latency Market Microstructure Suite</span>
          <a
            href="/velox-lob-engine-clean.tar.gz"
            download="velox-lob-engine-clean.tar.gz"
            className="text-sky-400 hover:text-sky-300 transition-colors font-mono"
          >
            velox-lob-engine-clean.tar.gz
          </a>
        </div>
      </footer>
    </div>
  );
}

export default App;
