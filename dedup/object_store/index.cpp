#include "index.h"

#include <fstream>
#include <stdexcept>

Index::Index(const std::filesystem::path &manifest_path): manifest_path_(manifest_path){
	load();
}

bool Index::contains(const std::string &digest) const{
	auto it = table_.find(digest);

	return (it == table_.end() ? false : true);
}

const ObjectLocation* Index::find(const std::string& digest) const {
    auto it = table_.find(digest);

    if (it == table_.end())
        return nullptr;

    return &it->second;
}

void Index::insert(const std::string& digest, const ObjectLocation& location){
    table_[digest] = location;
}

void Index::incrementRef(const std::string& digest) {
    auto it = table_.find(digest);

    if (it != table_.end())
        ++it->second.ref_count;
}

void Index::decrementRef(const std::string& digest) {
    auto it = table_.find(digest);

    if (it == table_.end())
        return;

    if (it->second.ref_count > 0)
        --it->second.ref_count;
}


/* All hashes and metadata ObjectLocation are persisted to disk to recover from crashes and restarting. */
void Index::save() const {
	
	std::ofstream out(manifest_path_, std::ios::binary);

    if (!out)
        throw std::runtime_error("Failed to save manifest.");

    uint64_t count = table_.size();

    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& [digest, loc] : table_) {

        uint32_t len = digest.size();

        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(digest.data(), len);

        out.write(reinterpret_cast<const char*>(&loc.pack_id), sizeof(loc.pack_id));
        out.write(reinterpret_cast<const char*>(&loc.offset), sizeof(loc.offset));
        out.write(reinterpret_cast<const char*>(&loc.size), sizeof(loc.size));
        out.write(reinterpret_cast<const char*>(&loc.ref_count), sizeof(loc.ref_count));
    }
}


/* loads the hash and metadata into unordered_map from disk to RAM when starting up or recovering from crash*/
void Index::load() {

    table_.clear();

    if (!std::filesystem::exists(manifest_path_))
        return;

    std::ifstream in(manifest_path_, std::ios::binary);

    if (!in)
        throw std::runtime_error("Failed to load manifest.");

    uint64_t count;

    in.read(reinterpret_cast<char*>(&count), sizeof(count));

    for (uint64_t i = 0; i < count; ++i) {

        uint32_t len;
        in.read(reinterpret_cast<char*>(&len), sizeof(len));

        std::string digest(len, '\0');
        in.read(digest.data(), len);

        ObjectLocation loc;

        in.read(reinterpret_cast<char*>(&loc.pack_id), sizeof(loc.pack_id));
        in.read(reinterpret_cast<char*>(&loc.offset), sizeof(loc.offset));
        in.read(reinterpret_cast<char*>(&loc.size), sizeof(loc.size));
        in.read(reinterpret_cast<char*>(&loc.ref_count), sizeof(loc.ref_count));

        table_[digest] = loc;
    }
}

