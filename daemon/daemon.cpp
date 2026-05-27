#include <string>

#include "daemon.h"

//TO-DO: This hammers read() syscall for every byte which is a bottleneck. Should implement buffer to reduce read()
// system calls.
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

static void send_json(int fd, const json &j) {

  const std::string response = j.dump() + "\n";
  write(fd, response.c_str(), response.size());
  close(fd);
}

static void send_error(int fd, const std::string &msg) {
  send_json(fd, {{"status", "error"}, {"message", msg}

                });
}

static std::optional<Request> parse_request(const std::string &line) {

  try {

    json j = json::parse(line);

    return Request{.action = j.at("action"),
                   .input = j.at("input"),
                   .output = j.at("output"),
                   .codec = j.value("codec", "lz4")

    };
  }

  catch (...) {
    return std::nullopt;
  }
}

static CompressionMode parse_codec(std::string codec) {

  if (codec == "lz4")
    return CompressionMode::LZ4;

  if (codec == "huffman")
    return CompressionMode::Huffman;

  throw std::runtime_error("unknown codec");
}

static void handle_compress(int fd, const Request &rq, Threadpool &pool,
                            SharedWriter &writer, uint64_t job_id,
                            size_t chunk_size) {

  CompressionMode mode;

  try {
    mode = parse_codec(rq.codec);
  }

  catch (...) {
    send_error(fd, "unknown codec: " + rq.codec);
    return;
  }

  const uint64_t input_size = fs::file_size(rq.input);
  auto job = std::make_shared<CompressionJob>(job_id, pool, writer, mode,
                                              rq.input, rq.output, chunk_size);

  job->set_on_complete(
      [fd, rq, input_size, job_id,
       job_weak = std::weak_ptr<CompressionJob>(job)](bool ok) {
        if (!ok) {
          std::string reason = "compression failed";
          if (auto sp = job_weak.lock(); sp && !sp->last_error().empty())
            reason = sp->last_error();
          send_json(fd, {{"status", "error"},
                         {"job_id", job_id},
                         {"input", rq.input},
                         {"output", rq.output},
                         {"message", reason}});
          return;
        }

        const uint64_t compressed_size = fs::file_size(rq.output);
        double ratio = compressed_size > 0
                           ? static_cast<double>(input_size) / compressed_size
                           : 0.0;

        send_json(fd, {{"status", "ok"},
                       {"job_id", job_id},
                       {"input", rq.input},
                       {"output", rq.output},
                       {"input_size", input_size},
                       {"compressed_size", compressed_size},
                       {"ratio", ratio}});
      });

  job->dispatch();
}

static void handle_decompress(int fd, const Request &rq, Threadpool &pool,
                              SharedWriter &writer, uint64_t job_id,
                              size_t chunk_size) {

  const uint64_t input_size = fs::file_size(rq.input);
  // Initial mode doesn't matter — dispatch() reads the codec byte from the
  // file header and overwrites self->mode_ before calling create_engine().
  auto job = std::make_shared<DecompressionJob>(job_id, pool, writer,
                                                CompressionMode::LZ4, rq.input,
                                                rq.output, chunk_size);

  job->set_on_complete(
      [fd, rq, input_size, job_id,
       job_weak = std::weak_ptr<DecompressionJob>(job)](bool ok) {
        if (!ok) {
          std::string reason = "decompression failed";
          if (auto sp = job_weak.lock(); sp && !sp->last_error().empty())
            reason = sp->last_error();
          send_json(fd, {{"status", "error"},
                         {"job_id", job_id},
                         {"input", rq.input},
                         {"output", rq.output},
                         {"message", reason}});
          return;
        }
        send_json(fd, {{"status", "ok"},
                       {"job_id", job_id},
                       {"input", rq.input},
                       {"output", rq.output},
                       {"input_size", input_size},
                       {"output_size", fs::file_size(rq.output)}});
      });

  job->dispatch();
}

void run_daemon(Threadpool &pool, SharedWriter &writer, size_t chunk_size) {

  int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);

  if (server_fd < 0) {
    std::cerr << "[flux daemon] failed to create socket" << "\n";
    return;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;

  strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
  unlink(SOCKET_PATH);

  if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {

    std::cerr << "[flux daemon] bind() failed" << "\n";
    close(server_fd);

    return;
  }

  if (listen(server_fd, 128) < 0) {

    std::cerr << "[flux daemon] listen() failed\n";
    close(server_fd);
    return;
  }

  std::atomic<uint64_t> next_job_id{1};

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

    const uint64_t job_id = next_job_id.fetch_add(1);

    if (req->action == "compress")
      handle_compress(client_fd, *req, pool, writer, job_id, chunk_size);

    else if (req->action == "decompress")
      handle_decompress(client_fd, *req, pool, writer, job_id, chunk_size);

    else {
      send_error(client_fd, "unknown action");
    }
  }

  close(server_fd);
  unlink(SOCKET_PATH);
}
