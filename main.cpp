#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <iterator>

#include "core/dedup_engine.h"

namespace {

void print_help() {
    std::cout <<
        "relic - deduplicating, LZ4-compressed dataset versioning\n\n"
        "  backup  <input> <manifest-name>   store a snapshot\n"
        "  restore <manifest-name> <output>  materialize a snapshot\n"
        "  gc      [--dry-run]               reclaim unreferenced space\n"
        "  help                              show this help\n"
        "  exit                              leave\n\n";
}

std::vector<std::string> split(const std::string &line) {
    std::istringstream iss(line);
    return {std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>()};
}

} 
int main(int argc, char **argv) {
    const std::string store = argc > 1 ? argv[1] : "store";
    DedupEngine engine(store);
    print_help();

    std::string line;
    while (std::cout << "relic> " << std::flush, std::getline(std::cin, line)){

        const auto args = split(line);
        if (args.empty()) continue;

        const std::string &cmd = args[0];
        if (cmd == "exit" || cmd == "quit") break;

        try {

            if (cmd == "help")
                print_help();

            else if (cmd == "backup" && args.size() >= 3)
                engine.backup(args[1], args[2]);

            else if (cmd == "restore" && args.size() >= 3)
                engine.restore(args[1], args[2]);

            else if (cmd == "gc")
                engine.gc(args.size() > 1 && args[1] == "--dry-run");
            
			else
                std::cout << "bad command or args (try 'help')\n";
        } 
		catch (const std::exception &e) {
			std::cerr << "error: " << e.what() << "\n";
        }
    }

    return 0;
}