#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "core/dedup_engine.h"
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

constexpr const char *kDefaultStore = "store";

void print_banner(const std::string &store) {
    std::cout << "relic - deduplicating snapshot storage\n"
              << "store: " << fs::absolute(store).string() << "\n"
              << "type 'help' for commands, 'exit' to quit\n\n";
}

void print_help() {
    std::cout <<
        "  backup  <path> <name>     snapshot a file or directory\n"
        "  restore <name> <path>     materialize a snapshot\n"
        "  list                      show stored snapshots\n"
        "  gc [--dry-run]            reclaim space from deleted snapshots\n"
        "  bench [size_mb]           self-test: generate data, snapshot, verify\n"
        "  help                      show this help\n"
        "  exit                      quit\n\n";
}

std::vector<std::string> split(const std::string &line) {
    std::istringstream iss(line);
    return {std::istream_iterator<std::string>(iss),
            std::istream_iterator<std::string>()};
}

void list_snapshots(const std::string &store) {
    const fs::path dir = fs::path(store) / "manifests";

    if (!fs::exists(dir)) {
        std::cout << "  no snapshots\n";
        return;
    }

    std::vector<std::string> names;
    for (const auto &entry : fs::directory_iterator(dir))
        if (entry.is_regular_file())
            names.push_back(entry.path().filename().string());

    if (names.empty()) {
        std::cout << "  no snapshots\n";
        return;
    }

    std::sort(names.begin(), names.end());
    for (const auto &name : names)
        std::cout << "  " << name << "\n";
}

uint64_t dir_size(const fs::path &dir) {
    uint64_t total = 0;
    if (fs::exists(dir))
        for (const auto &e : fs::recursive_directory_iterator(dir))
            if (e.is_regular_file())
                total += e.file_size();
    return total;
}

double mb(uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// Writes a log-like file of repeated lines with a sprinkle of variation, so it
// both compresses well (repetition) and chunk-dedups against similar files.
void write_log(const fs::path &path, size_t approx_bytes, uint32_t seed) {
    static const char *levels[] = {"INFO", "WARN", "DEBUG", "ERROR"};
    static const char *msgs[]   = {"request handled", "cache miss", "retry scheduled",
                                   "connection closed", "task complete"};

    std::mt19937 rng(seed);
    std::ofstream out(path, std::ios::binary);

    size_t written = 0;
    uint64_t i = 0;
    while (written < approx_bytes) {
        std::ostringstream line;
        line << "2026-07-24T10:00:00Z " << levels[rng() % 4]
             << " req=" << i++ << " " << msgs[rng() % 5] << " ok\n";
        const std::string s = line.str();
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
        written += s.size();
    }
}

void run_bench(const std::string &store, size_t size_mb) {

    const fs::path work = fs::temp_directory_path() / ("relic_bench_" + std::to_string(getpid()));
    const fs::path data = work / "data";
    const fs::path out  = work / "restored";
    fs::remove_all(work);
    fs::create_directories(data / "logs");

    const size_t per_file = (size_mb * 1024 * 1024) / 3;

    std::cout << "  generating ~" << size_mb << " MB corpus...\n";
    write_log(data / "logs" / "app.log",     per_file, 1);   
    write_log(data / "logs" / "app_v2.log",  per_file, 1);   
    fs::copy_file(data / "logs" / "app.log", data / "logs" / "exact_copy.log");  

    const std::string snap = "bench";
    fs::remove(fs::path(store) / "manifests" / snap);

    DedupEngine engine(store);

    const uint64_t input_bytes = dir_size(data);
    const uint64_t store_before = dir_size(fs::path(store) / "packs");

    auto t0 = std::chrono::steady_clock::now();
    engine.backup(data.string(), snap);
    const double backup_s = seconds_since(t0);

    const uint64_t store_after = dir_size(fs::path(store) / "packs");
    const uint64_t stored = store_after - store_before;

    auto t1 = std::chrono::steady_clock::now();
    engine.restore(snap, out.string());
    const double restore_s = seconds_since(t1);

    bool ok = true;
    for (const auto &e : fs::recursive_directory_iterator(data)) {
        if (!e.is_regular_file())
            continue;
        const fs::path rel = fs::relative(e.path(), data);
        const fs::path got = out / rel;

        if (!fs::exists(got) || fs::file_size(got) != e.file_size()) { ok = false; break; }

        std::ifstream a(e.path(), std::ios::binary), b(got, std::ios::binary);
        if (!std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(),
                        std::istreambuf_iterator<char>(b))) { ok = false; break; }
    }

    std::cout.setf(std::ios::fixed);
    std::cout.precision(2);
    std::cout << "\n";
    std::cout << "  input        " << mb(input_bytes) << " MB\n";
    std::cout << "  stored       " << mb(stored) << " MB\n";
    std::cout << "  ratio        " << (input_bytes ? static_cast<double>(stored) / static_cast<double>(input_bytes) : 0.0)
              << "x  (lower is better; dedup + compression)\n";
    std::cout << "  backup       " << backup_s << " s  ("
              << (backup_s > 0 ? mb(input_bytes) / backup_s : 0.0) << " MB/s)\n";
    std::cout << "  restore      " << restore_s << " s  ("
              << (restore_s > 0 ? mb(input_bytes) / restore_s : 0.0) << " MB/s)\n";
    std::cout << "  verify       " << (ok ? "PASS (byte-identical)" : "FAIL (restore differs)") << "\n\n";

    fs::remove_all(work);
    fs::remove(fs::path(store) / "manifests" / snap);

}

} 

int main(int argc, char **argv) {

    const std::string store = (argc > 1) ? argv[1] : kDefaultStore;

    DedupEngine engine(store);
    print_banner(store);

    std::string line;
    while (std::cout << "relic> " << std::flush, std::getline(std::cin, line)) {

        const auto args = split(line);
        if (args.empty())
            continue;

        const std::string &cmd = args[0];
        if (cmd == "exit" || cmd == "quit")
            break;

        try {
            if (cmd == "help") {
                print_help();

            } else if (cmd == "list") {
                list_snapshots(store);

            } else if (cmd == "backup") {
                if (args.size() < 3)
                    throw std::runtime_error("usage: backup <path> <name>");
                engine.backup(args[1], args[2]);
                std::cout << "  snapshot '" << args[2] << "' created\n";

            } else if (cmd == "restore") {
                if (args.size() < 3)
                    throw std::runtime_error("usage: restore <name> <path>");
                engine.restore(args[1], args[2]);
                std::cout << "  restored to '" << args[2] << "'\n";

            } else if (cmd == "gc") {
                engine.gc(args.size() > 1 && args[1] == "--dry-run");

            } else if (cmd == "bench") {
                const size_t size_mb = (args.size() > 1) ? std::stoul(args[1]) : 10;
                run_bench(store, size_mb);

            } else {
                std::cout << "  unknown command '" << cmd << "' (try 'help')\n";
            }

        } catch (const std::exception &e) {
            std::cerr << "  error: " << e.what() << "\n";
        }
    }

    std::cout << "\n";
    return 0;
}