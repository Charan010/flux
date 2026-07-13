#pragma once

#include <string>
#include "chunker.h"

class Hasher{
public:

	std::string hash(const Chunk &chunk) const;
	
};

