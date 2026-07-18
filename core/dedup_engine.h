#pragma once

#include <string>

#include "core/config.h"
#include "threadpool/threadpool.h"
#include "storage/object_store.h"

class DedupEngine {

public:
    explicit DedupEngine(const std::string &store_directory = "store", size_t threads = Config::threadpool_size);

    void backup(const std::string &input_path, const std::string &manifest_path);
    void restore(const std::string &manifest_path, const std::string &output_path) const;

private:
    ObjectStore store_;
    mutable Threadpool pool_;
};
