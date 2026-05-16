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
| 4B : original chunk size                           |
| 4B : compressed bit count                          |
| NB : Huffman encoded bitstream                    |
+-------------------------------------------------+

*/

void HuffmanEngine::prepare_encoder(const uint8_t* data, size_t size) {

    FrequencyTable freq = compute_frequency(const_cast<uint8_t*>(data), size);

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

void HuffmanEngine::decode_chunk(const uint8_t* input, size_t input_size, Chunk& output) {

    thread_local std::vector<uint8_t> decoded;
    decoded.clear();

    decoded.resize(output.original_size);

    uint8_t* out = decoded.data();

    BitReader reader(input, input_size);

    uint32_t bits_read = 0;

    while (bits_read < output.bit_count) {

        const uint32_t idx = reader.peek_bits(LUT_BITS);
        const auto& entry = primary_lut[idx];

        /*tells compiler that this path is going to be executed most of the times which helps CPU
         * predict branch conditions better*/
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

    decoded.resize(out - decoded.data());
    output.data = std::move(decoded);
}
