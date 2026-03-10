#pragma once
#include <fstream>
#include <cstdint>
#include <stdexcept>

struct BitWriter {
    static constexpr size_t BUF_SIZE = 1 * 1024 * 1024; 
    char io_buf[BUF_SIZE];

    uint64_t acc = 0;
    int bits_in_acc = 0;
    std::ofstream out;

    BitWriter(const std::string& file);
    void write_bits(uint64_t bits, int count);
    void write_bit(int b);
    void write_byte(uint8_t b);
    void flush();
};


struct BitReader {
    static constexpr size_t BUF_SIZE = 1 * 1024 * 1024;  
    char io_buf[BUF_SIZE];

    std::ifstream in;
    uint64_t bitbuf = 0;
    int bits_in_buf = 0;

    BitReader(const std::string& file);
    void refill(int needed);
    uint32_t peek_bit(int n);
    void consume_bits(int n);
    int read_bit();
    uint8_t read_byte();
    void align_to_byte();
};
