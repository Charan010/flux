#pragma once

#include "../codec_engine.h"
#include "bit_io.h"
#include "chunk.h"
#include "config.h"
#include "huffman.h"
#include <array>
#include <vector>

class HuffmanEngine : public CodecEngine {
public:

  	explicit HuffmanEngine();

  	void encode_chunk(const uint8_t *input, size_t input_size, Chunk &output) override;
  	void decode_chunk(const uint8_t *input, size_t input_size,Chunk &output) override;


  	void prepare_encoder(const uint8_t *data, size_t size) override;
  	void prepare_decoder(const uint8_t *header_data, size_t &pos) override;

  	void write_global_header(BitWriter &bw, uint32_t orig_size, uint32_t num_chunks, uint32_t chunk_size) override;
  	void read_global_header(const uint8_t *data, size_t size, uint32_t &orig_size, uint32_t &num_chunks, 
	uint32_t &chunk_size) override;


private:

  	std::array<HuffmanCode, 256> code_table{};
  	std::array<uint8_t, 256> lengths{};
  	DecodeLUT primary_lut{};
  	SecondaryLUT secondary_lut{};

};
