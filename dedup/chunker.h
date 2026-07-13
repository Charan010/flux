#pragma once

#include <cstdint>
#include <deque>
#include <istream>
#include <vector>

struct Chunk{
	std::vector<uint8_t> bytes;
};

class Chunker{
public:
	Chunker(std::istream &input, size_t window = 4, uint64_t base = 257, uint64_t mask = 0x03);
	bool next_chunk(Chunk &chunk);


private:
    std::istream& input_;

    size_t windowSize_;
    uint64_t base_;
    uint64_t mask_;
    uint64_t highestPower_;

    std::deque<uint8_t> window_;
    uint64_t rolling_hash_;

    bool eof_;
};