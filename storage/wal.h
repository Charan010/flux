#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>
 
#include "hashing/digest.h"
#include "object_store.h"


enum class WalOp: uint8_t {
	Insert = 1,
	Erase = 2,
};

struct WalRecord{

	WalOp op;
	uint64_t lsn;
	Digest digest;
	ObjectLocation location;
};


/*
WAL record — 66 bytes, fixed size

┌────┬─────┬────────┬──────────────────────────┬───────┐
│ op │ lsn │ digest │      ObjectLocation      │ crc32 │
│  1 │  8  │   32   │            21            │   4   │
└────┴─────┴────────┴──────────────────────────┴───────┘

*/

class Wal{

public:
	static constexpr size_t RecordSize = 66; // each operation is of 66 bytes in WAL.

	explicit Wal(const std::filesystem::path &path);
	~Wal();

	Wal(const Wal &) = delete;
	Wal &operator=(const Wal &) = delete;

	void stage(WalOp op, const Digest &digest, const ObjectLocation &location, uint64_t len);

	void commit();
	void reset();

	/* Returns number of bytes written to disk to calculate when to fold the wal into a checkpoint. */
	uint64_t durable_bytes()const{
		 return durable_bytes_; 
	}
	
	/* Returns number of wal entries in RAM and need to be batch flushed to disk. */
	size_t staged_count()const{
		 return staged_.size(); 
	}

	/*Returns list of records by replaying the index.bin and checks for any corruption using CRC32 and torn tail. */
	static std::vector<WalRecord> replay(const std::filesystem::path &path, uint64_t min_lsn);
 
private:
 
	std::filesystem::path   path_;
	int fd_ = -1;
	uint64_t durable_bytes_ = 0;

	/* Contains list of WalRecord which are not yet written to disk. */
	std::vector<WalRecord> staged_;
 
	void open_for_append();

};





