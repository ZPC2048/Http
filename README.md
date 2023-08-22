## C++ Httplib
A httplib based on C++11 on linux.

### Simple Example
```cpp
#include "HttpServer.hpp"

using namespace Http;

int main() {
  HttpServer httpServer;
  int requestCount = 0;
  // init must be call only once and before any other function
  httpServer.init(10, 100); // threadpool size, epoll size
  // set static file dir and bind it to given url
  httpServer.setBaseDir("./build/resources", "/static");
  // bind url process function
  httpServer.get("/test", [&requestCount](const Request& request, Response& response) {
    ++requestCount;
    response.version = request.version;
    response.statusCode = 200;
    response.headers.emplace("Content-Type", "text/html");
    response.body = "response test" + std::to_string(requestCount);
    response.headers.emplace("Content-Length", std::to_string(response.body.length()));
  });
  // listen hosts and port
  httpServer.listen("127.0.0.1", 20480);
}
```