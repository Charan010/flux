# huffman

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

2. Build a decode tree from the table.

3. For each chunk, read the valid bit count, walk the decode tree bit by bit, and buffered into a vector when we reach a leaf node or character and then written to the file.


## Install
```bash
cmake -S . -B build
cmake --build build
cmake --install build


## Usage:

```bash
huffman -c <absolute-path-to-the-file> (to encode)
huffman -d <file-to-be-decoded> <file-to-be-dumped> 

```

## Benchmarks

| Dataset    | Original | Compressed | Ratio  | Throughput |
|------------|----------|------------|--------|------------|
| random.bin | 512 MB   | 512 MB     | 0.99×  | 388 MB/s   |
| zeros.bin  | 512 MB   | 64 MB      | 7.99×  | 879 MB/s   |
| wiki.txt   | 1.2 GB   | 702 MB     | 1.76×  | 520 MB/s   |
| big.txt    | 6 MB     | 3.5 MB     | 1.76×  | 187 MB/s   |


random.bin is not at all compressed and is the same size as before because when the data is random, entropy is high so cannot be compressed. This shows the fundamental limit of
information theory.

## Requirements

- C++17 or later
- CMake 3.10+