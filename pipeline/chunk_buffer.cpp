#include "chunk_buffer.h"


/*
    Every chunk buffer has its own BitWriter class which is basically used to write to the file specified.

    contracts:

    @writer_loop
         
        chunk buffer should only write to file when expected_id == current chunk_id;
        uses unordered_map to keep track of what chunks has been submitted until now .
*/

void ChunkBuffer::writer_loop(){

    std::unique_lock<std::mutex> lock(mtx);

    while(true){

         cv.wait(lock, [&]() {
            return done || buffer.count(expected_chunk_id);
        });

        if(done && buffer.count(expected_chunk_id) == 0)
            break;

        while(buffer.count(expected_chunk_id)){

            Chunk chunk = std::move(buffer[expected_chunk_id]);
            buffer.erase(expected_chunk_id);

            expected_chunk_id++;

            lock.unlock();
            bw.write_bytes(chunk.data);

            lock.lock();
        }
    }

}









