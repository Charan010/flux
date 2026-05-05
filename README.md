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

All benchmarks were run on a Ryzen 5 5600H (6 cores / 12 threads)  
Configuration: 6 worker threads, 4 MB chunk size

| Dataset | Size | Compressed Size | Ratio | Compression Speed | Decompression Speed |
|--------|------|-----------------|-------|-------------------|---------------------|
| English text (corpus.txt) | 500 MB | 287 MB | 0.57 | 641 MB/s | 820 MB/s |
| Random data (urandom)     | 100 MB | 100 MB | 1.00 | 513 MB/s | 784 MB/s |
| Repetitive (zeros.bin)    | 50 MB  | 6.25 MB | 0.12 | 609 MB/s | 740 MB/s |


## Build

```bash
cmake -S . -B build
cmake --build build
```

## Usage

```bash

// this is a REPL based CLI tool for now :P.

flux
 -c <input_file> <output_file>          

# Decompress  
-d <file.huf> <output>

#to verify if both files are same using sha256sum.
-v <file1> <file2>


```

## Requirements

- C++17 or later
- CMake 3.10+