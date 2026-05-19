#pragma once

#include "threadpool.h"
#include <string>

class Coordinator {

  public:
    Coordinator(size_t threads);
    ~Coordinator();

    void compress(CompressionMode mode, const std::string& input, const std::string& output,
                  size_t chunk_size);

    void decompress(CompressionMode mode, const std::string& input, const std::string& output,
                    size_t chunk_size);

  private:
    Threadpool pool;
    SharedWriter writer;
};
