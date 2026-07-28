#include "index.h"

#include <fstream>
#include <stdexcept>

Index::Index(const std::filesystem::path &manifest_path) : manifest_path_(manifest_path) {
    load();
}	

bool Index::contains(const std::string &digest) const {
    return table_.find(digest) != table_.end();
}

const ObjectLocation *Index::find(const std::string &digest) const {
    auto it = table_.find(digest);
    return it == table_.end() ? nullptr : &it->second;
}

void Index::insert(const std::string &digest, const ObjectLocation &location) {
    table_[digest] = location;
}	

void Index::erase(const std::string &digest) {
    table_.erase(digest);
}

void Index::save() const {

    std::ofstream out(manifest_path_, std::ios::binary);
	
    if (!out)
        throw std::runtime_error("Failed to save manifest.");

    uint64_t count = table_.size();
    out.write(reinterpret_cast<const char *>(&count), sizeof(count));

    for (const auto &[digest, loc] : table_) {
        uint32_t len = static_cast<uint32_t>(digest.size());

        out.write(reinterpret_cast<const char *>(&len), sizeof(len));
        out.write(digest.data(), len);

        out.write(reinterpret_cast<const char *>(&loc.pack_id), sizeof(loc.pack_id));
        out.write(reinterpret_cast<const char *>(&loc.offset), sizeof(loc.offset));
        out.write(reinterpret_cast<const char *>(&loc.compressed_size), sizeof(loc.compressed_size));
        out.write(reinterpret_cast<const char *>(&loc.original_size), sizeof(loc.original_size));
        out.write(reinterpret_cast<const char *>(&loc.codec), sizeof(loc.codec));
    }
}

void Index::load() {

    table_.clear();
    if (!std::filesystem::exists(manifest_path_))
        return;

    std::ifstream in(manifest_path_, std::ios::binary);
    if (!in)
        throw std::runtime_error("Failed to load manifest.");

    uint64_t count = 0;
    in.read(reinterpret_cast<char *>(&count), sizeof(count));

    for (uint64_t i = 0; i < count; ++i) {
        uint32_t len = 0;
        in.read(reinterpret_cast<char *>(&len), sizeof(len));

        std::string digest(len, '\0');
        in.read(digest.data(), len);

        ObjectLocation loc{};
        in.read(reinterpret_cast<char *>(&loc.pack_id), sizeof(loc.pack_id));
        in.read(reinterpret_cast<char *>(&loc.offset), sizeof(loc.offset));
        in.read(reinterpret_cast<char *>(&loc.compressed_size), sizeof(loc.compressed_size));
        in.read(reinterpret_cast<char *>(&loc.original_size), sizeof(loc.original_size));
        in.read(reinterpret_cast<char *>(&loc.codec), sizeof(loc.codec));

        table_[digest] = loc;
    }
}