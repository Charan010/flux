#include "index.h"

#include <cstring>
#include <fstream>
#include <stdexcept>
 
#include <blake3.h>
 
#include "io/durability.h"
 
namespace fs = std::filesystem;

namespace{

	constexpr char kMagic[4] = {'R', 'L', 'C', '1'};
	constexpr uint32_t kVersion = 1;

	constexpr size_t kHeaderSize = 4 + 4;
	constexpr size_t kRecordSize = 1 + 8 + 32 + 21 + 4; //66 fixed

	constexpr uint64_t kMinCheckpointWal = 64ull * 1024 * 1024;


	template<typename T>
	T get(const uint8_t *&p){
		uint64_t v = 0;
		for (size_t i = 0; i < sizeof(T); ++i)
			v |= static_cast<uint64_t>(*p++) << (8 * i);

		return static_cast<T>(v);
	}

	template <typename T>
	void put(std::vector<uint8_t> &v, T value) {
		for (size_t i = 0; i < sizeof(T); ++i)
		v.push_back(static_cast<uint8_t>((static_cast<uint64_t>(value) >> (8 * i)) & 0xFF));
	}


	Digest blake3_of(const uint8_t *data, size_t len) {
		blake3_hasher h;
		blake3_hasher_init(&h);
		blake3_hasher_update(&h, data, len);
		Digest d{};
		blake3_hasher_finalize(&h, d.data(), d.size());
		return d;
	}

}

Index::Index(const fs::path &directory)
	: directory_(directory), checkpoint_path_(directory / "index.ckpt"),
	  wal_path_(directory / "index.wal"){
 
	fs::create_directories(directory_);
	recover();
}

Index::~Index(){

	try{ commit(); }
	catch(...) {}
}


void Index::recover(){

	checkpoint_lsn_ = load_checkpoint();
	next_lsn_ = checkpoint_lsn_ + 1;


	for(const WalRecord &r : Wal::replay(wal_path_, checkpoint_lsn_)){

		if(r.op == WalOp::Insert)
			table_[r.digest] = r.location;

		else{
			table_.erase(r.digest);
		}

		if(r.lsn >= next_lsn_)
			next_lsn_ = r.lsn + 1;
	}

	wal_ = std::make_unique<Wal>(wal_path_);
}


/* checks whether index.ckpt exists and verifies magic bytes, version number and header bytes.
	and repopulates data into table_ which keeps track of hash -> metadata about actual data in file system.
*/
uint64_t Index::load_checkpoint(){

	table_.clear();
	checkpoint_bytes_ = 0;

	std::error_code ec;
	if(!fs::exists(checkpoint_path_, ec))
		return 0;

	const uint64_t size = fs::file_size(checkpoint_path_, ec);

	if(ec || size < kHeaderSize)
		return 0;

	std::ifstream in(checkpoint_path_, std::ios::binary);

	if(!in)
		throw std::runtime_error("index: unable to open checkpoint");

	std::vector<uint8_t> buf(size);

	in.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(size));
	if (!in)
		throw std::runtime_error("index: short read on checkpoint");
		
 
	if (std::memcmp(buf.data(), kMagic, sizeof(kMagic)) != 0)
		throw std::runtime_error("index: checkpoint magic mismatch");
 
	const uint8_t *p = buf.data() + sizeof(kMagic);
 
	const uint32_t version = get<uint32_t>(p);
	if (version != kVersion)
		throw std::runtime_error("index: unsupported checkpoint version");

 
	const uint64_t count = get<uint64_t>(p);
	const uint64_t lsn   = get<uint64_t>(p);
 
	Digest stored{};
	std::memcpy(stored.data(), p, stored.size());
	p += stored.size();
 
	const uint64_t body = size - kHeaderSize;
 
	if (count != body / kRecordSize)
		throw std::runtime_error("index: checkpoint record count disagrees with file size");
 
	if (blake3_of(buf.data() + kHeaderSize, body) != stored)
		throw std::runtime_error("index: checkpoint body failed integrity check");
 
	table_.reserve(count);
 
	for (uint64_t i = 0; i < count; ++i) {
 
		Digest d{};
		std::memcpy(d.data(), p, d.size());
		p += d.size();
 
		ObjectLocation loc{};
		loc.pack_id         = get<uint32_t>(p);
		loc.offset          = get<uint64_t>(p);
		loc.compressed_size = get<uint32_t>(p);
		loc.original_size   = get<uint32_t>(p);
		loc.codec           = static_cast<CodecId>(get<uint8_t>(p));
 
		table_[d] = loc;
	}
 
	checkpoint_bytes_ = size;
	return lsn;
}


bool Index::contains(const Digest &d)const{
	return table_.find(d) != table_.end();
}


/* Returns ObjectLocation which contains metadata if hash exists else nullptr */
const ObjectLocation *Index::find(const Digest &digest) const {
	auto it = table_.find(digest);
	return it == table_.end() ? nullptr : &it->second;
}


void Index::insert(const Digest &digest, const ObjectLocation &location){
	table_[digest] = location;
	wal_ -> stage(WalOp::Insert, digest, location, next_lsn_++);
}

void Index::erase(const Digest &digest){

	table_.erase(digest);
	wal_ -> stage(WalOp::Erase, digest, ObjectLocation{}, next_lsn_++);
}

void Index::commit(){
	if(wal_)
		wal_ -> commit();
}

uint64_t Index::checkpoint_threshold()const{
	return std::max(kMinCheckpointWal, checkpoint_bytes_ / 4);
}

void Index::maybe_checkpoint() {
 
	commit();
 
	if (wal_->durable_bytes() >= checkpoint_threshold())
		checkpoint();
}
 
void Index::checkpoint() {
 
	commit();
 
	const uint64_t lsn = next_lsn_ - 1;
 
	std::vector<uint8_t> body;
	body.reserve(table_.size() * kRecordSize);
 
	for (const auto &[digest, loc] : table_) {
		body.insert(body.end(), digest.begin(), digest.end());
		put<uint32_t>(body, loc.pack_id);
		put<uint64_t>(body, loc.offset);
		put<uint32_t>(body, loc.compressed_size);
		put<uint32_t>(body, loc.original_size);
		put<uint8_t>(body, static_cast<uint8_t>(loc.codec));
	}
 
	std::vector<uint8_t> header;
	header.reserve(kHeaderSize);
	header.insert(header.end(), kMagic, kMagic + sizeof(kMagic));
	put<uint32_t>(header, kVersion);
	put<uint64_t>(header, static_cast<uint64_t>(table_.size()));
	put<uint64_t>(header, lsn);
 
	const Digest checksum = blake3_of(body.data(), body.size());
	header.insert(header.end(), checksum.begin(), checksum.end());
 
	const fs::path tmp = checkpoint_path_.string() + ".tmp";
 
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out)
			throw std::runtime_error("index: cannot open checkpoint tmp");
 
		out.write(reinterpret_cast<const char *>(header.data()), static_cast<std::streamsize>(header.size()));
		out.write(reinterpret_cast<const char *>(body.data()),   static_cast<std::streamsize>(body.size()));
		out.flush();
 
		if (!out)
			throw std::runtime_error("index: failed while writing checkpoint");
	}
 
	atomic_replace(tmp, checkpoint_path_);
 
	checkpoint_lsn_   = lsn;
	checkpoint_bytes_ = header.size() + body.size();
 
	wal_->reset();
}





