#include "chunk_buffer.h"

ChunkBuffer::ChunkBuffer(BitWriter &bw, uint32_t total_chunks)
    : bw(bw){
        
    buffer.resize(total_chunks);
    writer_thread = std::thread(&ChunkBuffer::writer_loop, this);
}

/*
    writer_loop()

    @brief It is a single thread loop which gets woken up everytime threadpool submits a task.
    and checks if it is the expected_chunk or not. If yes, it gets written to the file using BitWriter class which
    abstracts std::ofstream.

    @contract
        It keeps track of all chunks of the file and starts writing to file in the same order.
        It is not fault tolerant (to be done in future) so, data can be lost even if the data/chunk
        is encoded.


    submit_chunk()
    @param Chunk chunk

    @brief picks up the lock to put the chunk into the pre-sized vector slot at index chunk.id
    and notifies the writer thread to wake up and process the chunk and writes it to the file
    if its the expected chunk.
*/


void ChunkBuffer::submit_chunk(Chunk chunk){
    {
        std::lock_guard<std::mutex> lock(mtx);
        buffer[chunk.id] = std::move(chunk);
    }

    cv.notify_one();
}

void ChunkBuffer::writer_loop(){


    std::unique_lock<std::mutex> lock(mtx);

    while (true) {

        cv.wait(lock, [this]() {
            return done.load(std::memory_order::relaxed)
                || (expected_chunk_id < buffer.size()
                    && buffer[expected_chunk_id].has_value());
        });

        if (done.load(std::memory_order::relaxed)
                && (expected_chunk_id >= buffer.size()
                    || !buffer[expected_chunk_id].has_value()))
            break;

        while (expected_chunk_id < buffer.size()
               && buffer[expected_chunk_id].has_value())
        {
            // Move out of the optional slot, then clear it to release memory.
            Chunk chunk = std::move(*buffer[expected_chunk_id]);
            buffer[expected_chunk_id].reset();

            expected_chunk_id++;

            lock.unlock();
            bw.write_bytes(chunk.data);
            lock.lock();
        }
    }
}


void ChunkBuffer::finish(){
    
    done.store(true, std::memory_order::release);
    cv.notify_all();

    if (writer_thread.joinable())
        writer_thread.join();
}