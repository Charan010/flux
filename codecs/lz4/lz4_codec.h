#pragma once

#include <cstddef>
#include <cstdint>

#include "lz4.h"

class LZ4Codec {
public:
    static size_t compress_bound(size_t input_size) {
        return input_size + (input_size / 255) + 16;
    }

    static size_t compress(const uint8_t *input, size_t input_size, uint8_t *output) {
        return lz4_compress(input, input_size, output);
    }

    static size_t decompress(const uint8_t *input, size_t input_size, uint8_t *output) {
        return lz4_decompress(input, input_size, output);
    }
};
