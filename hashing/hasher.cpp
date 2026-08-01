#include "hasher.h"
#include <blake3.h>

Digest blake3_hash(const Chunk &chunk){

	blake3_hasher hasher;
	blake3_hasher_init(&hasher);
	blake3_hasher_update(&hasher, chunk.bytes.data(), chunk.bytes.size());
 
	Digest digest{};
	static_assert(BLAKE3_OUT_LEN == 32, "Digest assumes a 32-byte BLAKE3 output");
	blake3_hasher_finalize(&hasher, digest.data(), digest.size());
 
	return digest;
}