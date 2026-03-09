#include "bit_io.h"


BitWriter::BitWriter(const std::string& file)
    : out(file, std::ios::binary) {
    out.rdbuf()->pubsetbuf(io_buf, sizeof(io_buf));  
}

void BitWriter::write_bits(uint64_t bits, int count) {
    acc = (acc << count) | (bits & ((1ULL << count) - 1));
    bits_in_acc += count;

    int bytes_ready = bits_in_acc / 8;
    if (bytes_ready > 0) {
        int shift = bits_in_acc - (bytes_ready * 8);
        uint64_t to_write = acc >> shift;


        uint8_t buf[8];
        for (int i = bytes_ready - 1; i >= 0; i--) {
            buf[i] = to_write & 0xFF;
            to_write >>= 8;
        }
        out.write(reinterpret_cast<const char*>(buf), bytes_ready);

        bits_in_acc = shift;
        acc &= (1ULL << shift) - 1;
    }
}

void BitWriter::write_bit(int b) {
    write_bits(b, 1);
}

void BitWriter::write_byte(uint8_t b) {
    write_bits(b, 8);
}

void BitWriter::flush() {
    if (bits_in_acc > 0) {
        out.put(static_cast<uint8_t>((acc << (8 - bits_in_acc)) & 0xFF));
        acc = 0;
        bits_in_acc = 0;
    }
    out.flush();  
}


BitReader::BitReader(const std::string& file)
    : in(file, std::ios::binary) {
    in.rdbuf()->pubsetbuf(io_buf, sizeof(io_buf));  
}

int BitReader::read_bit() {
    if (bits == 0) {
        int c = in.get();
        
        if (c == EOF)
            return -1;
        buffer = static_cast<uint8_t>(c);
        bits = 8;
    }

    int bit = (buffer >> 7) & 1;
    buffer <<= 1;
    bits--;
    return bit;
}

uint8_t BitReader::read_byte() {
    if (bits == 0)
        return static_cast<uint8_t>(in.get());

    uint8_t b = buffer >> (8 - bits);
    int need = 8 - bits;             

    int c = in.get();
    if (c == EOF) throw std::runtime_error("unexpected EOF");
    buffer = static_cast<uint8_t>(c);

    b = (b << need) | (buffer >> bits);
    buffer <<= need;                   

    return b;
}

void BitReader::align_to_byte() {
    bits = 0;
    buffer = 0;
}