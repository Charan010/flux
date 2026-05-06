#include <iostream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>

#include "pipeline/coordinator.h"
#include "helper.h" 

namespace fs = std::filesystem;

const std::string RESET = "\033[0m";
const std::string GREEN = "\033[32m";
const std::string BLUE  = "\033[34m";
const std::string RED   = "\033[31m";
const std::string YELLOW = "\033[33m";
const std::string BOLD  = "\033[1m";

void print_header() {
    std::cout << BLUE << BOLD << "\nFLUX COMPRESSION REPL " << RESET << "(v1.0)\n"
              << "---------------------------------------------\n"
              << GREEN << "  -c " << RESET << "<original> <output>   | Compress\n"
              << GREEN << "  -d " << RESET << "<encoded> <output>    | Decompress\n"
              << GREEN << "  -v " << RESET << "<file1> <file2>       | Verify SHA-256\n"
              << RED   << "  exit" << RESET << "                     | Quit\n"
              << "---------------------------------------------\n\n";
}

int main() {

    Coordinator coordinator(6, 1 << 22);
    print_header();

    while (true) {
        std::cout << BLUE << BOLD << "flux> " << RESET;
        std::string line;
        if (!std::getline(std::cin, line)) break;

        std::istringstream iss(line);
        std::string mode;
        iss >> mode;

        if (mode.empty()) continue;

        try {
            if (mode == "exit" || mode == "quit") {
                break;
            }

            else if (mode == "-c") {
                std::string input, output;
                if (!(iss >> input >> output)) {
                    std::cout << YELLOW << "  Usage: -c <original_file> <output_file>\n" << RESET;
                    continue;
                }

                auto start = std::chrono::high_resolution_clock::now();
                coordinator.compress(input, output);
                auto end = std::chrono::high_resolution_clock::now();

                double duration = std::chrono::duration<double>(end - start).count();
                std::cout << "  └─ " << GREEN << "Compression Complete" << RESET << "\n"
                          << "     Source: " << input << "\n"
                          << "     Result: " << output << "\n"
                          << "     Time:   " << std::fixed << std::setprecision(4) << duration << "s\n";
            }

            else if (mode == "-d") {
                std::string input, output;
                if (!(iss >> input >> output)) {
                    std::cout << YELLOW << "  Usage: -d <encoded_file> <output_file>\n" << RESET;
                    continue;
                }

                auto start = std::chrono::high_resolution_clock::now();
                coordinator.decompress(input, output);
                auto end = std::chrono::high_resolution_clock::now();

                double duration = std::chrono::duration<double>(end - start).count();
                std::cout << "  └─ " << GREEN << "Decompression Complete" << RESET << "\n"
                          << "     Source: " << input << "\n"
                          << "     Result: " << output << "\n"
                          << "     Time:   " << std::fixed << std::setprecision(4) << duration << "s\n";
            }

            else if (mode == "-v") {
                std::string f1, f2;
                if (!(iss >> f1 >> f2)) {
                    std::cout << YELLOW << "  Usage: -v <file1> <file2>\n" << RESET;
                    continue;
                }
                
                std::cout << "  " << BLUE << "Checking SHA-256 hashes..." << RESET << "\n";
                std::string hash1 = get_sha256(f1);
                std::string hash2 = get_sha256(f2);

                if (hash1 != "ERROR" && hash1 == hash2) {
                    std::cout << "  └─ " << GREEN << BOLD << "MATCH: " << RESET << "Files are identical.\n"
                              << "     Hash: " << hash1 << "\n";
                } else {
                    std::cout << "  └─ " << RED << BOLD << "MISMATCH: " << RESET << "Data difference detected!\n";
                    std::cout << "     " << f1 << ": " << (hash1 == "ERROR" ? RED + "Failed to read" : hash1) << RESET << "\n";
                    std::cout << "     " << f2 << ": " << (hash2 == "ERROR" ? RED + "Failed to read" : hash2) << RESET << "\n";
                }
            }

            else {
                std::cout << RED << "  Unknown command. Type -c, -d, -v, or exit.\n" << RESET;
            }

        } catch (const std::exception& e) {
            std::cerr << RED << BOLD << "  Error: " << RESET << e.what() << "\n";
        }
    }

    std::cout << BLUE << "Cleaning up ...\n" << RESET;
    return 0;
}