#pragma once


#include <array>
#include <cstdint>
#include <cstring>
#include <string>

using Digest = std::array<uint8_t, 32>;

struct DigestHash{

	size_t operator()(const Digest &d) const noexcept{
		size_t h;
		std::memcpy(&h, d.data(), sizeof(h));
		return h;
	}
};

std::string to_hex(const Digest &d);
Digest from_hex(const std::string &hex);