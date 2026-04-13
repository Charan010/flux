#include "huffman.h"
#include "bit_io.h"
#include <array>


Node::Node(uint8_t c, uint64_t f)
    : ch(c), freq(f), left(nullptr), right(nullptr) {}


Node::Node(std::unique_ptr<Node> l, std::unique_ptr<Node> r)
    : ch(0), freq(l->freq + r->freq), 
      left(std::move(l)), right(std::move(r)) {}
    
Node::Node()
    : ch(0), freq(0), left(nullptr), right(nullptr) {}

bool Compare::operator()(Node* a, Node* b) {
    return a->freq > b->freq;
}


/*
    building huffman tree by using priority queue where most frequent characters are 
    assigned shorter codes.
*/
Node* build_huffman_tree(const FrequencyTable& freq) {
    std::priority_queue<Node*, std::vector<Node*>, Compare> pq;

    for (int i = 0; i < 256; i++)
        if (freq[i] > 0)
            pq.push(new Node((uint8_t)i, freq[i]));

    while (pq.size() > 1) {
        auto a = std::unique_ptr<Node>(pq.top()); pq.pop();
        auto b = std::unique_ptr<Node>(pq.top()); pq.pop();
        pq.push(new Node(std::move(a), std::move(b)));
    }

    return pq.top();
}


void compute_lengths(Node* root, uint8_t depth, std::array<uint8_t, 256>& lengths)
{
    if (!root) return;

    const bool is_leaf = (!root->left && !root->right);

    if (is_leaf) {
        lengths[root->ch] = (depth == 0) ? 1 : depth;
        return;
    }

    compute_lengths(root->left.get(),  depth + 1, lengths);
    compute_lengths(root->right.get(), depth + 1, lengths);
}


/*
    while building huffman tree, suppose two characters have same frequency x. Depending upon the 
    priority queue comparator, the tree structure can change as well.

    to make this whole process deterministic, i'm using canonical table.
*/
void generate_canonical_table(const std::array<uint8_t, 256>& lengths, std::array<HuffmanCode, 256>& table) {

    const int MAX_BITS = 32;

    std::array<uint32_t, MAX_BITS + 1> bl_count{};

    for (uint8_t len : lengths)
        if (len > 0)
            bl_count[len]++;

    std::array<uint64_t, MAX_BITS + 1> next_code{};

    uint64_t code = 0;

    for (int bits = 1; bits <= MAX_BITS; ++bits) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    for (int sym = 0; sym < 256; ++sym) {
        uint8_t len = lengths[sym];
        if (len != 0) {
            table[sym].bits = next_code[len];
            table[sym].len  = len;
            next_code[len]++;
        }
    }
}


void write_lengths(const std::array<uint8_t, 256>& lengths, BitWriter& bw) {
    for (int i = 0; i < 256; i++)
        bw.write_byte(lengths[i]);
}


void read_lengths(std::array<uint8_t, 256>& lengths, BitReader& br) {
    for (int i = 0; i < 256; i++)
        lengths[i] = br.read_byte();
}

uint32_t read_uint32(BitReader& br) {
    uint32_t x = 0;
    for (int i = 0; i < 4; i++)
        x = (x << 8) | br.read_byte();
    return x;
}

void write_uint32(BitWriter& bw, uint32_t x) {
    bw.write_byte((x >> 24) & 0xFF);
    bw.write_byte((x >> 16) & 0xFF);
    bw.write_byte((x >>  8) & 0xFF);
    bw.write_byte( x        & 0xFF);
}

Node* build_decode_tree(const std::array<HuffmanCode, 256>& table) {
    Node* root = new Node();

    for (int sym = 0; sym < 256; sym++) {
        const auto& code = table[sym];
        if (code.len == 0) continue;

        Node* curr = root;

        for (int i = code.len - 1; i >= 0; i--) {
            int bit = (code.bits >> i) & 1;

            if (bit == 0) {
                if (!curr->left)
                    curr->left = std::make_unique<Node>();
                curr = curr->left.get();
            } else {
                if (!curr->right)
                    curr->right = std::make_unique<Node>();
                curr = curr->right.get();
            }
        }

        curr->ch = sym;
    }

    return root;
}

/*
    reading bit by bit and going left or right based on the value is slow and also slows down
    cache prediction. To make this process faster, we use look up tables (LUT).

    @example
    suppose A code = 1100
    in this implementation, LUT table size = LUT_BITS,
    so all values starting from 1100xxxxx will point to A only.

    this way, we can simply read multiple bits from the stream and decode faster. if not possible, we fall
    back to decoding bit by bit.

    This reduces the bottleneck of reading bit by bit.
*/
void build_decode_lut(const std::array<HuffmanCode, 256>& table, Node* root, DecodeLUT& lut) {

    for (uint16_t sym = 0; sym < 256; ++sym) {

        const HuffmanCode& code = table[sym];
        const uint8_t len = code.len;

        if (len == 0)
            continue;

        if (len <= LUT_BITS) {

            const uint32_t shift = LUT_BITS - len;
            const uint32_t start = code.bits << shift;
            const uint32_t end   = start + (1u << shift);

            const uint8_t symbol = static_cast<uint8_t>(sym);

            for (uint32_t i = start; i < end; ++i) {        
                auto& e   = lut[i];
                e.symbol  = symbol;
                e.bits    = len;
                e.is_leaf = true;
            }

        } else {

            const uint32_t prefix = code.bits >> (len - LUT_BITS);
            auto& entry = lut[prefix];

            if (entry.next == nullptr) {

                Node* node = root;

                for (int i = LUT_BITS - 1; i >= 0; --i) {
                    const int bit = (prefix >> i) & 1;
                    node = bit ? node->right.get() : node->left.get();
                }

                entry.next    = node;  
                entry.bits    = LUT_BITS;
                entry.is_leaf = false;
            }
        }
    }
}