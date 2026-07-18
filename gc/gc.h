#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "storage/index.h"

struct PackStats {

    uint64_t live_objects = 0;
    uint64_t garbage_objects = 0;

    uint64_t live_bytes = 0;
    uint64_t garbage_bytes = 0;
};

struct GcReport {

    uint64_t total_objects = 0;
    uint64_t total_live_objects = 0;
    uint64_t total_garbage_objects = 0;

    uint64_t total_live_bytes = 0;
    uint64_t total_garbage_bytes = 0;

    std::unordered_map<uint32_t, PackStats> per_pack;
};

class GcScanner {

public:
    explicit GcScanner(const Index& index);

    std::unordered_set<std::string> collect_live_digests(const std::filesystem::path& snapshot_directory) const;
    GcReport dry_run(const std::unordered_set<std::string>& live_digests) const;

private:
    const Index& index_;
};