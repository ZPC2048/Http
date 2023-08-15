#ifndef MMAP_H_2048
#define MMAP_H_2048

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Exception.hpp"

namespace Http {

enum class MmapError {
  OPEN_FILE_FAIL,
  GET_FILE_INFO_FAIL,
  MMAP_FAIL
};

class MmapException : public Exception {
public:
  const MmapError errorCode;

  explicit MmapException (MmapError errorCode) :
    errorCode(errorCode) {
    switch (errorCode) {
    case MmapError::OPEN_FILE_FAIL:
      errorMessage = "Cannot open file! Error code: " + std::to_string(errno);
      break;
    case MmapError::GET_FILE_INFO_FAIL:
      errorMessage = "Cannot get file information! Error code: " + std::to_string(errno);
      break;
    case MmapError::MMAP_FAIL:
      errorMessage = "Cannot mmap file into memory! Error code: " + std::to_string(errno);
      break;
    }
  }
};

class Mmap {
public:
  Mmap(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
      throw MmapException(MmapError::OPEN_FILE_FAIL);
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
      throw MmapException(MmapError::GET_FILE_INFO_FAIL);
    }
    ptr = (const char*)mmap(nullptr, fileSize = st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (ptr == nullptr) {
      throw MmapException(MmapError::MMAP_FAIL);
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

}

#endif