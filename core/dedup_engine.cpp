#include "dedup_engine.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "io/mmap_file.h"
#include "chunking/chunker.h"
#include "gc/gc.h"
#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;


DedupEngine::DedupEngine(const std::string &store_directory, size_t threads):
	store_(store_directory), manifests_dir_(fs::path(store_directory) / "manifests"),
	pool_(threads){

	fs::create_directories(manifests_dir_);
}

DedupEngine::FileEntry DedupEngine::backup_file(const fs::path &file_path, const std::string &relative_path){

	FileEntry entry;
	entry.path = relative_path;
	entry.size = fs::file_size(file_path);
	entry.mode = static_cast<uint32_t>(fs::status(file_path).permissions());

	if(entry.size == 0)
		return entry;

	MappedFile file(file_path.string());
	Chunker chunker(file.data(), file.size());

	std::vector<Chunk> chunks;
    Chunk chunk;
    while (chunker.next_chunk(chunk))
        chunks.push_back(std::move(chunk));

    entry.chunks.resize(chunks.size());
    for (size_t i = 0; i < chunks.size(); ++i) {
        pool_.submit([this, &chunks, &entry, i] {
            entry.chunks[i] = store_.store(chunks[i]);
        });
    }

    pool_.wait();
    return entry;

}


void DedupEngine::backup(const std::string &input_path, const std::string &manifest_name) {
    const fs::path root = input_path;
    if (!fs::exists(root))
        throw std::runtime_error("input path does not exist: " + input_path);

    const bool is_directory = fs::is_directory(root);
    std::vector<FileEntry> entries;

    if (is_directory) {
        for (const auto &e : fs::recursive_directory_iterator(root)) {
            if (!e.is_regular_file())
                continue;

            const std::string rel = fs::relative(e.path(), root).generic_string();
            entries.push_back(backup_file(e.path(), rel));
        }
    }

	else {  entries.push_back(backup_file(root, root.filename().generic_string())); }
    

    // recursive_directory_iterator doesnt really give deterministic ordering of the files in directory. to
	// observe version changes it is necessary to have some determinstic ordering (for future).
    std::sort(entries.begin(), entries.end(),
              [](const FileEntry &a, const FileEntry &b) { return a.path < b.path; });

    json manifest;
    manifest["is_directory"] = is_directory;
    manifest["files"] = json::array();

    for (const auto &entry : entries) {
        manifest["files"].push_back({
            {"path",   entry.path},
            {"size",   entry.size},
            {"mode",   entry.mode},
            {"chunks", entry.chunks},
        });
    }

    const fs::path manifest_path = resolve_manifest(manifest_name);
    std::ofstream out(manifest_path);
    if (!out)
        throw std::runtime_error("failed to write manifest: " + manifest_path.string());
    out << manifest.dump(2);

    store_.save_index();
}


void DedupEngine::write_file(const FileEntry &entry, const fs::path &dest) const {
    fs::create_directories(dest.parent_path());

    std::ofstream out(dest, std::ios::binary);
    if (!out)
        throw std::runtime_error("failed to create output file: " + dest.string());

    for (const auto &digest : entry.chunks) {
        Chunk c = store_.load(digest);
        out.write(reinterpret_cast<const char *>(c.bytes.data()), c.bytes.size());
    }
}


void DedupEngine::restore(const std::string &manifest_name, const std::string &output_path) const {
    const fs::path manifest_path = resolve_manifest(manifest_name);
    std::ifstream in(manifest_path);
    if (!in)
        throw std::runtime_error("failed to open manifest: " + manifest_path.string());

    json manifest;
    in >> manifest;

    const bool is_directory = manifest.value("is_directory", true);
    const fs::path out_root = output_path;

    for (const auto &f : manifest.at("files")) {
        FileEntry entry;
        entry.path   = f.at("path").get<std::string>();
        entry.size   = f.value("size", 0ull);
        entry.mode   = f.value("mode", 0u);
        entry.chunks = f.at("chunks").get<std::vector<std::string>>();

        const fs::path rel(entry.path);
        if (rel.is_absolute())
            throw std::runtime_error("unsafe absolute path in manifest: " + entry.path);
        for (const auto &part : rel)
            if (part == "..")
                throw std::runtime_error("unsafe path traversal in manifest: " + entry.path);

        const fs::path dest = is_directory ? (out_root / rel) : out_root;
        write_file(entry, dest);

        if (entry.mode != 0) {
            std::error_code ec;   
            fs::permissions(dest, static_cast<fs::perms>(entry.mode),
                            fs::perm_options::replace, ec);
        }
    }
}


void DedupEngine::gc(bool dry_run) {

    constexpr double kMinGarbageRatio = 0.5;

    GcScanner scanner(store_.index());
    const auto live = scanner.collect_live_objects(manifests_dir_);
    const GcReport report = scanner.analyze(live);
    const auto packs = scanner.select_packs_for_compaction(report, kMinGarbageRatio);

    std::cout << "gc: " << report.total_objects << " objects, "
              << report.total_live_objects << " live, "
              << report.total_garbage_objects << " garbage ("
              << report.total_garbage_bytes << " bytes reclaimable)\n";


    if (packs.empty()){ 
		std::cout << "gc: nothing to compact\n";
		return; 
	}

    if (dry_run){
		 std::cout << "gc: " << packs.size() << " pack(s) would be compacted (dry-run)\n"; 
		 return; 
	}

    const uint64_t reclaimed = store_.compact(live, packs);
    std::cout << "gc: compacted " << packs.size() << " pack(s), reclaimed "
              << reclaimed << " bytes\n";
}

