#include "object_store.h"

#include <cstdio>
#include <stdexcept>

#include "../../codecs/lz4/lz4_codec.h"

ObjectStore::ObjectStore(const std::string &store_directory, uint64_t max_pack_size)
    : store_directory_(store_directory), packs_directory_(store_directory_ / "packs"),
      hasher_(), index_(store_directory_ / "index.bin"), max_pack_size_(max_pack_size),
      current_pack_id_(0), current_pack_offset_(0) {

    std::filesystem::create_directories(packs_directory_);

    current_pack_id_ = find_latest_packId();
    open_pack_for_append(current_pack_id_);
}

ObjectStore::~ObjectStore() {
    if (current_pack_.is_open())
        current_pack_.flush();

    save_index();
}

std::filesystem::path ObjectStore::pack_path(uint32_t pack_id) const {
    char name[32];
    std::snprintf(name, sizeof(name), "pack_%06u.pack", pack_id);
    return packs_directory_ / name;
}

uint32_t ObjectStore::find_latest_packId() const {

    uint32_t latest = 0;
    bool found = false;

    for (const auto &entry : std::filesystem::directory_iterator(packs_directory_)) {
        const std::string name = entry.path().filename().string();

        if (name.rfind("pack_", 0) != 0)
            continue;

        uint32_t id = static_cast<uint32_t>(std::stoul(name.substr(5, 6)));

        if (!found || id > latest) {
            latest = id;
            found = true;
        }
    }

    return found ? latest : 0;
}

void ObjectStore::open_pack_for_append(uint32_t pack_id) {

    if (current_pack_.is_open())
        current_pack_.close();

    std::filesystem::path path = pack_path(pack_id);
    current_pack_.open(path, std::ios::binary | std::ios::app);

    if (!current_pack_)
        throw std::runtime_error("Failed to open pack file: " + path.string());

    current_pack_offset_ = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
    current_pack_id_ = pack_id;
}

void ObjectStore::rotate_pack_if_needed(uint64_t incoming_size) {
    if (current_pack_offset_ + incoming_size <= max_pack_size_)
        return;

    open_pack_for_append(current_pack_id_ + 1);
}

bool ObjectStore::contains(const std::string &digest) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return index_.contains(digest);
}

std::string ObjectStore::store(const Chunk &chunk) {

    std::string digest = hasher_.hash(chunk);

    std::lock_guard<std::mutex> lock(mutex_);

    if (index_.find(digest) != nullptr) {
        index_.incrementRef(digest);
        return digest;
    }

    std::vector<uint8_t> compressed(LZ4Codec::compress_bound(chunk.bytes.size()));
    size_t compressed_size = LZ4Codec::compress(chunk.bytes.data(), chunk.bytes.size(), compressed.data());
    compressed.resize(compressed_size);

    rotate_pack_if_needed(compressed.size());
    uint64_t offset = current_pack_offset_;

    current_pack_.write(reinterpret_cast<const char *>(compressed.data()), compressed.size());
    if (!current_pack_)
        throw std::runtime_error("Failed to write chunk to pack file.");

    current_pack_.flush();
    current_pack_offset_ += compressed.size();

    ObjectLocation location{};
    location.pack_id = current_pack_id_;
    location.offset = offset;
    location.compressed_size = static_cast<uint32_t>(compressed.size());
    location.original_size = static_cast<uint32_t>(chunk.bytes.size());
    location.ref_count = 1;

    index_.insert(digest, location);

    return digest;
}

Chunk ObjectStore::load(const std::string &digest) const {

    ObjectLocation location;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const ObjectLocation *found = index_.find(digest);
        if (!found)
            throw std::runtime_error("Object not found: " + digest);
        location = *found;
    }

    std::ifstream in(pack_path(location.pack_id), std::ios::binary);
    if (!in)
        throw std::runtime_error("Failed to open pack file for digest: " + digest);

    in.seekg(static_cast<std::streamoff>(location.offset));

    std::vector<uint8_t> compressed(location.compressed_size);
    in.read(reinterpret_cast<char *>(compressed.data()), location.compressed_size);
    if (!in)
        throw std::runtime_error("Failed to read object from pack: " + digest);

    Chunk chunk;
    chunk.bytes.resize(location.original_size);

    size_t decoded = LZ4Codec::decompress(compressed.data(), compressed.size(), chunk.bytes.data());
    if (decoded != location.original_size)
        throw std::runtime_error("LZ4 decode size mismatch for digest: " + digest);

    return chunk;
}

void ObjectStore::save_index() const {
    std::lock_guard<std::mutex> lock(mutex_);
    index_.save();
}
