#include "coordinator.h"
#include "chunk.h"

#include <fstream>
#include <vector>
#include <chrono>
#include <iostream>
#include <assert.h>

Coordinator::Coordinator(size_t threads, size_t chunk):
    pool(threads), chunk_size(chunk) {}


void Coordinator::compress(const std::string &input_file, const std::string &output_file){


    FrequencyCounter counter(pool);
    std::ifstream in(input_file, std::ios::binary);
    int chunk_id = 0;

    while(true){
        std::vector<uint8_t> data(chunk_size);
        in.read((char*)data.data(), chunk_size);

        size_t read = in.gcount();
        if(read == 0)
            break;
        data.resize(read);
        counter.submit_chunk(Chunk(chunk_id++, std::move(data)));
    }

    //caller thread put on sleep until global frequency table is constructed by workers.
    counter.wait();

    auto freq = counter.get_result();
    int total_chunks = chunk_id;

    auto root = std::unique_ptr<Node>(build_huffman_tree(freq));
    std::array<uint8_t,256> lengths{};
    compute_lengths(root.get(), 0, lengths);

    std::array<HuffmanCode,256> table{};
    generate_canonical_table(lengths, table);

    BitWriter bw(output_file);
    write_lengths(lengths, bw);

    uint32_t total_len = 0;
    for(int i = 0; i < 256; i++)
        total_len += freq[i];

    write_uint32(bw, total_len);
    write_uint32(bw, total_chunks);
    bw.flush();


    ChunkBuffer buffer(bw);
    std::ifstream in2(input_file, std::ios::binary);
    chunk_id = 0;

    const auto* tbl = &table;

    while(true){

        // reuse thread_local buffer for reading to avoid per-chunk allocation
        std::vector<uint8_t> read_buf;
        read_buf.resize(chunk_size);

        in2.read((char*)read_buf.data(), chunk_size);
        size_t read = in2.gcount();

        if(read == 0)
            break;

        // copy into chunk_data for the lambda — thread_local can't be captured
        std::vector<uint8_t> chunk_data(read_buf.begin(), read_buf.begin() + read);

        int id = chunk_id++;

        pool.submit([data = std::move(chunk_data), id, &buffer, tbl]() mutable {

            uint64_t acc = 0;
            int bits_in_acc = 0;
            uint32_t bit_count = 0;

            //reserving data.size() because worst case scenario. The maximum size required to encode data is
            // data.size() * 8 bytes and + 4 for adding how many bits are written in the encoded vector.

            // fresh allocation per chunk — thread_local caused corruption on multi-chunk files
            // page faults are from file I/O anyway, not vector allocation, so no perf loss here
            std::vector<uint8_t> encoded;
            encoded.reserve(data.size() + 4);

            encoded.push_back(0);
            encoded.push_back(0);
            encoded.push_back(0);
            encoded.push_back(0);

            for(uint8_t c : data){

                const HuffmanCode& code = (*tbl)[c];

                assert(bits_in_acc + code.len <= 64);

                // making place for the new variable code in the accumulator.
                acc = (acc << code.len) | code.bits;
                bits_in_acc += code.len;
                bit_count += code.len;

                
            // In a 64 bit accumulator, if I have more than one byte, then i'm ready to start pushing these bytes into encoded vector.
            // bytes_ready represents how many bytes can be added to encoded vector and shift represents the remaining left
            // out bits.

                while(bits_in_acc >= 8){

                    int bytes_ready = bits_in_acc / 8;
                    int remainder = bits_in_acc % 8;       
                    uint64_t to_write = acc >> remainder;

                    size_t old_size = encoded.size();
                    encoded.resize(old_size + bytes_ready);
                    uint8_t* dst = encoded.data() + old_size;

                    for(int i = 0; i < bytes_ready; i++){
                        int byte_shift = (bytes_ready - 1 - i) * 8;  
                        dst[i] = (to_write >> byte_shift) & 0xFF;
                    }

                    bits_in_acc = remainder;
                    acc &= (1ULL << remainder) - 1;
                }
            }

            if(bits_in_acc > 0)
                encoded.push_back(static_cast<uint8_t>(acc << (8 - bits_in_acc)));

            // Writing 4 byte integer which represents valid huffman bits so that decoder can parse until valid bits 
            // and skip padded bits.

            encoded[0] = (bit_count >> 24) & 0xFF;
            encoded[1] = (bit_count >> 16) & 0xFF;
            encoded[2] = (bit_count >>  8) & 0xFF;
            encoded[3] = (bit_count >>  0) & 0xFF;

            // move directly into chunk — no copy needed since encoded is local
            buffer.submit_chunk(Chunk(id, std::move(encoded)));
        });
    }

    pool.shutdown();

    bw.flush();
}

/*
void Coordinator::decompress(const std::string &input_file, const std::string &dump_file){

    BitReader br(input_file);
    std::array<uint8_t,256> lengths{};
    read_lengths(lengths, br);

    std::array<HuffmanCode,256> table{};
    generate_canonical_table(lengths, table);

    auto root = std::unique_ptr<Node>(build_decode_tree(table));
    DecodeLUT lut;
    build_decode_lut(table, root.get(), lut);

    uint32_t total_len = read_uint32(br);
    uint32_t total_chunks = read_uint32(br);

    //skipping over padded bytes which can cause havoc :/
    br.align_to_byte();

    std::ofstream out;
    BitWriter bw(out);

    const size_t BUF_SIZE = 64 * 1024;
    std::vector<char> internal_buffer(BUF_SIZE);



    //instead of hammering system calls with doing out.write() for every byte, using a internal_buffer of 64KB.
    // where it just writes the whole 64kb at a time to reduce system calls. 
    out.rdbuf()->pubsetbuf(internal_buffer.data(), BUF_SIZE);
    out.open(dump_file, std::ios::binary);

    // reuse ChunkBuffer from compress path — handles out-of-order chunk arrivals
    // from the thread pool and writes them to disk in correct order.
    ChunkBuffer chunk_buf(out);

    for(uint32_t c = 0; c < total_chunks; c++){

        //read the 4 bytes bit_count , which contains total valid huffman bits and only consumes/constructs until that number
        //of bits, rest of the bits are just padded as we cant write half bytes to the file.
        uint32_t bit_count = 0;
        bit_count |= (uint32_t)br.read_byte() << 24;
        bit_count |= (uint32_t)br.read_byte() << 16;
        bit_count |= (uint32_t)br.read_byte() << 8;
        bit_count |= (uint32_t)br.read_byte();

        uint32_t byte_count = (bit_count + 7) / 8;
        std::vector<uint8_t> raw(byte_count);
        for(uint32_t i = 0; i < byte_count; i++)
            raw[i] = br.read_byte();

        int id = c;

        pool.submit([raw = std::move(raw), bit_count, id, total_len,
                     &chunk_buf, &lut, &root]() mutable {

            std::vector<uint8_t> decoded;
            decoded.reserve(bit_count / 4);

            uint32_t byte_pos   = 0;
            uint8_t  bit_buf    = 0;
            int      bits_avail = 0;

            auto read_bit = [&]() -> int {
                if(bits_avail == 0){
                    if(byte_pos >= raw.size()) return -1;
                    bit_buf    = raw[byte_pos++];
                    bits_avail = 8;
                }
                bits_avail--;
                return (bit_buf >> bits_avail) & 1;
            };

            // peek n bits without consuming — saves and restores reader state
            auto peek_bits = [&](int n) -> uint32_t {
                uint32_t result      = 0;
                uint32_t saved_pos   = byte_pos;
                uint8_t  saved_buf   = bit_buf;
                int      saved_avail = bits_avail;


                for(int i = 0; i < n; i++){
                    int b = read_bit();
                    if(b == -1) break;
                    result = (result << 1) | b;
                }
                byte_pos   = saved_pos;
                bit_buf    = saved_buf;
                bits_avail = saved_avail;
                return result;
            };

            auto consume_bits = [&](int n){
                for(int i = 0; i < n; i++) read_bit();
            };

            Node* curr = root.get();
            uint32_t bits_read = 0;
            uint32_t produced  = 0;

            while(bits_read < bit_count && produced < total_len){

                uint32_t bits_remaining = bit_count - bits_read;

                // if fewer bits left than LUT_BITS, fall back to bit-by-bit tree walk
                if(bits_remaining < LUT_BITS){

                    Node* node = root.get();
                    while(node->left || node->right){
                        if(bits_read >= bit_count)
                            throw std::runtime_error("corrupt: ran out of bits mid-symbol");
                        int bit = read_bit();
                        if(bit == -1)
                            throw std::runtime_error("unexpected EOF");
                        bits_read++;
                        node = bit ? node->right.get() : node->left.get();
                    }
                    decoded.push_back(node->ch);
                    produced++;
                    continue;
                }

                uint32_t peek = peek_bits(LUT_BITS);
                auto& entry = lut[peek];

                if(entry.is_leaf){
                    consume_bits(entry.bits);
                    bits_read += entry.bits;

                    decoded.push_back(entry.symbol);
                    produced++;
                }
                else{
                    consume_bits(LUT_BITS);
                    bits_read += LUT_BITS;

                    Node* node = entry.next;
                    while(node->left || node->right){
                        int bit = read_bit();
                        if(bit == -1)
                            throw std::runtime_error("corrupt compressed file");
                        bits_read++;
                        node = bit ? node->right.get() : node->left.get();
                    }
                    decoded.push_back(node->ch);
                    produced++;
                }
            }

            chunk_buf.submit_chunk(Chunk(id, std::move(decoded)));
        });
    }

    pool.shutdown();
    out.close();
}
*/



