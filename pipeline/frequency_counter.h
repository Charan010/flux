#pragma once

#include <array>
#include <string>
#include <cstdint>

using FrequencyTable = std::array<uint64_t, 256>;

class FrequencyCounter {
public:

   
    static FrequencyTable compute_frequency(const std::string& input_file,size_t chunk_size = 1 << 20);

private:

    static void count_range_chunked(const std::string& file, size_t start, size_t size, FrequencyTable& freq,
        size_t chunk_size);
};