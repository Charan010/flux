#include "chunk_buffer.h"

ChunkBuffer::ChunkBuffer(BitWriter& bw, uint32_t total_chunks)
    : bw(bw), total_chunks(total_chunks) , slots(new Slot[total_chunks]){

    writer_thread = std::thread(&ChunkBuffer::writer_loop, this);
}

ChunkBuffer::~ChunkBuffer(){
    finish();
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

    assert(chunk.id < (int)total_chunks);

    Slot &slot = slots[chunk.id];


    /* std::memory_order_release offers happens before gurantee and data and state would not be
        available to other cores until these instructions are executed. Cpu is free to do out of order
        optimization but it gives us the gurantee that this state will be flipped first.
    */

    slot.data = std::move(chunk.data);
    slot.state.store(SlotState::Filled, std::memory_order_release);
    sleep_cv.notify_one();

}


void ChunkBuffer::writer_loop(){

    uint32_t next = 0;

    while(next  < total_chunks){
        Slot &slot = slots[next];

        int spin = SPIN_COUNT;

        /* instead of multiple threads hammering on one lock, we can just afford to do spin lock waiting because of short
            time required for the chunks to be submitted, so, the maximum number of spins that we can afford is SPIN_COUNT = 1024
        */
        while(slot.state.load(std::memory_order_acquire) != SlotState::Filled && spin-- > 0){


            /*
                hints CPU that these instructions are busy waiting and coooperates with other threads and allows other threads
                for execution because its just burning CPU cycles without much progress. 
            
                also reduces pipeline power need.
            */
           
            #if defined(__x86_64__) || defined(_M_X64)
                __asm__ volatile("pause" ::: "memory");  
            #elif defined(__aarch64__)
                __asm__ volatile("yield" ::: "memory");
            
            #endif
        }

        if(slot.state.load(std::memory_order_acquire) != SlotState::Filled){

            std::unique_lock<std::mutex> lock(sleep_mtx);

            sleep_cv.wait(lock, [&] {
                return slot.state.load(std::memory_order_acquire) == SlotState::Filled || done.load(std::memory_order_relaxed);
            });

        }

        if(slot.state.load(std::memory_order_acquire) != SlotState::Filled)
            break;

        bw.write_bytes(slot.data);

        {std::vector<uint8_t> tmp; std::swap(slot.data, tmp); }

        slot.state.store(SlotState::Consumed, std::memory_order_release);
        ++next;
        }
}

void ChunkBuffer::finish() {
    done.store(true, std::memory_order_release);
    sleep_cv.notify_all();

    if (writer_thread.joinable())
        writer_thread.join();
}