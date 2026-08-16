#pragma once

#include <cstdint>

/*
 * in memory index basically contains BLAKE3 Hash -> ObjectLocation.
 *
 */
struct ObjectLocation {
	uint32_t pack_id = 0;
	uint64_t offset  = 0;   /* start of the OBJECT HEADER, not the payload */
};