#pragma once

#include <atomic>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "coordinator.h"
#include "json.hpp"

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
static void send_error(int fd, const std::string &msg);
static std::optional<Request> parse_request(const std::string &line);
static CompressionMode parse_codec(std::string codec);

void run_daemon(Coordinator &coordinator, size_t chunk_size);
