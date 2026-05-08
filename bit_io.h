#pragma once
#include <fstream>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <memory>

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

    uint64_t bitbuf = 0;
    int bits_in_buf = 0;

    BitReader(const uint8_t* ptr, size_t len);

    void refill();
    uint32_t peek_bits(int n);
    void consume_bits(int n);
    int read_bit();
    uint8_t read_byte();
    void align_to_byte();
    void read_bytes(uint8_t* dst, size_t n);

};
