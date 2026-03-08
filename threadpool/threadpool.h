#pragma once
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

class ThreadPool {
public:

    ThreadPool(size_t num_threads);

    void submit(std::function<void()> job);

    void shutdown();

    ~ThreadPool();

private:

    void worker_loop();

    std::vector<std::thread> workers;

    std::queue<std::function<void()>> jobs;

    std::mutex mtx;
    std::condition_variable cv;

    bool stop;
};