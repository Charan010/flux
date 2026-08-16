#include "pack_format.h"

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {

	constexpr std::array<uint8_t, 8> kPackMagic =
		{'R', 'E', 'L', 'I', 'C', 'P', 'A', 'K'};

	constexpr std::array<uint8_t, 4> kObjectMagic = {'R', 'L', 'O', 'B'};

	constexpr uint32_t kMaxObjectSize = 1u << 20;   /* Config::max_chunk_size  */
	constexpr uint32_t kMaxStoredSize = 2u << 20;   

	void put_u32(uint8_t *dst, uint32_t value) {
		dst[0] = static_cast<uint8_t>(value);
		dst[1] = static_cast<uint8_t>(value >> 8);
		dst[2] = static_cast<uint8_t>(value >> 16);
		dst[3] = static_cast<uint8_t>(value >> 24);
	}

	uint32_t get_u32(const uint8_t *src) {
		return static_cast<uint32_t>(src[0])
		     | (static_cast<uint32_t>(src[1]) << 8)
		     | (static_cast<uint32_t>(src[2]) << 16)
		     | (static_cast<uint32_t>(src[3]) << 24);
	}

	void put_i64(uint8_t *dst, int64_t value) {
		uint64_t v = static_cast<uint64_t>(value);

		for (int i = 0; i < 8; ++i)
			dst[i] = static_cast<uint8_t>(v >> (i * 8));
	}

	int64_t get_i64(const uint8_t *src) {
		uint64_t value = 0;

		for (int i = 0; i < 8; ++i)
			value |= static_cast<uint64_t>(src[i]) << (i * 8);

		return static_cast<int64_t>(value);
	}

	bool equal_bytes(const uint8_t *a, const uint8_t *b, size_t n) {
		return std::memcmp(a, b, n) == 0;
	}

	uint32_t crc32_impl(const uint8_t *data, size_t len) {

		uint32_t crc = 0xFFFFFFFFu;

		for (size_t i = 0; i < len; ++i) {
			crc ^= data[i];

			for (int bit = 0; bit < 8; ++bit) {
				uint32_t mask = -(crc & 1u);
				crc = (crc >> 1) ^ (0xEDB88320u & mask);
			}
		}

		return ~crc;
	}

	[[noreturn]]
	void corrupt(std::string_view section, std::string_view where, const char *reason) {
		throw std::runtime_error("corrupt " + std::string(section) + " at "
		                         + std::string(where) + ": " + reason);
	}

}

uint32_t pack_crc32(const uint8_t *data, size_t len) {
	return crc32_impl(data, len);
}

/* ------------------------------------------------------------------
 * PACK HEADER -- 28 bytes
 *
 * +0   8   magic "RELICPAK"
 * +8   4   version
 * +12  4   pack_id
 * +16  8   created_at
 * +24  4   header_crc, covers [0, 24)
 * ------------------------------------------------------------------ */

void PackHeader::encode(uint8_t *out) const {

	std::memcpy(out + 0, kPackMagic.data(), kPackMagic.size());

	put_u32(out + 8,  version);
	put_u32(out + 12, pack_id);
	put_i64(out + 16, created_at);

	put_u32(out + 24, pack_crc32(out, 24));
}

PackHeader PackHeader::decode(const uint8_t *in, std::string_view where) {

	if (!equal_bytes(in, kPackMagic.data(), kPackMagic.size()))
		corrupt("pack header", where, "invalid pack magic");

	if (get_u32(in + 24) != pack_crc32(in, 24))
		corrupt("pack header", where, "header CRC mismatch");

	PackHeader header;
	header.version    = get_u32(in + 8);
	header.pack_id    = get_u32(in + 12);
	header.created_at = get_i64(in + 16);

	if (header.version != kPackFormatVersion)
		corrupt("pack header", where, "unsupported pack format version");

	return header;
}

/* ------------------------------------------------------------------
 * OBJECT HEADER -- 53 bytes
 *
 * +0   4   magic "RLOB"
 * +4   1   codec
 * +5   4   original_size
 * +9   4   stored_size
 * +13  32  digest, blake3 of the ORIGINAL bytes
 * +45  4   payload_crc, crc32 of the STORED bytes
 * +49  4   header_crc, covers [0, 49)
 *
 * header_crc is validated before either size field is used, so a corrupt
 * header can never decide how many payload bytes to read.
 * ------------------------------------------------------------------ */

void ObjectHeader::encode(uint8_t *out) const {

	std::memcpy(out + 0, kObjectMagic.data(), kObjectMagic.size());

	out[4] = static_cast<uint8_t>(codec);

	put_u32(out + 5, original_size);
	put_u32(out + 9, stored_size);

	std::memcpy(out + 13, digest.data(), 32);

	put_u32(out + 45, payload_crc);
	put_u32(out + 49, pack_crc32(out, 49));
}

ObjectHeader ObjectHeader::decode(const uint8_t *in, std::string_view where) {

	if (!equal_bytes(in, kObjectMagic.data(), kObjectMagic.size()))
		corrupt("object header", where, "invalid object magic");

	if (get_u32(in + 49) != pack_crc32(in, 49))
		corrupt("object header", where, "header CRC mismatch");

	ObjectHeader header;
	header.codec         = static_cast<CodecId>(in[4]);
	header.original_size = get_u32(in + 5);
	header.stored_size   = get_u32(in + 9);

	
	if (header.original_size > kMaxObjectSize)
		corrupt("object header", where, "original object size exceeds limit");

	if (header.stored_size > kMaxStoredSize)
		corrupt("object header", where, "stored object size exceeds limit");

	std::memcpy(header.digest.data(), in + 13, 32);
	header.payload_crc = get_u32(in + 45);

	return header;
}



PackScanResult scan_pack(
	const std::filesystem::path &path,
	const std::function<void(const ObjectHeader &, uint64_t)> &on_record,
	bool verify_payloads) {

	const std::string where = path.filename().string();

	std::ifstream in(path, std::ios::binary);
	if (!in)
		throw std::runtime_error("pack: cannot open " + path.string());

	const uint64_t size = std::filesystem::file_size(path);

	if (size < PackHeader::kSize)
		corrupt("pack", where, "file shorter than a pack header");

	uint8_t pack_hdr[PackHeader::kSize];
	in.read(reinterpret_cast<char *>(pack_hdr), PackHeader::kSize);
	if (!in)
		corrupt("pack", where, "short read on pack header");

	const PackHeader ph = PackHeader::decode(pack_hdr, where);

	PackScanResult result;
	result.pack_id    = ph.pack_id;
	result.good_bytes = PackHeader::kSize;

	std::vector<uint8_t> payload;
	uint8_t obj_hdr[ObjectHeader::kSize];

	while (result.good_bytes + ObjectHeader::kSize <= size) {

		in.seekg(static_cast<std::streamoff>(result.good_bytes));
		in.read(reinterpret_cast<char *>(obj_hdr), ObjectHeader::kSize);
		if (!in)
			break;

		ObjectHeader oh;
		try {
			oh = ObjectHeader::decode(obj_hdr, where);
		} catch (const std::exception &) {
			break;
		}

		const uint64_t end = result.good_bytes + ObjectHeader::kSize + oh.stored_size;

		if (end > size)
			break;                          /* header intact, payload is not */

		if (verify_payloads) {

			payload.resize(oh.stored_size);
			in.read(reinterpret_cast<char *>(payload.data()), oh.stored_size);

			if (!in)
				break;

			if (pack_crc32(payload.data(), payload.size()) != oh.payload_crc)
				break;
		}

		if (on_record)
			on_record(oh, result.good_bytes);

		result.good_bytes = end;
		++result.records;
	}

	result.truncated = result.good_bytes < size;
	return result;
}