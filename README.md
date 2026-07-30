# relic

Stores compressed, deduplicated snapshots of files and directories saving only new data. This keeps incremental backups space efficient while allowing any snapshot to be restored.

## How it works

Files are split into variable size chunks using content-defined chunking. Chunks are identified by BLAKE3 hash and duplicate chunks are stored only once and chunks are compressed with zstd.

When a snapshot is deleted, some chunks are not used by any of the snapshots. Mark and sweep garbage collector removes the unreferenced chunks to reclaim space.


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
action                     Actual size(MB)   Stored size (MB)   Saved %
---------------------------------------------------------------
first backup                     200.43         77.30      61.4
re-backup, unchanged             200.43          0.00     100.0
re-backup, 1 file +64KB          200.49          0.06     100.0
backup 2 identical copies        400.85          0.00     100.0
---------------------------------------------------------------
total (4 snapshots)             1002.19         77.42      92.3
```


## Limitations

- Single process: one relic instance per store, enforced by a lock file.
- Garbage collection is offline — it assumes no backup is running concurrently.
- The chunk index is held in memory (persisted to disk between runs), so
  memory scales with the total number of chunks in the store.