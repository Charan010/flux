#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

#include "chunk_buffer.h"
#include "threadpool.h"

enum class CompressionMode { Huffman, LZ4, LZ4_Huffman };

class CompressionJob {

  public:
    CompressionJob(Threadpool& pool, CompressionMode mode, const std::string& input,
                   const std::string& output, size_t chunk_size);

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

    std::unique_ptr<ChunkBuffer> chunk_buffer;
};