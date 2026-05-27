#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include "bit_io.h"
#include "ordered_queue.h"


class CompressionJob;

/*
 * SharedWriter:
 * Centralized serial writer for all compression and decompression jobs.
 *
 * Contract:
 * - Multiple jobs can enqueue write notifications concurrently.
 * - Single internal thread performs writing of chunks to disk.
 * - Each OrderedQueue guarantees in-order chunk delivery.
 */

class OrderedQueue;

struct WriterJob {
  uint64_t id;
  std::shared_ptr<OrderedQueue> queue;
  std::shared_ptr<BitWriter> writer;
  std::weak_ptr<CompressionJob> owner;

  std::function<void()> on_complete;
  bool raw_output = false;
};

class SharedWriter {

public:
  SharedWriter();
  ~SharedWriter();

  void register_job(std::shared_ptr<WriterJob> job);

  /*
   * Wake writer thread.
   *
   * Invariant:
   * Waking up doesn't really imply that next chunk id has arrived in the
   * ordered queue but instead just a hint to the shared writer that this may be
   * the chunk that needs to be written to disk.
   */
  void notify();

private:
  void writer_loop();

private:
  std::unordered_map<uint64_t, std::shared_ptr<WriterJob>> jobs;
  std::mutex mtx;
  std::condition_variable cv;
  std::thread writer_thread;
  bool running = true;
};
