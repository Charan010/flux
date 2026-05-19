#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "chunk.h"

class CodecEngine {

  public:
    virtual ~CodecEngine() = default;

    virtual void encode_chunk(const uint8_t* input, size_t input_size, Chunk& output) = 0;
    virtual void decode_chunk(const uint8_t* input, size_t input_size, Chunk& output) = 0;
    virtual void prepare_encoder(const uint8_t* data, size_t size) = 0;
    virtual void prepare_decoder(const uint8_t* header_data, size_t& pos) = 0;
};
