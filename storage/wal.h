#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "hashing/digest.h"
#include "object_location.h"


enum class WalOp : uint8_t {
	Insert = 1,
	Erase  = 2,
};

struct WalRecord {
	WalOp          op;
	uint64_t       lsn;
	Digest         digest;
	ObjectLocation location;
};

class Wal {

public:

	/* type(1) + lsn(8) + digest(32) + pack_id(4) + offset(8) + crc32(4) */
	static constexpr size_t kRecordSize = 57;

	explicit Wal(const std::filesystem::path &path);
	~Wal();

	Wal(const Wal &) = delete;
	Wal &operator=(const Wal &) = delete;

	/* Stages a record in memory. Nothing is durable until commit(). */
	void stage(WalOp op, const Digest &digest, const ObjectLocation &location, uint64_t lsn);

	/* All staged records are written to disk at a time as it uses fsync syscall which is I/O blocking and 
		expensive for every single record.
	*/
	void commit();

	/* truncates the WAL after a checkpoint has been created succesfully. */
	void reset();

	uint64_t durable_bytes() const { return durable_bytes_; }
	size_t   staged_count() const { return staged_.size(); }

	/* Iterates through WAL records and repopulates data in memory table and stops at incomplete or corrupt data.*/
	static std::vector<WalRecord> replay(const std::filesystem::path &path, uint64_t min_lsn);

private:

	std::filesystem::path   path_;
	int                     fd_ = -1;
	uint64_t                durable_bytes_ = 0;
	std::vector<WalRecord>  staged_;

	void open_for_append();
};