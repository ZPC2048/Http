#ifndef SOCKET_H_2048
#define SOCKET_H_2048

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include <exception>
#include <string>

namespace Http {

using socket_t = int;

enum class SocketError {
  BAD_ALLOC,
  INVALID_SOCKET
};

class SocketException : public std::exception {
public:
  const SocketError errorCode;

  explicit SocketException(SocketError errorCode) :
    errorCode(errorCode) {
    switch (errorCode) {
    case SocketError::BAD_ALLOC:
      errorMessage = "Cannot allocate a new socket! Error code: " + std::to_string(errno);
      break;
    case SocketError::INVALID_SOCKET:
      errorMessage = "Invalid socket passed in!";
      break;
    }
  }

  virtual const char* what() const noexcept override {
    return errorMessage.c_str();
  }

protected:
  std::string errorMessage;
};

class Socket {
private:
  const static int TRUE;
  const socket_t socketFd;
  bool useable;

public:
  // using enum SocketError;

  explicit Socket() : socketFd(socket(AF_INET, SOCK_STREAM, 0)), useable(true) {
    if (socketFd < 0) {
      throw SocketException(SocketError::BAD_ALLOC);
    }
    setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &TRUE, sizeof(TRUE));
    setsockopt(socketFd, SOL_SOCKET, SO_REUSEPORT, &TRUE, sizeof(TRUE));
    fcntl(socketFd, F_SETFL, fcntl(socketFd, F_GETFL) | O_NONBLOCK);
  }

  explicit Socket(socket_t socketFd) : socketFd(socketFd), useable(true) {
    if (socketFd < 0) {
      throw SocketException(SocketError::INVALID_SOCKET);
    }
    setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &TRUE, sizeof(TRUE));
    setsockopt(socketFd, SOL_SOCKET, SO_REUSEPORT, &TRUE, sizeof(TRUE));
    fcntl(socketFd, F_SETFL, fcntl(socketFd, F_GETFL) | O_NONBLOCK);
  }
  
  ~Socket() {
    if (useable) {
      close(socketFd);
    }
  }

  void bind(const std::string& address, u_short port) {
    if (!useable) return;
    sockaddr_in tmp;
    tmp.sin_addr.s_addr = inet_addr(address.c_str());
    tmp.sin_family = AF_INET;
    tmp.sin_port = htons(port);
    ::bind(socketFd, (struct sockaddr*) &tmp, sizeof(tmp));
  }

  void listen(int queueSize) {
    ::listen(socketFd, queueSize);
  }

  inline socket_t getSocket() const {
    return socketFd;
  }
};

const int Socket::TRUE = 1;

}

#endif