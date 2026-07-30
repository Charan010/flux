#include "index.h"

#include <fstream>
#include <stdexcept>

Index::Index(const std::filesystem::path &manifest_path): manifest_path_(manifest_path) {
	load();
}


/* returns bool if the hash exists in the index which is source of truth for metadata of hash*/
bool Index::contains(const Digest &digest) const{
	return table_.find(digest) != table_.end();
}


/* if the hash exists in the index then returns the ObjectLocation struct containing metadata else, nullptr*/
const ObjectLocation *Index::find(const Digest &digest) const {

	auto it = table_.find(digest);
	return it == table_.end() ? nullptr : &it -> second;
}


void Index::insert(const Digest &digest, const ObjectLocation &location){
	table_[digest] = location;
}

void Index::erase(const Digest &digest){
	table_.erase(digest);
}



/* save persists to disk all the data of index_ so that data could be restored even if it crashes or after shutdown

	TO-DO: currently everytime to persist the disk, the older file is truncated first and then the data is written. which is pretty expensive when
	table_ has millions of entries.

	the better way is to have a fast append only file and after the file hits some triggering point. The file should be compacted in a tmp file
	and after compaction is done, the real index.bin should be atomically swapped.

*/
void Index::save() const{

	std::ofstream out(manifest_path_, std::ios::binary);

	if(!out)
		throw std::runtime_error("failed to save manifest");

	uint64_t count = table_.size();
	out.write(reinterpret_cast<const char *>(&count), sizeof(count));


	for(const auto &[digest, loc] : table_){

		out.write(reinterpret_cast<const char *>(digest.data()), static_cast<std::streamsize>(digest.size()));
        out.write(reinterpret_cast<const char *>(&loc.pack_id), sizeof(loc.pack_id));
        out.write(reinterpret_cast<const char *>(&loc.offset), sizeof(loc.offset));
        out.write(reinterpret_cast<const char *>(&loc.compressed_size), sizeof(loc.compressed_size));
        out.write(reinterpret_cast<const char *>(&loc.original_size), sizeof(loc.original_size));
        out.write(reinterpret_cast<const char *>(&loc.codec), sizeof(loc.codec));
    }

	  if (!out)
        throw std::runtime_error("Failed while writing manifest.");

}


/* restoring of data by reading from index.bin and reproducing the data in table_ . load() is implicitly called when
	index objected is created by the constructer.
*/
void Index::load() {
 
    table_.clear();
    if (!std::filesystem::exists(manifest_path_)) return;
 
    std::ifstream in(manifest_path_, std::ios::binary);
    if (!in) throw std::runtime_error("Failed to load manifest.");
 
    uint64_t count = 0;

    if (!in.read(reinterpret_cast<char *>(&count), sizeof(count)))
        throw std::runtime_error("manifest: truncated header");
 
    constexpr uint64_t kRecordSize = 32 + sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(CodecId);
 
    const uint64_t body = std::filesystem::file_size(manifest_path_) - sizeof(count);


    if (count > body / kRecordSize)
        throw std::runtime_error("manifest: record count exceeds file size");
 
    table_.reserve(count);
 
    for (uint64_t i = 0; i < count; ++i) {
 
        Digest digest{};
        in.read(reinterpret_cast<char *>(digest.data()),static_cast<std::streamsize>(digest.size()));
 
        ObjectLocation loc{};
        in.read(reinterpret_cast<char *>(&loc.pack_id), sizeof(loc.pack_id));
        in.read(reinterpret_cast<char *>(&loc.offset), sizeof(loc.offset));
        in.read(reinterpret_cast<char *>(&loc.compressed_size), sizeof(loc.compressed_size));
        in.read(reinterpret_cast<char *>(&loc.original_size), sizeof(loc.original_size));
        in.read(reinterpret_cast<char *>(&loc.codec), sizeof(loc.codec));
 
        if (!in)
            throw std::runtime_error("manifest: truncated record");
 
        table_[digest] = loc;
    }
}

