#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "chunking/chunker.h"
#include "hashing/hasher.h"
#include "hashing/digest.h"
#include "index.h"

class ObjectStore {

public:
    explicit ObjectStore(const std::string &store_directory = "store", 
						uint64_t max_pack_size = 256ull * 1024 * 1024);
    ~ObjectStore();

    Digest store(const Chunk &chunk);
    Chunk load(const Digest &digest) const;

    bool contains(const Digest &digest) const;
    void save_index() const;

    const Index &index() const { return index_; }

    uint64_t compact(const std::unordered_set<Digest, DigestHash> &live_objects, 
					const std::vector<uint32_t> &packs_to_compact);

private:

    std::filesystem::path store_directory_;
    std::filesystem::path packs_directory_;
	
	/* exclusive lock to hold store directory. only one process can access store/ to prevent race conditions or corrupt data from multiple 
		instances.
	*/
	int lock_fd_  = -1;

    Index index_;

    mutable std::mutex mutex_;

    uint64_t max_pack_size_;
    uint32_t current_pack_id_;
    mutable std::ofstream current_pack_;
    uint64_t current_pack_offset_;

    std::filesystem::path pack_path(uint32_t pack_id) const;
    void open_pack_for_append(uint32_t pack_id);
    void rotate_pack_if_needed(uint64_t incoming_size);
    uint32_t find_latest_packId() const;
};