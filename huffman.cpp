#include "huffman.h"
#include "bit_io.h"
#include <array>
#include <unordered_map>
#include <assert.h>

Node::Node(Node *l, Node *r) : ch(0), freq(l->freq + r->freq), left(l), right(r) {}

Node::Node(uint8_t c, uint64_t f) : ch(c), freq(f), left(nullptr), right(nullptr) {}

Node::Node() : ch(0), freq(0), left(nullptr), right(nullptr) {}

bool Compare::operator()(Node *a, Node *b)
{
    return a->freq > b->freq;
}

uint16_t flatten_tree(Node *root, FlatTree &flat)
{

    uint16_t idx = static_cast<uint16_t>(flat.size());
    flat.push_back({0, 0, 0, false});

    bool is_leaf = (!root->left && !root->right);

    flat[idx].is_leaf = is_leaf;

    if (is_leaf)
    {
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
Node *build_huffman_tree(const FrequencyTable &freq, std::vector<Node> &storage)
{

    std::priority_queue<Node *, std::vector<Node *>, Compare> pq;
    int sym_count = 0;
    for (int i = 0; i < 256; ++i)
    {
        if (freq[i] > 0)
            sym_count++;
    }

    storage.reserve(512);

    for (int i = 0; i < 256; ++i)
    {
        if (freq[i] > 0)
        {
            storage.emplace_back((uint8_t)i, freq[i]);
            pq.push(&storage.back());
        }
    }

    while (pq.size() > 1)
    {

        Node *a = pq.top();
        pq.pop();

        Node *b = pq.top();
        pq.pop();

        storage.emplace_back(a, b);
        pq.push(&storage.back());
    }

    return pq.top();
}

void compute_lengths(Node *root, uint8_t depth, std::array<uint8_t, 256> &lengths)
{

    if (!root)
        return;

    const bool is_leaf = (!root->left && !root->right);

    if (is_leaf)
    {
        lengths[root->ch] = (depth == 0) ? 1 : depth;
        return;
    }

    compute_lengths(root->left, depth + 1, lengths);
    compute_lengths(root->right, depth + 1, lengths);
}

/*
        Huffman tree is built using greedy approach which is repeatedly merges the least two frequent characters as two childs for the new node (bottom up approach).
        because it makes the codes shorter as well.

        But if the two characters can have same frequency. Then, depending upon priority queue, the codes for the same frequency characters can change.
        Suppose if A and B both have frequency of X.

        Then, one possibility is A = 0 , B = 1
        another possibility could be B = 0 , A = 1.

        The process of assignment of these shorter codes is not deterministic, so the codes can vary from the encoded data to
        decoder building the tree which can cause corruption of data.

        Canonical can assign codes from the lengths of all possible bytes (0 - 255) without requiring to rebuild the tree and traverse
        one by one.

        Canonical assigned codes are prefix free. So, no code has same prefix as the other codes which makes it easier for the decoder to parse the encoded
        data.

        we calculate depth of these characters in the tree and represent the depth as lengths.
        example:

        A - 2   bl_count[1] = 0
        B - 3   bl_count[2] = 1
        C - 3   bl_count[3] = 3
        D - 3

        to generate the code which is prefix free. the formula is
        code = (code + bl_count[bits - 1]) << 1.

        where bl_count[bits] basically represents how many characters have lengths "bits".

*/
void generate_canonical_table(const std::array<uint8_t, 256> &lengths, std::array<HuffmanCode, 256> &table)
{

    const int MAX_BITS = 32;

    std::array<uint32_t, MAX_BITS + 1> bl_count{};

    for (uint8_t len : lengths)
    {
        if (len > 0)
        {
            if (len > MAX_BITS)
            {
                throw std::runtime_error("Huffman tree depth exceeds 32 bits. Use a length-limited algorithm.");
            }
            bl_count[len]++;
        }
    }

    std::array<uint64_t, MAX_BITS + 1> next_code{};

    uint64_t code = 0;

    for (int bits = 1; bits <= MAX_BITS; ++bits)
    {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    for (int sym = 0; sym < 256; ++sym)
    {
        uint8_t len = lengths[sym];
        if (len != 0)
        {
            table[sym].bits = next_code[len];
            table[sym].len = len;
            next_code[len]++;
        }
    }
}

void write_lengths(const std::array<uint8_t, 256> &lengths, BitWriter &bw)
{
    for (int i = 0; i < 256; i++)
        bw.write_byte(lengths[i]);
}

void read_lengths(std::array<uint8_t, 256> &lengths, BitReader &br)
{
    for (int i = 0; i < 256; i++)
        lengths[i] = br.read_byte();
}

uint32_t read_uint32(BitReader &br)
{
    uint32_t x = 0;
    for (int i = 0; i < 4; i++)
        x = (x << 8) | br.read_byte();
    return x;
}

void write_uint32(BitWriter &bw, uint32_t x)
{
    bw.write_byte((x >> 24) & 0xFF);
    bw.write_byte((x >> 16) & 0xFF);
    bw.write_byte((x >> 8) & 0xFF);
    bw.write_byte(x & 0xFF);
}

uint32_t read_uint32_at(const uint8_t *ptr, size_t pos){
    uint32_t x = 0;
    x |= (static_cast<uint32_t>(ptr[pos + 0]) << 24);
    x |= (static_cast<uint32_t>(ptr[pos + 1]) << 16);
    x |= (static_cast<uint32_t>(ptr[pos + 2]) << 8);
    x |= (static_cast<uint32_t>(ptr[pos + 3]));
    return x;
}

Node *build_decode_tree(const std::array<HuffmanCode, 256> &table, std::vector<Node> &storage){

    storage.reserve(512);
    storage.emplace_back();

    Node *root = &storage.back();

    for (int sym = 0; sym < 256; sym++)
    {

        const auto &code = table[sym];

        if (code.len == 0)
            continue;

        Node *curr = root;

        for (int i = code.len - 1; i >= 0; i--)
        {
            int bit = (code.bits >> i) & 1;
            if (bit == 0)
            {
                if (!curr->left)
                {
                    storage.emplace_back();
                    curr->left = &storage.back();
                }
                curr = curr->left;
            }
            else
            {
                if (!curr->right)
                {
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
void build_decode_lut(const std::array<HuffmanCode, 256> &table, DecodeLUT &primary, SecondaryLUT &secondary)
{

    std::unordered_map<uint32_t, SubtableInfo> subtables;

    for (auto &e : primary)
    {
        e.value = 0;
        e.bits = 0;
        e.flags = 0;
    }

    for (uint16_t sym = 0; sym < 256; ++sym)
    {

        const HuffmanCode &code = table[sym];

        const uint8_t len = code.len;

        if (len == 0)
            continue;

        if (len <= LUT_BITS)
        {

            const uint32_t shift = LUT_BITS - len;
            const uint32_t start = code.bits << shift;
            const uint32_t count = 1u << shift;

            for (uint32_t i = 0; i < count; ++i)
            {

                auto &entry = primary[start + i];
                entry.value = static_cast<uint16_t>(sym);
                entry.bits = len;
                entry.flags = ENTRY_SYMBOL;
            }
        }

        else
        {

            // fetching the first LUT_BITS like a prefix to secondary LUT table.
            const uint32_t prefix = (code.bits >> (len - LUT_BITS)) & ((1u << LUT_BITS) - 1);

            const uint8_t extra_bits = len - LUT_BITS;

            SubtableInfo info;

            auto it = subtables.find(prefix);
            if (it == subtables.end())
            {

                info.offset = secondary.size();
                info.bits = extra_bits;
                secondary.resize(secondary.size() + (1u << extra_bits));

                for (size_t i = info.offset; i < secondary.size(); ++i)
                {
                    secondary[i].value = 0;
                    secondary[i].bits = 0;
                    secondary[i].flags = 0;
                }
                subtables[prefix] = info;

                primary[prefix].value = info.offset;
                primary[prefix].bits = extra_bits;
                primary[prefix].flags = ENTRY_SUBTABLE;
            }

            else
            {
                info = it->second;
            }

            const uint32_t suffix = code.bits & ((1u << extra_bits) - 1);
            const uint32_t shift = info.bits - extra_bits;
            const uint32_t start = suffix << shift;
            const uint32_t count = 1u << shift;

            for (uint32_t i = 0; i < count; ++i)
            {

                assert(info.offset + start + i < secondary.size());
                auto &entry = secondary[info.offset + start + i];

                entry.value = static_cast<uint16_t>(sym);
                entry.bits = len;
                entry.flags = ENTRY_SYMBOL;
            }
        }
    }
}