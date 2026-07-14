#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

struct ObjectLocation{
	uint32_t pack_id;
	uint64_t offset;
	uint32_t size;
	uint32_t ref_count;
};


class Index{

public:
	explicit Index(const std::filesystem::path &manifest_path = "manifest.bin");
	bool contains(const std::string &digest) const;

	/* Returns ObjectLocation struct if the the hash is present in the index else null*/
	const ObjectLocation* find(const std::string &digest) const;
	void insert(const std::string &digest, const ObjectLocation &object_location);

	void incrementRef(const std::string& digest);
    void decrementRef(const std::string& digest);

    void erase(const std::string& digest);
    void load();
    void save() const;

private:

	std::filesystem::path manifest_path_;
	std::unordered_map<std::string, ObjectLocation> table_;
};
