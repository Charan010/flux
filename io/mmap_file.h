#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class MappedFile {
public:
  explicit MappedFile(const std::string &path);
  ~MappedFile();

  MappedFile(const MappedFile &) = delete;
  MappedFile &operator=(const MappedFile &) = delete;

  const uint8_t *data() const { return file_ptr; }
  size_t size() const { return file_size; }

private:

  void map_file(const std::string &path);

  int fd{-1};
  const uint8_t *file_ptr{nullptr};
  size_t file_size{0};
  
};