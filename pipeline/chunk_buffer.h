#pragma once

#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <cstdint>
#include <cassert>

#include "bit_io.h"
#include "chunk.h"
#include "progress_bar.h"

class ChunkBuffer {
public:

    ChunkBuffer(BitWriter& bw, uint32_t total_chunks, ProgressBar* progress = nullptr, bool write_headers = true);
    ~ChunkBuffer();

    void submit_chunk(Chunk chunk);
    void finish();

private:
    enum class SlotState : uint32_t {
        Empty    = 0,
        Filled   = 1,
        Consumed = 2,
    };

    struct alignas(64) Slot {

        // separate cacheline ownership
        std::atomic<SlotState> state{SlotState::Empty};

        std::vector<uint8_t> data;

        uint32_t bit_count = 0;
        uint32_t original_size;


        Slot() = default;

        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;

        Slot(Slot&&) = delete;
        Slot& operator=(Slot&&) = delete;
    };

private:
    BitWriter& bw;

    uint32_t total_chunks;

    std::unique_ptr<Slot[]> slots;

    ProgressBar* progress;

    std::mutex sleep_mtx;
    std::condition_variable sleep_cv;
    

    std::atomic<bool> done{false};

    std::thread writer_thread;
    bool write_headers;

private:
    void writer_loop();

    static constexpr int SPIN_COUNT = 1024;
};