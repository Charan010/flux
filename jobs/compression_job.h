#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "codecs/codec_engine.h"
#include "jobs_common.h"
#include "ordered_queue.h"
#include "shared_writer.h"
#include "threadpool.h"

class CompressionJob : public std::enable_shared_from_this<CompressionJob> {

public:
  CompressionJob(uint64_t job_id, Threadpool &pool, SharedWriter &shared_writer,
                 CompressionMode mode, const std::string &input_file,
                 const std::string &output_file, size_t chunk_size);

  void dispatch();

  uint64_t id() const;
  JobState state() const;

  const std::string &input_path() const;
  const std::string &output_path() const;

  void set_on_complete(std::function<void(bool)> cb) {
    on_complete = std::move(cb);
  }

  void mark_completed();
  void mark_failed();
  void mark_failed(std::string reason);

  const std::string &last_error() const {
	return last_error_; 
}

private:
  std::shared_ptr<CodecEngine> create_engine();

private:
  uint64_t job_id;	

  Threadpool &pool;
  SharedWriter &shared_writer;

  CompressionMode mode;

  std::string input_file;
  std::string output_file;

  size_t chunk_size;

  std::atomic<JobState> state_{JobState::QUEUED};
  std::atomic<uint32_t> remaining_chunks{0};

  std::shared_ptr<OrderedQueue> ordered_queue;
  std::shared_ptr<WriterJob> writer_job;

  std::string last_error_;
  std::mutex error_mtx;

  std::function<void(bool)> on_complete;
};