#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>

#include "codecs/codec.h"
#include "hashing/digest.h"

constexpr uint32_t kPackFormatVersion = 1;

/*
 *   PACK HEADER — 28 bytes, once per file
 *   ┌──────────┬─────────┬─────────┬────────────┬─────┐
 *   │"RELICPAK"│ version │ pack_id │ created_at │ crc │
 *   │    8     │    4    │    4    │     8      │  4  │
 *   └──────────┴─────────┴─────────┴────────────┴─────┘
 *
 *   OBJECT RECORD — 53-byte header + payload
 *   ┌────────┬───────┬───────────┬────────┬────────┬─────────┬─────┐
 *   │ "RLOB" │ codec │ orig_size │ stored │ digest │ pay_crc │ crc │
 *   │   4    │   1   │     4     │   4    │   32   │    4    │  4  │
 *   └────────┴───────┴───────────┴────────┴────────┴─────────┴─────┘
 */

struct PackHeader {
    static constexpr size_t kSize = 28;

    uint32_t version    = kPackFormatVersion;
    uint32_t pack_id    = 0;
    int64_t  created_at = 0;

    void encode(uint8_t* out) const;
    static PackHeader decode(const uint8_t* in, std::string_view where);
};

struct ObjectHeader {
    static constexpr size_t kSize = 53;

    CodecId  codec         = CodecId::Raw;
    uint32_t original_size = 0;
    uint32_t stored_size   = 0;
    Digest   digest{};
    uint32_t payload_crc   = 0;

    void encode(uint8_t* out) const;
    static ObjectHeader decode(const uint8_t* in, std::string_view where);
};

uint32_t pack_crc32(const uint8_t* data, size_t len);

struct PackScanResult {
    uint32_t pack_id    = 0;
    uint64_t good_bytes = 0;
    uint64_t records    = 0;
    bool     truncated  = false;
};

PackScanResult scan_pack(
    const std::filesystem::path& path,
    const std::function<void(const ObjectHeader&, uint64_t record_offset)>& on_record,
    bool verify_payloads);

	