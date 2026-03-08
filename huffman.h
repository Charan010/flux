#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <cstdint>
#include <array>
#include <queue>
#include <unordered_map>
#include <string>

#include "bit_io.h"

using FrequencyTable = std::array<uint64_t,256>;

struct Node {
    uint8_t ch;
    uint64_t freq;
    Node* left;
    Node* right;

    Node(uint8_t c, uint64_t f);
    Node(Node* l, Node* r);
};

struct Compare {
    bool operator()(Node* a, Node* b);
};

Node* build_huffman_tree(const FrequencyTable& freq);

void build_codes(Node* root_node, std::string code, std::array<std::string,256>& table);

void write_tree(Node* root, BitWriter& bw);
Node* read_tree(BitReader& br);

void free_tree(Node* root);

uint32_t read_uint32(BitReader& br);
void write_uint32(BitWriter& bw, uint32_t x);

#endif