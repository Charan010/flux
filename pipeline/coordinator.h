#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "compression_job.h"
#include "decompression_job.h"
#include "shared_writer.h"
#include "threadpool.h"

struct JobHandle {
  uint64_t id;
  JobState state() const { return job_state->load(); }

  bool done() const {
    auto s = job_state->load();
    return s == JobState::COMPLETED || s == JobState::FAILED;
  }

  std::shared_ptr<std::atomic<JobState>> job_state;
};


class Coordinator {
public:
  explicit Coordinator(size_t threads);
  ~Coordinator();

  JobHandle compress(CompressionMode mode, const std::string &input,const std::string &output,
		 size_t chunk_size, std::function<void(uint64_t, bool)> on_done = nullptr);

  JobHandle decompress(CompressionMode mode, const std::string &input, const std::string &output,
		 size_t chunk_size, std::function<void(uint64_t, bool)> on_done = nullptr);

private:
  template <typename Job>
  JobHandle submit(std::shared_ptr<Job> job, uint64_t job_id,
                   std::function<void(uint64_t, bool)> on_done);

  Threadpool pool_;
  SharedWriter writer_;
};
