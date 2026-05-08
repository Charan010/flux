#pragma once

#include <vector>
#include <cstdint>

struct Chunk {

    int id;

    uint32_t original_size;
    uint32_t bit_count;

    std::vector<uint8_t> data;

    Chunk(int chunk_id,
          uint32_t original_size,
          uint32_t bit_count,
          std::vector<uint8_t>&& buffer);

};