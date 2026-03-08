#pragma once
#include <vector>
#include <cstdint>

struct Chunk
{
    int id;                     
    std::vector<uint8_t> data; 
};