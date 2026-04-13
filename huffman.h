#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <cstdint>
#include <array>
#include <queue>
#include <string>
#include <memory>

#include "bit_io.h"


struct Node {
    uint8_t ch;
    uint64_t freq;
    std::unique_ptr<Node> left, right;

    Node(std::unique_ptr<Node> l, std::unique_ptr<Node> r);
    Node(uint8_t c, uint64_t f);
    Node();
};


struct HuffmanCode {
    uint64_t bits = 0;
    uint8_t len = 0;
};

struct DecodeEntry{
    uint8_t symbol;
    uint8_t bits;

    Node* next;
    bool is_leaf;
};

constexpr int LUT_BITS = 12;
constexpr int LUT_SIZE = 1 << LUT_BITS;


using FrequencyTable = std::array<uint64_t,256>;
using DecodeLUT = std::array<DecodeEntry, LUT_SIZE>;


struct Compare {
    bool operator()(Node* a, Node* b);
};

Node* build_huffman_tree(const FrequencyTable& freq);

void build_codes(Node* root , uint64_t code_bits, uint8_t depth ,std::array<HuffmanCode, 256> &table);

void write_lengths(const std::array<uint8_t,256>& lengths, BitWriter& bw);
void read_lengths(std::array<uint8_t,256>& lengths, BitReader& br);

void free_tree(Node* root);

uint32_t read_uint32(BitReader& br);
void write_uint32(BitWriter& bw, uint32_t x);

void compute_lengths(Node* root , uint8_t depth, std::array<uint8_t, 256>&lengths);
void generate_canonical_table(const std::array<uint8_t , 256>&lengths, std::array<HuffmanCode, 256>&table);

Node* build_decode_tree(const std::array<HuffmanCode,256>& table);

void build_decode_lut(const std::array<HuffmanCode,256>& table, Node* root, DecodeLUT& lut);

#endif