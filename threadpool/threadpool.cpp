#include "threadpool.h"

Threadpool::Threadpool(size_t num_threads) {

    workers.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i)
        workers.emplace_back(&Threadpool::worker_loop, this);
}

Threadpool::~Threadpool() {
    shutdown();
}

void Threadpool::submit(std::function<void()> job) {

    {
        std::lock_guard<std::mutex> lock(mtx);
        jobs.push(std::move(job));
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

        auto job = std::move(jobs.front());
        jobs.pop();

        lock.unlock();

        job(); 
    }
}