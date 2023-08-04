#include "HttpServer.hpp"

int main() {
  HttpServer httpServer;
  httpServer.init(10);
  httpServer.listen("127.0.0.1", 20480);
}