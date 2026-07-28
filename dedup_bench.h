#pragma once

#include <string>

class DedupEngine; //forward declaration.

/*
	benchmarks deduplication of Silesia dataset by measuring how much each snapshot grows when some 
	data changes or when nothing changes.
*/
void run_dedup_bench(DedupEngine &engine, const std::string &store, const std::string &corpus_path);
