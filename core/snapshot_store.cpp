#include "snapshot_store.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <semaphore>
#include <stdexcept>
#include <utility>
 
#include <fcntl.h>
#include <unistd.h>
 
#include "chunking/chunker.h"
#include "gc/gc.h"
#include "io/durability.h"
#include "io/mmap_file.h"
#include "json.hpp"
 
using json = nlohmann::json;
namespace fs = std::filesystem;


namespace {

	std::vector<std::string> convert_to_readable_hex(const std::vector<Digest> &digests){

		std::vector<std::string> out;
		out.reserve(digests.size());

		for(const Digest &d : digests)
			out.push_back(to_hex(d));

		return out;
	}

	void validate_snapshot_name(const std::string &name){


		if(name.empty() || name == "." || name == "..")
			throw std::runtime_error("invalid snapshot name: " + name);

		if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        throw std::runtime_error("snapshot name must not contain a path separator: " + name);


	}

}

SnapshotStore::SnapshotStore(const std::string &store_directory, size_t threads):
	store_(store_directory), manifests_dir_(fs::path(store_directory) / "manifests"), pool_(threads){

	fs::create_directories(manifests_dir_);
}


fs::path SnapshotStore::manifest_path(const std::string &snapshot_name) const{

	validate_snapshot_name(snapshot_name);
	return manifests_dir_ / snapshot_name;
}


std::vector<Digest> SnapshotStore::store_file(const fs::path &file) {
 
    const uint64_t size = fs::file_size(file);
    if (size == 0)
        return {};
 
    MappedFile mapped(file.string());
    Chunker chunker(mapped.data(), mapped.size());
 
  
    const size_t max_chunks = static_cast<size_t>(size / Config::min_chunk_size) + 1;
 
    std::vector<Digest> digests;
    digests.reserve(max_chunks);
 
    std::counting_semaphore<>slots(Config::max_inflight_chunks);
 
    while (true) {
 
        Chunk chunk;
        if (!chunker.next_chunk(chunk))
            break;
 
        if (digests.size() == max_chunks)
            throw std::logic_error("chunk count exceeded its upper bound");
 
        slots.acquire();           
        digests.emplace_back();
        Digest *slot = &digests.back();
 
        pool_.submit([this, owned = std::move(chunk), slot, &slots] {
            try {
                *slot = store_.store(owned);
            } catch (...) {
                slots.release();
                throw;
            }
            slots.release();
        });
    }
 
    pool_.wait();                   
    return digests;
}
 
void SnapshotStore::backup(const std::string &input_path, const std::string &snapshot_name) {
 
    const fs::path manifest_file = manifest_path(snapshot_name);   
 
    const fs::path root = input_path;
    if (!fs::exists(root))
        throw std::runtime_error("input path does not exist: " + input_path);
 
    const bool is_directory = fs::is_directory(root);
 
    std::vector<std::pair<fs::path, std::string>> targets;
 
    if (is_directory) {
        for (const auto &e : fs::recursive_directory_iterator(root))
            if (e.is_regular_file())
                targets.emplace_back(e.path(), fs::relative(e.path(), root).generic_string());
    } else {
        targets.emplace_back(root, root.filename().generic_string());
    }
 
    // recursive_directory_iterator gives no ordering guarantee; sort so an
    // unchanged tree always produces a byte-identical manifest.
    std::sort(targets.begin(), targets.end(), [](const auto &a, const auto &b) {
        return a.second < b.second;
    });
 
    json manifest;
    manifest["is_directory"] = is_directory;
    manifest["files"] = json::array();
 
    for (const auto &[path, relative] : targets)
        manifest["files"].push_back({
            {"path",   relative},
            {"size",   fs::file_size(path)},
            {"mode",   static_cast<uint32_t>(fs::status(path).permissions())},
            {"chunks", convert_to_readable_hex(store_file(path))},
        });
 
 
    store_.sync();
 
    const fs::path tmp = manifest_file.string() + ".tmp";
 
    {
        std::ofstream out(tmp);
        if (!out)
            throw std::runtime_error("failed to write manifest: " + snapshot_name);
 
        out << manifest.dump(2);
        out.flush();
 
        if (!out)
            throw std::runtime_error("failed while writing manifest: " + snapshot_name);
    }
 
    fsync_file(tmp);                    
    fs::rename(tmp, manifest_file);     
    fsync_dir(manifests_dir_);         
}
 
void SnapshotStore::restore(const std::string &snapshot_name, const std::string &output_path) const {
 
    std::ifstream in(manifest_path(snapshot_name));
    if (!in)
        throw std::runtime_error("failed to open snapshot: " + snapshot_name);
 
    json manifest;
    in >> manifest;
 
    const bool is_directory = manifest.value("is_directory", true);
    const fs::path out_root = output_path;
 
    for (const auto &file : manifest.at("files")) {
 
        const fs::path relative = file.at("path").get<std::string>();
 
        if (relative.is_absolute())
            throw std::runtime_error("unsafe absolute path: " + relative.string());
 
        for (const auto &part : relative)
            if (part == "..")
                throw std::runtime_error("unsafe path traversal: " + relative.string());
 
        const fs::path dest = is_directory ? out_root / relative : out_root;
        if (dest.has_parent_path())
            fs::create_directories(dest.parent_path());
 
        std::ofstream out(dest, std::ios::binary);
        if (!out)
            throw std::runtime_error("failed to create output file: " + dest.string());
 
        for (const auto &digest : file.at("chunks")) {
            const Chunk chunk = store_.load(from_hex(digest.get<std::string>()));
            out.write(reinterpret_cast<const char *>(chunk.bytes.data()),
                      static_cast<std::streamsize>(chunk.bytes.size()));
        }
 
        out.close();
        if (!out)
            throw std::runtime_error("failed while writing output file: " + dest.string());
 
        if (const uint32_t mode = file.value("mode", 0u)) {
            std::error_code ec;
            fs::permissions(dest, static_cast<fs::perms>(mode), fs::perm_options::replace, ec);
        }
    }
}
 
void SnapshotStore::gc(bool dry_run) {
 
    constexpr double min_garbage_ratio = 0.5;
 
    GcScanner scanner(store_.index(), store_.packs_directory());
    const auto live   = scanner.collect_live_objects(manifests_dir_);
    const auto report = scanner.analyze(live);
    const auto packs  = scanner.select_packs_for_compaction(report, min_garbage_ratio);
 
    std::cout << "gc: " << report.total_objects << " objects, "
              << report.total_live_objects << " live, "
              << report.total_garbage_objects << " garbage ("
              << report.total_garbage_bytes << " bytes reclaimable)\n";
 
    if (packs.empty()) {
        std::cout << "gc: nothing to compact\n";
        return;
    }
 
    if (dry_run) {
        std::cout << "gc: would compact " << packs.size() << " pack(s)\n";
        return;
    }
 
    const uint64_t reclaimed = store_.compact(live, packs);
    std::cout << "gc: compacted " << packs.size() << " pack(s), reclaimed "
              << reclaimed << " bytes\n";
}

void SnapshotStore::remove_snapshot(const std::string &snapshot_name) {

	const fs::path path = manifest_path(snapshot_name);   /* validates the name */

	if (!fs::exists(path))
		throw std::runtime_error("no such snapshot: " + snapshot_name);

	fs::remove(path);
	fsync_dir(manifests_dir_);
}


/*
 * Checks the store and optionally repairs the index.
 *
 * Two directions matter and they mean different things:
 *
 *   orphan    in a pack, in no manifest   -- leaked by a crash, GC reclaims it
 *   dangling  in a manifest, in no pack   -- real data loss, that snapshot is
 *                                            unrestorable and always will be
 *
 * Only the second is a problem no tool can fix, so it is what fsck reports
 * loudest.
 */
SnapshotStore::FsckReport SnapshotStore::fsck(bool rebuild, bool verify_payloads) {

	FsckReport report;

	if (rebuild)
		report.rebuild = store_.rebuild_index_from_packs(verify_payloads);

	if (!fs::exists(manifests_dir_))
		return report;

	for (const auto &entry : fs::directory_iterator(manifests_dir_)) {

		if (!entry.is_regular_file())
			continue;

		++report.snapshots;

		std::ifstream in(entry.path());
		json manifest;
		in >> manifest;

		bool broken = false;

		for (const auto &file : manifest["files"])
			for (const auto &hex : file["chunks"]) {

				const Digest digest = from_hex(hex.get<std::string>());
				++report.live_chunks;

				if (!store_.contains(digest)) {
					++report.dangling;
					broken = true;
				}
			}

		if (broken)
			report.broken_snapshots.push_back(entry.path().filename().string());
	}

	return report;
}