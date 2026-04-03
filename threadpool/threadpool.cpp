#include "threadpool.h"
#include "pipeline/coordinator.h"

Threadpool::Threadpool(size_t num_threads) {

    workers.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i)
        workers.emplace_back(&Threadpool::worker_loop, this);
}

Threadpool::~Threadpool() {
    shutdown();
}

void Threadpool::submit(EncodeTask task) {

    {
        std::lock_guard<std::mutex> lock(mtx);
        jobs.push(std::move(task));
    }

    cv.notify_one();
}

void Threadpool::shutdown() {

    bool expected = false;
    if (!stop.compare_exchange_strong(expected, true))
        return;

    cv.notify_all();

    for (auto &t : workers) {
        if (t.joinable())
            t.join();
    }
}

void Threadpool::worker_loop() {

    while (true) {

            std::unique_lock<std::mutex> lock(mtx);

            cv.wait(lock, [this] {
                return stop.load() || !jobs.empty();
            });

            if (stop.load() && jobs.empty())
                return;

            EncodeTask task = std::move(jobs.front());
            jobs.pop();

            lock.unlock();

        task.coord->encode_chunk(std::move(task.data), task.id, task.table, task.buffer);
        
    }
}