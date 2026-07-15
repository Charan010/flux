#include "dedup_engine.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "../io/mmap_file.h"
#include "chunker.h"

DedupEngine::DedupEngine(const std::string &store_directory, size_t threads)
    : store_(store_directory), pool_(threads) {}

void DedupEngine::backup(const std::string &input_path, const std::string &manifest_path) {

    if (std::filesystem::file_size(input_path) == 0) {
        std::ofstream manifest(manifest_path);
        if (!manifest)
            throw std::runtime_error("Failed to create manifest: " + manifest_path);
        return;
    }

    MappedFile file(input_path);
    Chunker chunker(file.data(), file.size());

    std::vector<Chunk> chunks;
    Chunk chunk;
    while (chunker.next_chunk(chunk))
        chunks.push_back(std::move(chunk));

    std::vector<std::string> digests(chunks.size());

    for (size_t i = 0; i < chunks.size(); ++i) {
        pool_.submit([this, &chunks, &digests, i] {
            digests[i] = store_.store(chunks[i]);
        });
    }
    pool_.wait();

    std::ofstream manifest(manifest_path);
    if (!manifest)
        throw std::runtime_error("Failed to create manifest: " + manifest_path);

    for (const auto &digest : digests)
        manifest << digest << '\n';

    store_.save_index();
}

void DedupEngine::restore(const std::string &manifest_path, const std::string &output_path) const {

    std::ifstream manifest(manifest_path);
    if (!manifest)
        throw std::runtime_error("Failed to open manifest: " + manifest_path);

    std::vector<std::string> digests;
    std::string digest;

    while (std::getline(manifest, digest))
        if (!digest.empty())
            digests.push_back(digest);

    std::vector<Chunk> chunks(digests.size());

    for (size_t i = 0; i < digests.size(); ++i) {
        pool_.submit([this, &digests, &chunks, i] {
            chunks[i] = store_.load(digests[i]);
        });
    }
    pool_.wait();

    std::ofstream output(output_path, std::ios::binary);
    if (!output)
        throw std::runtime_error("Failed to create output file: " + output_path);

    for (const auto &c : chunks)
        output.write(reinterpret_cast<const char *>(c.bytes.data()), c.bytes.size());
}
