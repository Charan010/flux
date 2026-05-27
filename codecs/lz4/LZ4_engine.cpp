#include "LZ4_engine.h"

#include <stdexcept>
#include <vector>

#include "bit_io.h"
#include "chunk.h"
#include "lz4.h"

size_t lz4_compress_bound(size_t input_size) {

  return input_size + (input_size / 255) + 16;
}

void LZ4Engine::write_global_header(BitWriter &bw, uint32_t orig_size,
                                    uint32_t num_chunks, uint32_t chunk_size) {
  for (uint8_t b : Config::MAGIC)
    bw.write_byte(b);

  bw.write_byte(Config::CODEC_LZ4);

  write_uint32(bw, orig_size);
  write_uint32(bw, num_chunks);
  write_uint32(bw, chunk_size);
}

void LZ4Engine::read_global_header(const uint8_t *data, size_t size,
                                   uint32_t &orig_size, uint32_t &num_chunks,
                                   uint32_t &chunk_size) {
  BitReader br(data, size);

  for (uint8_t expected : Config::MAGIC) {
    if (br.read_byte() != expected) {
      throw std::runtime_error("bad magic");
    }
  }

  uint8_t codec = br.read_byte();
  if (codec != Config::CODEC_LZ4) {
    throw std::runtime_error("codec mismatch");
  }

  orig_size = read_uint32(br);
  num_chunks = read_uint32(br);
  chunk_size = read_uint32(br);
}

void LZ4Engine::encode_chunk(const uint8_t *input, size_t input_size,
                             Chunk &output) {
  thread_local std::vector<uint8_t> encoded;

  encoded.clear();

  encoded.resize(lz4_compress_bound(input_size));

  const size_t compressed_size =
      lz4_compress(input, input_size, encoded.data());

  encoded.resize(compressed_size);

  output.original_size = static_cast<uint32_t>(input_size);

  output.compressed_bytes = static_cast<uint32_t>(compressed_size);

  output.bit_count = 0;

  output.data.swap(encoded);
}

void LZ4Engine::decode_chunk(const uint8_t *input, size_t input_size,
                             Chunk &output) {
  thread_local std::vector<uint8_t> decoded;

  decoded.clear();
  decoded.resize(output.original_size + 8); // +8 slack for 8-byte overshoot

  const size_t decoded_size = lz4_decompress(input, input_size, decoded.data());

  if (decoded_size != output.original_size)
    throw std::runtime_error("lz4 decode size mismatch");

  decoded.resize(decoded_size);
  output.data.swap(decoded);
}

void LZ4Engine::prepare_encoder(const uint8_t *input, size_t size) {
  (void)input;
  (void)size;
}

void LZ4Engine::prepare_decoder(const uint8_t *header_data, size_t &pos) {
  (void)header_data;
  (void)pos;
}