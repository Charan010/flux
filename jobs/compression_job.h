#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

#include "ordered_queue.h"
#include "threadpool.h"
#include "shared_writer.h"

enum class CompressionMode { Huffman, LZ4, LZ4_Huffman };

class CompressionJob {

  public:
  
    CompressionJob(Threadpool& pool, SharedWriter& shared_writer, CompressionMode mode,
     const std::string& input, const std::string& output, size_t chunk_size);

    void start();

  private:

    void submit_chunks();

  private:

    Threadpool& pool;

    CompressionMode mode;

    std::string input_file;
    std::string output_file;

    size_t chunk_size;

    std::atomic<uint32_t> remaining_chunks{0};

    std::mutex done_mtx;
    std::condition_variable done_cv;
    std::unique_ptr<OrderedQueue>ordered_queue;
    
};