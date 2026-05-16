#include "LZ4_engine.h"

#include <algorithm>
#include <vector>

#include "chunk.h"
#include "lz4.h"

size_t lz4_compress_bound(size_t input_size){
    return input_size + (input_size / 255) + 16;
}

void LZ4Engine::encode_chunk(const uint8_t *input, size_t input_size, Chunk &output){

    thread_local std::vector<uint8_t> encoded;

    encoded.clear();

    encoded.resize(lz4_compress_bound(input_size));

    const size_t compressed_size = lz4_compress(input, input_size, encoded.data());

    encoded.resize(compressed_size);

    output.original_size = static_cast<uint32_t>(input_size);
    output.bit_count = 0;

    output.data.swap(encoded);
}


void LZ4Engine::decode_chunk(const uint8_t *input, size_t input_size, Chunk &output){

    thread_local std::vector<uint8_t> decoded;
    decoded.clear();

    decoded.resize(output.original_size);

    const size_t decoded_size = lz4_decompress(input, input_size, decoded.data());
    decoded.resize(decoded_size);

    output.data.swap(decoded);

}


void LZ4Engine::prepare_encoder(const uint8_t *input, size_t size ){

    /*
        Doing nothing because there is nothing like table or building tree like huffman in lz4.
        So,i have added this function just to match the same process for huffman.
    
    */
}


void LZ4Engine::prepare_decoder(const uint8_t* header_data, size_t& pos){

    /*
       Same as prepare_encoder :P
    */
}