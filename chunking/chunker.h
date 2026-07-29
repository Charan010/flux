#pragma once

#include <cstdint>
#include <vector>

#include "core/config.h"

struct Chunk {
    std::vector<uint8_t> bytes;
};


class Chunker {
public:
    explicit Chunker(const uint8_t *data, size_t size, size_t window = Config::chunk_window,
		uint64_t base = Config::chunk_base,
		uint64_t mask = Config::chunk_mask);
	
    bool next_chunk(Chunk &chunk);

private:
    const uint8_t *data_;
    size_t size_;
    size_t pos_;

    size_t window_;
    uint64_t base_;
    uint64_t mask_;
    uint64_t highest_power_;
	
};
