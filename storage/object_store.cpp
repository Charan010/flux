#include "object_store.h"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
 
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
 
 
#include "codecs/codec.h"
#include "io/durability.h"
#include "pack_format.h"

#include <chrono>
#include "hashing/digest.h"


ObjectStore::ObjectStore(const std::string &store_directory, uint64_t max_pack_size)
    : store_directory_(store_directory), packs_directory_(store_directory_ / "packs"), index_(store_directory_),
	  max_pack_size_(max_pack_size), current_pack_id_(0), current_pack_offset_(0) {
 

	std::filesystem::create_directories(packs_directory_);
	const std::filesystem::path lock_path = store_directory_ / ".lock";
 
	lock_fd_ = open(lock_path.c_str(), O_CREAT | O_RDWR, 0644);

	if(lock_fd_ < 0) throw std::runtime_error("cannot open store lock: " + lock_path.string());
 

	if(flock(lock_fd_, LOCK_EX | LOCK_NB ) != 0){
		close(lock_fd_);
		lock_fd_ = -1;
		throw std::runtime_error("store is already in use by another relic instance.");
	}
 
	current_pack_id_ = find_latest_packId();
	open_pack_for_append(current_pack_id_);
}


ObjectStore::~ObjectStore() {
    try {
        sync();
    } catch (...) {
        /* destructors must not throw; staged records are lost, which only
           costs us re-storing those chunks on the next run */
    }
 
	if(lock_fd_ >= 0)
		close(lock_fd_);	
}


std::filesystem::path ObjectStore::pack_path(uint32_t pack_id) const {
    char name[32];
    std::snprintf(name, sizeof(name), "pack_%06u.pack", pack_id);
    return packs_directory_ / name;
}


/* iterates through all store/packs for latest pack-id to keep track of packfiles id and returns 0 if there are none.*/
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

   
    if (current_pack_offset_ == 0) {

        PackHeader header;
        header.pack_id    = pack_id;
        header.created_at = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();

        uint8_t buf[PackHeader::kSize];
        header.encode(buf);

        current_pack_.write(reinterpret_cast<const char *>(buf), PackHeader::kSize);
        if (!current_pack_)
            throw std::runtime_error("failed to write pack header: " + path.string());

        current_pack_offset_ = PackHeader::kSize;
    }
}
 

/* Opens a new packfile if the amount of incoming size is greater than remaining size of current packfile. */
void ObjectStore::rotate_pack_if_needed(uint64_t incoming_size){
 
    if (current_pack_offset_ + incoming_size <= max_pack_size_)
        return;
 
    open_pack_for_append(current_pack_id_ + 1);
}


bool ObjectStore::contains(const Digest &digest) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return index_.contains(digest);
}



/*
	To reduce lock contention, chunk is hashed and compressed using zstd beforehand.
	and acquires the mutex afterwards, if the chunk is already present, then the compression is wasted and bytes
	are dumped. else, the digest is stored in the index and compressed bytes is written to the packfile. This
	reduces the lock acquired time for each thread for the tradeoff by extra/wasteful compression on chunk.

	TO-DO: Could instead use std::shared_mutex which allows multiple readers but when a writer has acquired the lock,
	readers cant acquire the mutex.

*/

Digest ObjectStore::store(const Chunk &chunk) {
 
    const Digest digest = blake3_hash(chunk);
 
    std::vector<uint8_t> compressed;
    const CodecId codec = codec_compress(chunk.bytes.data(), chunk.bytes.size(), compressed);
 
    std::lock_guard<std::mutex> lock(mutex_);
 
    if (index_.find(digest) != nullptr)
        return digest;
 
    ObjectHeader header;
    header.codec         = codec;
    header.original_size = static_cast<uint32_t>(chunk.bytes.size());
    header.stored_size   = static_cast<uint32_t>(compressed.size());
    header.digest        = digest;
    header.payload_crc   = pack_crc32(compressed.data(), compressed.size());

    uint8_t header_bytes[ObjectHeader::kSize];
    header.encode(header_bytes);

    rotate_pack_if_needed(ObjectHeader::kSize + compressed.size());
    const uint64_t offset = current_pack_offset_;

    current_pack_.write(reinterpret_cast<const char *>(header_bytes), ObjectHeader::kSize);
    current_pack_.write(reinterpret_cast<const char *>(compressed.data()),
                        static_cast<std::streamsize>(compressed.size()));

    if (!current_pack_)
        throw std::runtime_error("Failed to write chunk to pack file.");

    current_pack_offset_ += ObjectHeader::kSize + compressed.size();

    ObjectLocation location{};
    location.pack_id = current_pack_id_;
    location.offset  = offset;

    index_.insert(digest, location);
    return digest;
 
}



ObjectStore::RebuildReport ObjectStore::rebuild_index_from_packs(bool verify_payloads) {

    std::lock_guard<std::mutex> lock(mutex_);

    RebuildReport report;
    index_.reset_for_rebuild();

    std::vector<std::filesystem::path> packs;
    for (const auto &entry : std::filesystem::directory_iterator(packs_directory_))
        if (entry.path().filename().string().rfind("pack_", 0) == 0)
            packs.push_back(entry.path());

    /* Ascending pack id, so a later copy of a digest overwrites an earlier one. */
    std::sort(packs.begin(), packs.end());

    for (const auto &path : packs) {

        const std::string name = path.filename().string();

        uint32_t pack_id = 0;
        try {
            pack_id = static_cast<uint32_t>(std::stoul(name.substr(5, 6)));
        } catch (...) {
            continue;
        }

        const PackScanResult result = scan_pack(path,
            [&](const ObjectHeader &header, uint64_t offset) {
                ObjectLocation location{};
                location.pack_id = pack_id;
                location.offset  = offset;
                index_.insert(header.digest, location);
                ++report.records;
            },
            verify_payloads);

    
        if (result.pack_id != pack_id)
            throw std::runtime_error("pack " + name + " declares id "
                                     + std::to_string(result.pack_id));

        ++report.packs_scanned;

        if (result.truncated) {
            ++report.packs_truncated;
            report.trailing_bytes += std::filesystem::file_size(path) - result.good_bytes;
        }
    }

    index_.checkpoint();
    return report;
}


/* Size of a whole record (header + payload) at a location, read from the pack. */
uint64_t ObjectStore::record_size_at(const ObjectLocation &location) const {

    std::ifstream in(pack_path(location.pack_id), std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open pack " + std::to_string(location.pack_id));

    in.seekg(static_cast<std::streamoff>(location.offset));

    uint8_t header_bytes[ObjectHeader::kSize];
    in.read(reinterpret_cast<char *>(header_bytes), ObjectHeader::kSize);
    if (!in)
        throw std::runtime_error("short read on object header during compaction");

    const ObjectHeader oh = ObjectHeader::decode(header_bytes, "compaction");
    return ObjectHeader::kSize + oh.stored_size;
}


/* returns the chunk by using metadata of the digest and loading the compressed bytes from the packfiles disk.*/
Chunk ObjectStore::load(const Digest &digest) const {
 
    ObjectLocation location;
 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const ObjectLocation *found = index_.find(digest);
        if (!found)
            throw std::runtime_error("Object not found: " + to_hex(digest));
        location = *found;
 
        if (location.pack_id == current_pack_id_ && current_pack_.is_open())
            current_pack_.flush();
    }
 
 
    std::ifstream in(pack_path(location.pack_id), std::ios::binary);
 
    if (!in)
        throw std::runtime_error("Failed to open pack file for digest: " + to_hex(digest));
 
 
    in.seekg(static_cast<std::streamoff>(location.offset));

    uint8_t header_bytes[ObjectHeader::kSize];
    in.read(reinterpret_cast<char *>(header_bytes), ObjectHeader::kSize);
    if (!in)
        throw std::runtime_error("Failed to read object header: " + to_hex(digest));

    const ObjectHeader header = ObjectHeader::decode(header_bytes, to_hex(digest));

    /* packfiles are self contained. Index tells the metadata of the object. So, if there is any conflict
	 * and packfile is storing different metadata. Then reject it.
	*/
    if (header.digest != digest)
        throw std::runtime_error("pack holds a different object at this offset: " + to_hex(digest));

    std::vector<uint8_t> compressed(header.stored_size);
    in.read(reinterpret_cast<char *>(compressed.data()), header.stored_size);
    if (!in)
        throw std::runtime_error("Failed to read object from pack: " + to_hex(digest));

    if (pack_crc32(compressed.data(), compressed.size()) != header.payload_crc)
        throw std::runtime_error("pack payload checksum mismatch: " + to_hex(digest));

    Chunk chunk;
    chunk.bytes.resize(header.original_size);

    codec_decompress(header.codec, compressed.data(), compressed.size(),
                     chunk.bytes.data(), chunk.bytes.size());

    return chunk;
}


void ObjectStore::sync_current_pack() {
    if (!current_pack_.is_open())
        return;

    current_pack_.flush();
    if (!current_pack_)
        throw std::runtime_error("failed to flush pack file");

    fsync_file(pack_path(current_pack_id_));
}


void ObjectStore::checkpoint() {
    std::lock_guard<std::mutex> lock(mutex_);
    sync_current_pack();
    index_.checkpoint();
}

void ObjectStore::sync() {
    std::lock_guard<std::mutex> lock(mutex_);
    sync_current_pack();
    index_.maybe_checkpoint();
}

void ObjectStore::save_index() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const_cast<Index &>(index_).checkpoint();
}


/* garbage collector walks through all manifest files and collect information about how many chunks are being referenced and 
	keeps stats about how many unreferenced chunk bytes in each packfile > X% and returns vector of pack-id to be compacted.
*/
uint64_t ObjectStore::compact(const std::unordered_set<Digest, DigestHash> &live_objects, const std::vector<uint32_t> &packs_to_compact) {
	
    std::lock_guard<std::mutex> lock(mutex_);
 
    if (packs_to_compact.empty())
        return 0;
 
    const std::unordered_set<uint32_t> targets(packs_to_compact.begin(), packs_to_compact.end());
 
    std::vector<Digest> in_targets;
 
    for (const auto &[digest, loc] : index_){
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
            reclaimed += record_size_at(*loc);
            index_.erase(object);
            continue;
        }
 
        ObjectLocation moved = *loc;
        std::vector<uint8_t> buf;

        {
            std::ifstream in(pack_path(moved.pack_id), std::ios::binary);
            if (!in)
                throw std::runtime_error("compact: cannot open pack file " + std::to_string(moved.pack_id));

            in.seekg(static_cast<std::streamoff>(moved.offset));

            uint8_t header_bytes[ObjectHeader::kSize];
            in.read(reinterpret_cast<char *>(header_bytes), ObjectHeader::kSize);
            if (!in)
                throw std::runtime_error("compact: short read on object header");

            const ObjectHeader oh = ObjectHeader::decode(header_bytes, to_hex(object));

            buf.resize(ObjectHeader::kSize + oh.stored_size);
            std::memcpy(buf.data(), header_bytes, ObjectHeader::kSize);

            in.read(reinterpret_cast<char *>(buf.data() + ObjectHeader::kSize), oh.stored_size);
            if (!in)
                throw std::runtime_error("compact: short read from pack file "+ std::to_string(moved.pack_id));
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
 
    sync_current_pack();
    index_.checkpoint();

    for (uint32_t id : targets)
        std::filesystem::remove(pack_path(id));

    std::filesystem::path packs_dir = packs_directory_;
    fsync_dir(packs_dir);

    return reclaimed;
}