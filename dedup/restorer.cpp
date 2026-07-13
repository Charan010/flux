#include "restorer.h"

#include <fstream>
#include <stdexcept>

Restorer::Restorer(const ObjectStore& objectStore)
    : objectStore_(objectStore){}

void Restorer::restore(const std::string& manifestPath, const std::string& outputPath) const{
    std::ifstream manifest(manifestPath);

    if (!manifest)
        throw std::runtime_error("Failed to open manifest.");

    std::ofstream output(outputPath, std::ios::binary);

    if (!output)
        throw std::runtime_error("Failed to create output file.");

    std::string digest;

    while (std::getline(manifest, digest)){
        if (digest.empty())
            continue;

        Chunk chunk = objectStore_.load(digest);

        output.write(reinterpret_cast<const char*>(chunk.bytes.data()), chunk.bytes.size());
    }
}