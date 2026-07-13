#include "daemon.h"

#include <string>

// TO-DO: This hammers read() syscall for every byte which is a bottleneck.
// Should implement a buffer to reduce read() system calls.
static bool read_line(int fd, std::string &out) {
  out.clear();
  char c;
  while (true) {
    ssize_t n = read(fd, &c, 1);
    if (n <= 0)
      return false;
    if (c == '\n')
      return true;
    out.push_back(c);
  }
}

// Used only for pre-dispatch errors (bad JSON, missing file, unknown codec).
// After dispatch the Coordinator / ErrorReporter owns the fd.
static void send_error(int fd, const std::string &msg) {
  const json j = {{"status", "error"}, {"message", msg}};
  const std::string s = j.dump() + "\n";
  ::write(fd, s.c_str(), s.size());
  ::close(fd);
}

static std::optional<Request> parse_request(const std::string &line) {
  try {
    json j = json::parse(line);
    return Request{.action = j.at("action"),
                   .input  = j.at("input"),
                   .output = j.at("output"),
                   .codec  = j.value("codec", "lz4")};
  } catch (...) {
    return std::nullopt;
  }
}

static CompressionMode parse_codec(std::string codec) {
  if (codec == "lz4")     return CompressionMode::LZ4;
  if (codec == "huffman") return CompressionMode::Huffman;
  throw std::runtime_error("unknown codec");
}

void run_daemon(Coordinator &coordinator, size_t chunk_size) {

  int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "[flux daemon] failed to create socket\n";
    return;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
  unlink(SOCKET_PATH);

  if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    std::cerr << "[flux daemon] bind() failed\n";
    close(server_fd);
    return;
  }

  if (listen(server_fd, 128) < 0) {
    std::cerr << "[flux daemon] listen() failed\n";
    close(server_fd);
    return;
  }

  std::cerr << "[flux daemon] listening on " << SOCKET_PATH << "\n";

  while (true) {

    int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd < 0)
      continue;

    std::string line;
    if (!read_line(client_fd, line)) {
      close(client_fd);
      continue;
    }

    auto req = parse_request(line);
    if (!req) {
      send_error(client_fd, "invalid request");
      continue;
    }

    if (!fs::exists(req->input)) {
      send_error(client_fd, "input file does not exist");
      continue;
    }

    CompressionMode mode;
    try {
      mode = parse_codec(req->codec);
    } catch (...) {
      send_error(client_fd, "unknown codec: " + req->codec);
      continue;
    }

    // Ownership of client_fd is handed to the Coordinator.
    // It will write the JSON response (ok or error) and close the fd.
    if (req->action == "compress") {
      coordinator.compress(client_fd, mode, req->input, req->output, chunk_size);

    } else if (req->action == "decompress") {
      coordinator.decompress(client_fd, mode, req->input, req->output, chunk_size);

    } else {
      send_error(client_fd, "unknown action");
    }
  }

  close(server_fd);
  unlink(SOCKET_PATH);
}
