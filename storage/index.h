#pragma once
 
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
 
#include "hashing/digest.h"
#include "object_store.h"
#include "wal.h"
#include "object_location.h"


class Index{

public:

	explicit Index(const std::filesystem::path &directory);
	~Index();

	Index(const Index&)= delete;
	Index &operator=(const Index&) = delete;

	bool contains(const Digest &digest) const;
	const ObjectLocation* find(const Digest &digest) const;

	void insert(const Digest &digest, const ObjectLocation &location);
	void erase(const Digest &digest);

	void commit();

	void checkpoint();

	void maybe_checkpoint();

	size_t size(){
		return table_.size();
	}

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

	uint64_t next_lsn_ = 1, checkpoint_lsn_ = 0, checkpoint_bytes_ = 0;

	void recover();

	uint64_t load_checkpoint();
	uint64_t checkpoint_threshold() const;

};
