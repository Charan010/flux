#pragma once
#include <cstddef>
#include <cstdint>

namespace Config{

    const constexpr size_t threadpool_size = 8;
    const constexpr size_t chunk_size = 4 << 20; 

	//Changing the magic would break version of older files and wouldnt be parsed.
    static constexpr uint8_t MAGIC[4]  = {'F', 'L', 'U', 'X'};
    static constexpr uint8_t CODEC_HUFFMAN = 0;
    static constexpr uint8_t CODEC_LZ4 = 1;


}