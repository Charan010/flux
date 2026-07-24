#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

enum class CodecId : uint8_t {
    Raw  = 0,  
    Zstd = 1,
};

const char *codec_name(CodecId codec);

CodecId codec_compress(const uint8_t *input, size_t input_size,
                       std::vector<uint8_t> &output);

void codec_decompress(CodecId codec, const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size);