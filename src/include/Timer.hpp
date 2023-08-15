#ifndef TIMER_H_2048
#define TIMER_H_2048

#include <vector>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <mutex>

#include "Socket.hpp"

namespace Http {

// Multi thread safe
class Timer {
public:
  Timer() {
    priorityQueue.push_back(Node());
  }

  // @param timeOut ms
  bool insert(socket_t fd, size_t timeOut) {
    std::unique_lock<std::mutex> lock(mutex);
    if (fdToIndex.find(fd) != fdToIndex.end()) {
      return false;
    }
    push(Node(std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()) + std::chrono::milliseconds(timeOut), fd));
    return true;
  }

  // @return fd, -1 for no more timeout socket
  socket_t getNextTimeOutSocket() {
    std::unique_lock<std::mutex> lock(mutex);
    if (empty() || top().timeOut > std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now())) {
      return -1;
    }
    socket_t retSocket = top().fd;
    pop();
    return retSocket;
  }

  // @param timeOut ms
  bool modifyTimeOut(socket_t fd, size_t timeOut) {
    std::unique_lock<std::mutex> lock(mutex);
    auto iter = fdToIndex.find(fd);
    if (iter == fdToIndex.end()) {
      return false;
    }
    Node newNode(std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()) + std::chrono::milliseconds(timeOut), fd);
    if (newNode < priorityQueue[iter->second]) {
      priorityQueue[iter->second] = newNode;
      shift(iter->second);
    } else {
      priorityQueue[iter->second] = newNode;
      pushdown(iter->second);
    }
    return true;
  }

  // @return ms, -1 for infinity
  int getNextTriggerTime() {
    std::unique_lock<std::mutex> lock(mutex);
    if (empty()) return -1;
    return (top().timeOut - std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now())).count();
  }

  bool removeSocket(socket_t fd) {
    std::unique_lock<std::mutex> lock(mutex);
    auto iter = fdToIndex.find(fd);
    if (iter == fdToIndex.end()) {
      return false;
    }
    remove(iter->second);
    return true;
  }

private:
  using TimePoint = std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds>;
  struct Node {
    TimePoint timeOut;
    socket_t fd;

    Node(TimePoint timeOut = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()), socket_t fd = -1) :
      timeOut(timeOut), fd(fd) {
    }

    inline bool operator < (const Node& other) const {
      return timeOut < other.timeOut;
    }
  };

  inline bool empty() {
    return priorityQueue.size() <= 1;
  }

  bool push(Node&& node) {
    priorityQueue.emplace_back(node);
    size_t curIndex = priorityQueue.size() - 1;
    fdToIndex[node.fd] = curIndex;
    shift(curIndex);
    return true;
  }

  bool remove(size_t index) {
    swap(index, priorityQueue.size() - 1);
    fdToIndex.erase(priorityQueue.back().fd);
    priorityQueue.pop_back();
    pushdown(index);
    return true;
  }

  void swap(size_t index1, size_t index2) {
    std::swap(fdToIndex[priorityQueue[index1].fd], fdToIndex[priorityQueue[index2].fd]);
    std::swap(priorityQueue[index1], priorityQueue[index2]);
  }

  void shift(size_t index) {
    while (index && priorityQueue[index] < priorityQueue[index >> 1]) {
      swap(index, index >> 1);
      index >>= 1;
    }
  }

  void pushdown(size_t index) {
    size_t leftSon = index << 1;
    size_t rightSon = index << 1 | 1;
    while (leftSon < priorityQueue.size()) {
      size_t minSon = leftSon;
      if (rightSon < priorityQueue.size() && priorityQueue[rightSon] < priorityQueue[minSon]) {
        minSon = rightSon;
      }
      if (priorityQueue[index] < priorityQueue[minSon]) {
        break;
      }
      swap(minSon, index);
      index = minSon;
      leftSon = index << 1;
      rightSon = index << 1 | 1;
    }
  }

  inline Node& top() {
    return priorityQueue[1];
  }

  inline void pop() {
    swap(1, priorityQueue.size() - 1);
    fdToIndex.erase(priorityQueue.back().fd);
    priorityQueue.pop_back();
    pushdown(1);
  }

  std::mutex mutex;
  std::vector<Node> priorityQueue;
  std::unordered_map<socket_t, size_t> fdToIndex;
};

}

#endif