#pragma once

#include <vector>
#include <optional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <thread>
#include "bit_io.h"
#include "chunk.h"

class ChunkBuffer {

    private:
        BitWriter& bw;
        std::vector<std::optional<Chunk>> buffer;   

        std::mutex mtx;
        std::condition_variable cv;

        uint32_t expected_chunk_id = 0;
        std::atomic<bool> done{false};              

        std::thread writer_thread;

    public:
        ChunkBuffer(BitWriter& bw, uint32_t total_chunks); 

        void submit_chunk(Chunk chunk);
        void writer_loop();
        void finish();
};