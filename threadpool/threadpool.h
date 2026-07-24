#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

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

  std::deque<std::function<void()>> jobs;

  std::mutex mtx;
  std::condition_variable cv;
  std::condition_variable cv_done;
  size_t active_workers = 0;

  std::atomic_bool stop{false};

  std::mutex err_mtx;
  std::exception_ptr first_error;
};