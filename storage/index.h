#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "codecs/codec.h"

struct ObjectLocation {
    uint32_t pack_id;
    uint64_t offset;
    uint32_t compressed_size;   
    uint32_t original_size;     
    CodecId  codec;             
};

class Index {

public:

    explicit Index(const std::filesystem::path &manifest_path = "manifest.bin");

    bool contains(const std::string &digest) const;

    const ObjectLocation *find(const std::string &digest) const;
    void insert(const std::string &digest, const ObjectLocation &object_location);

    void erase(const std::string &digest);
    void load();
    void save() const;

    auto begin()const{
		 return table_.begin(); 
	}

    auto end()const{
		return table_.end();   
	}

private:

    std::filesystem::path manifest_path_;
    std::unordered_map<std::string, ObjectLocation> table_;
};