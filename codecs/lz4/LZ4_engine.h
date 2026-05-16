#pragma once

#include "codec_engine.h"

class LZ4Engine : public CodecEngine{

public:

    void encode_chunk(const uint8_t *input, size_t input_size, Chunk &output) override;
    void decode_chunk(const uint8_t *input, size_t input_size, Chunk &output) override;
    void prepare_encoder(const uint8_t* data, size_t size ) override {}
    void prepare_decoder(const uint8_t* header_data, size_t& pos) override {}

};