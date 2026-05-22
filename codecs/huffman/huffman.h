#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <queue>
#include <string>

#include "bit_io.h"

struct Node {

    uint8_t ch;
    uint64_t freq;

    Node* left;
    Node* right;

    Node(Node* l, Node* r);
    Node(uint8_t c, uint64_t f);
    Node();
};

struct FlatNode {
    uint16_t left;
    uint16_t right;

    uint8_t symbol;
    bool is_leaf;
};

struct HuffmanCode {
    uint64_t bits;
    uint8_t len;
};

struct DecodeEntry {

    uint16_t value;
    uint8_t bits;
    uint8_t flags;
};

struct SubtableInfo {

    uint32_t offset;
    uint8_t bits;
};

constexpr uint8_t ENTRY_SYMBOL = 1;
constexpr uint8_t ENTRY_SUBTABLE = 2;

constexpr int LUT_BITS = 11;
constexpr int LUT_SIZE = 1 << LUT_BITS;

using FrequencyTable = std::array<uint64_t, 256>;
using DecodeLUT = std::array<DecodeEntry, LUT_SIZE>;
using FlatTree = std::vector<FlatNode>;
using SecondaryLUT = std::vector<DecodeEntry>;

struct Compare {
    bool operator()(Node* a, Node* b);
};

Node* build_huffman_tree(const FrequencyTable& freq, std::vector<Node>& storage);
void write_lengths(const std::array<uint8_t, 256>& lengths, BitWriter& bw);
void read_lengths(std::array<uint8_t, 256>& lengths, BitReader& br);

uint32_t read_uint32(BitReader& br);
void write_uint32(BitWriter& bw, uint32_t x);
void compute_lengths(Node* root, uint8_t depth, std::array<uint8_t, 256>& lengths);
void generate_canonical_table(const std::array<uint8_t, 256>& lengths,
                              std::array<HuffmanCode, 256>& table);

Node* build_decode_tree(const std::array<HuffmanCode, 256>& table, std::vector<Node>& storage);

void build_decode_lut(const std::array<HuffmanCode, 256>& table, DecodeLUT& primary,
                      SecondaryLUT& secondary);

FrequencyTable compute_frequency(const uint8_t* file_ptr, size_t file_size);
