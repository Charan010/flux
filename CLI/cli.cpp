#include "cli.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "compression_job.h"
#include "config.h"
#include "decompression_job.h"

namespace fs = std::filesystem;

static void print_header() {
  using namespace term;

  std::cout << "\n"
            << bold << blue << "  flux" << reset << dim
            << "  parallel compression utility" << reset << "\n\n"

            << "  " << cyan << "-c" << reset << "  <codec> <input> <output>"
            << dim << "   compress file" << reset << "\n"

            << "  " << cyan << "-d" << reset << "  <input> <output>" << dim
            << "           decompress file" << reset << "\n"

            << "  " << cyan << "help" << reset << dim
            << "                           show help" << reset << "\n"

            << "  " << cyan << "clear" << reset << dim
            << "                          clear terminal" << reset << "\n"

            << "  " << cyan << "exit" << reset << dim
            << "                           quit" << reset << "\n\n"

            << gray << "  codecs:" << reset << " huffman, lz4\n\n";
}

static void print_info(const std::string &msg) {
  std::cout << term::blue << "[INFO] " << term::reset << msg << "\n";
}

static void print_success(const std::string &msg) {
  std::cout << term::green << "[OK] " << term::reset << msg << "\n";
}

static void print_warning(const std::string &msg) {
  std::cout << term::yellow << "[WARN] " << term::reset << msg << "\n";
}

static void print_error(const std::string &msg) {
  std::cout << term::red << "[ERR] " << term::reset << msg << "\n";
}

template <typename Job>
static bool wait_for_job(const std::shared_ptr<Job> &job, double &seconds) {
  const auto start = std::chrono::high_resolution_clock::now();

  job->dispatch();

  while (true) {

    JobState state = job->state();

    if (state == JobState::COMPLETED) {

      const auto end = std::chrono::high_resolution_clock::now();

      seconds = std::chrono::duration<double>(end - start).count();

      return true;
    }

    if (state == JobState::FAILED) {

      const auto end = std::chrono::high_resolution_clock::now();

      seconds = std::chrono::duration<double>(end - start).count();

      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

static std::optional<CompressionMode> parse_codec(const std::string &codec) {

  if (codec == "lz4")
    return CompressionMode::LZ4;

  if (codec == "huffman")
    return CompressionMode::Huffman;

  return std::nullopt;
}

static void handle_compress(Threadpool &pool, SharedWriter &writer,
                            uint64_t &next_job_id, size_t chunk_size,
                            std::stringstream &ss) {

  std::string codec;
  std::string input;
  std::string output;

  ss >> codec >> input >> output;

  if (codec.empty() || input.empty() || output.empty()) {
    print_warning("Usage: -c <codec> <input> <output>");
    return;
  }

  if (!fs::exists(input)) {
    print_error("Input file does not exist");
    return;
  }

  auto mode = parse_codec(codec);

  if (!mode) {
    print_error("Unknown codec: " + codec);
    return;
  }

  const uint64_t original_size = fs::file_size(input);

  auto job = std::make_shared<CompressionJob>(next_job_id++, pool, writer,
                                              *mode, input, output, chunk_size);

  double seconds = 0.0;

  if (!wait_for_job(job, seconds)) {
    print_error("Compression failed");
    return;
  }

  const uint64_t compressed_size = fs::file_size(output);

  const double ratio =
      compressed_size > 0 ? static_cast<double>(original_size) / compressed_size
                          : 0.0;

  const double throughput = (original_size / (1024.0 * 1024.0)) / seconds;
  print_success("Compression complete");

  std::cout

      << "\n"
      << "  Input:       " << input << "\n"

      << "  Output:      " << output << "\n"

      << "  Original:    " << human_size(original_size) << "\n"

      << "  Compressed:  " << human_size(compressed_size) << "\n"

      << "  Ratio:       " << std::fixed << std::setprecision(2) << ratio
      << "x\n"

      << "  Speed:       " << throughput << " MB/s\n"

      << "  Time:        " << seconds << " s\n\n";
}

static void handle_decompress(Threadpool &pool, SharedWriter &writer,
                              uint64_t &next_job_id, size_t chunk_size,
                              std::stringstream &ss) {

  std::string input;
  std::string output;

  ss >> input >> output;

  if (input.empty() || output.empty()) {
    print_warning("Usage: -d <input> <output>");
    return;
  }

  if (!fs::exists(input)) {
    print_error("Input file does not exist");
    return;
  }

  auto job = std::make_shared<DecompressionJob>(next_job_id++, pool, writer,
                                                CompressionMode::LZ4, input,
                                                output, chunk_size);

  double seconds = 0.0;

  if (!wait_for_job(job, seconds)) {
    print_error("Decompression failed");
    return;
  }

  const uint64_t output_size = fs::file_size(output);
  const double throughput = (output_size / (1024.0 * 1024.0)) / seconds;
  print_success("Decompression complete");

  std::cout << "\n"
            << "  Input:       " << input << "\n"

            << "  Output:      " << output << "\n"

            << "  Size:        " << human_size(output_size) << "\n"

            << "  Speed:       " << throughput << " MB/s\n"

            << "  Time:        " << seconds << " s\n\n";
}

void run_cli(Threadpool &pool, SharedWriter &writer, size_t chunk_size) {

  uint64_t next_job_id = 1;
  print_header();
  print_info("Threads: " + std::to_string(Config::threadpool_size));
  print_info("Chunk Size: " + human_size(chunk_size));

  std::cout << "\n";

  std::string line;

  while (true) {

    std::cout << term::bold << term::blue << "flux> " << term::reset;
    if (!std::getline(std::cin, line))
      break;

    std::stringstream ss(line);

    std::string cmd;
    ss >> cmd;

    if (cmd.empty())
      continue;

    if (cmd == "clear")
      clear_screen();

    else if (cmd == "exit" || cmd == "quit") {
      print_info("Shutting down...");
      break;
    }

    else if (cmd == "help")
      print_header();

    else if (cmd == "-c")
      handle_compress(pool, writer, next_job_id, chunk_size, ss);

    else if (cmd == "-d")
      handle_decompress(pool, writer, next_job_id, chunk_size, ss);

    else {
      print_error("Unknown command");
      print_info("Type 'help' for usage");
    }
  }
}