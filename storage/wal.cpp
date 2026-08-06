#include "wal.h"

#include <cstring>
#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

/* CRC-32 (IEEE 802.3), table built once. Enough to catch a torn or
   bit-rotted record; this is an integrity check, not a security boundary. */
uint32_t crc32(const uint8_t *data, size_t len) {

	static uint32_t table[256];
	static bool     ready = false;

	if (!ready) {
		for (uint32_t i = 0; i < 256; ++i) {
			uint32_t c = i;
			for (int k = 0; k < 8; ++k)
				c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			table[i] = c;
		}
		ready = true;
	}

	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; ++i)
		c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);

	return c ^ 0xFFFFFFFFu;
}

/* Little-endian field writers. Explicit byte order so a store written on one
   machine is readable on another. */
template <typename T>
void put(uint8_t *&p, T value) {
	for (size_t i = 0; i < sizeof(T); ++i)
		*p++ = static_cast<uint8_t>((static_cast<uint64_t>(value) >> (8 * i)) & 0xFF);
}

template <typename T>
T get(const uint8_t *&p) {
	uint64_t v = 0;
	for (size_t i = 0; i < sizeof(T); ++i)
		v |= static_cast<uint64_t>(*p++) << (8 * i);
	return static_cast<T>(v);
}

void encode(const WalRecord &r, uint8_t *out) {

	uint8_t *p = out;

	put<uint8_t>(p, static_cast<uint8_t>(r.op));
	put<uint64_t>(p, r.lsn);

	std::memcpy(p, r.digest.data(), r.digest.size());
	p += r.digest.size();

	put<uint32_t>(p, r.location.pack_id);
	put<uint64_t>(p, r.location.offset);
	put<uint32_t>(p, r.location.compressed_size);
	put<uint32_t>(p, r.location.original_size);
	put<uint8_t>(p, static_cast<uint8_t>(r.location.codec));

	put<uint32_t>(p, crc32(out, Wal::RecordSize - 4));
}

/* Returns false if the record fails its CRC or carries an unknown opcode. */
bool decode(const uint8_t *in, WalRecord &r){

	const uint32_t want = crc32(in, Wal::RecordSize - 4);

	const uint8_t *c = in + Wal::RecordSize - 4;
	if (get<uint32_t>(c) != want)
		return false;

	const uint8_t *p = in;

	const uint8_t op = get<uint8_t>(p);
	if (op != static_cast<uint8_t>(WalOp::Insert) && op != static_cast<uint8_t>(WalOp::Erase))
		return false;

	r.op  = static_cast<WalOp>(op);
	r.lsn = get<uint64_t>(p);

	std::memcpy(r.digest.data(), p, r.digest.size());
	p += r.digest.size();

	r.location.pack_id         = get<uint32_t>(p);
	r.location.offset          = get<uint64_t>(p);
	r.location.compressed_size = get<uint32_t>(p);
	r.location.original_size   = get<uint32_t>(p);
	r.location.codec           = static_cast<CodecId>(get<uint8_t>(p));

	return true;
}

}

Wal::Wal(const fs::path &path) : path_(path) {
	open_for_append();
}

Wal::~Wal() {
	if (fd_ >= 0)
		::close(fd_);
}

void Wal::open_for_append() {

	if (fd_ >= 0)
		::close(fd_);


	/* O_APPEND resolves the offset inside the write() system call. So, two writers can never land on the same 
	 * offset and can never interleave their data. */
	
	fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

	if (fd_ < 0)
		throw std::runtime_error("wal: cannot open " + path_.string());

	std::error_code ec;
	durable_bytes_ = fs::exists(path_, ec) ? fs::file_size(path_, ec) : 0;
	if (ec) durable_bytes_ = 0;

}


/* Stores the record in a buffer which is not yet durable and only persisted to disk when commit() is called. */
void Wal::stage(WalOp op, const Digest &digest, const ObjectLocation &location, uint64_t lsn) {
	staged_.push_back(WalRecord{op, lsn, digest, op == WalOp::Erase ? ObjectLocation{} : location});
}


/*

* Encodes every staged record into a buffer and issues a single write and then fsync the whole batch.

* fsync costs hundreds of milliseconds for every system call and syncing every chunk would put reduce the speed.
* So, records are fsynced group wise. The tradeoff is that if the program crashes while still holding the staged records, then
* the data is lost and not persisted to disk. 


*/
void Wal::commit() {

	if (staged_.empty())
		return;

	std::vector<uint8_t> buf(staged_.size() * RecordSize);

	for (size_t i = 0; i < staged_.size(); ++i)
		encode(staged_[i], buf.data() + i * RecordSize);

	size_t written = 0;

	while (written < buf.size()){

		const ssize_t n = ::write(fd_, buf.data() + written, buf.size() - written);
		if (n < 0)
			throw std::runtime_error("wal: write failed");

		written += static_cast<size_t>(n);
	}

	if (::fdatasync(fd_) != 0)
		throw std::runtime_error("wal: fdatasync failed");

	durable_bytes_ += buf.size();
	staged_.clear();
}

/* 
* clears the staged entries and truncates the index.bin file.

* This function is only invoked when a checkpoint is succesfully written and atomically swapped with actual checkpoint file and
* then Wal file is safe to be truncated as the checkpoint now stores the state of data at this time.


*/
void Wal::reset() {

	staged_.clear();

	if (::ftruncate(fd_, 0) != 0)
		throw std::runtime_error("wal: truncate failed");

	if (::fsync(fd_) != 0)
		throw std::runtime_error("wal: fsync after truncate failed");

	durable_bytes_ = 0;
}

/*Returns list of records by replaying the index.bin and checks for any corruption using CRC32 and torn tail. */
std::vector<WalRecord> Wal::replay(const fs::path &path, uint64_t min_lsn) {

	std::vector<WalRecord> out;

	std::error_code ec;
	if (!fs::exists(path, ec))
		return out;

	const uint64_t size = fs::file_size(path, ec);
	if (ec || size == 0)
		return out;
		

	const int fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0)
		throw std::runtime_error("wal: cannot open for replay " + path.string());

	std::vector<uint8_t> buf(size);
	size_t read_total = 0;

	while (read_total < size) {
		const ssize_t n = ::read(fd, buf.data() + read_total, size - read_total);
		if (n <= 0)
			break;
		read_total += static_cast<size_t>(n);
	}
	::close(fd);

	uint64_t good_bytes = 0;

	for (size_t off = 0; off + RecordSize <= read_total; off += RecordSize) {

		WalRecord r{};
		if (!decode(buf.data() + off, r))
			break;    /* torn or corrupt: everything after is unreachable */

		good_bytes = off + RecordSize;

		if (r.lsn > min_lsn)
			out.push_back(r);
	}

	/* Trim the unusable tail so the next replay starts from a clean file. */
	if (good_bytes < size)
		fs::resize_file(path, good_bytes, ec);

	return out;
}