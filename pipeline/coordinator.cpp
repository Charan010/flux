#include "coordinator.h"
#include "chunk.h"

#include <fstream>
#include <vector>
#include <chrono>
#include <iostream>

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

    Node* root = build_huffman_tree(freq);
    std::array<uint8_t,256> lengths{};
    compute_lengths(root , 0 , lengths);

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

        std::vector<uint8_t> data(chunk_size);
        in2.read((char*)data.data(), chunk_size);
        size_t read = in2.gcount();

        if(read == 0)
            break;

        data.resize(read);

        int id = chunk_id++;

        pool.submit([data, id, &buffer, tbl]() mutable {

            uint64_t acc = 0;
            int bits_in_acc = 0;
            uint32_t bit_count = 0;

      
            std::vector<uint8_t> encoded;
            encoded.reserve(data.size() + 4);

            //reserving data.size() because worst case scenario. The maximum size required to encode data is
            // data.size() * 8 bytes and + 4 for adding how many bits are written in the encoded vector.

            encoded.push_back(0);
            encoded.push_back(0);
            encoded.push_back(0);
            encoded.push_back(0);

            for(uint8_t c : data){

                const HuffmanCode& code = (*tbl)[c];

                // making place for the new variable code in the accumulator.
                acc = (acc << code.len) | code.bits;
                bits_in_acc += code.len;
                bit_count += code.len;

                
            // In a 64 bit accumulator, if I have more than one byte, then i'm ready to start pushing these bytes into encoded vector.
            // bytes_ready represents how many bytes can be added to encoded vector and shift represents the remaining left
            // out bits.

                while(bits_in_acc >= 8){
                    int bytes_ready = bits_in_acc / 8;
                    int shift = bits_in_acc % 8;
                    uint64_t to_write = acc >> shift;

                    size_t old_size = encoded.size();
                    encoded.resize(old_size + bytes_ready);
                    uint8_t* dst = encoded.data() + old_size;

                    for(int i = 0; i < bytes_ready; i++){
                        int shift = (bytes_ready - 1 - i) * 8;
                        dst[i] = (to_write >> shift) & 0xFF;
                    }

                //mask and apply AND to preserve only the remaining bits in the accumulator.
                    bits_in_acc = shift;
                    acc &= (1ULL << shift) - 1;
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

            buffer.submit_chunk(Chunk(id, std::move(encoded)));
        });
    }

    pool.shutdown();


    buffer.flush_ready_chunks();
    bw.flush();
 
}



void Coordinator::decompress(const std::string &input_file, const std::string &dump_file){


    BitReader br(input_file);
    std::array<uint8_t,256> lengths{};
    read_lengths(lengths, br);

    std::array<HuffmanCode,256> table{};
    generate_canonical_table(lengths, table);

    auto root = std::unique_ptr<Node>(build_decode_tree(table));

    uint32_t total_len = read_uint32(br);
    uint32_t total_chunks = read_uint32(br);

    //skipping over padded bytes which can cause havoc :/
    br.align_to_byte();

    std::ofstream out;

    const size_t BUF_SIZE = 64 * 1024;
    std::vector<char> internal_buffer(BUF_SIZE);

    //instead of hammering system calls with doing out.write() for every byte, using a internal_buffer of 64KB.
    // where it just writes the whole 64kb at a time to reduce system calls. 

    out.rdbuf()->pubsetbuf(internal_buffer.data(), BUF_SIZE);

    out.open(dump_file, std::ios::binary);

    uint32_t produced = 0;

    for(uint32_t c = 0; c < total_chunks; c++){


        //read the 4 bytes bit_count , which contains total valid huffman bits and only consumes/constructs until that number
        //of bits, rest of the bits are just padded as we cant write half bytes to the file.

        uint32_t bit_count = 0;
        bit_count |= (uint32_t)br.read_byte() << 24;
        bit_count |= (uint32_t)br.read_byte() << 16;
        bit_count |= (uint32_t)br.read_byte() << 8;
        bit_count |= (uint32_t)br.read_byte();


         Node* curr = root.get();
        uint32_t bits_read = 0;
        while(bits_read < bit_count && produced < total_len){

            int bit = br.read_bit();

            if(bit == -1)
                throw std::runtime_error("corrupt compressed file");

            bits_read++;

            curr = (bit == 0) ? curr->left.get() : curr->right.get(); 

            if(!curr->left && !curr->right){
                out.put(static_cast<char>(curr->ch));
                curr = root.get();
                produced++;
            }
        }

        br.align_to_byte();
    }

    out.close();
}