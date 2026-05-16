#include <cstdint>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

const uint8_t* map_file(const std::string& input_file, int& fd, size_t& file_size){

    fd = open(input_file.c_str(), O_RDONLY);

    if (fd < 0)
        throw std::runtime_error("cannot open the file");

    struct stat st;
    fstat(fd, &st);

    file_size = st.st_size;

     if (file_size == 0) {
        close(fd);
        throw std::runtime_error("file is empty");
    }

    void *ptr = (mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0));

    if (ptr == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("mmap failed");
    }

    uint8_t *file_ptr = static_cast<uint8_t*>(ptr);
    

    /* Hints to the operating system that i would be fetchings pages sequentially. So, OS starts
       fetching next pages asynchronously.
    */
    madvise(file_ptr, file_size, MADV_SEQUENTIAL);
    return file_ptr;
}
