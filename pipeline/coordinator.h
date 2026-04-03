#pragma once

#include <string>
#include <array>

#include "threadpool.h"
#include "chunk_buffer.h"
#include "huffman.h"
#include "bit_io.h"

using FrequencyTable = std::array<uint64_t, 256>;

class Coordinator {

private:
    Threadpool pool;
    size_t chunk_size;

public:
    Coordinator(size_t threads, size_t chunk);

    void encode_chunk(std::vector<uint8_t> data, int id, const std::array<HuffmanCode, 256> *tbl, ChunkBuffer *buffer);

    void compress(const std::string &input_file, const std::string &output_file);

    void decompress(const std::string& input_file, const std::string& output_file);

    void decode_chunk(std::vector<uint8_t> encoded, uint32_t bit_count, const DecodeLUT& lut, Node* root, ChunkBuffer* buffer, int id);

};