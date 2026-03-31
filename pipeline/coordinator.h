#pragma once

#include <string>
#include <array>

#include "threadpool.h"
#include "frequency_counter.h"
#include "chunk_buffer.h"
#include "huffman.h"
#include "bit_io.h"


class Coordinator {

private:

    ThreadPool pool;
    size_t chunk_size;

public:

    Coordinator(size_t threads, size_t chunk);

    void compress(const std::string& input_file, const std::string& output_file);
    //void decompress(const std::string &input_file, const std::string &dump_file);
};
