#ifndef HTTP_SERVER_2048
#define HTTP_SERVER_2048

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <cstdio>
#include <string>
#include <functional>
#include <exception>
#include <algorithm>
#include <unordered_map>

#include "threadpool.hpp"
#include "Buffer.hpp"

#include <iostream>

namespace Http {

using socket_t = int;
using epoll_t = int;
using Headers = std::unordered_multimap<std::string, std::string>;

enum class RequestMethod {
  // The GET method requests a representation of the specified resource. Requests using GET should only retrieve data.
  GET,
  // The HEAD method asks for a response identical to a GET request, but without the response body.
  HEAD,
  // The POST method submits an entity to the specified resource, often causing a change in state or side effects on the server.
  POST,
  // The PUT method replaces all current representations of the target resource with the request payload.
  PUT,
  // The DELETE method deletes the specified resource.
  DELETE,
  // The CONNECT method establishes a tunnel to the server identified by the target resource.
  CONNECT,
  // The OPTIONS method describes the communication options for the target resource.
  OPTIONS,
  // The TRACE method performs a message loop-back test along the path to the target resource.
  TRACE,
  // The PATCH method applies partial modifications to a resource.
  PATCH
};

enum class HttpVersion { HTTP1_0, HTTP1_1, HTTP2, HTTP3 };

const char* getRequestMethodString(RequestMethod requestMethod) {
  switch(requestMethod) {
  case RequestMethod::GET:
    return "GET";
  case RequestMethod::HEAD:
    return "HEAD";
  case RequestMethod::POST:
    return "POST";
  case RequestMethod::PUT:
    return "PUT";
  case RequestMethod::DELETE:
    return "DELETE";
  case RequestMethod::OPTIONS:
    return "OPTIONS";
  case RequestMethod::TRACE:
    return "TRACE";
  case RequestMethod::PATCH:
    return "PATCH";
  }
  return nullptr;
}

const char* getHttpVersionString(HttpVersion httpVersion) {
  switch(httpVersion) {
  case HttpVersion::HTTP1_0:
    return "HTTP/1.0";
  case HttpVersion::HTTP1_1:
    return "HTTP/1.1";
  case HttpVersion::HTTP2:
    return "HTTP/2";
  case HttpVersion::HTTP3:
    return "HTTP/3";
  }
  return nullptr;
}

struct Request {
  // using enum RequestMethod;

  RequestMethod method;
  std::string url;
  HttpVersion version;
  Headers headers;
};

struct Response {
  HttpVersion version;
  std::string statusCode;
  std::string statusDescription;

  Headers headers;
  std::string body;
};

enum class SocketError {
  BAD_ALLOC,
  INVALID_SOCKET
};

class SocketException : public std::exception {
public:
  // using enum SocketError;

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
      shutdown(socketFd, SHUT_RDWR);
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

class Epoll {
public:
  explicit Epoll(size_t maxEpollSize) : 
    epoll(epoll_create(128)), epollEvents(new epoll_event[maxEpollSize]), maxEpollSize(maxEpollSize) {
  }

  explicit Epoll() = default;

  ~Epoll() {
    if (epoll > 0) {
      close(epoll);
    }
  }

  bool init(size_t maxEpollSize) {
    if (epollEvents) {
      return false;
    }
    epoll = epoll_create(128);
    epollEvents.reset(new epoll_event[maxEpollSize]);
    this->maxEpollSize = maxEpollSize;
    return true;
  }

  int addEvent(int eventFd, uint32_t events = EPOLLIN | EPOLLET) {
    epoll_event event;
    event.data.fd = eventFd;
    event.events = events;
    return epoll_ctl(epoll, EPOLL_CTL_ADD, eventFd, &event);
  }

  int removeEvent(int eventFd) {
    return epoll_ctl(epoll, EPOLL_CTL_DEL, eventFd, nullptr);
  }

  int modifyEvent(int eventFd, uint32_t events) {
    epoll_event event;
    event.data.fd = eventFd;
    event.events = events;
    return epoll_ctl(epoll, EPOLL_CTL_MOD, eventFd, &event);
  }

  int wait(size_t maxWaitEvents = 1, int timeOut = -1) {
    return epoll_wait(epoll, epollEvents.get(), std::min(maxWaitEvents, maxEpollSize), timeOut);
  }

  inline socket_t getEventFd(unsigned index) const {
    return epollEvents[index].data.fd;
  }

  inline int getEvents(unsigned index) const {
    return epollEvents[index].events;
  }

  inline size_t size() const {
    return maxEpollSize;
  }

protected:
  epoll_t epoll;
  std::unique_ptr<epoll_event[]> epollEvents;
  size_t maxEpollSize;
};

class HttpServer {
public:
  using Handler = std::function<void(const Request&, Response&)>;
  using Handlers = std::vector<std::pair<std::string, Handler>>;
  // using enum RequestMethod;

  HttpServer() = default;

  void init(size_t threadPoolSize, size_t maxEpollSize) {
    maxQueueSize = 1000;
    threadPool.init(threadPoolSize);
    epoll.init(maxEpollSize);
  }

  void get(const std::string& pattern, Handler handler) {
    getHandlers.emplace_back(std::make_pair(pattern, handler));
  }

  void listen(const std::string& host, u_short port) {
    Socket listenSocket;
    listenSocket.bind(host, port);
    listenSocket.listen(maxQueueSize);

    epoll.addEvent(listenSocket.getSocket());


    while (true) {
      int eventNumber = epoll.wait(20, -1);
      if (eventNumber < 0) {
        return;
      }
      if (eventNumber == 0) {
        continue;
      }
      for (int i = 0; i < eventNumber; ++i) {
        if (epoll.getEventFd(i) == listenSocket.getSocket() && (epoll.getEvents(i) & EPOLLIN)) {
          socket_t clientSocketFd = accept(listenSocket.getSocket(), nullptr, nullptr);
          {
          std::unique_lock<std::mutex> temp(tempMutex);
          clientSockets[clientSocketFd] = std::make_unique<Socket>(clientSocketFd);
          }
          if (clientSocketFd < 0) {
            continue;
          }
          epoll.addEvent(clientSocketFd);
        } else if (epoll.getEvents(i) & EPOLLIN) {
          socket_t clientSocketFd = epoll.getEventFd(i);
          // epoll.removeEvent(clientSocketFd);
          threadPool.submitTask(processClient, clientSocketFd, std::ref(*this), std::ref(epoll));
        }
      }
    }
  }

private:
  Epoll epoll;
  ThreadPool threadPool;
  size_t maxQueueSize;
  Handlers getHandlers;

  std::mutex tempMutex;
  std::unordered_map<socket_t, std::unique_ptr<Socket>> clientSockets;

  static void parseRequestLine(const char* s, size_t len, Request& request) {
    static const std::vector<std::string> totalMethod = {"GET", "HEAD", "POST", "PUT", "DELETE", "OPTIONS", "TRACE", "PATCH"};
    static const std::vector<std::string> totalVersion = {"HTTP/1.0", "HTTP/1.1", "HTTP/2", "HTTP/3"};

    size_t begin = 0, cur = 0;

    while (cur < len && s[cur] != ' ') {
      ++cur;
    }
    std::string method(s + begin, cur - begin);
    for (int i = 0; i < totalMethod.size(); ++i) {
      if (totalMethod[i] == method) {
        request.method = static_cast<RequestMethod>(i);
        break;
      }
    }

    begin = ++cur;
    while (cur < len && s[cur] != ' ') {
      ++cur;
    }
    request.url = std::string(s + begin, cur - begin);

    begin = ++cur;
    while (cur < len && s[cur] != '\r' && s[cur] != '\n') {
      ++cur;
    }
    std::string version(s + begin, cur - begin);
    for (int i = 0; i < totalVersion.size(); ++i) {
      if (totalVersion[i] == version) {
        request.version = static_cast<HttpVersion>(i);
      }
    }
  }

  static void parseRequestHeader(const char* s, size_t len, Request& request) {
    size_t begin = 0, cur = 0;
    while (cur < len && s[cur] != ':') {
      ++cur;
    }
    std::string key(s + begin, cur - begin);

    begin = ++cur;
    while (cur < len && s[cur] != '\r' && s[cur] != '\n') {
      ++cur;
    }
    std::string value(s + begin, cur - begin);

    request.headers.emplace(std::move(key), std::move(value));
  }

  static void routeGet(Request& request, Response& response, Handlers& handlers) {
    for (auto& handler : handlers) {
      if (handler.first == request.url) {
        handler.second(std::ref(request), std::ref(response));
        break;
      }
    }
  }

  static void routeNotImplement(Request& request, Response& response, Handlers& handlers) {

  }

  static void route(Request& request, Response& response, HttpServer& server) {
    switch (request.method) {
    case RequestMethod::GET:
      routeGet(request, response, server.getHandlers);
      break;
    case RequestMethod::HEAD:
      routeNotImplement(request, response, server.getHandlers);
      break;
    case RequestMethod::POST:
      routeNotImplement(request, response, server.getHandlers);
      break;
    case RequestMethod::PUT:
      routeNotImplement(request, response, server.getHandlers);
      break;
    case RequestMethod::DELETE:
      routeNotImplement(request, response, server.getHandlers);
      break;
    case RequestMethod::CONNECT:
      routeNotImplement(request, response, server.getHandlers);
      break;
    case RequestMethod::OPTIONS:
      routeNotImplement(request, response, server.getHandlers);
      break;
    case RequestMethod::TRACE:
      routeNotImplement(request, response, server.getHandlers);
      break;
    case RequestMethod::PATCH:
      routeNotImplement(request, response, server.getHandlers);
      break;
    }
  }

  static void processClient(socket_t clientSocketFd, HttpServer& server, Epoll& epoll) {
    // printf("clientSocket: %d\n", clientSocketFd);
    Buffer buffer(clientSocketFd, 65536);
    Request request;
    Response response;
    auto line = buffer.readline();

    // std::string debug(line.first, line.second);
    // printf("RequestLine: %s\n", debug.c_str());

    if (line.first == nullptr) return;

    parseRequestLine(line.first, line.second, request);
    while (true) {
      line = buffer.readline();
      if (line.second == 2 && *line.first == '\r' && *(line.first + 1) == '\n') {
        break;
      }
      // std::string debug(line.first, line.second);
      // printf("RequestHeader: %s\n", debug.c_str());

      parseRequestHeader(line.first, line.second, request);
    }
    line = buffer.readline();
    route(request, response, server);
    
    char tmp[2048];
    int charNumber = sprintf(tmp, "%s %s %s\r\n", getHttpVersionString(response.version), 
                             response.statusCode.c_str(), response.statusDescription.c_str());
    write(clientSocketFd, tmp, charNumber);

    for (auto& header : response.headers) {
      charNumber = sprintf(tmp, "%s: %s\r\n", header.first.c_str(), header.second.c_str());
      write(clientSocketFd, tmp, charNumber);
    }
    write(clientSocketFd, "\r\n", 2);

    write(clientSocketFd, response.body.c_str(), response.body.size());

    // while (true) {
    //   char buff[4096];
    //   read(clientSocketFd, buff, sizeof(buff));
    //   printf("%s\n", buff);
    //   const char ret[] = "HTTP/1.1 501 Method Not Implemeted\r\nServer: jdbhttpd/0.1.0\r\nContent-Type: text/html\r\n\r\nMethod Not Implemeted\r\n";
    //   int offset = 0;
    //   while (offset < sizeof(ret)) {
    //     offset += send(clientSocketFd, ret + offset, sizeof(ret) - offset, 0);
    //     printf("has sent %d data\n", offset);
    //   }
    // }
    // printf("thread ID: %d clientScoketFd: %d\n", gettid(), clientSocketFd);
    // epoll.addEvent(clientSocketFd);
    std::unique_lock<std::mutex> temp(server.tempMutex);
    server.clientSockets.erase(clientSocketFd);
    // shutdown(clientSocketFd, SHUT_RDWR);
  }
};

}

#endif