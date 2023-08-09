#ifndef HTTP_SERVER_H_2048
#define HTTP_SERVER_H_2048

#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <functional>
#include <exception>
#include <algorithm>
#include <unordered_map>
#include <regex>

#include "Socket.hpp"
#include "Epoll.hpp"
#include "ThreadPool.hpp"
#include "Buffer.hpp"
#include "Mmap.hpp"

#include <iostream>

namespace Http {

using Headers = std::unordered_multimap<std::string, std::string>;
using QueryStrings = std::unordered_map<std::string, std::string>;
using ContentProvider = std::function<bool(Buffer&)>;

enum class RequestMethod { GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH };

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
  RequestMethod method;
  std::string url;
  QueryStrings queryStrings;
  HttpVersion version;
  Headers headers;

  std::smatch match;
};

struct Response {
  HttpVersion version;
  std::string statusCode;
  std::string statusDescription;

  Headers headers;
  std::string body;
  ContentProvider contentProvider;
};

class HttpServer {
public:
  using Handler = std::function<void(const Request&, Response&)>;
  using Handlers = std::vector<std::pair<std::regex, Handler>>;
  using BaseDirs = std::vector<std::pair<std::string, std::string>>;

  HttpServer() = default;

  void init(size_t threadPoolSize, size_t maxEpollSize) {
    maxQueueSize = 1000;
    threadPool.init(threadPoolSize);
    epoll.init(maxEpollSize);
  }

  void get(const std::string& pattern, Handler handler) {
    getHandlers.emplace_back(std::make_pair(pattern, handler));
  }

  void post(const std::string& pattern, Handler handler) {
    postHandlers.emplace_back(std::make_pair(pattern, handler));
  }

  void put(const std::string& pattern, Handler handler) {
    putHandlers.emplace_back(std::make_pair(pattern, handler));
  }

  void Delete(const std::string& pattern, Handler handler) {
    deleteHandlers.emplace_back(std::make_pair(pattern, handler));
  }

  void patch(const std::string& pattern, Handler handler) {
    patchHandlers.emplace_back(std::make_pair(pattern, handler));
  }

  void options(const std::string& pattern, Handler handler) {
    optionsHandlers.emplace_back(std::make_pair(pattern, handler));
  }

  void setBaseDir(const std::string& mountDir, const std::string& url) {
    if (!isValidDirectroy(mountDir.c_str())) return;
    baseDirs.push_back(std::make_pair(mountDir, url));
  }

  /*
  case "css"_t: return "text/css";
  case "csv"_t: return "text/csv";
  case "htm"_t:
  case "html"_t: return "text/html";
  case "js"_t:
  case "mjs"_t: return "text/javascript";
  case "txt"_t: return "text/plain";
  case "vtt"_t: return "text/vtt";

  case "apng"_t: return "image/apng";
  case "avif"_t: return "image/avif";
  case "bmp"_t: return "image/bmp";
  case "gif"_t: return "image/gif";
  case "png"_t: return "image/png";
  case "svg"_t: return "image/svg+xml";
  case "webp"_t: return "image/webp";
  case "ico"_t: return "image/x-icon";
  case "tif"_t: return "image/tiff";
  case "tiff"_t: return "image/tiff";
  case "jpg"_t:
  case "jpeg"_t: return "image/jpeg";

  case "mp4"_t: return "video/mp4";
  case "mpeg"_t: return "video/mpeg";
  case "webm"_t: return "video/webm";

  case "mp3"_t: return "audio/mp3";
  case "mpga"_t: return "audio/mpeg";
  case "weba"_t: return "audio/webm";
  case "wav"_t: return "audio/wave";

  case "otf"_t: return "font/otf";
  case "ttf"_t: return "font/ttf";
  case "woff"_t: return "font/woff";
  case "woff2"_t: return "font/woff2";

  case "7z"_t: return "application/x-7z-compressed";
  case "atom"_t: return "application/atom+xml";
  case "pdf"_t: return "application/pdf";
  case "json"_t: return "application/json";
  case "rss"_t: return "application/rss+xml";
  case "tar"_t: return "application/x-tar";
  case "xht"_t:
  case "xhtml"_t: return "application/xhtml+xml";
  case "xslt"_t: return "application/xslt+xml";
  case "xml"_t: return "application/xml";
  case "gz"_t: return "application/gzip";
  case "zip"_t: return "application/zip";
  case "wasm"_t: return "application/wasm";
  */

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
          epoll.removeEvent(clientSocketFd);
          threadPool.submitTask(processClient, clientSocketFd, std::ref(*this), std::ref(epoll));
        }
      }
    }
  }

// private:
  Epoll epoll;
  ThreadPool threadPool;
  size_t maxQueueSize;
  Handlers getHandlers;
  Handlers postHandlers;
  Handlers putHandlers;
  Handlers deleteHandlers;
  Handlers patchHandlers;
  Handlers optionsHandlers;
  BaseDirs baseDirs;

  std::mutex tempMutex;
  std::unordered_map<socket_t, std::unique_ptr<Socket>> clientSockets;

  static inline bool isValidDirectroy(const char* dir) {
    struct stat dirStatus;
    return stat(dir, &dirStatus) >= 0 && S_ISDIR(dirStatus.st_mode);
  }

  static inline bool isValidFile(const char* dir) {
    struct stat dirStatus;
    return stat(dir, &dirStatus) >= 0 && S_ISREG(dirStatus.st_mode);
  }

  static inline int hexCharToInt(char c) {
    if (isalpha(c)) {
      return tolower(c) - 'a';
    }
    if (isdigit(c)) {
      return c - '0';
    }
    return -1;
  }

  static std::string decode(const char* s, size_t len) {
    std::string decodeString;
    decodeString.reserve(len);
    for (size_t i = 0; i < len; ++i) {
      if (s[i] == '#') {
        break;
      }
      if (s[i] == '%') {
        if (i + 2 >= len || hexCharToInt(s[i + 1]) == -1 || hexCharToInt(s[i + 2]) == -1) {
          return "";
        }
        decodeString.push_back(hexCharToInt(s[i + 1]) * 16 + hexCharToInt(s[i + 2]));
        i += 2;
      } else {
        decodeString.push_back(s[i]);
      }
    }
    return decodeString;
  }

  static bool parseUrl(Request& request, const char* s, size_t len) {
    const char* query = (const char*)memchr(s, '?', len);
    if (query == nullptr) {
      request.url = decode(s, len);
      return !request.url.empty();
    }
    request.url = decode(s, query - s);
    if (request.url.empty()) {
      return false;
    }
    return parseQueryString(request, query + 1, len - (query - s) - 1);
  }

  static bool parseQueryString(Request& request, const char* s, size_t len) {
    while (len) {
      const char* mid = (const char*)memchr(s, '=', len);
      if (mid == nullptr) {
        return false;
      }
      len -= (mid + 1 - s);
      const char* end = (const char*)memchr(mid + 1, '&', len);
      if (end == nullptr) {
        end = mid + 1 + len;
        len = 0;
      } else {
        len -= (end - mid);
      }
      std::string key(decode(s, mid - s));
      std::string value(decode(mid + 1, end - mid - 1));
      if (key == "") {
        return false;
      }
      request.queryStrings[key] = value;
      s = end + 1;
    }

    return true;
  }

  static bool parseRequestLine(const char* s, size_t len, Request& request) {
    static const std::unordered_map<std::string, RequestMethod> totalMethod = {
      {"GET", RequestMethod::GET},
      {"HEAD", RequestMethod::HEAD}, 
      {"POST", RequestMethod::POST}, 
      {"PUT", RequestMethod::PUT}, 
      {"DELETE", RequestMethod::DELETE}, 
      {"OPTIONS", RequestMethod::OPTIONS}, 
      {"TRACE", RequestMethod::TRACE}, 
      {"PATCH", RequestMethod::PATCH}
    };
    static const std::unordered_map<std::string, HttpVersion> totalVersion = {
      {"HTTP/1.0", HttpVersion::HTTP1_0},
      {"HTTP/1.1", HttpVersion::HTTP1_1},
      {"HTTP/2", HttpVersion::HTTP2},
      {"HTTP/3", HttpVersion::HTTP3}
    };

    size_t begin = 0, cur = 0;

    while (cur < len && s[cur] != ' ') {
      ++cur;
    }
    std::string method(s + begin, cur - begin);
    auto methodIter = totalMethod.find(method);
    if (methodIter == totalMethod.end()) {
      return false;
    }
    request.method = methodIter->second;
    
    begin = ++cur;
    size_t fragment = SIZE_MAX;
    while (cur < len && s[cur] != ' ') {
      if (fragment != SIZE_MAX && s[cur] == '#') {
        fragment = cur;
      }
      ++cur;
    }
    if (!parseUrl(request, s + begin, std::min(fragment, cur) - begin)) {
      return false;
    }

    begin = ++cur;
    while (cur < len && s[cur] != '\r' && s[cur] != '\n') {
      ++cur;
    }
    std::string version(s + begin, cur - begin);
    auto versionIter = totalVersion.find(version);
    if (versionIter == totalVersion.end()) {
      return false;
    }
    request.version = versionIter->second;

    return true;
  }

  static bool parseRequestHeader(const char* s, size_t len, Request& request) {
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
    return true;
  }

  static bool parseRequest(Buffer& buffer, Request& request) {
    auto line = buffer.readline();

    // std::string debug(line.first, line.second);
    // printf("RequestLine: %s\n", debug.c_str());

    if (line.first == nullptr) return false;

    if (!parseRequestLine(line.first, line.second, request)) return false;

    while (true) {
      line = buffer.readline();
      if (line.second == 2 && *line.first == '\r' && *(line.first + 1) == '\n') {
        break;
      }
      // std::string debug(line.first, line.second);
      // printf("RequestHeader: %s\n", debug.c_str());
      if (!parseRequestHeader(line.first, line.second, request)) return false;
    }
    line = buffer.readline();
    return true;
  }

  static bool writeResponse(Buffer& buffer, const Response& response) {
    buffer.bufferedWriteFd("%s %s %s\r\n", getHttpVersionString(response.version), response.statusCode.c_str(), response.statusDescription.c_str());

    for (auto& header : response.headers) {
      buffer.bufferedWriteFd("%s: %s\r\n", header.first.c_str(), header.second.c_str());
    }
    buffer.bufferedWriteFd("\r\n");

    buffer.bufferedWriteFd("%s", response.body.c_str());

    buffer.flushWrite();
    // TODO: add more pattern

    return true;
  }

  static bool routeStaticFiles(Request& request, Response& response, BaseDirs& baseDirs) {
    for (const auto& baseDir : baseDirs) {
      if (!request.url.compare(0, baseDir.second.length(), baseDir.second)) {
        continue;
      }
      std::string filePath = baseDir.first + "/" + request.url.substr(baseDir.second.length());
      if (!isValidFile(filePath.c_str())) {
        continue;
      }
      std::shared_ptr mmap = std::make_shared<Mmap>(filePath);
      response.contentProvider = [&mmap](Buffer& buffer) -> bool {
        return buffer.writeFd(mmap->data(), mmap->size());
      };
    }
    return false;
  }

  static bool routeHandlers(Request& request, Response& response, Handlers& handlers) {
    for (auto& handler : handlers) {
      if (std::regex_match(request.url, request.match, handler.first)) {
        handler.second(std::ref(request), std::ref(response));
        return true;
      }
    }
    return false;
  }

  static bool routeNotImplement(Request& request, Response& response, Handlers& handlers) {
    return false;
  }

  static bool route(Request& request, Response& response, HttpServer& server) {
    if ((request.method == RequestMethod::GET || request.method == RequestMethod::HEAD) && 
        routeStaticFiles(request, response, server.baseDirs)) {
      return true;
    }
    switch (request.method) {
    case RequestMethod::GET:
      return routeHandlers(request, response, server.getHandlers);
    case RequestMethod::HEAD:
      return routeNotImplement(request, response, server.getHandlers);
    case RequestMethod::POST:
      return routeHandlers(request, response, server.postHandlers);
    case RequestMethod::PUT:
      return routeHandlers(request, response, server.putHandlers);
    case RequestMethod::DELETE:
      return routeHandlers(request, response, server.deleteHandlers);
    case RequestMethod::CONNECT:
      return routeNotImplement(request, response, server.getHandlers);
    case RequestMethod::OPTIONS:
      return routeHandlers(request, response, server.optionsHandlers);
    case RequestMethod::TRACE:
      return routeNotImplement(request, response, server.getHandlers);
    case RequestMethod::PATCH:
      return routeHandlers(request, response, server.patchHandlers);
    }
    return false;
  }

  static void processClient(socket_t clientSocketFd, HttpServer& server, Epoll& epoll) {
    // printf("clientSocket: %d\n", clientSocketFd);
    Buffer buffer(clientSocketFd);
    Request request;
    Response response;
    if (!parseRequest(buffer, request)) {
      // TODO: add error handler
    }

    if (!route(request, response, server)) {
      // TODO: add error handler
    }

    writeResponse(buffer, response);
    epoll.addEvent(clientSocketFd);

    // std::unique_lock<std::mutex> temp(server.tempMutex);
    // server.clientSockets.erase(clientSocketFd);
    // shutdown(clientSocketFd, SHUT_RDWR);
  }
};

}

#endif