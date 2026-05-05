#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>  

class Threadpool {

public:
    explicit Threadpool(size_t num_threads);

    void submit(std::function<void()> job); 
    void shutdown();
    void wait();

    ~Threadpool();

private:
    void worker_loop();

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> jobs;  

    std::mutex mtx;
    std::condition_variable cv;
    std::condition_variable cv_done;
    size_t active_workers = 0;

    std::atomic_bool stop{false};
};