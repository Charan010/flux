#include <iostream>
#include <filesystem>

#include "pipeline/coordinator.h"

namespace fs = std::filesystem;

int main(int argc, char* argv[]){

     size_t threads = std::thread::hardware_concurrency();
        if(threads == 0)
            threads = 4;


    if (argc < 3){
        std::cerr << "Usage:\n" << "  Compress:   ./huffman -c <input_file>\n" <<
         "  Decompress: ./huffman -d <input_file.huf> <output_file>\n";
         return 1;
    }

    std::string mode = argv[1];

    try{

        Coordinator coordinator(threads,  1 << 20);

        if (mode == "-c"){

            fs::path input_path  = fs::absolute(argv[2]);
            fs::path output_path =
                input_path.parent_path() /
                (input_path.stem().string() + ".huf");

            coordinator.compress(input_path.string(), output_path.string());

            std::cout << "Compressed → " << output_path.string() << "\n";
        }

        else if (mode == "-d"){

            if (argc < 4){
                std::cerr << "Decode requires output file path\n";
                return 1;
            }

            fs::path input_path  = fs::absolute(argv[2]);
            fs::path output_path = fs::absolute(argv[3]);

            coordinator.decompress(input_path.string(), output_path.string());

            std::cout << "Decompressed → " << output_path.string() << "\n";
        }

        else{
            std::cerr << "Unknown mode: " << mode << "\n";
            return 1;
        }
    
    }

    catch (const std::exception& e){
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

/*

◎ time huffman -c random.bin
Compressed → /home/charan010/random.huf

real	0m1.277s
user	0m4.771s
sys	0m1.478s

                                                                                                                                        
◎ 

*/