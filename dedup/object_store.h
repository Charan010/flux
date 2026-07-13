#pragma once

#include <filesystem>
#include <string>

#include "chunker.h"
#include "hasher.h"

class ObjectStore{

public:
	explicit ObjectStore(const std::string & object_directory = "objects");
	std::string store(const Chunk &chunk);

	Chunk load(const std::string &digest) const;

private:

	std::filesystem::path object_directory_;
	Hasher hasher_;
	
};
