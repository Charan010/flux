#pragma once

#include <string>


class DedupEngine;

/* Backs up Silesia dataset which consists of all kinds of data to test the deduplication and compression and
   correctness of the backup tool with proper metrics. */
void run_bench(DedupEngine &engine,std::string store, const std::string &corpus_path);
