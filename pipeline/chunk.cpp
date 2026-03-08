#include "chunk.h"


Chunk::Chunk(int chunk_id, std::vector<uint8_t>&& buffer)
    : id(chunk_id), data(std::move(buffer)) {}