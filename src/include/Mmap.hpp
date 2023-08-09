#ifndef MMAP_H_2048
#define MMAP_H_2048

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdexcept>

class Mmap {
public:
  Mmap(const char* path) {
    // TODO: replace exception
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
      throw std::runtime_error("open file failed");
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
      throw std::runtime_error("get file information failed");
    }
    ptr = (const char*)mmap(nullptr, fileSize = st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (ptr == nullptr) {
      throw std::runtime_error("mmap file into memory failed");
    }
  }

  ~Mmap() {
    if (ptr) {
      munmap((void*)ptr, fileSize);
    }
    if (fd > 0) {
      close(fd);
    }
  }

  inline int size() {
    return fileSize;
  }

  inline const char* data() {
    return ptr;
  }

private:
  int fd;
  int fileSize;
  const char* ptr = nullptr;
};

#endif