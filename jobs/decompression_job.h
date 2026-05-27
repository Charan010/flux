#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "LZ4_engine.h"
#include "codecs/codec_engine.h"
#include "jobs_common.h"
#include "ordered_queue.h"
#include "shared_writer.h"
#include "threadpool.h"

class DecompressionJob : public std::enable_shared_from_this<DecompressionJob> {

public:
  DecompressionJob(uint64_t job_id, Threadpool &pool,
                   SharedWriter &shared_writer, CompressionMode mode,
                   const std::string &input_file,
                   const std::string &output_file, size_t chunk_size);

  void dispatch();

  uint64_t id() const;
  JobState state() const;

  const std::string &input_path() const;
  const std::string &output_path() const;

  void set_on_complete(std::function<void(bool)> cb) {
    on_complete_ = std::move(cb);
  }

  void mark_completed();
  void mark_failed();
  void mark_failed(std::string reason);

  const std::string &last_error() const { return last_error_; }

private:
  std::shared_ptr<CodecEngine> create_engine();

private:
  uint64_t job_id_;

  Threadpool &pool_;
  SharedWriter &shared_writer_;

  CompressionMode mode_;

  std::string input_file_;
  std::string output_file_;

  size_t chunk_size_;

  std::atomic<JobState> state_{JobState::QUEUED};
  std::atomic<uint32_t> remaining_chunks_{0};

  std::shared_ptr<OrderedQueue> ordered_queue_;
  std::shared_ptr<WriterJob> writer_job_;

  std::string last_error_;
  std::mutex error_mtx_;

  std::once_flag fail_once_;

  std::function<void(bool)> on_complete_;
};