#pragma once

#include <cstddef>
#include <cstdint>

/*
    Returns maximum possible compressed size
    for an input buffer of size input_size.

    Used to safely preallocate output buffers.
*/
size_t lz4_compress_bound(size_t input_size);

size_t lz4_compress(const uint8_t *__restrict input, size_t input_length,
                    uint8_t *output);

size_t lz4_decompress(const uint8_t *input, size_t input_len, uint8_t *output);