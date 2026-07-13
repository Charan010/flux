#include "bench.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <vector>

#include "LZ4_engine.h"
#include "chunk.h"
#include "config.h"
#include "lz4.h"
#include "mmap_file.h"
#include "threadpool.h"

static double median(std::vector<double> &v) {
  std::sort(v.begin(), v.end());
  size_t n = v.size();
  return (n % 2 == 0) ? (v[n / 2 - 1] + v[n / 2]) / 2.0 : v[n / 2];
}

struct BenchResult {
  double compress_mb_s = 0;
  double decompress_mb_s = 0;
  double ratio = 0;
  size_t compressed_bytes = 0;
  size_t original_bytes = 0;
};

static BenchResult run_bench(const uint8_t *data, size_t data_size,
                             size_t chunk_size, int iters, Threadpool &pool) {

  const size_t num_chunks = (data_size + chunk_size - 1) / chunk_size;
  const size_t bound = lz4_compress_bound(chunk_size);

  struct ChunkBuf {
    std::vector<uint8_t> compressed;
    std::vector<uint8_t> decompressed;
    size_t compressed_size = 0;
    size_t original_size = 0;
  };

  std::vector<ChunkBuf> bufs(num_chunks);
  for (size_t i = 0; i < num_chunks; ++i) {
    bufs[i].compressed.resize(bound);
    size_t orig = std::min(chunk_size, data_size - i * chunk_size);
    bufs[i].decompressed.resize(orig);
    bufs[i].original_size = orig;
  }

  for (size_t i = 0; i < num_chunks; ++i) {
    const uint8_t *src = data + i * chunk_size;
    bufs[i].compressed_size =
        lz4_compress(src, bufs[i].original_size, bufs[i].compressed.data());
  }

  std::vector<double> comp_times(iters);
  for (int it = 0; it < iters; ++it) {

    std::atomic<size_t> done{0};
    std::mutex finish_mtx;
    std::condition_variable finish_cv;

    auto t0 = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < num_chunks; ++i) {
      pool.submit([&, i] {
        const uint8_t *src = data + i * chunk_size;
        bufs[i].compressed_size =
            lz4_compress(src, bufs[i].original_size, bufs[i].compressed.data());

        if (done.fetch_add(1) + 1 == num_chunks)
          finish_cv.notify_one();
      });
    }

    {
      std::unique_lock lk(finish_mtx);
      finish_cv.wait(lk, [&] { return done.load() == num_chunks; });
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    comp_times[it] = std::chrono::duration<double>(t1 - t0).count();
  }

  std::vector<double> decomp_times(iters);
  for (int it = 0; it < iters; ++it) {

    std::atomic<size_t> done{0};
    std::mutex finish_mtx;
    std::condition_variable finish_cv;

    auto t0 = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < num_chunks; ++i) {
      pool.submit([&, i] {
        lz4_decompress(bufs[i].compressed.data(), bufs[i].compressed_size,
                       bufs[i].decompressed.data());

        if (done.fetch_add(1) + 1 == num_chunks)
          finish_cv.notify_one();
      });
    }

    {
      std::unique_lock lk(finish_mtx);
      finish_cv.wait(lk, [&] { return done.load() == num_chunks; });
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    decomp_times[it] = std::chrono::duration<double>(t1 - t0).count();
  }

  size_t total_compressed = 0;
  for (auto &b : bufs)
    total_compressed += b.compressed_size;

  const double mb = data_size / (1024.0 * 1024.0);
  const double comp_med = median(comp_times);
  const double decomp_med = median(decomp_times);

  BenchResult r;
  r.original_bytes = data_size;
  r.compressed_bytes = total_compressed;
  r.ratio = static_cast<double>(data_size) / total_compressed;
  r.compress_mb_s = mb / comp_med;
  r.decompress_mb_s = mb / decomp_med;
  return r;
}


static void print_row(const char *label, const char *value,
                      const char *unit = "", const char *color = term::reset) {
  std::cout << "  " << term::gray << std::left << std::setw(18) << label
            << term::reset << color << std::right << std::setw(10) << value
            << term::reset << "  " << term::dim << unit << term::reset << "\n";
}


void run_bench_mode(const std::string &input_path, Threadpool &pool) {

  using namespace term;

  std::cout << "\n"
            << bold << blue << "  flux" << reset << dim << "  benchmark mode"
            << reset << "\n\n"
            << "  " << gray << "file:   " << reset << input_path << "\n"
            << "  " << gray << "codec:  " << reset << "lz4 (raw)\n"
            << "  " << gray << "threads:" << reset << " "
            << Config::threadpool_size << "\n\n";

  MappedFile mapped(input_path);
  if (mapped.size() == 0) {
    std::cerr << red << "[ERR]" << reset
              << " input file is empty or unreadable\n";
    return;
  }

  std::cout << "  " << cyan << "running benchmark..." << reset
            << "  (1 warm-up + 5 timed iterations each)\n\n";

  struct Trial {
    const char *label;
    size_t chunk_size;
  };
  const Trial trials[] = {
      {"64 KB chunks", 64 << 10},
      {"1 MB  chunks", 1 << 20},
      {"4 MB  chunks", 4 << 20}, 
  };

  for (auto &t : trials) {
    if (t.chunk_size > mapped.size() * 4)
      continue;

    BenchResult r =
        run_bench(mapped.data(), mapped.size(), t.chunk_size, 5, pool);

    auto fmt = [](double v) -> std::string {
      std::ostringstream s;
      s << std::fixed << std::setprecision(0) << v;
      return s.str();
    };
    auto fmt2 = [](double v) -> std::string {
      std::ostringstream s;
      s << std::fixed << std::setprecision(2) << v;
      return s.str();
    };

    std::cout << "  " << bold << yellow << t.label << reset << "\n"
              << "  " << std::string(38, '-') << "\n";

    print_row("original", human_size(r.original_bytes).c_str());
    print_row("compressed", human_size(r.compressed_bytes).c_str());
    print_row("ratio", fmt2(r.ratio).c_str(), "x", green);
    print_row("compress", fmt(r.compress_mb_s).c_str(), "MB/s", cyan);
    print_row("decompress", fmt(r.decompress_mb_s).c_str(), "MB/s", cyan);

    std::cout << "\n";
  }
}
