#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
 
#include "../chunker.h"
#include "../hasher.h"
#include "index.h"

class ObjectStore{

public:
	explicit ObjectStore(const std::string &store_directory = "store", 
		uint64_t max_pack_size = 256ull * 1024 * 1024);
	
	~ObjectStore();

	std::string store(const Chunk &chunk);

	Chunk load(const std::string &digest) const;
	void save_index() const;

private:

	std::filesystem::path store_directory_;
	std::filesystem::path packs_directory_;
 
	Hasher hasher_;
	Index index_;
 
	uint64_t max_pack_size_;
	uint32_t current_pack_id_;
	mutable std::ofstream current_pack_;
	uint64_t current_pack_offset_;
 
	std::filesystem::path pack_path(uint32_t pack_id) const;
	void open_pack_for_append(uint32_t pack_id);
	void rotate_pack_if_needed(uint64_t incoming_size);
	uint32_t find_latest_packId() const;

};