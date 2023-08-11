#include "HttpServer.hpp"

using namespace Http;

int main() {
  HttpServer httpServer;
  // test request parsing
  // Request request;
  // const char* s = "GET /hos%20t/xxx?bbb=xxx&ccc=&bbb=233%21 HTTP/1.1";
  // std::cerr << std::boolalpha << HttpServer::parseRequestLine(s, strlen(s), request) << std::endl;
  // std::cerr << getRequestMethodString(request.method) << std::endl; 
  // std::cerr << request.url << std::endl; 
  // std::cerr << getHttpVersionString(request.version) << std::endl; 
  // for (auto& x : request.queryStrings) {
  //   std::cerr << x.first << " " << x.second << std::endl; 
  // }
  // return 0;
  int requestCount = 0;
  httpServer.init(10, 100);
  httpServer.setBaseDir("./build/resources", "/static");
  httpServer.get("/help", [](const Request& request, Response& response) {
    response.version = request.version;
    response.statusCode = 200;
    response.headers.emplace("Content-Type", "text/html");
    response.body = "response help";
    response.headers.emplace("Content-Length", std::to_string(response.body.length()));
  });
  httpServer.get("/test", [&requestCount](const Request& request, Response& response) {
    ++requestCount;
    response.version = request.version;
    response.statusCode = 200;
    response.headers.emplace("Content-Type", "text/html");
    response.body = "response test" + std::to_string(requestCount);
    response.headers.emplace("Content-Length", std::to_string(response.body.length()));
  });
  httpServer.listen("127.0.0.1", 20480);
}