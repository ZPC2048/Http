#ifndef THREADPOOL_H_2048
#define THREADPOOL_H_2048

#include <thread>
#include <mutex>
#include <queue>
#include <functional>
#include <memory>
#include <future>
#include <exception>

class ThreadPool {
public:
  explicit ThreadPool() {
  }

  explicit ThreadPool(const size_t threadPoolSize, bool detach) :
    threadPoolPtr(std::make_shared<ThreadPoolPtr>(threadPoolSize, false, detach)) {
    for (size_t i = 0; i < threadPoolSize; ++i) {
      threadPoolPtr->threads[i] = std::move(std::thread(ThreadWorker(i, threadPoolPtr)));
      if (threadPoolPtr->detach) {
        threadPoolPtr->threads[i].detach();
      }
    }
  }

  ~ThreadPool() {
    if (!useable()) return;
    {
      std::unique_lock<std::mutex> lockPoolMutex(threadPoolPtr->poolMutex);
      threadPoolPtr->closed = true;
    }
    threadPoolPtr->conditionVariable.notify_all();
    if (!threadPoolPtr->detach) {
      for (auto &thread : threadPoolPtr->threads) {
        thread.join();
      }
    }
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator= (const ThreadPool&) = delete;
  ThreadPool& operator= (ThreadPool&&) = delete;

  void init(const size_t threadPoolSize = 10, bool detach = true) {
    threadPoolPtr = std::make_shared<ThreadPoolPtr>(threadPoolSize, false, detach);
    for (size_t i = 0; i < threadPoolSize; ++i) {
      threadPoolPtr->threads[i] = std::move(std::thread(ThreadWorker(i, threadPoolPtr)));
      if (threadPoolPtr->detach) {
        threadPoolPtr->threads[i].detach();
      }
    }
  }

  template<typename F, typename... Args>
  auto submitTask(F&& f, Args&&... args) -> std::future<decltype(std::forward<F>(f)(std::forward<Args>(args)...))> {
    using returnType = decltype(std::forward<F>(f)(std::forward<Args>(args)...));
    std::function<returnType()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    auto funcPtr = std::make_shared<std::packaged_task<returnType()>>(func);
    auto wrappedFunction = [funcPtr]() {
      (*funcPtr)();
    };
    {
      std::unique_lock<std::mutex> lockPoolMutex(threadPoolPtr->poolMutex);
      threadPoolPtr->taskQueue.emplace(wrappedFunction);
    }
    threadPoolPtr->conditionVariable.notify_one();
    return funcPtr->get_future();
  }

  void join() {
    {
      std::unique_lock<std::mutex> lockPoolMutex(threadPoolPtr->poolMutex);
      threadPoolPtr->closed = true;
    }
    threadPoolPtr->conditionVariable.notify_all();
    for (auto &thread : threadPoolPtr->threads) {
      thread.join();
    }
  }

  inline bool useable() {
    return threadPoolPtr && !threadPoolPtr->closed;
  }

protected:
  struct ThreadPoolPtr {
    bool closed;
    bool detach;
    std::mutex poolMutex;
    std::vector<std::thread> threads;
    std::queue<std::function<void()>> taskQueue;
    std::condition_variable conditionVariable;

    explicit ThreadPoolPtr(size_t threadPoolSize, bool closed, bool detach) :
      threads(threadPoolSize), closed(closed), detach(detach) {
    }
  };

  class ThreadWorker {
  public:
    ThreadWorker(int threadId, std::shared_ptr<ThreadPoolPtr> pool) :
      threadId(threadId), pool(pool) {}
    
    void operator() () {
      std::function<void()> f;
      std::unique_lock<std::mutex> lockPoolMutex(pool->poolMutex);
      while (true) {
        if (!pool->taskQueue.empty()) {
          f = std::move(pool->taskQueue.front());
          pool->taskQueue.pop();
          lockPoolMutex.unlock();
          f();
          lockPoolMutex.lock();
        } else if (pool->closed) {
          break;
        } else {
          pool->conditionVariable.wait(lockPoolMutex);
        }
      }
    }
    int threadId;
    std::shared_ptr<ThreadPoolPtr> pool;
  };

  std::shared_ptr<ThreadPoolPtr> threadPoolPtr;
};

#endif