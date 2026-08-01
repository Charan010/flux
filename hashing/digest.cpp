#include "digest.h"

#include <stdexcept>

namespace {

	constexpr char kHexDigits[] = "0123456789abcdef";

	uint8_t nibble(char c){
		if(c >= 'A' && c <= 'Z')
			return static_cast<uint8_t>(c - 'A' + 10);

		if(c >='a' && c <= 'z')
			return static_cast<uint8_t>(c - 'a' + 10);

		if(c >= '0' && c <= '9')
			return static_cast<uint8_t>(c - '0');
			
		throw std::runtime_error("invalid hex character in digest");
	}

}


	std::string to_hex(const Digest &d){
    	std::string out(d.size() * 2, '\0');
    	for (size_t i = 0; i < d.size(); ++i) {
        	out[2 * i]     = kHexDigits[d[i] >> 4];
        	out[2 * i + 1] = kHexDigits[d[i] & 0x0F];
    	}
    	return out;
	}

	Digest from_hex(const std::string &hex) {
    	Digest d{};
    	if (hex.size() != d.size() * 2)
        	throw std::runtime_error("digest must be 64 hex characters, got " + std::to_string(hex.size()));
 
    	for (size_t i = 0; i < d.size(); ++i)
        	d[i] = static_cast<uint8_t>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]));
 
    	return d;
	}

