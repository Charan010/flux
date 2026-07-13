#include "lz4.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

constexpr uint32_t MAX_CHAINS = 6;
constexpr uint32_t HASH_BITS  = 18;

struct MatchFinder {

  static constexpr uint32_t EMPTY     = UINT32_MAX;
  static constexpr size_t   HASH_SIZE = 1 << HASH_BITS;

	/* padding with 64 bytes so that entire array sits in one cache line. If the array is not aligned then it would
	cause CPU to fetch two cache lines.

	Just a small optimization to increase speed of cache.

	and also this avoid false sharing which means, each thread has its own MatchFinder instance. forcing each MatchFinder
	hash  to be stored in one independent cache line and avoid invalidating cache lines when data is collided.

	*/
  alignas(64) uint32_t pos[HASH_SIZE][MAX_CHAINS];

  uint8_t next_slot[HASH_SIZE];

  MatchFinder() {
    for (size_t h = 0; h < HASH_SIZE; ++h) {
      for (uint32_t s = 0; s < MAX_CHAINS; ++s)
        pos[h][s] = EMPTY;
      next_slot[h] = 0;
    }
  }



  inline void insert(uint32_t h, uint32_t p) {
    uint8_t slot = next_slot[h];
    pos[h][slot] = p;
    next_slot[h] = static_cast<uint8_t>((slot + 1 == MAX_CHAINS) ? 0 : slot + 1);
  }

  template <typename Fn>

  inline void for_each(uint32_t h, Fn &&fn) const {

    	uint8_t slot = next_slot[h];
    	for (uint32_t i = 0; i < MAX_CHAINS; ++i) {
      	slot = (slot == 0) ? MAX_CHAINS - 1 : slot - 1;
      	uint32_t candidate = pos[h][slot];
      	if (candidate == EMPTY)
        	return;
      	fn(candidate);
    }
  }
};

inline uint32_t read32(const void *ptr) {

  	uint32_t value;
  	std::memcpy(&value, ptr, sizeof(value));
  	return value;
}

inline uint64_t read64(const void *ptr) {

  	uint64_t value;
  	std::memcpy(&value, ptr, sizeof(value));
  	return value;
}

inline uint32_t get_hash(const uint8_t *ptr) {

  	constexpr uint32_t PRIME = 2654435761u;
  	uint32_t value = read32(ptr);
  	return (value * PRIME) >> (32 - HASH_BITS);
}

inline size_t count_match(const uint8_t *current, const uint8_t *candidate, const uint8_t *input_end) {
  	const uint8_t *start = current;
  	while (current + 8 <= input_end && candidate + 8 <= input_end) {

    	uint64_t diff = read64(current) ^ read64(candidate);
    	if (diff == 0) {
      		current   += 8;
      		candidate += 8;
    	} else {
      		return (current - start) + (__builtin_ctzll(diff) >> 3);
    	}
  	}
  	while (current < input_end && candidate < input_end && *current == *candidate) {
    	++current;
    	++candidate;
  	}

  	return current - start;
}

inline uint8_t *emit_length(uint8_t *ptr, size_t length){

  	while (length >= 255) {
    	*ptr++ = 255;
    	length -= 255;
  	}

  	*ptr++ = static_cast<uint8_t>(length);
  	return ptr;
}

inline uint8_t *emit_sequence(uint8_t *ptr, const uint8_t *literals, size_t literal_length, uint16_t offset, 
		size_t match_length) {


  	uint8_t literal_nibble = static_cast<uint8_t>(std::min<size_t>(15, literal_length));
  	uint8_t match_nibble   = static_cast<uint8_t>(std::min<size_t>(match_length - 4, 15));
  	uint8_t token          = static_cast<uint8_t>((literal_nibble << 4) | match_nibble);
  	*ptr++ = token;

  	if (literal_length >= 15)
    	ptr = emit_length(ptr, literal_length - 15);

  	std::memcpy(ptr, literals, literal_length);
  	ptr += literal_length;

  	*ptr++ = static_cast<uint8_t>(offset & 0xFF);
  	*ptr++ = static_cast<uint8_t>(offset >> 8);

  	size_t remaining_match_len = match_length - 4;
  	if (remaining_match_len >= 15)
    	ptr = emit_length(ptr, remaining_match_len - 15);

  	return ptr;

}

size_t lz4_compress(const uint8_t *__restrict input, size_t input_length, uint8_t *output) {

  	MatchFinder match_finder;

  	size_t ip = 0 , anchor = 0;
  	uint8_t *op     = output;

  	const uint8_t *input_end = input + input_length;

  	while (ip + 4 < input_length) {

	    const uint8_t *current = input + ip;
	    uint32_t hash = get_hash(current);

	    match_finder.insert(hash, static_cast<uint32_t>(ip));

    	size_t best_len = 0, best_pos = 0;

    	match_finder.for_each(hash, [&](uint32_t candidate) {

      	if (candidate >= ip)
        	return;

      	uint32_t offset = static_cast<uint32_t>(ip - candidate);
      	if (offset > 65535)
        	return;

      	if (read32(current) != read32(input + candidate))
        	return;

      	size_t len = 4 + count_match(current + 4, input + candidate + 4, input_end);

      	if (len > best_len) {
        	best_len = len;
        	best_pos = candidate;
      	}

    });


    	if (__builtin_expect(best_len < 4, 1)) {
      		++ip;
      		continue;
    	}

    	size_t literal_length = ip - anchor;
    	uint16_t offset = static_cast<uint16_t>(ip - best_pos);

    	op = emit_sequence(op, input + anchor, literal_length, offset, best_len);

    	size_t match_end = ip + best_len;
    	++ip;

    	while (ip + 4 <= input_length && ip < match_end) {
      		match_finder.insert(get_hash(input + ip), static_cast<uint32_t>(ip));
      		++ip;
    	}

    	ip = match_end;
    	anchor = ip;
  	}

  	size_t  remaining_literals = input_length - anchor;
  	uint8_t token = static_cast<uint8_t>(std::min<size_t>(remaining_literals, 15) << 4);
  	*op++ = token;

  	if (remaining_literals >= 15)
    	op = emit_length(op, remaining_literals - 15);

  	std::memcpy(op, &input[anchor], remaining_literals);
  	op += remaining_literals;

  	return static_cast<size_t>(op - output);

}

inline uint32_t read_length(const uint8_t *input, size_t /*input_len*/, size_t &ip, uint32_t initial) {
  
	uint32_t length = initial;
  	if (length != 15)
    	return length;

  	uint8_t extra;
  	do {
    	extra   = input[ip++];
    	length += extra;

  	} while (extra == 255);

  	return length;
}

size_t lz4_decompress(const uint8_t *input, size_t input_len, uint8_t *output) {
  	
	size_t ip = 0;
  	uint8_t *op = output;

  	while (ip < input_len) {

    	uint8_t  token  = input[ip++];
    	uint32_t literal_len = token >> 4;
    	uint32_t match_len   = token & 0x0F;

    	literal_len = read_length(input, input_len, ip, literal_len);

    	if (ip + literal_len > input_len)
      		throw std::runtime_error("corrupt literals");

    	std::memcpy(op, input + ip, literal_len);
    	op += literal_len;
    	ip += literal_len;

    	if (ip >= input_len)
      		break;

    	if (ip + 2 > input_len)
      		throw std::runtime_error("corrupt offset");

    	uint16_t offset = static_cast<uint16_t>(input[ip] | (input[ip + 1] << 8));
    	ip += 2;

    	if (offset == 0 || offset > static_cast<size_t>(op - output))
      		throw std::runtime_error("invalid offset");

    	match_len = read_length(input, input_len, ip, match_len);
    	match_len += 4;

    	uint8_t *match = op - offset;

    	if (offset >= 8) {
      		uint8_t *end = op + match_len;
      		do {
        		std::memcpy(op, match, 8);
        		op    += 8;
        		match += 8;

      		}while (op < end);

      		op = end;
    	} else {
			
      		uint8_t *end = op + match_len;
      		while (op < end)
        		*op++ = *match++;
    	}
  	}

  	return static_cast<size_t>(op - output);
}