#pragma once

#include <array>
#include <cstdint>
#include <cstddef>


namespace flux {


constexpr  char MAGIC[4] = {'F', 'L', 'U', 'X'};    
constexpr uint8_t version = 1;
constexpr size_t HUFFMAN_SYMBOLS = 256;

struct GlobalHeader{

    char magic[4];
    uint8_t version;
    uint32_t original_file_size;
    uint32_t chunk_size;
    uint32_t total_chunks;
};

struct ChunkHeader {
    uint32_t compressed_size;
    uint8_t padding_bits;
};

using HuffmanCodeLengths =
    std::array<uint8_t, HUFFMAN_SYMBOLS>;

}