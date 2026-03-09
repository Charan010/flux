#ifndef CHUNK_BUFFER_H
#define CHUNK_BUFFER_H

#include <vector>
#include <queue>
#include <mutex>
#include <cstdint>
#include "bit_io.h"
#include "chunk.h"

struct ChunkCompare {
    bool operator()(const Chunk& a, const Chunk& b) const {
        return a.id > b.id;   
    }
};

class ChunkBuffer {

private:

    BitWriter& bw;

    std::priority_queue<Chunk, std::vector<Chunk>, ChunkCompare> buffer;

    std::mutex mtx;
    std::mutex write_mtx; 
    int expected_chunk_id;
    void flush_ready_chunks_locked();

public:

    ChunkBuffer(BitWriter& writer);
    void submit_chunk(Chunk chunk);
    void flush_ready_chunks();
    
};

#endif