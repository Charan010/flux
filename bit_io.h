#pragma once
#include <fstream>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <memory>
#include <cstring>

struct BitWriter {
    std::unique_ptr<char[]> io_buf;
    static constexpr size_t BUF_SIZE = 4 * 1024 * 1024;

    uint64_t acc = 0;
    int bits_in_acc = 0;
    std::ofstream out;

    BitWriter(const std::string& file);
    void write_bits(uint64_t bits, int count);
    void write_bit(int b);
    void write_byte(uint8_t b);
    void flush();
    void write_bytes(const std::vector<uint8_t>& data);
};


struct BitReader {
    
    const uint8_t* data;
    size_t size;

    size_t byte_pos = 0;

    __uint128_t bitbuf = 0;
    int bits_in_buf = 0;

    BitReader(const uint8_t* ptr, size_t len);

    inline void refill() {
        while(bits_in_buf <=  120 && byte_pos < size) {
            bitbuf = (bitbuf << 8) | data[byte_pos++];
            bits_in_buf += 8;
        }
    }

    inline uint32_t peek_bits(int n) {
        if(n == 0)
            return 0;

        refill();
        if(bits_in_buf < n)
            return static_cast<uint32_t>((bitbuf << (n - bits_in_buf)) & ((((__uint128_t)1 << n) - 1)) - 1));
        return static_cast<uint32_t>((bitbuf >> (bits_in_buf - n)) & ((((__uint128_t)1 << n) - 1));
    }

    inline void consume_bits(int n) {
        bits_in_buf -= n;
    }


    int read_bit();
    uint8_t read_byte();
    void align_to_byte();
    void read_bytes(uint8_t* dst, size_t n);

};
