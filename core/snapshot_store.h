#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "config.h"
#include "hashing/digest.h"
#include "storage/object_store.h"
#include "threadpool/threadpool.h"


class SnapshotStore {

public:
	explicit SnapshotStore(const std::string &store_directory = "store",
	                       size_t threads = Config::threadpool_size);

	void backup(const std::string &input_path, const std::string &snapshot_name);
	void restore(const std::string &snapshot_name, const std::string &output_path) const;
	void gc(bool dry_run = false);

	/* Force a full index checkpoint and truncate the log. */
	void checkpoint() { store_.checkpoint(); }

	/* Deletes a snapshot manifest. Space is not reclaimed until gc runs. */
	void remove_snapshot(const std::string &snapshot_name);

	struct FsckReport {
		ObjectStore::RebuildReport rebuild;
		uint64_t                   snapshots     = 0;
		uint64_t                   live_chunks   = 0;
		uint64_t                   dangling      = 0;
		std::vector<std::string>   broken_snapshots;
	};

	FsckReport fsck(bool rebuild, bool verify_payloads);

	bool               degraded() const { return store_.index().degraded(); }
	const std::string &degraded_reason() const { return store_.index().degraded_reason(); }

private:

	ObjectStore store_;
	std::filesystem::path manifests_dir_;
	Threadpool pool_;

	std::filesystem::path manifest_path(const std::string &snapshot_name) const;
	std::vector<Digest> store_file(const std::filesystem::path &file);
};