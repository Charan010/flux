#include "chunk_buffer.h"

ChunkBuffer::ChunkBuffer(BitWriter &writer): 
    bw(writer), expected_chunk_id(0){
}

void ChunkBuffer::submit_chunk(Chunk chunk){
    {
        std::lock_guard<std::mutex> lock(mtx);
        buffer.push(std::move(chunk));
    }

    flush_ready_chunks();
}

void ChunkBuffer::flush_ready_chunks(){
    std::lock_guard<std::mutex> lock(mtx);  
    while(!buffer.empty()){

        Chunk chunk = buffer.top();

        if(chunk.id != expected_chunk_id)
            break;

        buffer.pop();

        for(uint8_t b : chunk.data)
            bw.write_bit(b);        

        expected_chunk_id++;        
    }
}