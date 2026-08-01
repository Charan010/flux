#pragma once
#include <cstddef>
#include <cstdint>

namespace Config {

constexpr size_t threadpool_size = 8;

constexpr size_t chunk_window = 48;
constexpr size_t min_chunk_size = 2u << 10;    // 2 KB
constexpr size_t max_chunk_size = 1u << 20;    // 1 MB hard cap


constexpr size_t max_inflight_chunks = 256;

constexpr uint64_t chunk_base = 257;
constexpr uint64_t chunk_mask = 0x1FFF;

}