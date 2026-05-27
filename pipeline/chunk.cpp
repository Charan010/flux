#include "chunk.h"

Chunk::Chunk(uint32_t chunk_id, uint32_t original_size, uint32_t bit_count,
             std::vector<uint8_t> &&buffer)
    : id(chunk_id), original_size(original_size), bit_count(bit_count),
      data(std::move(buffer)) {}