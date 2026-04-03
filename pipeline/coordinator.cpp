#include "coordinator.h"


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

        acc = (acc << code.len) | code.bits;
        bits_in_acc += code.len;
        bit_count += code.len;

        while (bits_in_acc >= 8) {

            int bytes_ready = bits_in_acc / 8;
            int remainder  = bits_in_acc % 8;

            uint64_t to_write = acc >> remainder;

            size_t old_size = encoded.size();
            encoded.resize(old_size + bytes_ready);
            uint8_t* dst = encoded.data() + old_size;

            for (int i = 0; i < bytes_ready; i++) {
                int shift = (bytes_ready - 1 - i) * 8;
                dst[i] = (to_write >> shift) & 0xFF;
            }

            bits_in_acc = remainder;
            acc &= (1ULL << remainder) - 1;
        }
    }

    if (bits_in_acc > 0)
        encoded.push_back(static_cast<uint8_t>(acc << (8 - bits_in_acc)));

    encoded[0] = (bit_count >> 24) & 0xFF;
    encoded[1] = (bit_count >> 16) & 0xFF;
    encoded[2] = (bit_count >>  8) & 0xFF;
    encoded[3] = (bit_count >>  0) & 0xFF;

    buffer->submit_chunk(Chunk(id, std::move(encoded)));

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

        for (size_t i = 0; i < read; ++i) {
            freq[read_buf[i]]++;
        }

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

        if (read == 0)
            break;

        std::vector<uint8_t> chunk_data(read_buf.begin(), read_buf.begin() + read);

        pool.submit(EncodeTask{std::move(chunk_data), chunk_id++, this, tbl, &buffer });
    }

    in2.close();
    bw.flush();
}
