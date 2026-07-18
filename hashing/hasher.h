#pragma once

#include <string>
#include "chunking/chunker.h"

class Hasher{
public:

	std::string hash(const Chunk &chunk) const;
	
};

