#pragma once

#include <cstdint>
#include <vector>

struct Chunk {

    uint32_t id;

    uint32_t original_size;

    /*
         bit_count is a useful semantic for huffman decoding because extra bits are padded to make it a complete byte.
         so, deocder should know how many valid bits are present and where to stop.

         LZ4 uses bytes directly. so its just redundant for LZ4.

    */
    uint32_t bit_count;

    std::vector<uint8_t> data;

    Chunk() = default;

    Chunk(uint32_t chunk_id, uint32_t original_size, uint32_t bit_count, std::vector<uint8_t>&& buffer);
};