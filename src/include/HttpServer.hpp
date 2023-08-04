#ifndef HTTP_SERVER_2048
#define HTTP_SERVER_2048

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <cstdio>
#include <string>
#include <functional>

#include "threadpool.hpp"

struct Request {

};

struct Response {

};

class HttpServer {
public:
  using Handler = std::function<void(const Request&, Response&)>;

  HttpServer() = default;

  void init(size_t threadPoolSize) {
    threadPool.init(threadPoolSize);
  }

  void get(const std::string& pattern, Handler handler) {

  }

  void listen(const std::string& host, u_short port) {
    httpSocket = socket(AF_INET, SOCK_STREAM, 0);
    const int TRUE = 1;
    setsockopt(httpSocket, SOL_SOCKET, SO_REUSEADDR, &TRUE, sizeof(TRUE));
    setsockopt(httpSocket, SOL_SOCKET, SO_REUSEPORT, &TRUE, sizeof(TRUE));
    sockaddr_in addr;
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (bind(httpSocket, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
      perror("bind error");
      exit(-1);
    }
    if (::listen(httpSocket, 10) < 0) {
      perror("listen error");
      exit(-1);
    }

    httpEpoll = epoll_create(10);
    epoll_event event;
    event.data.fd = httpSocket;
    event.events = EPOLLIN;
    epoll_ctl(httpEpoll, EPOLL_CTL_ADD, httpSocket, &event);

    while (true) {
      int eventNumber = epoll_wait(httpEpoll, events, 20, -1);
      if (eventNumber < 0) {
        return;
      }
      if (eventNumber == 0) {
        continue;
      }
      for (int i = 0; i < eventNumber; ++i) {
        if (events[i].data.fd == httpSocket && (events[i].events & EPOLLIN)) {
          sockaddr_in clientAddr;
          socklen_t clientAddrLen = sizeof(clientAddr);
          socket_t clientSocket = accept(httpSocket, (struct sockaddr*) &clientAddr, &clientAddrLen);
          if (clientSocket < 0) {
            continue;
          }
          epoll_event clientEvent;
          clientEvent.data.fd = clientSocket;
          clientEvent.events = EPOLLIN;
          epoll_ctl(httpEpoll, EPOLL_CTL_ADD, clientSocket, &clientEvent);
        } else if (events[i].events & EPOLLIN) {
          int clientSocket = events[i].data.fd;
          threadPool.submitTask([clientSocket](){
            printf("clientSocket: %d\n", clientSocket);
            char buff[4096];
            read(clientSocket, buff, sizeof(buff));
            printf("%s\n", buff);
            const char ret[] = "HTTP/1.1 501 Method Not Implemeted\r\nServer: jdbhttpd/0.1.0\r\nContent-Type: text/html\r\n\r\nMethod Not Implemeted\r\n";
            int offset = 0;
            while (offset < sizeof(ret)) {
              offset += send(clientSocket, ret + offset, sizeof(ret) - offset, 0);
              printf("has sent %d data\n", offset);
            }
          });
        }
      }
    }
  }

private:
  using socket_t = int;
  using epoll_t = int;

  epoll_event events[20];
  epoll_t httpEpoll;
  socket_t httpSocket;
  ThreadPool threadPool;
  
};

#endif