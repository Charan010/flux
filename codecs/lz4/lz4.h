#pragma once

#include <cstddef>
#include <cstdint>


/*
 * Returns the upper bound compressed size of data to pre allocate buffer memory to avoid memory
 allocation overhead.
*/
size_t lz4_compress_bound(size_t input_size);

size_t lz4_compress(const uint8_t *__restrict input, size_t input_length,
                    uint8_t *output);

size_t lz4_decompress(const uint8_t *input, size_t input_len, uint8_t *output);