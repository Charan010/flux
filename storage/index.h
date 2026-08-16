#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include "hashing/digest.h"
#include "object_location.h"
#include "wal.h"

class Index {

public:

	explicit Index(const std::filesystem::path &directory);
	~Index();

	Index(const Index &) = delete;
	Index &operator=(const Index &) = delete;

	bool contains(const Digest &digest) const;
	const ObjectLocation *find(const Digest &digest) const;

	/* Both stage a WAL record; neither is durable until commit(). */
	void insert(const Digest &digest, const ObjectLocation &location);
	void erase(const Digest &digest);

	/* Makes every staged mutation durable. One fsync for the whole batch. */
	void commit();

	/* Full snapshot + log truncation. Safe to call at any time. */
	void checkpoint();

	/* commit(), then checkpoint() if the log has outgrown its threshold. */
	void maybe_checkpoint();

	size_t size() const { return table_.size(); }

	bool degraded() const { return degraded_; }
	const std::string &degraded_reason() const { return degraded_reason_; }

	void reset_for_rebuild();

	auto begin()const{
		 return table_.begin(); 
	}

	auto end()const{
		 return table_.end();   
	}

private:

	std::filesystem::path directory_;
	std::filesystem::path checkpoint_path_;
	std::filesystem::path wal_path_;

	std::unordered_map<Digest, ObjectLocation, DigestHash> table_;

	std::unique_ptr<Wal> wal_;

	bool degraded_ = false;
	std::string degraded_reason_;

	uint64_t next_lsn_         = 1;
	uint64_t checkpoint_lsn_   = 0;
	uint64_t checkpoint_bytes_ = 0;

	void recover();
	uint64_t load_checkpoint();
	uint64_t checkpoint_threshold() const;
};