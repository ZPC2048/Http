#include "HttpServer.hpp"

using namespace Http;

int main() {
  HttpServer httpServer;
  int requestCount = 0;
  httpServer.init(10, 20);
  httpServer.get("/help", [](const Request& request, Response& response) {
    response.version = request.version;
    response.statusCode = "200";
    response.statusDescription = "OK";
    response.headers.emplace("Content-Type", "text/html");
    response.body = "response help";
  });
  httpServer.get("/test", [&requestCount](const Request& request, Response& response) {
    ++requestCount;
    response.version = request.version;
    response.statusCode = "200";
    response.statusDescription = "OK";
    response.headers.emplace("Content-Type", "text/html");
    response.body = "response test" + std::to_string(requestCount);
  });
  httpServer.listen("127.0.0.1", 20480);
}