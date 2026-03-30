#pragma once

#include <vector>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include "bit_io.h"
#include "chunk.h"


class ChunkBuffer{

    private:
        BitWriter &bw;
        std::unordered_map<int, Chunk> buffer;

        std::mutex mtx;
        std::condition_variable cv;

        int expected_chunk_id = 0;
        bool done = false;


    public:

        void submit_chunk(Chunk chunk);
        void writer_loop();
        void finish();
};