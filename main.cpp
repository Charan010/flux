#include <algorithm>
#include <exception>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bench.h"
#include "core/dedup_engine.h"

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


std::vector<std::string> split(const std::string &line){

    std::istringstream iss(line);
    return {std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>()};

}

void list_snapshots(const std::string &store) {
    const fs::path dir = fs::path(store) / "manifests";

    if (!fs::exists(dir))
        std::cout << "  no snapshots\n"; return;
    

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

}

int main(int argc, char **argv) {

    const std::string store = (argc > 1) ? argv[1] : kDefaultStore;

    try {

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
                if (cmd == "help")
                    print_help();

                else if (cmd == "list")
                    list_snapshots(store);

                else if (cmd == "backup") {
                    if (args.size() < 3)
                        throw std::runtime_error("usage: backup <path> <name>");

                    engine.backup(args[1], args[2]);
                    std::cout << "  snapshot '" << args[2] << "' created\n";

                } else if (cmd == "restore") {
                    if (args.size() < 3)
                        throw std::runtime_error("usage: restore <name> <path>");

                    engine.restore(args[1], args[2]);
                    std::cout << "  restored to '" << args[2] << "'\n";

                } else if (cmd == "gc")
                    engine.gc(args.size() > 1 && args[1] == "--dry-run");

                else if (cmd == "bench") {
                    const size_t size_mb = (args.size() > 1) ? std::stoul(args[1]) : 10;
                    run_bench(store, size_mb);

                }else
                    std::cout << "  unknown command '" << cmd << "' (try 'help')\n";
    
            }catch (const std::exception &e) {
                std::cerr << "  error: " << e.what() << "\n";
            }
        }

    }catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\n";
    return 0;
}