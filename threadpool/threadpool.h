#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <array>

#include "pipeline/chunk.h"
#include "pipeline/chunk_buffer.h"
#include "huffman.h"

class Coordinator;

struct EncodeTask {
    std::vector<uint8_t> data;
    int id;
    Coordinator* coord;
    const std::array<HuffmanCode,256>* table;
    ChunkBuffer* buffer;
};


class Threadpool{

public:
        explicit Threadpool(size_t num_threads);

        void submit(EncodeTask task);
        void shutdown();

        ~Threadpool();
private:

    void worker_loop();
    std::vector<std::thread> workers;
    std::queue<EncodeTask> jobs;

    std::mutex mtx;
    std::condition_variable cv;

    std::atomic_bool stop{false};
};

