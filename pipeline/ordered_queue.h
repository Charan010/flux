#pragma once

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

#include "chunk.h"

class OrderedQueue {

  public:
    explicit OrderedQueue(uint32_t total_chunks)
        : total(total_chunks), remaining(total_chunks),
          next_expected(0), slots(std::make_unique<Slot[]>(total_chunks)) {}

    void push(Chunk chunk) {
        const uint32_t id = static_cast<uint32_t>(chunk.id);
        assert(id < total);

        slots[id].chunk = std::move(chunk);
        slots[id].filled.store(true, std::memory_order_release);

        remaining.fetch_sub(1, std::memory_order_acq_rel);
        cv.notify_one();
    }

    Chunk wait_next() {
        assert(!finished());

        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] {
            return slots[next_expected].filled
                       .load(std::memory_order_acquire);
        });


        Chunk out = std::move(slots[next_expected].chunk);
        slots[next_expected].filled.store(false, std::memory_order_release);
        next_expected++;
        return out;

    }

    bool finished() const {
        return next_expected == total;
    }

  private:
    struct Slot {
        Chunk chunk;
        std::atomic<bool> filled{false};

        Slot() = default;
        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;
        Slot(Slot&&) = delete;
        Slot& operator=(Slot&&) = delete;
    };

    uint32_t total;
    std::atomic<uint32_t> remaining;
    uint32_t next_expected;        

    std::unique_ptr<Slot[]> slots;

    std::mutex mtx;
    std::condition_variable cv;
};