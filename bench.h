#pragma once

#include <string>


class SnapshotStore;

/* Backs up Silesia dataset which consists of all kinds of data to test the deduplication and compression and
   correctness of the backup tool with proper metrics. */
void run_bench(SnapshotStore &engine,std::string store, const std::string &corpus_path);
