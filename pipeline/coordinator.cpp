#include "coordinator.h"

#include <filesystem>
#include <sys/socket.h>
#include <unistd.h>

#include "json.hpp"

namespace fs = std::filesystem;
using json   = nlohmann::json;


static void send_json_fd(int fd, const json &j) {
  const std::string s = j.dump() + "\n";
  ::write(fd, s.c_str(), s.size());
  ::close(fd);
}


void ErrorReporter::report(const std::string &reason) noexcept {
  try {
    std::call_once(fired, [&] {
      send_json_fd(fd,
                   {{"status",  "error"},
                    {"job_id",  job_id},
                    {"message", reason}});
    });
  } catch (...) {
    ::close(fd);
  }
}


Coordinator::Coordinator(size_t threads) : pool_(threads) {}
Coordinator::~Coordinator() = default;


template <typename Job>
JobHandle Coordinator::submit(std::shared_ptr<Job> job, uint64_t job_id, std::shared_ptr<ErrorReporter> reporter) {

  job->set_on_complete([reporter](bool ok) {
    if (!ok) {
      reporter->report("job failed");  
    }
  });

  job->dispatch();

  auto state_ptr = std::make_shared<std::atomic<JobState>>(JobState::QUEUED);

  JobHandle handle;
  handle.id        = job_id;
  handle.job_state = std::make_shared<std::atomic<JobState>>(job->state());

  auto job_weak = std::weak_ptr<Job>(job);
  auto hs       = handle.job_state;
  pool_.submit([job_weak, hs] {
    while (true) {
      auto sp = job_weak.lock();
      if (!sp) break;
      auto s = sp->state();
      hs->store(s);
      if (s == JobState::COMPLETED || s == JobState::FAILED) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  });

  return handle;
}

JobHandle Coordinator::compress(int client_fd, CompressionMode mode, const std::string &input, const std::string &output,
size_t chunk_size) {

  	const uint64_t job_id = next_job_id_.fetch_add(1);

  	auto reporter       = std::make_shared<ErrorReporter>();
  	reporter->job_id    = job_id;
  	reporter->fd        = client_fd;

  	auto job = std::make_shared<CompressionJob>(job_id, pool_, writer_, mode, input, output, chunk_size);
  	const uint64_t input_size = fs::exists(input) ? fs::file_size(input) : 0;

  	job->set_on_complete([reporter, output, input, input_size, job_id,
                         job_weak = std::weak_ptr<CompressionJob>(job)](bool ok) {
    if (!ok) {

      std::string reason = "compression failed";
      if (auto sp = job_weak.lock(); sp && !sp->last_error().empty())
        reason = sp->last_error();
      reporter->report(reason);
      return;
    }

    uint64_t compressed_size = 0;
    try { compressed_size = fs::file_size(output); } catch (...) {}
    double ratio = (compressed_size > 0)
                       ? static_cast<double>(input_size) / compressed_size
                       : 0.0;

    send_json_fd(reporter->fd,
                 {{"status",          "ok"},
                  {"job_id",          job_id},
                  {"input",           input},
                  {"output",          output},
                  {"input_size",      input_size},
                  {"compressed_size", compressed_size},
                  {"ratio",           ratio}});

    std::call_once(reporter->fired, [] { /* fd already closed above */ });


  });

  job->dispatch();

  JobHandle handle;
  handle.id        = job_id;
  handle.job_state = std::make_shared<std::atomic<JobState>>(job->state());

  return handle;
}

JobHandle Coordinator::decompress(int client_fd, CompressionMode mode,const std::string &input, const std::string &output,
						 size_t chunk_size) {

  const uint64_t job_id = next_job_id_.fetch_add(1);

  auto reporter    = std::make_shared<ErrorReporter>();
  reporter->job_id = job_id;
  reporter->fd     = client_fd;

  auto job = std::make_shared<DecompressionJob>(
      job_id, pool_, writer_, mode, input, output, chunk_size);

  const uint64_t input_size = fs::exists(input) ? fs::file_size(input) : 0;

  job->set_on_complete([reporter, output, input, input_size, job_id,
                         job_weak = std::weak_ptr<DecompressionJob>(job)](bool ok) {
    if (!ok) {
      std::string reason = "decompression failed";
      if (auto sp = job_weak.lock(); sp && !sp->last_error().empty())
        reason = sp->last_error();
      reporter->report(reason);
      return;
    }

    uint64_t output_size = 0;
    try { output_size = fs::file_size(output); } catch (...) {}

    send_json_fd(reporter->fd,
                 {{"status",      "ok"},
                  {"job_id",      job_id},
                  {"input",       input},
                  {"output",      output},
                  {"input_size",  input_size},
                  {"output_size", output_size}});

    std::call_once(reporter->fired, [] {});
  });

  job->dispatch();

  JobHandle handle;
  handle.id        = job_id;
  handle.job_state = std::make_shared<std::atomic<JobState>>(job->state());

  return handle;
}
