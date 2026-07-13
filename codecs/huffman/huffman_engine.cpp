#include "huffman_engine.h"

static constexpr uint8_t MAGIC[4] = {'F', 'L', 'U', 'X'};
static constexpr uint8_t CODEC_HUFFMAN = 0;

void HuffmanEngine::write_global_header(BitWriter &bw, uint32_t orig_size, uint32_t num_chunks, uint32_t chunk_size){

  	for (uint8_t b : Config::MAGIC)
    	bw.write_byte(b);

  	bw.write_byte(Config::CODEC_HUFFMAN);

  	for (uint32_t len : lengths)
    	bw.write_byte(len);

  	write_uint32(bw, orig_size);
  	write_uint32(bw, num_chunks);
  	write_uint32(bw, chunk_size);

}

void HuffmanEngine::read_global_header(const uint8_t *data, size_t size, uint32_t &orig_size, uint32_t &num_chunks,
	uint32_t &chunk_size){

	BitReader br(data, size);

  	for (uint8_t expected : MAGIC) {
    	if (br.read_byte() != expected)
      		throw std::runtime_error("bad magic: not a FLUX file");
  	}

  	uint8_t codec = br.read_byte();
  	if (codec != CODEC_HUFFMAN)
    	throw std::runtime_error("codec mismatch");

  	for (uint8_t &len : lengths)
    	len = br.read_byte();

  	generate_canonical_table(lengths, code_table);
  	build_decode_lut(code_table, primary_lut, secondary_lut);

  	orig_size = read_uint32(br);
  	num_chunks = read_uint32(br);
  	chunk_size = read_uint32(br);

}

void HuffmanEngine::prepare_encoder(const uint8_t *data, size_t size) {

  	FrequencyTable freq = compute_frequency_parallel(data, size);

  	std::vector<Node> storage;
  	Node *root = build_huffman_tree(freq, storage);

  	compute_lengths(root, 0, lengths);
  	generate_canonical_table(lengths, code_table);
  	build_decode_lut(code_table, primary_lut, secondary_lut);

}

void HuffmanEngine::encode_chunk(const uint8_t *input, size_t input_size, Chunk &output) {
  
  	thread_local std::vector<uint8_t> encoded;
  	encoded.clear();

  	const size_t max_bytes = (static_cast<size_t>(input_size) * 32 + 7) / 8 + 8;
  	encoded.resize(max_bytes);

  	uint32_t bit_count = 0;
  	size_t pos = 0;
  	uint64_t acc = 0;
  	int bits_in_acc = 0;

  	constexpr size_t PREFETCH_DIST = 64;

  	for (size_t i = 0; i < input_size; ++i) {
    	if (i + PREFETCH_DIST < input_size)
      		__builtin_prefetch(&code_table[input[i + PREFETCH_DIST]], 0, 1);

    	const auto &code = code_table[input[i]];

    	while (bits_in_acc + code.len > 64) {
      		encoded[pos++] = static_cast<uint8_t>((acc >> (bits_in_acc - 8)) & 0xFF);
      		bits_in_acc -= 8;
    	}

    	acc = (acc << code.len) | code.bits;
    	bits_in_acc += code.len;
    	bit_count += code.len;

    	while (bits_in_acc >= 8) {
      		bits_in_acc -= 8;
      		encoded[pos++] = static_cast<uint8_t>((acc >> bits_in_acc) & 0xFF);
    	}

    	if (bits_in_acc > 0)
      		acc &= (1ULL << bits_in_acc) - 1;
    	else
      		acc = 0;
  	}

  	if (bits_in_acc > 0)
    	encoded[pos++] = static_cast<uint8_t>((acc << (8 - bits_in_acc)) & 0xFF);

  	encoded.resize(pos);

  	output.original_size = static_cast<uint32_t>(input_size);
  	output.bit_count = bit_count;
  	output.compressed_bytes = static_cast<uint32_t>(encoded.size());
  	output.data = std::move(encoded);

}

void HuffmanEngine::decode_chunk(const uint8_t *input, size_t input_size, Chunk &chunk) {

  	chunk.data.clear();
  	chunk.data.resize(chunk.original_size);

  	uint8_t *out_begin = chunk.data.data();
  	uint8_t *out = out_begin;
  	uint8_t *out_end = out_begin + chunk.original_size;

  	BitReader reader(input, input_size);
  	uint32_t bits_read = 0;

  	while (out < out_end) {

    	const uint32_t idx = reader.peek_bits(LUT_BITS);
    	const auto &entry = primary_lut[idx];

    	if (__builtin_expect(entry.flags & ENTRY_SYMBOL, 1)) {

      		if (bits_read + entry.bits > chunk.bit_count)
        		throw std::runtime_error("bitstream overflow");

      		reader.consume_bits(entry.bits);
      		bits_read += entry.bits;
      		*out++ = static_cast<uint8_t>(entry.value);

      		if (__builtin_expect(out < out_end, 1)) {
        		const uint32_t next_idx = reader.peek_bits(LUT_BITS);
        		__builtin_prefetch(&primary_lut[next_idx], 0, 1);
      		}

    	} else {

      		const int extra_bits = entry.bits;
      		reader.consume_bits(LUT_BITS);

      		const uint32_t extra = reader.peek_bits(extra_bits);
      		__builtin_prefetch(&secondary_lut[entry.value + extra], 0, 1);

      		const auto &sub = secondary_lut[entry.value + extra];
      		const int suffix_bits = sub.bits - LUT_BITS;

      		if (bits_read + sub.bits > chunk.bit_count)
        		throw std::runtime_error("bitstream overflow");

      		reader.consume_bits(suffix_bits);
      		bits_read += sub.bits;

      		*out++ = static_cast<uint8_t>(sub.value);
    	}
  	}

  	if (bits_read > chunk.bit_count)
    	throw std::runtime_error("decoder consumed too many bits");

  	chunk.data.resize(out - out_begin);

}

HuffmanEngine::HuffmanEngine() {}

void HuffmanEngine::prepare_decoder(const uint8_t *header_data, size_t &pos) {
  	(void)header_data;
  	(void)pos;
}
