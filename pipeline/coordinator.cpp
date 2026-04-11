#include "coordinator.h"
#include <iostream>

Coordinator::Coordinator(size_t threads, size_t chunk)
    : pool(threads), chunk_size(chunk) {}

void Coordinator::encode_chunk(std::vector<uint8_t> data, int id, const std::array<HuffmanCode,256>* tbl,
    ChunkBuffer* buffer) {

    uint64_t acc = 0;
    int bits_in_acc = 0;
    uint32_t bit_count = 0;

    std::vector<uint8_t> encoded;
    encoded.reserve(data.size() + 4);

    encoded.resize(4);

    for (uint8_t c : data) {
    const HuffmanCode& code = (*tbl)[c];
    bit_count += code.len;

    acc = (acc << code.len) | code.bits;
    bits_in_acc += code.len;

    while (bits_in_acc >= 8) {
        bits_in_acc -= 8;
        encoded.push_back((acc >> bits_in_acc) & 0xFF);
        acc &= (1ULL << bits_in_acc) - 1;
    }
    }

    if (bits_in_acc > 0)
        encoded.push_back(static_cast<uint8_t>(acc << (8 - bits_in_acc)));

    /* writing a 4 byte total number of valid bits in the chunk. */
    encoded[0] = (bit_count >> 24) & 0xFF;
    encoded[1] = (bit_count >> 16) & 0xFF;
    encoded[2] = (bit_count >>  8) & 0xFF;
    encoded[3] = (bit_count >>  0) & 0xFF;

    buffer-> submit_chunk(Chunk(id, std::move(encoded)));

}

void Coordinator::compress(const std::string &input_file, const std::string &output_file) {
    
    FrequencyTable freq{};
    freq.fill(0);
    
    int total_chunks = 0;
    std::ifstream in(input_file, std::ios::binary);
    std::vector<uint8_t> read_buf(chunk_size);

    while (true) {
        in.read(reinterpret_cast<char*>(read_buf.data()), chunk_size);
        size_t read = in.gcount();

        if (read == 0)
            break;

        for (size_t i = 0; i < read; ++i)
            freq[read_buf[i]]++;
        total_chunks++;
    }

    in.close();

    auto root = std::unique_ptr<Node>(build_huffman_tree(freq));

    std::array<uint8_t, 256> lengths{};
    compute_lengths(root.get(), 0, lengths);

    std::array<HuffmanCode, 256> table{};
    generate_canonical_table(lengths, table);

    BitWriter bw(output_file);
    write_lengths(lengths, bw);

    uint32_t total_len = 0;
    for (int i = 0; i < 256; i++)
        total_len += freq[i];

    write_uint32(bw, total_len);
    write_uint32(bw, total_chunks);
    bw.flush();

    ChunkBuffer buffer(bw);
    const auto* tbl = &table;

    std::ifstream in2(input_file, std::ios::binary);
    int chunk_id = 0;

    while (true) {

        in2.read(reinterpret_cast<char*>(read_buf.data()), chunk_size);
        size_t read = in2.gcount();
        if (read == 0) break;

        std::vector<uint8_t> chunk_data(read_buf.begin(), read_buf.begin() + read);
        int id = chunk_id++;

        pool.submit([this, data = std::move(chunk_data), id, tbl, &buffer]() mutable {
            encode_chunk(std::move(data), id, tbl, &buffer);
        });
    }

    in2.close();
    pool.shutdown();
    buffer.finish();
    bw.flush();
}


void Coordinator::decode_chunk(std::vector<uint8_t> encoded, uint32_t bit_count, 
    const DecodeLUT &lut, Node *root, ChunkBuffer *buffer, int id){

    std::vector<uint8_t> decoded;
    decoded.reserve(bit_count/2);

    uint64_t acc = 0;
    int bits_in_acc = 0;
    uint32_t bits_read = 0;
    size_t byte_pos = 0;

    while (bits_read < bit_count) {

        while (bits_in_acc < LUT_BITS && byte_pos < encoded.size()) {
            acc = (acc << 8) | encoded[byte_pos++];
            bits_in_acc += 8;
        }

        if (bits_in_acc == 0)
            break;

        uint32_t idx;
        if (bits_in_acc >= LUT_BITS) {
            int shift = bits_in_acc - LUT_BITS;
            idx = (acc >> shift) & ((1 << LUT_BITS) - 1);
            
        } else {
            
            // fewer bits than LUT_BITS remain — left-align into LUT_BITS window
            int shift = LUT_BITS - bits_in_acc;
            idx = (acc << shift) & ((1 << LUT_BITS) - 1);
        }

        const auto& entry = lut[idx];

        if (entry.is_leaf) {
            decoded.push_back(entry.symbol);

            bits_in_acc -= entry.bits;
            bits_read += entry.bits;

            acc &= (1ULL << bits_in_acc) - 1;

        } else {
            Node* cur = entry.next;

            bits_in_acc -= entry.bits;
            bits_read += entry.bits;

            acc &= (1ULL << bits_in_acc) - 1;

            while (true) {
                if (bits_in_acc == 0 && byte_pos < encoded.size()) {
                    acc = (acc << 8) | encoded[byte_pos++];
                    bits_in_acc += 8;
                }

                int bit = (acc >> (bits_in_acc - 1)) & 1;
                bits_in_acc--;
                bits_read++;

                cur = bit ? cur->right.get() : cur->left.get();

                if (!cur->left && !cur->right) {
                    decoded.push_back(cur->ch);
                    break;
                }
            }
        }
    }

    buffer->submit_chunk(Chunk(id, std::move(decoded)));
}


void Coordinator::decompress(const std::string& input, const std::string& output) {

    BitReader br(input);

    std::array<uint8_t, 256> lengths{};
    read_lengths(lengths, br);

    uint32_t total_len   = read_uint32(br);
    uint32_t total_chunks = read_uint32(br);


    std::array<HuffmanCode, 256> table{};
    generate_canonical_table(lengths, table);

    auto root_ptr = std::unique_ptr<Node>(build_decode_tree(table));
    Node* root = root_ptr.get();

    DecodeLUT lut{};
    build_decode_lut(table, root, lut);

    BitWriter bw(output);
    ChunkBuffer buffer(bw);

    for (uint32_t id = 0; id < total_chunks; id++) {

        uint32_t bit_count = read_uint32(br);
        uint32_t byte_size = (bit_count + 7) / 8;

        std::vector<uint8_t> encoded(byte_size);
        for (uint32_t i = 0; i < byte_size; i++)
            encoded[i] = br.read_byte();

        pool.submit([this,encoded = std::move(encoded), bit_count, &buffer, &lut, root, id]() mutable {
            decode_chunk(std::move(encoded), bit_count, lut, root, &buffer, id);
        });
    }

    pool.shutdown();
    buffer.finish();
    bw.flush();
}