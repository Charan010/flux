#include "huffman.h"
#include "bit_io.h"
#include <array>
#include <assert.h>
#include <unordered_map>

Node::Node(Node *l, Node *r)
    : ch(0), freq(l->freq + r->freq), left(l), right(r) {}


Node::Node(uint8_t c, uint64_t f)
    : ch(c), freq(f), left(nullptr), right(nullptr) {}

Node::Node() : ch(0), freq(0), left(nullptr), right(nullptr) {}

bool Compare::operator()(Node *a, Node *b){
	 return a->freq > b->freq; 
}

Node *build_huffman_tree(const FrequencyTable &freq, std::vector<Node> &storage) {

	std::priority_queue<Node *, std::vector<Node *>, Compare> pq;
  	storage.reserve(512);

  	for (int i = 0; i < 256; ++i) {
    	if (freq[i] > 0) {
      		storage.emplace_back((uint8_t)i, freq[i]);
      		pq.push(&storage.back());
    	}
  	}

  	if (pq.empty())
    	return nullptr;

  	while (pq.size() > 1) {

    	Node *a = pq.top();
    	pq.pop();

    	Node *b = pq.top();
    	pq.pop();

    	storage.emplace_back(a, b);
    	pq.push(&storage.back());
  }

  return pq.empty() ? nullptr : pq.top();

}

void compute_lengths(Node *root, uint8_t depth, std::array<uint8_t, 256> &lengths){


  if (!root)
    return;

  const bool is_leaf = (!root->left && !root->right);

  if (is_leaf) {
    lengths[root->ch] = (depth == 0) ? 1 : depth;
    return;

  }

  compute_lengths(root->left, depth + 1, lengths);
  compute_lengths(root->right, depth + 1, lengths);
}

void generate_canonical_table(const std::array<uint8_t, 256> &lengths, std::array<HuffmanCode, 256> &table) {

  	const int MAX_BITS = 32;
  	std::array<uint32_t, MAX_BITS + 1> bl_count{};

  	for (uint8_t len : lengths) {
    	if (len > 0) {
      		if (len > MAX_BITS)
        		throw std::runtime_error("Huffman tree depth exceeds 32 bits.");
      		bl_count[len]++;
    	}
  	}

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
      		table[sym].len = len;
      		next_code[len]++;
    	}
  	}


}

void write_lengths(const std::array<uint8_t, 256> &lengths, BitWriter &bw){
  	for (int i = 0; i < 256; i++)
    	bw.write_byte(lengths[i]);
}

void read_lengths(std::array<uint8_t, 256> &lengths, BitReader &br) {
  	for(int i = 0; i < 256; i++)
    	lengths[i] = br.read_byte();
}

uint32_t read_uint32(BitReader &br) {
  	uint32_t x = 0;
  	for (int i = 0; i < 4; i++)
    		x = (x << 8) | br.read_byte();
  	return x;

}

void build_decode_lut(const std::array<HuffmanCode, 256> &table, DecodeLUT &primary, SecondaryLUT &secondary) {

  	for (auto &e : primary) {
    	e.value = 0;
    	e.bits = 0;
    	e.flags = 0;
  	}

  	secondary.clear();

  	std::array<uint8_t, LUT_SIZE> max_extra{};
  	max_extra.fill(0);

  	for (uint16_t sym = 0; sym < 256; ++sym) {
    	const auto &code = table[sym];
    	if (code.len == 0 || code.len <= LUT_BITS)
      		continue;

    	const uint32_t prefix = static_cast<uint32_t>(code.bits >> (code.len - LUT_BITS)) & (LUT_SIZE - 1);
    	const uint8_t extra = code.len - LUT_BITS;
    	if (extra > max_extra[prefix])
      		max_extra[prefix] = extra;
  	}

  	std::array<int32_t, LUT_SIZE> subtable_offset{};
  	subtable_offset.fill(-1);

  	for (uint32_t prefix = 0; prefix < LUT_SIZE; ++prefix) {
    	if (max_extra[prefix] == 0)
      		continue;

    	const uint32_t offset = static_cast<uint32_t>(secondary.size());
    	const uint32_t slots = 1u << max_extra[prefix];
    	secondary.resize(secondary.size() + slots, {0, 0, 0});
    	subtable_offset[prefix] = static_cast<int32_t>(offset);

    	primary[prefix].value = static_cast<uint16_t>(offset);
    	primary[prefix].bits = max_extra[prefix];
    	primary[prefix].flags = ENTRY_SUBTABLE;
  	}

  	for (uint16_t sym = 0; sym < 256; ++sym) {
    	const auto &code = table[sym];
    	if (code.len == 0)
      		continue;

    	if (code.len <= LUT_BITS) {
      		const uint32_t shift = LUT_BITS - code.len;
      		const uint32_t start = static_cast<uint32_t>(code.bits) << shift;
      		const uint32_t count = 1u << shift;

      		for (uint32_t i = 0; i < count; ++i) {
        		auto &entry = primary[start + i];
        		entry.value = sym;
        		entry.bits = code.len;
        		entry.flags = ENTRY_SYMBOL;
      		}	
    	
		}else {

      		const uint32_t prefix = static_cast<uint32_t>(code.bits >> (code.len - LUT_BITS)) & (LUT_SIZE - 1);
      		const uint8_t max_e = max_extra[prefix];
      		const uint8_t extra_bits = code.len - LUT_BITS;

      		const uint32_t suffix = static_cast<uint32_t>(code.bits) & ((1u << extra_bits) - 1);
			const uint32_t shift = max_e - extra_bits;

      		const uint32_t start = suffix << shift;
      		const uint32_t count = 1u << shift;
      		const uint32_t base = static_cast<uint32_t>(subtable_offset[prefix]);

      		for (uint32_t i = 0; i < count; ++i) {

        		assert(base + start + i < secondary.size());
        		auto &entry = secondary[base + start + i];
		        entry.value = sym;
		        entry.bits = code.len;
		        entry.flags = ENTRY_SYMBOL;
      		}
    	}
  	}
}

FrequencyTable compute_frequency(const uint8_t *file_ptr, size_t file_size) {

	FrequencyTable t0{}, t1{}, t2{}, t3{}, t4{}, t5{}, t6{}, t7{};

  	constexpr size_t PREFETCH_DISTANCE = 256;

  	size_t i = 0;
  	for (; i + 8 <= file_size; i += 8) {
    	if (i + PREFETCH_DISTANCE < file_size)
      		__builtin_prefetch(file_ptr + i + PREFETCH_DISTANCE, 0, 0);

    	t0[file_ptr[i + 0]]++;
    	t1[file_ptr[i + 1]]++;
    	t2[file_ptr[i + 2]]++;
    	t3[file_ptr[i + 3]]++;
    	t4[file_ptr[i + 4]]++;
    	t5[file_ptr[i + 5]]++;
    	t6[file_ptr[i + 6]]++;
    	t7[file_ptr[i + 7]]++;
  	}


  	for (; i < file_size; ++i)
    	t0[file_ptr[i]]++;

  	FrequencyTable table{};
  	for (int j = 0; j < 256; ++j)
    	table[j] = t0[j] + t1[j] + t2[j] + t3[j] + t4[j] + t5[j] + t6[j] + t7[j];

  	return table;
	
}
