#include "bit_io.h"
#include <cassert>
#include <cstring>

using namespace std;

BitWriter::BitWriter(const std::string &file)
    : io_buf(std::make_unique<char[]>(BUF_SIZE)), out(file, std::ios::binary) {
  out.rdbuf()->pubsetbuf(io_buf.get(), BUF_SIZE);
}

void BitWriter::write_bits(uint64_t bits, int count) {
  if (count == 0)
    return;

  if (count < 64)
    bits &= (1ULL << count) - 1;

  if (bits_in_acc + count > 64) {
    int room = 64 - bits_in_acc;
    write_bits(bits >> (count - room), room);
    write_bits(bits, count - room);
    return;
  }

  acc = (acc << count) | bits;
  bits_in_acc += count;

  // Drain 8 bytes at once when possible
  if (bits_in_acc >= 64) {
    uint8_t tmp[8];
    int t = 0;
    while (bits_in_acc >= 8) {
      bits_in_acc -= 8;
      tmp[t++] = static_cast<uint8_t>((acc >> bits_in_acc) & 0xFF);
    }
    out.write(reinterpret_cast<char *>(tmp), t);
  } else {
    uint8_t tmp[8];
    int t = 0;
    while (bits_in_acc >= 8) {
      bits_in_acc -= 8;
      tmp[t++] = static_cast<uint8_t>((acc >> bits_in_acc) & 0xFF);
    }
    if (t > 0)
      out.write(reinterpret_cast<char *>(tmp), t);
  }

  if (bits_in_acc > 0)
    acc &= (1ULL << bits_in_acc) - 1;
  else
    acc = 0;
}

void BitWriter::write_bit(int b) {
  assert(b == 0 || b == 1);
  write_bits(b, 1);
}

void BitWriter::write_byte(uint8_t b) { write_bits(b, 8); }

void BitWriter::flush() {
  if (bits_in_acc > 0) {
    uint8_t byte = static_cast<uint8_t>((acc << (8 - bits_in_acc)) & 0xFF);
    out.put(static_cast<char>(byte));
  }
  acc = 0;
  bits_in_acc = 0;
  out.flush();
}

void BitWriter::write_bytes(const std::vector<uint8_t> &data) {
  assert(bits_in_acc == 0 && "write_bytes called with unflushed bits");
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

BitReader::BitReader(const uint8_t *ptr, size_t len) : data(ptr), size(len) {}

int BitReader::read_bit() {
  refill();
  if (bits_in_buf == 0)
    return -1;
  int bit = static_cast<int>((bitbuf >> (bits_in_buf - 1)) & 1);
  bits_in_buf--;
  return bit;
}

uint8_t BitReader::read_byte() {
  refill();
  if (bits_in_buf < 8)
    throw std::runtime_error("unexpected EOF");
  uint8_t val = static_cast<uint8_t>((bitbuf >> (bits_in_buf - 8)) & 0xFF);
  bits_in_buf -= 8;
  return val;
}

void BitReader::align_to_byte() {
  bits_in_buf = 0;
  bitbuf = 0;
}

void BitReader::read_bytes(uint8_t *dst, size_t n) {
  align_to_byte();
  if (byte_pos + n > size)
    throw std::runtime_error("unexpected EOF");
  memcpy(dst, data + byte_pos, n);
  byte_pos += n;
}

void write_uint32(BitWriter &bw, uint32_t x) {
  bw.write_byte((x >> 24) & 0xFF);
  bw.write_byte((x >> 16) & 0xFF);
  bw.write_byte((x >> 8) & 0xFF);
  bw.write_byte(x & 0xFF);
}
