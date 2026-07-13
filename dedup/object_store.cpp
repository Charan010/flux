#include "object_store.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

ObjectStore::ObjectStore(const std::string & object_directory):
	object_directory_(object_directory){

	std::filesystem::create_directories(object_directory_);
}

std::string ObjectStore::store(const Chunk& chunk){\


    std::string digest = hasher_.hash(chunk);
    std::filesystem::path object_path = object_directory_ / digest;

	if(std::filesystem::exists(object_path))
		return digest;
	
    std::ofstream out(object_path, std::ios::binary);
    if (!out)
        throw std::runtime_error("Failed to create object file.");

    out.write(reinterpret_cast<const char*>(chunk.bytes.data()), chunk.bytes.size());
    return digest;
}


Chunk ObjectStore::load(const std::string& digest) const{

    std::filesystem::path objectPath = object_directory_ / digest;
    std::ifstream in(objectPath, std::ios::binary);

    if (!in)
        throw std::runtime_error("Object not found: " + digest);

    Chunk chunk;
    chunk.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return chunk;
	
}