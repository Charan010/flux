# flux

A parallel Huffman encoder/decoder written in C++ from scratch — no compression libraries used.

## How It Works

According to information theory, I(x) ∝ 1/P(x) — the more likely a symbol is, the less
information it carries. Huffman coding assigns shorter binary codes to frequent symbols and
longer codes to rare ones, reducing the total bits required to represent data.

**Compression pipeline:**
1. The input file is split into chunks and sent to a thread pool. Each thread counts symbol frequencies locally, then merges into a global frequency table.

2. A Huffman tree is built from the frequency table using a min-heap (higher frequency characters get smaller code).

3. The tree is converted to a canonical form so the decoder can reconstruct it from just 
with just characters length instead of encoding a whole tree on disk.

4. Each chunk is encoded in parallel — symbols replaced with their variable-length bit codes, packed tightly into bytes, and written in order via a chunk buffer that preserves original sequence.


**Decompression pipeline:**

1. Read the 256-byte header and reconstruct the canonical Huffman table deterministically.

2. Build a decode tree from the table. Look up tables(LUT) speed up the process by allowing to look at multiple bits at a time instead of reading bit by bit.

3. Chunks are managed by ChunkBuffered and writes in order and to prevent contention, using lock free Buffer.

## Benchmarks

Tested on Ryzen 5 5600H (6 cores), 6 threads, 4MB chunks.

| File | Size | Compressed | Ratio | Comp. Speed | Decomp. Speed |
|------|------|------------|-------|-------------|---------------|
| corpus.txt (English text) | 500 MB | 287 MB | 0.57x | 641 MB/s | 820 MB/s |
| random.dat (urandom) | 100 MB | 100 MB | 1.00x | 513 MB/s | 784 MB/s |
| zeros.bin (single symbol) | 50 MB | 6.25 MB | 0.12x | 609 MB/s | 740 MB/s |


> random.dat does not compress — high entropy data is incompressible by 
> definition. This is the fundamental limit of lossless compression.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Usage

```bash
# Compress
flux -c <input_file>          # outputs <input_file>.huf

# Decompress  
flux -d <file.huf> <output>

```

## Requirements

- C++17 or later
- CMake 3.10+