#pragma once
#include <cstddef>
#include <cstdint>

namespace Config {

constexpr size_t threadpool_size = 8;

constexpr size_t chunk_window = 48;
constexpr uint64_t chunk_base = 257;
constexpr uint64_t chunk_mask = 0x1FFF;

}
