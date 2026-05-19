#pragma once

#include <array>
#include <vector>
#include "huffman.h"
#include <codec_engine.h>
#include "chunk.h"
#include "ordered_queue.h"

class HuffmanEngine : public CodecEngine {

  public:
    explicit HuffmanEngine();

    void encode_chunk(const uint8_t* input, size_t input_size, Chunk& output) override;
    void decode_chunk(const uint8_t* input, size_t input_size, Chunk& output) override;
    void prepare_encoder(const uint8_t* data, size_t size) override;
    void prepare_decoder(const uint8_t* header_data, size_t& pos) override;

  private:
    std::array<HuffmanCode, 256> code_table{};
    std::array<uint8_t, 256> lengths{};
    DecodeLUT primary_lut{};
    SecondaryLUT secondary_lut{};
};
