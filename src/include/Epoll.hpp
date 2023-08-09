#ifndef EPOLL_H_2048
#define EPOLL_H_2048

#include <sys/types.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <memory>

#include "Socket.hpp"

namespace Http {

using epoll_t = int;

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
}

#endif