#pragma once

#include "chunking/chunker.h"
#include "digest.h"

Digest blake3_hash(const Chunk &chunk);