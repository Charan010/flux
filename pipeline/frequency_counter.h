#pragma once

#include "threadpool.h"
#include "chunk.h"
#include <array>
#include <atomic>
#include <mutex>

using FrequencyTable = std::array<uint64_t,256>;

class FrequencyCounter {

public:


    FrequencyCounter(ThreadPool& pool);
    void submit_chunk(const Chunk& chunk);
    void wait();
    FrequencyTable get_result();

private:
    ThreadPool& pool;
    FrequencyTable global_freq;
    std::mutex freq_mtx;
    std::atomic<int> pending;
    std::condition_variable done_cv;
    std::mutex done_mtx;
};