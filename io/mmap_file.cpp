#include "mmap_file.h"

#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

MappedFile::MappedFile(const std::string& path) {
    map_file(path);
    
}

MappedFile::~MappedFile() {
    if (file_ptr)
        munmap(const_cast<uint8_t*>(file_ptr), file_size);

    if (fd >= 0)
        close(fd);
}


void MappedFile::map_file(const std::string& input_file) {

    fd = open(input_file.c_str(), O_RDONLY);

    if (fd < 0)
        throw std::runtime_error("cannot open file");

    struct stat st;

    if (fstat(fd, &st) < 0) {
        close(fd);
        throw std::runtime_error(
            "fstat failed for file: " + input_file);
    }

    file_size = st.st_size;

    if (file_size == 0) {
        close(fd);
        throw std::runtime_error("file is empty");
    }

    void* ptr = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (ptr == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("mmap failed");
    }

    file_ptr = static_cast<const uint8_t*>(ptr);

    /* Hints to the operating system that i would be fetchings pages sequentially. So, OS starts
       fetching next pages asynchronously.
    */
    madvise(const_cast<uint8_t*>(file_ptr), file_size, MADV_SEQUENTIAL);
}

