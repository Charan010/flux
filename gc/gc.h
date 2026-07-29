#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "storage/index.h"


struct PackStats{
	uint64_t live_objects = 0;
	uint64_t garbage_objects = 0;
	uint64_t live_bytes = 0;
	uint64_t garbage_bytes = 0;
};

struct GcReport {
    uint64_t total_objects         = 0;
    uint64_t total_live_objects    = 0;
    uint64_t total_garbage_objects = 0;
    uint64_t total_live_bytes      = 0;
    uint64_t total_garbage_bytes   = 0;
    std::unordered_map<uint32_t, PackStats> per_pack;

    /* Digests referenced by a manifest but absent from the index. Non-empty means
       the store is already corrupt: a snapshot cannot be restored. Compaction must
       refuse to run, because sweeping on top of corruption destroys evidence. */
    std::vector<std::string> dangling;
	
};

class GcScanner{
public:
	explicit GcScanner(const Index &index);

	std::unordered_set<std::string> collect_live_objects(const std::filesystem::path &manifest_dir) const;
	GcReport analyze(const std::unordered_set<std::string> &live_objects) const;

	std::vector<uint32_t> select_packs_for_compaction(const GcReport &report, double min_garbage_ratio) const;

private:
	const Index &index_;
};