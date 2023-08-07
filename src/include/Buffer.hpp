#ifndef BUFFER_2048
#define BUFFER_2048

#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <vector>

namespace Http {

class Buffer {
public:
  explicit Buffer(int fd, size_t maxBufferSize) :
    fd(fd), size(maxBufferSize), buffer(new char[size]), readPos(0), writePos(0) {
  }

  ~Buffer() {
    if (buffer != nullptr) {
      delete[] buffer;
    }
  }

  /**
   * @return a pointer to the begin and the length of string if read successfully,
   *         or nullptr and EOF if read has been finished,
   *         or nullptr and errno if error occurred.
   */
  std::pair<const char*, size_t> readline() {
    curPos = readPos;
    while (true) {
      while (curPos < writePos) {
        if (buffer[curPos] == '\n' && curPos - 1 >= readPos && buffer[curPos - 1] == '\r') {
          auto oldReadPos = readPos;
          readPos = curPos + 1;
          return std::make_pair<const char*, size_t>(buffer + oldReadPos, curPos - oldReadPos + 1);
        }
        ++curPos;
      }
      int readNumber = readFd();
      if (readNumber < 0) {
        return std::make_pair(nullptr, errno);
      }
      if (readNumber == 0) {
        return std::make_pair(nullptr, EOF);
      }
    }
    return std::make_pair(nullptr, EOF);
  }

protected:
  void resize(size_t newBufferSize) {
    char* tmp = buffer;
    buffer = new char[newBufferSize];
    memcpy(buffer, tmp + readPos, writePos - readPos);
    size = newBufferSize;
    writePos -= readPos;
    curPos -= readPos;
    readPos = 0;
    delete tmp;
  }

  bool overflow() {
    if (writePos - readPos > size * 0.75) {
      resize(size << 1);
      return true;
    }
    memmove(buffer, buffer + readPos, writePos - readPos);
    writePos -= readPos;
    curPos -= readPos;
    readPos = 0;
    return false;
  }

  int readFd() {
    int readNumber = read(fd, buffer + writePos, size - writePos);
    if (readNumber > 0) {
      writePos += readNumber;
      if (writePos >= size) {
        overflow();
      }
    }
    return readNumber;
  }

private:
  int fd;
  size_t readPos;
  size_t writePos;
  size_t curPos;
  size_t size;
  char* buffer;
};

}

#endif