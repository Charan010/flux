#include "huffman.h"
#include "bit_io.h"
#include <array>


Node::Node(Node* l, Node* r): ch(0), freq(l->freq + r->freq), left(l),right(r) {}

Node::Node(uint8_t c, uint64_t f) : ch(c), freq(f), left(nullptr), right(nullptr) {}

Node::Node():ch(0), freq(0), left(nullptr), right(nullptr) {}

bool Compare::operator()(Node* a, Node* b) {
    return a->freq > b->freq;
}


uint16_t flatten_tree(Node* root, FlatTree& flat){
    
    uint16_t idx = static_cast<uint16_t>(flat.size());
    flat.push_back({0, 0, 0, false});

    bool is_leaf = (!root->left && !root->right);

    flat[idx].is_leaf = is_leaf;

    if(is_leaf){
        flat[idx].symbol = root->ch;
        flat[idx].left = 0;
        flat[idx].right = 0;
        return idx;
    }

    flat[idx].left = flatten_tree(root->left, flat);

    flat[idx].right = flatten_tree(root->right, flat);
    return idx;
}



/*
    building huffman tree by using priority queue where most frequent characters are 
    assigned shorter codes.
*/
Node* build_huffman_tree( const FrequencyTable& freq, std::vector<Node>& storage){

    std::priority_queue<Node*, std::vector<Node*>, Compare> pq;
    int sym_count = 0;
    for (int i = 0; i < 256; ++i){
        if (freq[i] > 0)
            sym_count++;
    } 

    storage.reserve(2 * sym_count);

    for(int i = 0 ; i < 256; ++i){
        if(freq[i] > 0){
            storage.emplace_back((uint8_t)i, freq[i]);
            pq.push(&storage.back());

        }
    }

    while(pq.size() > 1){

        Node *a = pq.top();
        pq.pop();

        Node *b = pq.top();
        pq.pop();

        storage.emplace_back(a, b);
        pq.push(&storage.back());
    }

    return pq.top();

}

void compute_lengths(Node* root, uint8_t depth, std::array<uint8_t, 256>& lengths){

    if (!root)
        return;

    const bool is_leaf = (!root->left && !root->right);

    if (is_leaf) {
        lengths[root->ch] = (depth == 0) ? 1 : depth;
        return;
    }

    compute_lengths(root->left,  depth + 1, lengths);
    compute_lengths(root->right, depth + 1, lengths);
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

Node* build_decode_tree(const std::array<HuffmanCode, 256>& table, std::vector<Node>& storage){

    storage.reserve(512);
    storage.emplace_back();

    Node* root = &storage.back();

    for(int sym = 0; sym < 256; sym++) {

        const auto& code = table[sym];

        if (code.len == 0)
            continue;

        Node* curr = root;

        for (int i = code.len - 1; i >= 0; i--) {
            int bit = (code.bits >> i) & 1;
            if (bit == 0) {
                if (!curr->left) {
                    storage.emplace_back();
                    curr->left = &storage.back();
                }
                curr = curr->left;

            }else {
                if (!curr->right){
                    storage.emplace_back();
                    curr->right = &storage.back();
                }
                curr = curr->right;
            }
        }

        curr->ch = static_cast<uint8_t>(sym);
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
void build_decode_lut(const std::array<HuffmanCode, 256>& table, const FlatTree& flat, DecodeLUT& lut){
    
    for (uint16_t sym = 0; sym < 256; ++sym){
        
        const HuffmanCode& code = table[sym];
        const uint8_t len = code.len;

        if (len == 0)
            continue;

        if (len <= LUT_BITS){

            const uint32_t shift = LUT_BITS - len;
            const uint32_t start = code.bits << shift;
            const uint32_t end =start + (1u << shift);

            const uint8_t symbol = static_cast<uint8_t>(sym);

            for (uint32_t i = start; i < end; ++i){
                auto& e = lut[i];
                e.symbol = symbol;
                e.bits = len;
                e.is_leaf = true;
                e.has_next = false;
            }
        }

        else{
            
            const uint32_t prefix = code.bits >> (len - LUT_BITS);
            auto& entry = lut[prefix];

            if(entry.has_next)
                continue;

            uint16_t node_idx = 0;

            for (int i = LUT_BITS - 1; i >= 0; --i){
                int bit = (prefix >> i) & 1;
                node_idx = bit ? flat[node_idx].right : flat[node_idx].left;
            }

            entry.next_index = node_idx;
            entry.bits = LUT_BITS;
            entry.is_leaf =false;
            entry.has_next = true;
        }
    }
}