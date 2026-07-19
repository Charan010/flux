#include <filesystem>
#include <iostream>

#include "core/dedup_engine.h"

namespace {

void print_usage() {
    std::cerr <<
        "flux - deduplicating, LZ4-compressed file backup\n\n"
        "usage:\n"
        "  flux backup   <input-file> <manifest-file> \n"
        "  flux restore  <manifest-file> <output-file> \n\n";
}

} 

int main(int argc, char **argv) {

    if (argc < 4) {
        print_usage();
        return 1;
    }

    const std::string command = argv[1];
    const std::string first_path = argv[2];
    const std::string second_path = argv[3];
    const std::string store_dir = argc > 4 ? argv[4] : "store";

    try {

        DedupEngine engine(store_dir);

        if (command == "backup") {
            engine.backup(first_path, second_path);
			std::cout << "backed up '" << first_path << "' -> manifest '" << second_path
                      << "' (store: " << store_dir << ")\n";

        } else if (command == "restore") {
            engine.restore(first_path, second_path);
            std::cout << "restored manifest '" << first_path << "' -> '" << second_path << "'\n";

        } else {
            print_usage();
            return 1;
        }

    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
