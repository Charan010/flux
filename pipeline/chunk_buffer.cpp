#include "chunk_buffer.h"
#include "huffman.h"

ChunkBuffer::ChunkBuffer(BitWriter& bw, uint32_t total_chunks, ProgressBar* progress, bool write_headers):
    bw(bw),
    total_chunks(total_chunks),
    progress(progress),
    write_headers(write_headers),
    slots(new Slot[total_chunks]) {

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


void ChunkBuffer::submit_chunk(Chunk chunk) {
    if (chunk.id >= (int)total_chunks) return;

    Slot &slot = slots[chunk.id];

    slot.bit_count = chunk.bit_count;
    slot.data = std::move(chunk.data);
    
    slot.state.store(SlotState::Filled, std::memory_order_release);
    
    {
        std::lock_guard<std::mutex> lock(sleep_mtx);
        sleep_cv.notify_one();
    }
}

void ChunkBuffer::writer_loop() {
    uint32_t next = 0;

    while (next < total_chunks) {
        Slot &slot = slots[next];

        int spin = SPIN_COUNT;
        while (slot.state.load(std::memory_order_acquire) != SlotState::Filled && spin-- > 0) {
            #if defined(__x86_64__) || defined(_M_X64)
                __asm__ volatile("pause" ::: "memory");  
            #elif defined(__aarch64__)
                __asm__ volatile("yield" ::: "memory");
            #endif
        }

        if (slot.state.load(std::memory_order_acquire) != SlotState::Filled) {
            std::unique_lock<std::mutex> lock(sleep_mtx);
            sleep_cv.wait(lock, [&] {
                return slot.state.load(std::memory_order_acquire) == SlotState::Filled || done.load(std::memory_order_relaxed);
            });
        }

        if (slot.state.load(std::memory_order_acquire) != SlotState::Filled) {
            break; 
        }

        if (write_headers) {
            write_uint32(bw, slot.bit_count);
        }
        
        bw.write_bytes(slot.data); 

        if (progress) {
            progress->update(1); 
        }

        slot.data.clear();
        slot.data.shrink_to_fit();

        slot.state.store(SlotState::Consumed, std::memory_order_release);
        ++next;
    }
}

void ChunkBuffer::finish() {
    if (done.exchange(true, std::memory_order_acq_rel))
        return; 
    sleep_cv.notify_all();
    if (writer_thread.joinable())
        writer_thread.join();
}