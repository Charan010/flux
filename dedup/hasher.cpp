#include "hasher.h"

#include <blake3.h>
#include <iomanip>
#include <sstream>


std::string Hasher::hash(const Chunk &chunk) const{

	blake3_hasher hasher;
	blake3_hasher_init(&hasher);

	blake3_hasher_update(&hasher, chunk.bytes.data(), chunk.bytes.size());
    uint8_t digest[BLAKE3_OUT_LEN];
    blake3_hasher_finalize(&hasher, digest, BLAKE3_OUT_LEN);

    std::stringstream ss;

    for (size_t i = 0; i < BLAKE3_OUT_LEN; i++)
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    

    return ss.str();
}