#include "bench.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include <unistd.h>

#include "core/dedup_engine.h"

namespace fs = std::filesystem;

namespace {

uint64_t dir_size(const fs::path &dir) {
    uint64_t total = 0;

    if (fs::exists(dir))
        for (const auto &e : fs::recursive_directory_iterator(dir))
            if (e.is_regular_file())
                total += e.file_size();

    return total;
}

inline double mb(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

inline double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void write_log(const fs::path &path, size_t approx_bytes, uint32_t seed){


    static const char *levels[] = {"INFO", "WARN", "DEBUG", "ERROR"};
    static const char *msgs[]   = {"request handled", "cache miss", "retry scheduled",
                                   "connection closed", "task complete"};

    std::mt19937 rng(seed);
    std::ofstream out(path, std::ios::binary);

    size_t written = 0;
    uint64_t i = 0;
    while (written < approx_bytes){

        std::ostringstream line;
        line << "2026-07-24T10:00:00Z " << levels[rng() % 4] << " req=" << i++ << " " << msgs[rng() % 5] << " ok\n";
        const std::string s = line.str();
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
        written += s.size();

    }
}

bool trees_identical(const fs::path &a_root, const fs::path &b_root){


    for (const auto &e : fs::recursive_directory_iterator(a_root)) {
        if (!e.is_regular_file())
            continue;
        const fs::path rel = fs::relative(e.path(), a_root);
        const fs::path got = b_root / rel;

        if (!fs::exists(got) || fs::file_size(got) != e.file_size())
            return false;

        std::ifstream a(e.path(), std::ios::binary), b(got, std::ios::binary);
        if (!std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(),
                        std::istreambuf_iterator<char>(b)))
            return false;
    }
    return true;
}

} 

void run_bench(const std::string &store, std::size_t size_mb) {

    const fs::path work = fs::temp_directory_path() / ("relic_bench_" + std::to_string(getpid()));
    const fs::path data = work / "data";
    const fs::path out  = work / "restored";
    fs::remove_all(work);
    fs::create_directories(data / "logs");

    const size_t per_file = (size_mb * 1024 * 1024) / 3;

    std::cout << "  generating ~" << size_mb << " MB corpus...\n";
    write_log(data / "logs" / "app.log",    per_file, 1);
    write_log(data / "logs" / "app_v2.log", per_file, 1);
    fs::copy_file(data / "logs" / "app.log", data / "logs" / "exact_copy.log");

    const std::string snap = "bench";
    fs::remove(fs::path(store) / "manifests" / snap);

    DedupEngine engine(store);

    const uint64_t input_bytes  = dir_size(data);
    const uint64_t store_before = dir_size(fs::path(store) / "packs");

    auto t0 = std::chrono::steady_clock::now();
    engine.backup(data.string(), snap);
    const double backup_s = seconds_since(t0);

    const uint64_t stored = dir_size(fs::path(store) / "packs") - store_before;

    auto t1 = std::chrono::steady_clock::now();
    engine.restore(snap, out.string());
    const double restore_s = seconds_since(t1);

    const bool ok = trees_identical(data, out);

    std::cout.setf(std::ios::fixed);
    std::cout.precision(2);
    std::cout << "\n";
    std::cout << "  input        " << mb(input_bytes) << " MB\n";
    std::cout << "  stored       " << mb(stored) << " MB\n";
    std::cout << "  ratio        " << (input_bytes ? static_cast<double>(stored) / static_cast<double>(input_bytes) : 0.0)
              << "x  (lower is better; dedup + compression)\n";


    std::cout << "  backup       " << backup_s << " s  (" << (backup_s > 0 ? mb(input_bytes) / backup_s : 0.0) << " MB/s)\n";
    std::cout << "  restore      " << restore_s << " s  (" << (restore_s > 0 ? mb(input_bytes) / restore_s : 0.0) << " MB/s)\n";
    std::cout << "  verify       " << (ok ? "PASS (byte-identical)" : "FAIL (restore differs)") << "\n\n";

    fs::remove_all(work);
    fs::remove(fs::path(store) / "manifests" / snap);
}