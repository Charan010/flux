#pragma once
#include <fstream>
#include <cstdint>

struct BitWriter {

    public:

    std::ofstream out;
    uint8_t buffer = 0;
    int bits = 0;

    BitWriter(const std::string& file);
    void write_bit(int b);
    void write_byte(uint8_t b);
    void flush();
    
};

struct BitReader {
    std::ifstream in;
    uint8_t buffer = 0;
    int bits = 0;

    BitReader(const std::string& file);
    int read_bit();
    uint8_t read_byte();
    void align_to_byte();

};