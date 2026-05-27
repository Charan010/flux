#pragma once

#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "chunk.h"

class SharedWriter;

class OrderedQueue {

public:

  OrderedQueue(size_t total_chunks, SharedWriter *writer, uint64_t job_id);

  void push(Chunk chunk);
  bool try_pop(Chunk &out);
  bool is_done();
  void close();

private:
  
  std::unordered_map<uint32_t, Chunk> pending;

  uint32_t expected = 0;
  bool done = false;
  std::mutex mtx;
  size_t total_chunks_;
  uint64_t job_id_;
  SharedWriter *shared_writer_;
};
