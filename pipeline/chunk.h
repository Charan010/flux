#pragma once

#include <vector>
#include <cstdint>

struct Chunk {

    int id;
    std::vector<uint8_t> data;

    Chunk(int chunk_id, std::vector<uint8_t>&& buffer);
};
