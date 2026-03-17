#include "chunk_buffer.h"

ChunkBuffer::ChunkBuffer(BitWriter &writer): 
    bw(writer), expected_chunk_id(0) {}



// using a priority queue where each chunk are numbered from 0 and instead of waiting for all chunks to be encoded and passed to chunk_buffer
// which increases RAM overhead. peeking priority queue and if its the chunk that i have to write in the order, 
// pop it from the priority queue and write the chunk to disk.


// Currently a single thread writer only because its just extra work and no returns and more bugs which might happen
// when using multiple writer threads which is a pain to synchronize.


void ChunkBuffer::submit_chunk(Chunk chunk) {
    std::vector<Chunk> ready;

    {
        std::lock_guard<std::mutex> lock(mtx);
        buffer.push(std::move(chunk));

        while (!buffer.empty()) {
            if (buffer.top().id != expected_chunk_id)
                break;

            ready.push_back(std::move(const_cast<Chunk&>(buffer.top())));
            buffer.pop();
            expected_chunk_id++;
        }
    } 

    if (!ready.empty()) {
        std::lock_guard<std::mutex> wlock(write_mtx); 

        for (const Chunk& c : ready)
            bw.out.write(reinterpret_cast<const char*>(c.data.data()), c.data.size());
    }
}


void ChunkBuffer::flush_ready_chunks() {
    std::lock_guard<std::mutex> wlock(write_mtx);
    std::lock_guard<std::mutex> lock(mtx);
    flush_ready_chunks_locked();
}

void ChunkBuffer::flush_ready_chunks_locked() {
    while (!buffer.empty()) {
        
        if (buffer.top().id != expected_chunk_id)
            break;
        const Chunk& chunk = buffer.top();

        //writing the whole chunk to the disk.
        bw.out.write(reinterpret_cast<const char*>(chunk.data.data()), chunk.data.size());
        buffer.pop();
        expected_chunk_id++;
    }
}