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


struct ErrorReporter {
  uint64_t    job_id;
  int         fd;          // daemon client fd — send_json writes here then closes
  std::once_flag fired;

  void report(const std::string &reason) noexcept;

};

class Coordinator {
public:
  explicit Coordinator(size_t threads);
  ~Coordinator();

  JobHandle compress(int client_fd, CompressionMode mode, const std::string &input, const std::string &output, size_t chunk_size);

  JobHandle decompress(int client_fd, CompressionMode mode, const std::string &input, const std::string &output, size_t chunk_size);

private:
  template <typename Job>
  JobHandle submit(std::shared_ptr<Job> job, uint64_t job_id,
                   std::shared_ptr<ErrorReporter> reporter);

  Threadpool   pool_;
  SharedWriter writer_;
  std::atomic<uint64_t> next_job_id_{1};
};
