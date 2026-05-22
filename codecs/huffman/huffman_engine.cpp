#include "huffman_engine.h"

/* +---------------------------------------------+
|              GLOBAL HEADER                        |
+------------------------------------------------+
| 256B : Huffman code lengths                      |
| 4B   : total original file size                  |
| 4B   : total chunks                               |
+------------------------------------------------+


For each chunk:

+--------------------------------------------------+
| 4B : compressed bit count                          |
| NB : Huffman encoded bitstream                    |
+-------------------------------------------------+

*/

static constexpr uint8_t MAGIC[4] = {'F','L','U','X'};
static constexpr uint8_t CODEC_HUFFMAN = 0;
static constexpr uint8_t CODEC_LZ4 = 1;
static constexpr uint8_t CODEC_MIXED = 2;\


void HuffmanEngine::write_global_header(BitWriter &bw, uint32_t orig_size, uint32_t num_chunks){


    for(uint8_t b : MAGIC)
        bw.write_byte(b);

    bw.write_byte(CODEC_HUFFMAN);

    //256 bytes of code lengths to build canonical table.
    for(uint32_t len : lengths)
        bw.write_byte(len);
        
    write_uint32(bw, orig_size);
    write_uint32(bw, num_chunks);

}

void HuffmanEngine::read_global_header(const uint8_t* data, size_t size,
                     uint32_t& orig_size, uint32_t& num_chunks)
{
    BitReader br(data, size);  

    for (uint8_t expected : MAGIC) {
        if (br.read_byte() != expected)
            throw std::runtime_error("bad magic: not a FLUX file");
    }

    uint8_t codec = br.read_byte();
    if (codec != CODEC_HUFFMAN)
        throw std::runtime_error("codec mismatch");

    for (uint8_t& len : lengths)
        len = br.read_byte();

    generate_canonical_table(lengths, code_table);
    build_decode_lut(code_table, primary_lut, secondary_lut);

    orig_size  = read_uint32(br);
    num_chunks = read_uint32(br);
    
}


void HuffmanEngine::prepare_encoder(const uint8_t* data, size_t size) {

    FrequencyTable freq = compute_frequency(data, size);

    std::vector<Node> storage;

    Node* root = build_huffman_tree(freq, storage);

    compute_lengths(root, 0, lengths);
    generate_canonical_table(lengths, code_table);
    build_decode_lut(code_table, primary_lut, secondary_lut);
}

void HuffmanEngine::encode_chunk(const uint8_t* input, size_t input_size, Chunk& output) {

    thread_local std::vector<uint8_t> encoded;

    encoded.clear();
    encoded.resize(input_size * 4 + 8);

    uint32_t bit_count = 0;

    size_t pos = 0;
    uint64_t acc = 0;
    int bits_in_acc = 0;

    for (size_t i = 0; i < input_size; ++i) {

        const auto& code = code_table[input[i]];
        acc = (acc << code.len) | code.bits;

        bits_in_acc += code.len;
        bit_count += code.len;

        while (bits_in_acc >= 8) {
            bits_in_acc -= 8;
            encoded[pos++] = static_cast<uint8_t>((acc >> bits_in_acc) & 0xFF);
        }
    }

    if (bits_in_acc > 0)
        encoded[pos++] = static_cast<uint8_t>(acc << (8 - bits_in_acc));

    encoded.resize(pos);

    output.original_size = static_cast<uint32_t>(input_size);
    output.bit_count = bit_count;

    output.data = std::move(encoded);
}

void HuffmanEngine::decode_chunk(const uint8_t* input, size_t input_size,Chunk& chunk){

    chunk.data.clear();
    chunk.data.resize(chunk.original_size);

    uint8_t* out = chunk.data.data();

    BitReader reader(input, input_size);

    uint32_t bits_read = 0;

    while (bits_read < chunk.bit_count) {

        const uint32_t idx = reader.peek_bits(LUT_BITS);
        const auto& entry = primary_lut[idx];

        if (__builtin_expect(entry.flags & ENTRY_SYMBOL, 1)) {

            reader.consume_bits(entry.bits);
            bits_read += entry.bits;

            *out++ = static_cast<uint8_t>(entry.value);

        } else {

            reader.consume_bits(LUT_BITS);

            const uint32_t extra = reader.peek_bits(entry.bits);

            __builtin_prefetch(&secondary_lut[entry.value + extra], 0, 1);

            const auto& sub = secondary_lut[entry.value + extra];
            const uint8_t suffix_bits = sub.bits - LUT_BITS;
            reader.consume_bits(suffix_bits);

            bits_read += sub.bits;

            *out++ = static_cast<uint8_t>(sub.value);
        }
    }

    chunk.data.resize(out - chunk.data.data());
}
