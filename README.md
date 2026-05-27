# flux

A parallel file compression system built around a chunked pipeline architecture in C++, optimized for throughput over maximum compression ratio.

---

## How it works

Files are split into fixed-size chunks (default 4 MB), processed concurrently by a worker pool, and reassembled in order.

```
File → Chunk Splitter → Job Queue → Worker Pool → Codec → Ordered Writer → Output
```

**Chunk splitter** — memory-maps input and divides it into independent chunks

**Worker pool** — each worker compresses a chunk and returns metadata asynchronously

**Ordered writer** — buffers out-of-order completions and flushes sequentially to disk

**Codec layer** — pluggable interface; new algorithms slot in without touching the pipeline

Available codecs: `lz4` `huffman`

If you want to go deeper on the design decisions, tradeoffs, and the intuition behind the pipeline — take a look at [architecture.md](architecture.md).

---

## Build & run

```bash
./run.sh      # build, install, start daemon, launch web UI
./start.sh    # start an existing installation
```

Web UI (nothing fancy) → `http://localhost:8080`

---

## Go ↔ C++ communication

The Go server talks to the C++ daemon over a Unix socket at `/tmp/flux.sock`. Unix sockets are a Linux primitive — lower overhead than TCP and a cleaner abstraction for local IPC. Requests and responses are newline-delimited JSON; no custom wire format needed.

---

## Benchmarks

Tested on `enwik9` (953.67 MB). Numbers reflect computation only — I/O is excluded.

| Chunk size | Compressed | Ratio | Compress | Decompress |
|---|---|---|---|---|
| 1 MB | 452.88 MB | 2.11× | 1311 MB/s | 4322 MB/s |
| 4 MB | 451.54 MB | 2.11× | 1282 MB/s | 4568 MB/s |

---

## Structure

| Directory | Purpose |
|---|---|
| `codecs/` | Huffman and LZ4 implementations |
| `io/` | mmap I/O and bitstream utilities |
| `pipeline/` | Ordered queues and writer |
| `threadpool/` | Worker pool |
| `daemon/` | Unix socket daemon |
| `server/` | Go HTTP server and web UI |
| `benchmark/` | Throughput and ratio benchmarking |

---

## Requirements

- C++20, CMake ≥ 3.16
- Go
- Linux or macOS