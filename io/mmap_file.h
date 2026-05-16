#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class MappedFile {

    public:
    explicit MappedFile(const std::string& path);
    ~MappedFile();
    
    //move semantics
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

  
    uint8_t* data() const { return file_ptr; }

    size_t size() const { return file_size; }

  private:
    int fd{-1};
    uint8_t* file_ptr{nullptr};
    size_t file_size{0};
};