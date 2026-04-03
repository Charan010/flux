#include <array>
#include <vector>
#include <thread>
#include <fstream>
#include <cstdint>

using FrequencyTable = std::array<uint64_t, 256>;


void count_range_chunked(const std::string& file,size_t start, size_t size, FrequencyTable& freq, size_t chunk_size) {

    std::ifstream in(file, std::ios::binary);
    in.seekg(start);

    std::vector<uint8_t> buffer(chunk_size);

    size_t remaining = size;

    while (remaining > 0) {

        size_t to_read = std::min(chunk_size, remaining);

        in.read(reinterpret_cast<char*>(buffer.data()), to_read);
        size_t read = in.gcount();

        if (read == 0)
            break;

        for (size_t i = 0; i < read; ++i) {
            freq[buffer[i]]++;
        }

        remaining -= read;
    }
}

FrequencyTable compute_frequency(const std::string& file, size_t chunk_size = 1 << 20) {
    
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("Cannot open file");

    FrequencyTable freq{};
    freq.fill(0);

    std::vector<uint8_t> buffer(chunk_size);
    while (in.read(reinterpret_cast<char*>(buffer.data()), chunk_size) || in.gcount() > 0) {
        size_t read = in.gcount();
        for (size_t i = 0; i < read; ++i) {
            freq[buffer[i]]++;
        }
    }

    return freq;
}