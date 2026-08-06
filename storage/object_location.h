#pragma once

#include <cstdint>
#include "codecs/codec.h"


struct ObjectLocation {
	uint32_t pack_id         = 0;
	uint64_t offset          = 0;
	uint32_t compressed_size = 0;
	uint32_t original_size   = 0;
	CodecId  codec           = CodecId::Raw;
};