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

private:

	ObjectStore store_;
	std::filesystem::path manifests_dir_;
	Threadpool pool_;

	std::filesystem::path manifest_path(const std::string &snapshot_name) const;
	std::vector<Digest> store_file(const std::filesystem::path &file);
};