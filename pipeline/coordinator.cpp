#include "coordinator.h"
#include <iostream>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "progress_bar.h"
#include "progress_renderer.h"

Coordinator::Coordinator(size_t threads, size_t chunk) :
     pool(threads), chunk_size(chunk) {}

Coordinator::~Coordinator() {
    pool.shutdown();
}
    

void Coordinator::encode_chunk(const uint8_t* data, size_t len, int id, const std::array<HuffmanCode, 256>* tbl,
     ChunkBuffer* buffer) {
    
    thread_local std::vector<uint8_t> encoded;
    encoded.clear();

    if (encoded.capacity() < (len + 64)) {
        encoded.reserve(len * 2); 
    }

    size_t pos = 0;
    encoded.clear();

    uint64_t acc = 0;
    int bits_in_acc = 0;
    uint32_t bit_count = 0;

    for (size_t i = 0; i < len; ++i) {
        const auto& code = (*tbl)[data[i]];
        bit_count += code.len;

        acc = (acc << code.len) | code.bits;
        bits_in_acc += code.len;

        while (bits_in_acc >= 8) {
            bits_in_acc -= 8;
            encoded[pos++] = static_cast<uint8_t>((acc >> bits_in_acc) & 0xFF);
            
            if (pos >= encoded.size())
                encoded.resize(encoded.size() * 2);
        }
    }

    if (bits_in_acc > 0) {
        encoded[pos++] = static_cast<uint8_t>(acc << (8 - bits_in_acc));
    }

    std::vector<uint8_t> out(encoded.begin(), encoded.begin() + pos);
    
    buffer->submit_chunk(Chunk(id, bit_count, std::move(out)));
}


uint8_t* map_file(const std::string &input_file, int &fd, size_t &file_size){

    fd = open(input_file.c_str(), O_RDONLY);

    if(fd < 0)
        throw std::runtime_error("cannot open the file");
    
    struct stat st;
    fstat(fd, &st);

    file_size = st.st_size;

    uint8_t* file_ptr = static_cast<uint8_t*>(mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0));

    if(file_ptr == MAP_FAILED){
        close(fd);
        throw std::runtime_error("mmap failed");
    }

    /* Hints to the operating system that i would be fetchings pages sequentially. So, OS starts fetching
        next pages asynchronously.
    */
    madvise(file_ptr, file_size, MADV_SEQUENTIAL);
    return file_ptr;

}


FrequencyTable compute_frequency(uint8_t *file_ptr, size_t &file_size){
    
    FrequencyTable table{};
    table.fill(0);

    /* This helps CPU to apply loop unrolling and as there is no dependency chain in the results.
        CPU can parallelize the instructions.
    */
    FrequencyTable freq0{}, freq1{}, freq2{}, freq3{};

    size_t i = 0;
    for(; i + 4<= file_size; i += 4){
            freq0[file_ptr[i]]++;
            freq1[file_ptr[i+1]]++;
            freq2[file_ptr[i+2]]++;
            freq3[file_ptr[i+3]]++;
    }

    for (; i < file_size; ++i) 
        freq0[file_ptr[i]]++;

    for (int j = 0; j < 256; ++j)
        table[j] = freq0[j] + freq1[j] + freq2[j] + freq3[j];

    return table;
}


void Coordinator::compress(const std::string &input_file, const std::string &output_file) {

    int fd;
    size_t file_size;

    uint8_t *file_ptr = map_file(input_file, fd, file_size);

    if (file_size == 0) {
        BitWriter bw(output_file);
        std::array<uint8_t, 256> empty_lengths{};
        write_lengths(empty_lengths, bw);
        write_uint32(bw, 0); // total_len
        write_uint32(bw, 0); // total_chunks
        bw.flush();
        munmap(file_ptr, file_size);
        close(fd);
        return;
    }

    FrequencyTable freq = compute_frequency(file_ptr, file_size);
    int total_chunks = (file_size + chunk_size - 1) / chunk_size;

    std::vector<Node> encode_storage;
    Node* root = build_huffman_tree(freq, encode_storage);

    std::array<uint8_t, 256> lengths{};
    compute_lengths(root, 0, lengths);

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

    ProgressBar progress(total_chunks, file_size);
    ProgressRenderer renderer(progress );

    
    ChunkBuffer buffer(bw, total_chunks, &progress, true);
    const auto* tbl = &table;

    int chunk_id = 0;

    for (size_t offset = 0; offset < file_size; offset += chunk_size) {

        size_t current_chunk_size = std::min(chunk_size, file_size - offset);
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

 /* Peek LUT_BITS from the stream of data and check if there is any valid character mapping.
            currently LUT_BITS is 15. so, most characters can be decoded by using LUT table. instead of
            traversing through tree bit by bit.

            Because LUT_BITS is more, the chance of needing the traversal bit by bit is very rare. which is ok :P
        
*/


void Coordinator::decode_chunk(const uint8_t* encoded, size_t encoded_size, uint32_t bit_count,
    const DecodeLUT& primary, const SecondaryLUT& secondary, ChunkBuffer* buffer, int id) {


    thread_local std::vector<uint8_t> decoded;
    decoded.clear();

    uint32_t bits_read = 0;
    BitReader reader(encoded, encoded_size);

    while(bits_read < bit_count){

        const uint32_t idx = reader.peek_bits(LUT_BITS);

        const auto &entry = primary[idx];

        if(entry.flags & ENTRY_SYMBOL){

            reader.consume_bits(entry.bits);
            bits_read += entry.bits;

            decoded.push_back(static_cast<uint8_t>(entry.value));

        }

        else {

            reader.consume_bits(LUT_BITS);

            const uint32_t extra = reader.peek_bits(entry.bits);
            const auto &sub = secondary[entry.value + extra];

            assert(entry.value + extra < secondary.size());

            assert(sub.bits >= LUT_BITS);
            const uint8_t suffix_bits = sub.bits - LUT_BITS;
            reader.consume_bits(suffix_bits);


            bits_read += sub.bits;
            decoded.push_back(static_cast<uint8_t>(sub.value));

        }
    }

    std::vector<uint8_t> out = decoded;
    buffer->submit_chunk(Chunk(id, static_cast<uint32_t>(out.size()), std::move(out)));

}


void Coordinator::decompress(const std::string& input, const std::string& output) {

    int fd;
    size_t file_size;


    uint8_t *file_ptr = map_file(input, fd, file_size);
    size_t pos = 0;

    std::array<uint8_t, 256> lengths{};
    memcpy(lengths.data(), file_ptr + pos, 256);
    pos += 256;

   auto read_u32 = [&]() {

        uint32_t value =
            (static_cast<uint32_t>(file_ptr[pos]) << 24)
          | (static_cast<uint32_t>(file_ptr[pos + 1]) << 16)
          | (static_cast<uint32_t>(file_ptr[pos + 2]) << 8)
          | static_cast<uint32_t>(file_ptr[pos + 3]);

        pos += 4;

        return value;
    };


    uint32_t total_len    = read_u32();
    uint32_t total_chunks = read_u32();


    ProgressBar progress(total_chunks, total_len);
    ProgressRenderer renderer(progress );


    std::array<HuffmanCode, 256> table{};
    generate_canonical_table(lengths, table);


    DecodeLUT lut{};
    SecondaryLUT secondary;

    build_decode_lut(table, lut, secondary);

    BitWriter bw(output);
    ChunkBuffer buffer(bw, total_chunks, &progress, false);


    for (uint32_t id = 0; id < total_chunks; id++) {

        if (pos + 4 > file_size) {
           fprintf(stderr, "Error: Unexpected EOF while reading chunk %u header\n", id);
            break;
       }


         uint32_t bit_count = 
        (static_cast<uint32_t>(file_ptr[pos])     << 24) |
        (static_cast<uint32_t>(file_ptr[pos + 1]) << 16) |
        (static_cast<uint32_t>(file_ptr[pos + 2]) << 8)  |
         static_cast<uint32_t>(file_ptr[pos + 3]); 

        
         pos += 4;

        uint32_t byte_size = (bit_count + 7) / 8;

        if (pos + byte_size > file_size) {
            fprintf(stderr, "Error: Chunk %u (size %u) exceeds file boundaries\n", id, byte_size);
            break;
        }
        const uint8_t* chunk_ptr = file_ptr + pos;
        pos += byte_size;


        pool.submit([this, chunk_ptr, byte_size, bit_count, &buffer, &lut, &secondary, id](){
            decode_chunk(chunk_ptr, byte_size, bit_count,lut,secondary,&buffer, id);
        });

    }

    pool.wait();
    buffer.finish();
    bw.flush();

    munmap(file_ptr, file_size);
    close(fd);

}