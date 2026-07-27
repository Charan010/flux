#include "object_store.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>


#include "codecs/codec.h"

ObjectStore::ObjectStore(const std::string &store_directory, uint64_t max_pack_size)
    : store_directory_(store_directory), packs_directory_(store_directory_ / "packs"),
      index_(store_directory_ / "index.bin"), max_pack_size_(max_pack_size),
      current_pack_id_(0), current_pack_offset_(0) {

	std::filesystem::create_directories(packs_directory_);

	const std::filesystem::path lock_path = store_directory_ / ".lock";

	lock_fd_ = open(lock_path.c_str(), O_CREAT | O_RDWR, 0644);
	
	if(lock_fd_ < 0)
		throw std::runtime_error("cannot open store lock: " + lock_path.string());

	if(flock(lock_fd_, LOCK_EX | LOCK_NB ) != 0){

		close(lock_fd_);
		lock_fd_ = -1;
		throw std::runtime_error("store is already in use by another relic instance.");
	}

	current_pack_id_ = find_latest_packId();
	open_pack_for_append(current_pack_id_);
	
}

ObjectStore::~ObjectStore() {
    if (current_pack_.is_open())
        current_pack_.flush();

    save_index();

	if(lock_fd_ >= 0)
		close(lock_fd_);	
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

    // PHASE A - parallel, no lock held. Both are pure functions of the bytes.
    std::string digest = blake3_hash(chunk);

    std::vector<uint8_t> compressed;
    const CodecId codec = codec_compress(chunk.bytes.data(), chunk.bytes.size(), compressed);

    // PHASE B - locked. Check-and-insert must be atomic or two threads storing
    // identical content both write it. Compression above may be wasted on a
    // duplicate; that waste is parallel and cheap, the race is not.
    std::lock_guard<std::mutex> lock(mutex_);

    if (index_.find(digest) != nullptr)
        return digest;

    rotate_pack_if_needed(compressed.size());
    uint64_t offset = current_pack_offset_;

    current_pack_.write(reinterpret_cast<const char *>(compressed.data()),
                        static_cast<std::streamsize>(compressed.size()));
    if (!current_pack_)
        throw std::runtime_error("Failed to write chunk to pack file.");

    current_pack_offset_ += compressed.size();

    ObjectLocation location{};
    location.pack_id = current_pack_id_;
    location.offset = offset;
    location.compressed_size = static_cast<uint32_t>(compressed.size());
    location.original_size = static_cast<uint32_t>(chunk.bytes.size());
    location.codec = codec;

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

        if (location.pack_id == current_pack_id_ && current_pack_.is_open())
            current_pack_.flush();
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

    codec_decompress(location.codec, compressed.data(), compressed.size(),
                     chunk.bytes.data(), chunk.bytes.size());

    return chunk;
}

void ObjectStore::save_index() const {
    std::lock_guard<std::mutex> lock(mutex_);
    index_.save();
}

uint64_t ObjectStore::compact(const std::unordered_set<std::string> &live_objects,
                              const std::vector<uint32_t> &packs_to_compact) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (packs_to_compact.empty())
        return 0;

    const std::unordered_set<uint32_t> targets(packs_to_compact.begin(), packs_to_compact.end());

    std::vector<std::string> in_targets;
    for (const auto &[digest, loc] : index_) {
        if (targets.count(loc.pack_id))
            in_targets.push_back(digest);
    }

    open_pack_for_append(current_pack_id_ + 1);
    uint64_t reclaimed = 0;

    for (const auto &object : in_targets) {

        const ObjectLocation *loc = index_.find(object);
        if (!loc)
            continue;

        if (live_objects.find(object) == live_objects.end()) {
            reclaimed += loc->compressed_size;
            index_.erase(object);
            continue;
        }

        ObjectLocation moved = *loc;
        std::vector<uint8_t> buf(moved.compressed_size);

        {
            std::ifstream in(pack_path(moved.pack_id), std::ios::binary);
            if (!in)
                throw std::runtime_error("compact: cannot open pack file "
                                         + std::to_string(moved.pack_id));

            in.seekg(static_cast<std::streamoff>(moved.offset));
            in.read(reinterpret_cast<char *>(buf.data()), moved.compressed_size);

            if (!in)
                throw std::runtime_error("compact: short read from pack file "
                                         + std::to_string(moved.pack_id));
        }

        rotate_pack_if_needed(buf.size());
        moved.pack_id = current_pack_id_;
        moved.offset = current_pack_offset_;

        current_pack_.write(reinterpret_cast<const char *>(buf.data()),
                            static_cast<std::streamsize>(buf.size()));

        if (!current_pack_)
            throw std::runtime_error("compact: write to dest failed");

        current_pack_offset_ += buf.size();

        index_.insert(object, moved);
    }

    current_pack_.flush();

    for (uint32_t id : targets)
        std::filesystem::remove(pack_path(id));

    index_.save();

    return reclaimed;
}