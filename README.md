# relic

Deduplicating snapshot storage with compression. relic backs up files and
directories, storing only data it hasn't seen before, so keeping many versions
of slowly-changing data costs little extra space. Any snapshot can be restored
exactly.

## How it works

Files are split into variable-sized chunks by content-defined chunking (a
rolling hash chooses the boundaries). Each chunk is identified by its BLAKE3
hash; a chunk is stored only if that hash is not already present, so identical
data — across files or across snapshots — is stored once. Stored chunks are
compressed with zstd (level 3), with a raw fallback for data that does not
compress.

Deleting a snapshot leaves chunks that nothing references. Garbage collection
reclaims them with a mark-and-sweep pass over the live snapshots.

## Build

Requires a C++20 compiler, CMake, zstd, and BLAKE3.

```bash
sudo apt install libzstd-dev libblake3-dev   # Debian/Ubuntu
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The binary is `build/relic`.

## Usage

```
./relic /path/to/store

relic> backup <path> <name>    snapshot a file or directory
relic> restore <name> <path>   materialize a snapshot
relic> list                    show stored snapshots
relic> info <snapshot>         show a snapshot's size and chunk stats
relic> gc [--dry-run]          reclaim space from deleted snapshots
relic> benchmark [path]        backup + restore + verify a directory
relic> dedup-bench [path]      measure deduplication: store growth per snapshot
```

## Benchmarks

This benchmark measures deduplication efficiency by backing up mostly identical snapshots with minimal changes and recording the additional storage consumed. Its sole focus is deduplication.

```
action                     logical (MB)   stored (MB)   saved %
---------------------------------------------------------------
first backup                     200.43         77.30      61.4
re-backup, unchanged             200.43          0.00     100.0
re-backup, 1 file +64KB          200.49          0.06     100.0
backup 2 identical copies        400.85          0.00     100.0
---------------------------------------------------------------
total (4 snapshots)             1002.19         77.42      92.3
```

Logical is the original data size; stored is what relic actually wrote
(compressed and deduplicated). A 64 KB random edit added 0.06 MB — the change
is stored, not the file it lives in. Reproduce with `dedup-bench <corpus>`.

## Limitations

- Single process: one relic instance per store, enforced by a lock file.
- Garbage collection is offline — it assumes no backup is running concurrently.
- The chunk index is held in memory (persisted to disk between runs), so
  memory scales with the total number of chunks in the store.