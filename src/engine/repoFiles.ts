import cmakeSrc from '../../velox-lob-engine/CMakeLists.txt?raw';
import readmeSrc from '../../velox-lob-engine/README.md?raw';
import gitignoreSrc from '../../velox-lob-engine/.gitignore?raw';
import protocolSrc from '../../velox-lob-engine/include/velox/protocol.hpp?raw';
import ringBufferSrc from '../../velox-lob-engine/include/velox/ring_buffer.hpp?raw';
import orderBookSrc from '../../velox-lob-engine/include/velox/order_book.hpp?raw';
import telemetrySrc from '../../velox-lob-engine/include/velox/telemetry.hpp?raw';
import strategySrc from '../../velox-lob-engine/include/velox/strategy_interface.hpp?raw';
import mainCppSrc from '../../velox-lob-engine/src/main.cpp?raw';
import fileWriterSrc from '../../velox-lob-engine/src/file_writer.hpp?raw';
import feedHandlerSrc from '../../velox-lob-engine/python/feed_handler.py?raw';
import benchPipelineSrc from '../../velox-lob-engine/benchmarks/bench_pipeline.cpp?raw';

export interface RepoFileEntry {
  path: string;
  category: 'Build & Docs' | 'C++20 Headers (include/velox)' | 'C++20 Engine (src)' | 'Python Gateway' | 'Benchmarks';
  language: 'cpp' | 'python' | 'cmake' | 'markdown';
  summary: string;
  content: string;
}

export const VELOX_REPO_FILES: RepoFileEntry[] = [
  {
    path: 'velox-lob-engine/include/velox/ring_buffer.hpp',
    category: 'C++20 Headers (include/velox)',
    language: 'cpp',
    summary: 'Unified SPSC Lock-Free Queue with power-of-2 bitmask, heap allocation, and alignas(64) atomic cursors.',
    content: ringBufferSrc,
  },
  {
    path: 'velox-lob-engine/include/velox/order_book.hpp',
    category: 'C++20 Headers (include/velox)',
    language: 'cpp',
    summary: '64B cache-aligned MarketUpdate + sorted contiguous L2 vectors (update_level, load_snapshot, 128-bit Micro-Price & OBI).',
    content: orderBookSrc,
  },
  {
    path: 'velox-lob-engine/include/velox/telemetry.hpp',
    category: 'C++20 Headers (include/velox)',
    language: 'cpp',
    summary: '1,000-trade circular history with O(1) running volume accumulators for signed VPIN, trade velocity, and staleness.',
    content: telemetrySrc,
  },
  {
    path: 'velox-lob-engine/include/velox/strategy_interface.hpp',
    category: 'C++20 Headers (include/velox)',
    language: 'cpp',
    summary: 'Abstract IStrategy interface and textbook AvellanedaStoikovDemo reservation-price market maker.',
    content: strategySrc,
  },
  {
    path: 'velox-lob-engine/include/velox/protocol.hpp',
    category: 'C++20 Headers (include/velox)',
    language: 'cpp',
    summary: 'Packed binary wire structs (#pragma pack(1), E8 fixed-point): Trade (25B), Snapshot (1608B), Liq (25B), Ticker (32B).',
    content: protocolSrc,
  },
  {
    path: 'velox-lob-engine/src/main.cpp',
    category: 'C++20 Engine (src)',
    language: 'cpp',
    summary: 'Zero-allocation std::string_view + std::from_chars E8 ingestion loop and clean draining consumer thread.',
    content: mainCppSrc,
  },
  {
    path: 'velox-lob-engine/src/file_writer.hpp',
    category: 'C++20 Engine (src)',
    language: 'cpp',
    summary: 'RAII 1 MiB std::vector<char> buffered binary writer with automatic 60-minute file rotation.',
    content: fileWriterSrc,
  },
  {
    path: 'velox-lob-engine/python/feed_handler.py',
    category: 'Python Gateway',
    language: 'python',
    summary: 'Asyncio WebSocket feed with Top-50 L2 book state, persistent ticker_state delta cache, and exponential backoff.',
    content: feedHandlerSrc,
  },
  {
    path: 'velox-lob-engine/benchmarks/bench_pipeline.cpp',
    category: 'Benchmarks',
    language: 'cpp',
    summary: '1,000,000-event latency & throughput benchmark across RingBuffer, OrderBook, TelemetryEngine, and AvellanedaStoikovDemo.',
    content: benchPipelineSrc,
  },
  {
    path: 'velox-lob-engine/CMakeLists.txt',
    category: 'Build & Docs',
    language: 'cmake',
    summary: 'C++20 build configuration (-O3 -march=native -Wall -Wextra -pthread) targeting velox_engine and velox_bench.',
    content: cmakeSrc,
  },
  {
    path: 'velox-lob-engine/README.md',
    category: 'Build & Docs',
    language: 'markdown',
    summary: 'Systems architecture documentation with Mermaid diagram, memory layout breakdown, and benchmark report.',
    content: readmeSrc,
  },
  {
    path: 'velox-lob-engine/.gitignore',
    category: 'Build & Docs',
    language: 'markdown',
    summary: 'Clean C++20/Python ignore rules for build artifacts, binary captures, and virtual environments.',
    content: gitignoreSrc,
  },
];
