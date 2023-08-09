#ifndef BUFFER_H_2048
#define BUFFER_H_2048

#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <vector>

namespace Http {

class Buffer {
public:
  explicit Buffer(int fd) :
    fd(fd), readBufferSize(INIT_BUFFER_SIZE), writeBufferSize(INIT_BUFFER_SIZE), 
    readBuffer(new char[INIT_BUFFER_SIZE]), writeBuffer(new char[INIT_BUFFER_SIZE]), 
    readBufferReadPos(0), readBufferWritePos(0), writeBufferWritePos(0) {
  }

  ~Buffer() {
    if (readBuffer != nullptr) {
      delete[] readBuffer;
    }
    if (writeBuffer != nullptr) {
      delete[] writeBuffer;
    }
  }

  /**
   * @return a pointer to the begin and the length of string if read successfully,
   *         or nullptr and EOF if read has been finished,
   *         or nullptr and errno if error occurred.
   */
  std::pair<const char*, size_t> readline() {
    readBufferCurPos = readBufferReadPos;
    while (true) {
      while (readBufferCurPos < readBufferWritePos) {
        if (readBuffer[readBufferCurPos] == '\n' && readBufferCurPos - 1 >= readBufferReadPos && readBuffer[readBufferCurPos - 1] == '\r') {
          auto oldReadPos = readBufferReadPos;
          readBufferReadPos = readBufferCurPos + 1;
          return std::make_pair<const char*, size_t>(readBuffer + oldReadPos, readBufferCurPos - oldReadPos + 1);
        }
        ++readBufferCurPos;
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

  template<typename... Args>
  void bufferedWriteFd(const char* format, Args&&... args){
    int writeNumber = snprintf(writeBuffer + writeBufferWritePos, writeBufferSize - writeBufferWritePos, 
                               format, std::forward<Args>(args)...);
    if (writeNumber < writeBufferSize - writeBufferWritePos) {
      writeBufferWritePos += writeNumber;
      return;
    }
    flushWrite();
    do {
      overflowWriteBuffer();
      writeNumber = snprintf(writeBuffer + writeBufferWritePos, writeBufferSize - writeBufferWritePos, 
                             format, std::forward<Args>(args)...);
    } while (writeNumber >= writeBufferSize - writeBufferWritePos);
    writeBufferWritePos += writeNumber;
  }

  void bufferedWriteFd(const char* content){
    bufferedWriteFd("%s", content);
  }

  inline bool writeFd(const char* content, size_t size) {
    size_t cur = 0;
    while (cur < size) {
      int writeNumber = write(fd, content + cur, size - cur);
      if (writeNumber < 0) {
        continue;
      }
      if (writeNumber == 0) {
        return true;
      }
      cur += writeNumber;
    }
    return true;
  }

  void flushWrite() {
    size_t cur = 0;
    while (cur < writeBufferWritePos) {
      int writeNumber = write(fd, writeBuffer + cur, writeBufferWritePos - cur);
      if (writeNumber < 0) {
        continue;
      }
      if (writeNumber == 0) {
        writeBufferWritePos = 0;
        return;
      }
      cur += writeNumber;
    }
    writeBufferWritePos = 0;
  }

protected:
  void resizeReadBuffer(size_t newBufferSize) {
    char* tmp = readBuffer;
    readBuffer = new char[newBufferSize];
    memcpy(readBuffer, tmp + readBufferReadPos, readBufferWritePos - readBufferReadPos);
    readBufferSize = newBufferSize;
    readBufferWritePos -= readBufferReadPos;
    readBufferCurPos -= readBufferReadPos;
    readBufferReadPos = 0;
    delete[] tmp;
  }

  bool overflowReadBuffer() {
    if (readBufferWritePos - readBufferReadPos > readBufferSize * 0.75) {
      resizeReadBuffer(readBufferSize << 1);
      return true;
    }
    memmove(readBuffer, readBuffer + readBufferReadPos, readBufferWritePos - readBufferReadPos);
    readBufferWritePos -= readBufferReadPos;
    readBufferCurPos -= readBufferReadPos;
    readBufferReadPos = 0;
    return false;
  }

  void overflowWriteBuffer() {
    char* tmp = writeBuffer;
    writeBuffer = new char[writeBufferSize << 1];
    memcpy(writeBuffer, tmp, writeBufferWritePos);
    writeBufferSize <<= 1;
    delete[] tmp;
  }

  int readFd() {
    int readNumber = read(fd, readBuffer + readBufferWritePos, readBufferSize - readBufferWritePos);
    // printf("readNumber: %d; errno: %d\n", readNumber, errno);
    if (readNumber > 0) {
      readBufferWritePos += readNumber;
      if (readBufferWritePos >= readBufferSize) {
        overflowReadBuffer();
      }
    }
    return readNumber;
  }

private:
  const static size_t INIT_BUFFER_SIZE = 4096;

  int fd;

  size_t readBufferReadPos;
  size_t readBufferWritePos;
  size_t readBufferCurPos;
  size_t readBufferSize;
  char* readBuffer;

  size_t writeBufferWritePos;
  size_t writeBufferSize;
  char* writeBuffer;
};

}

#endif