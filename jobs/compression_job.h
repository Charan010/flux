#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "codec_engine.h"
#include "ordered_queue.h"
#include "shared_writer.h"
#include "threadpool.h"
#include "flux_format.h"

enum class CompressionMode {
    Huffman,
    LZ4
};

enum class JobState {
    QUEUED,
    RUNNING,
    WRITING,
    COMPLETED,
    FAILED
};

/*

*CompressionJob lifecycle:

QUEUED
  ↓
RUNNING      -> worker threads compress chunks
  ↓
WRITING      -> SharedWriter draining ordered chunks
  ↓
COMPLETED    -> output flushed + fsync succeeded

*INVARIANTS:
* - SharedWriter is sole owner of output ordering.
* - Compression workers never write directly to disk.
* - COMPLETED means output file is finalized successfully.
* - CompressionJob may outlive request thread.
* - OrderedQueue guarantees in-order chunk visibility.

*/


class CompressionJob :
    public std::enable_shared_from_this<CompressionJob> {

public:

    CompressionJob(uint64_t job_id, Threadpool& pool,
        SharedWriter& shared_writer, CompressionMode mode,
        const std::string& input_file, const std::string& output_file,
        size_t chunk_size
    
    );

    void start_async();

    uint64_t id() const;

    JobState state() const;

    const std::string& input_path() const;
    const std::string& output_path() const;

    void mark_completed();
    void mark_failed();

private:

    void submit_chunks(const uint8_t* data, size_t input_size);
    std::shared_ptr<CodecEngine> create_engine();

private:

    uint64_t job_id_;

    Threadpool& pool_;
    SharedWriter& shared_writer_;

    CompressionMode mode_;

    std::string input_file_;
    std::string output_file_;

    size_t chunk_size_;

    std::atomic<JobState> state_ {
        JobState::QUEUED
    };

    std::atomic<uint32_t> remaining_chunks_ {0};
    std::shared_ptr<OrderedQueue> ordered_queue_;
    std::shared_ptr<WriterJob> writer_job_;
    
};