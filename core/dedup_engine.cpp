#include "dedup_engine.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "chunking/chunker.h"
#include "gc/gc.h"
#include "io/mmap_file.h"
#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

DedupEngine::DedupEngine(const std::string &store_directory, size_t threads)
    : store_(store_directory),
      manifests_dir_(fs::path(store_directory) / "manifests"),
      pool_(threads) {

    fs::create_directories(manifests_dir_);
}

fs::path DedupEngine::manifest_path(const std::string &name) const {
    return manifests_dir_ / fs::path(name).filename();
}

std::vector<std::string> DedupEngine::store_file(const fs::path &file) {

    if (fs::file_size(file) == 0)
        return {};                      

    MappedFile mapped(file.string());
    Chunker chunker(mapped.data(), mapped.size());

    std::vector<Chunk> chunks;
    Chunk chunk;
    while (chunker.next_chunk(chunk))
        chunks.push_back(std::move(chunk));

    std::vector<std::string> digests(chunks.size());
    for (size_t i = 0; i < chunks.size(); ++i)
        pool_.submit([this, &chunks, &digests, i] {
            digests[i] = store_.store(chunks[i]);
        });
    pool_.wait();

    return digests;
}

void DedupEngine::backup(const std::string &input_path, const std::string &manifest_name) {

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
    std::sort(targets.begin(), targets.end(),
              [](const auto &a, const auto &b) { return a.second < b.second; });

    json manifest;
    manifest["is_directory"] = is_directory;
    manifest["files"] = json::array();

    for (const auto &[path, relative] : targets)
        manifest["files"].push_back({
            {"path",   relative},
            {"size",   fs::file_size(path)},
            {"mode",   static_cast<uint32_t>(fs::status(path).permissions())},
            {"chunks", store_file(path)},
        });

    std::ofstream out(manifest_path(manifest_name));
    if (!out)
        throw std::runtime_error("failed to write manifest: " + manifest_name);

    out << manifest.dump(2);
    store_.save_index();
}

void DedupEngine::restore(const std::string &manifest_name, const std::string &output_path) const {

    std::ifstream in(manifest_path(manifest_name));
    if (!in)
        throw std::runtime_error("failed to open manifest: " + manifest_name);

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
            const Chunk chunk = store_.load(digest.get<std::string>());
            out.write(reinterpret_cast<const char *>(chunk.bytes.data()),
                      static_cast<std::streamsize>(chunk.bytes.size()));
        }
        out.close();

        if (const uint32_t mode = file.value("mode", 0u)) {
            std::error_code ec;   
            fs::permissions(dest, static_cast<fs::perms>(mode), fs::perm_options::replace, ec);
        }
    }
}

void DedupEngine::gc(bool dry_run) {

    constexpr double min_garbage_ratio = 0.5;

    GcScanner scanner(store_.index());
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

    std::cout << "gc: compacted " << packs.size() << " pack(s), reclaimed "
              << store_.compact(live, packs) << " bytes\n";
}