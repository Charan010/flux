#include <iostream>
#include <filesystem>
#include <thread>

#include "pipeline/coordinator.h"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {

    size_t threads = std::thread::hardware_concurrency();
    if (threads == 0)
        threads = 4;

    if (argc < 3) {
        std::cerr << "Usage:\n"
                  << "  Compress:   ./huffman -c <input_file>\n"
                  << "  Decompress: ./huffman -d <input_file.huf> <output_file>\n";
        return 1;
    }

    std::string mode = argv[1];

    try {

        Coordinator coordinator(threads, 1 << 20);

        if (mode == "-c") {

            fs::path input_path = fs::absolute(argv[2]);

            if (!fs::exists(input_path)) {
                std::cerr << "Input file does not exist\n";
                return 1;
            }

            fs::path output_path =
                input_path.parent_path() /
                (input_path.stem().string() + ".huf");

            coordinator.compress(input_path.string(), output_path.string());

            std::cout << "Compressed → " << output_path << "\n";
        }

        else if (mode == "-d") {

            if (argc < 4) {
                std::cerr << "Usage: ./huffman -d <input_file.huf> <output_file>\n";
                return 1;
            }

            fs::path input_path = fs::absolute(argv[2]);
            fs::path output_path = fs::absolute(argv[3]);

            if (!fs::exists(input_path)) {
                std::cerr << "Input file does not exist\n";
                return 1;
            }

            coordinator.decompress(input_path.string(), output_path.string());

            std::cout << "Decompressed → " << output_path << "\n";
        }

        else {
            std::cerr << "Unknown mode: " << mode << "\n";
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}