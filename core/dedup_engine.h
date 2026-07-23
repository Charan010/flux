#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/config.h"
#include "threadpool/threadpool.h"
#include "storage/object_store.h"


class DedupEngine{

public:
	explicit DedupEngine(const std::string &store_directory = "store", size_t threads = Config::threadpool_size);

	void backup(const std::string &input_path, const std::string &manifest_name);
	void restore(const std::string &manifest_name, const std::string &output_path) const;

	void gc(bool dry_run = false);

	struct FileEntry{
		std::string path;
		uint64_t size = 0;
		uint32_t mode = 0;
		std::vector<std::string> chunks;
	};

private:

	ObjectStore store_;
	std::filesystem::path manifests_dir_;
	Threadpool pool_;

	std::filesystem::path resolve_manifest(const std::string &manifest_name) const;

	FileEntry backup_file(const std::filesystem::path &file_path, const std::string &relative_path);
	void write_file(const FileEntry &entry, const std::filesystem::path &dest) const;

};