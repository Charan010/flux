#include "coordinator.h"
#include <iostream>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

    Coordinator::Coordinator(size_t threads, size_t chunk)
        : pool(threads), chunk_size(chunk) {}

    Coordinator::~Coordinator() {
        pool.shutdown();
    }
    

    void Coordinator::encode_chunk(const uint8_t* data, size_t len, int id,
    const std::array<HuffmanCode, 256>* tbl, ChunkBuffer* buffer){
    
    thread_local std::vector<uint8_t> encoded;
    encoded.clear();

    // reserve once, reused forever
    if (encoded.capacity() < len * 2)
            encoded.reserve(len * 2);

    encoded.resize(4); // header space

    uint64_t acc = 0;
    int bits_in_acc = 0;
    uint32_t bit_count = 0;

    uint8_t tmp[64];
    int t = 0;

    size_t pos = 4; // write pointer

   for (size_t i = 0; i < len; ++i) {

        uint8_t c = data[i];
        const auto& code = (*tbl)[c];
        bit_count += code.len;

        acc = (acc << code.len) | code.bits;
        bits_in_acc += code.len;

        while (bits_in_acc >= 32) {
            bits_in_acc -= 32;

            uint32_t out32 = (acc >> bits_in_acc) & 0xFFFFFFFFu;

            tmp[t++] = (out32 >> 24) & 0xFF;
            tmp[t++] = (out32 >> 16) & 0xFF;
            tmp[t++] = (out32 >> 8)  & 0xFF;
            tmp[t++] = (out32 >> 0)  & 0xFF;

            acc &= (bits_in_acc > 0) ? ((1ULL << bits_in_acc) - 1) : 0;

            if (t >= 56) {
                if (pos + t > encoded.size())
                    encoded.resize(pos + t);

                memcpy(encoded.data() + pos, tmp, t);
                pos += t;
                t = 0;
            }
        }

        while (bits_in_acc >= 8) {
            bits_in_acc -= 8;

            tmp[t++] = (acc >> bits_in_acc) & 0xFF;

            acc &= (bits_in_acc > 0) ? ((1ULL << bits_in_acc) - 1) : 0;

            if (t >= 56) {
                if (pos + t > encoded.size())
                    encoded.resize(pos + t);

                memcpy(encoded.data() + pos, tmp, t);
                pos += t;
                t = 0;
            }
        }
    }

    if (bits_in_acc > 0)
        tmp[t++] = static_cast<uint8_t>(acc << (8 - bits_in_acc));

    if (t > 0) {
        if (pos + t > encoded.size())
            encoded.resize(pos + t);

        memcpy(encoded.data() + pos, tmp, t);
        pos += t;
    }

    encoded.resize(pos);

    // header
    encoded[0] = (bit_count >> 24) & 0xFF;
    encoded[1] = (bit_count >> 16) & 0xFF;
    encoded[2] = (bit_count >> 8)  & 0xFF;
    encoded[3] = (bit_count >> 0)  & 0xFF;

    std::vector<uint8_t> out;
    out.swap(encoded);
    buffer->submit_chunk(Chunk(id, std::move(out)));

}

    void Coordinator::compress(const std::string &input_file, const std::string &output_file) {

        int fd = open(input_file.c_str(), O_RDONLY);
        if(fd < 0)
            throw std::runtime_error("Cannot open file");

        struct stat st;
        fstat(fd, &st);

        size_t file_size = st.st_size;

        uint8_t* file_ptr = static_cast<uint8_t*>(
            mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0));

        if(file_ptr == MAP_FAILED){
            close(fd);
            throw std::runtime_error("mmap failed");
        }

        madvise(file_ptr, file_size, MADV_SEQUENTIAL);

        FrequencyTable freq{};
        freq.fill(0);

        size_t i = 0;
        std::array<uint64_t, 256> freq0{}, freq1{}, freq2{}, freq3{};

        for(; i + 4 <= file_size; i += 4){
            freq0[file_ptr[i]]++;
            freq1[file_ptr[i+1]]++;
            freq2[file_ptr[i+2]]++;
            freq3[file_ptr[i+3]]++;
        }

        for (; i < file_size; ++i) 
            freq0[file_ptr[i]]++;


        for (int j = 0; j < 256; ++j)
            freq[j] = freq0[j] + freq1[j] + freq2[j] + freq3[j];

        int total_chunks = (file_size + chunk_size - 1) / chunk_size;
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

        ChunkBuffer buffer(bw, total_chunks);
        const auto* tbl = &table;

        int chunk_id = 0;

        for (size_t offset = 0; offset < file_size; offset += chunk_size) {


            size_t current_chunk_size = std::min(chunk_size, file_size - offset);
            //std::vector<uint8_t> chunk_data(file_ptr + offset, file_ptr + offset + current_chunk_size);
            const uint8_t* ptr = file_ptr + offset;
            size_t  len = current_chunk_size;

            int id = chunk_id++;
            pool.submit([this, ptr, len, id, tbl, &buffer]() {
                encode_chunk(ptr, len, id, tbl, &buffer);
            });
        }

        pool.wait();   
        buffer.finish();
        bw.flush();

        munmap(file_ptr, file_size);
        close(fd);
    }


    void Coordinator::decode_chunk(std::vector<uint8_t> encoded, uint32_t bit_count,
    const DecodeLUT &lut, Node *root,
    ChunkBuffer *buffer, int id)
{
    thread_local std::vector<uint8_t> decoded;
    decoded.clear();

    if (decoded.capacity() < (bit_count * 6) / 8)
        decoded.reserve((bit_count * 6) / 8);

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

    std::vector<uint8_t> out;
    out.swap(decoded);
    buffer->submit_chunk(Chunk(id, std::move(out)));
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
        ChunkBuffer buffer(bw, total_chunks);

        for (uint32_t id = 0; id < total_chunks; id++) {

            uint32_t bit_count = read_uint32(br);
            uint32_t byte_size = (bit_count + 7) / 8;

            std::vector<uint8_t> encoded(byte_size);
            br.read_bytes(encoded.data(), byte_size);

            pool.submit([this, encoded = std::move(encoded), bit_count, &buffer, &lut, root, id]() mutable {
                decode_chunk(std::move(encoded), bit_count, lut, root, &buffer, id);
            });
        }

        pool.wait();   
        buffer.finish();
        bw.flush();
    }