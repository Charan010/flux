#pragma once

#include <atomic>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "compression_job.h"
#include "decompression_job.h"
#include "json.hpp"
#include "shared_writer.h"
#include "threadpool.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

static constexpr const char *SOCKET_PATH = "/tmp/flux.sock";

struct Request {
  std::string action;
  std::string input;
  std::string output;
  std::string codec = "lz4";
  
};

static bool read_line(int fd, std::string &out);
static void send_json(int fd, const json &j);
static void send_error(int fd, const std::string &msg);
static std::optional<Request> parse_request(const std::string &line);

static CompressionMode parse_codec(std::string codec);

static void handle_compress(int fd, const Request &rq, Threadpool &pool, SharedWriter &writer,
		 uint64_t job_id, size_t chunk_size);

void run_daemon(Threadpool &pool, SharedWriter &writer, size_t chunk_size);
