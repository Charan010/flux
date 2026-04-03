#include "chunk_buffer.h"

ChunkBuffer::ChunkBuffer(BitWriter &bw) : bw(bw) {
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


    submit_task()
    @param Chunk chunk

    @brief picks up the lock to put the chunk.id and Chunk into unordered_map and notifies the writer thread to wake up and process the
    chunk and writes it to the file if its the expected chunk.


*/



/*
    The main bottleneck is still that threads are fighting for one mutex and could implement
    lock free CAS solution to reduce mutex and condition variable overhead.

    and also can simply resize the vector to total number of chunks and use that to store becaue of
    it being O(1) access and simple pointer arithmetic.

*/
void ChunkBuffer::submit_chunk(Chunk chunk){
    {
        std::unique_lock<std::mutex> lock(mtx);
        buffer.emplace(chunk.id, std::move(chunk));
    }
    
    cv.notify_one();
}

void ChunkBuffer::writer_loop(){

    std::unique_lock<std::mutex> lock(mtx);

    while (true){

        cv.wait(lock, [&](){
            return done || buffer.count(expected_chunk_id);
        });

        if (done && buffer.count(expected_chunk_id) == 0)
            break;

        while (buffer.count(expected_chunk_id)){

            auto it = buffer.find(expected_chunk_id);
            Chunk chunk = std::move(it->second);

            buffer.erase(expected_chunk_id);

            expected_chunk_id++;

            lock.unlock();
            bw.write_bytes(chunk.data);

            lock.lock();
        }
    }
}

void ChunkBuffer::finish() {
    {
        std::lock_guard<std::mutex> lock(mtx);
        done = true;
    }

    cv.notify_all();

    if (writer_thread.joinable())
        writer_thread.join();
}
