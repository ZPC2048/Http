#include "HttpServer.hpp"

using namespace Http;

int main() {
  HttpServer httpServer;
  int requestCount = 0;
  httpServer.init(10, 100);
  httpServer.get("/help", [](const Request& request, Response& response) {
    response.version = HttpVersion::HTTP1_0;
    response.statusCode = "200";
    response.statusDescription = "OK";
    response.headers.emplace("Content-Type", "text/html");
    response.body = "response help";
    response.headers.emplace("Content-Length", std::to_string(response.body.length()));
  });
  httpServer.get("/test", [&requestCount](const Request& request, Response& response) {
    ++requestCount;
    response.version = HttpVersion::HTTP1_0;
    response.statusCode = "200";
    response.statusDescription = "OK";
    response.headers.emplace("Content-Type", "text/html");
    response.body = "response test" + std::to_string(requestCount);
    response.headers.emplace("Content-Length", std::to_string(response.body.length()));
  });
  httpServer.listen("127.0.0.1", 20480);
}