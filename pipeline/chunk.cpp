#include "chunk.h"


Chunk::Chunk(int chunk_id, uint32_t bits, std::vector<uint8_t>&& buffer) 
    : id(chunk_id), bit_count(bits), data(std::move(buffer)) {}