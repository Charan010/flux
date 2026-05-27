#pragma once
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

struct BitWriter {
  // 8 MiB I/O buffer — fewer fwrite syscalls
  static constexpr size_t BUF_SIZE = 8 * 1024 * 1024;
  std::unique_ptr<char[]> io_buf;

  uint64_t acc = 0;
  int bits_in_acc = 0;
  std::ofstream out;

  BitWriter(const std::string &file);
  void write_bits(uint64_t bits, int count);
  void write_bit(int b);
  void write_byte(uint8_t b);
  void flush();
  void write_bytes(const std::vector<uint8_t> &data);
};

struct BitReader {

  const uint8_t *data;
  size_t size;

  size_t byte_pos = 0;

  // 128-bit buffer — holds up to 16 bytes; keeps refill calls rare
  __uint128_t bitbuf = 0;
  int bits_in_buf = 0;

  BitReader(const uint8_t *ptr, size_t len);

  // Bulk-load up to 8 bytes at once when buffer is low
  inline void refill() {

    if (bits_in_buf > 56)
      return;

    while (bits_in_buf <= 56 && byte_pos + 8 <= size) {
      uint64_t word;
      __builtin_memcpy(&word, data + byte_pos, 8);
      word = __builtin_bswap64(word);
      bitbuf = (bitbuf << 64) | static_cast<__uint128_t>(word);
      bits_in_buf += 64;
      byte_pos += 8;
    }
    // mop up remaining bytes one at a time
    while (bits_in_buf <= 120 && byte_pos < size) {
      bitbuf = (bitbuf << 8) | data[byte_pos++];
      bits_in_buf += 8;
    }
  }

  inline uint32_t peek_bits(int n) {

    refill();

    if (bits_in_buf < n) {

      const int missing = n - bits_in_buf;

      return static_cast<uint32_t>((bitbuf << missing) & ((1ULL << n) - 1));
    }

    return static_cast<uint32_t>((bitbuf >> (bits_in_buf - n)) &
                                 ((1ULL << n) - 1));
  }

  inline void consume_bits(int n) { bits_in_buf -= n; }

  int read_bit();
  uint8_t read_byte();
  void align_to_byte();
  void read_bytes(uint8_t *dst, size_t n);
};
