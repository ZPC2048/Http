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
#include "Timer.hpp"

#include <iostream>

namespace Http {

using Headers = std::unordered_map<std::string, std::string>;
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

const char* getStatusMessage(int statusCode) {
  switch (statusCode) {
  case 100: return "Continue";
  case 101: return "Switching Protocol";
  case 102: return "Processing";
  case 103: return "Early Hints";
  case 200: return "OK";
  case 201: return "Created";
  case 202: return "Accepted";
  case 203: return "Non-Authoritative Information";
  case 204: return "No Content";
  case 205: return "Reset Content";
  case 206: return "Partial Content";
  case 207: return "Multi-Status";
  case 208: return "Already Reported";
  case 226: return "IM Used";
  case 300: return "Multiple Choice";
  case 301: return "Moved Permanently";
  case 302: return "Found";
  case 303: return "See Other";
  case 304: return "Not Modified";
  case 305: return "Use Proxy";
  case 306: return "unused";
  case 307: return "Temporary Redirect";
  case 308: return "Permanent Redirect";
  case 400: return "Bad Request";
  case 401: return "Unauthorized";
  case 402: return "Payment Required";
  case 403: return "Forbidden";
  case 404: return "Not Found";
  case 405: return "Method Not Allowed";
  case 406: return "Not Acceptable";
  case 407: return "Proxy Authentication Required";
  case 408: return "Request Timeout";
  case 409: return "Conflict";
  case 410: return "Gone";
  case 411: return "Length Required";
  case 412: return "Precondition Failed";
  case 413: return "Payload Too Large";
  case 414: return "URI Too Long";
  case 415: return "Unsupported Media Type";
  case 416: return "Range Not Satisfiable";
  case 417: return "Expectation Failed";
  case 418: return "I'm a teapot";
  case 421: return "Misdirected Request";
  case 422: return "Unprocessable Entity";
  case 423: return "Locked";
  case 424: return "Failed Dependency";
  case 425: return "Too Early";
  case 426: return "Upgrade Required";
  case 428: return "Precondition Required";
  case 429: return "Too Many Requests";
  case 431: return "Request Header Fields Too Large";
  case 451: return "Unavailable For Legal Reasons";
  case 501: return "Not Implemented";
  case 502: return "Bad Gateway";
  case 503: return "Service Unavailable";
  case 504: return "Gateway Timeout";
  case 505: return "HTTP Version Not Supported";
  case 506: return "Variant Also Negotiates";
  case 507: return "Insufficient Storage";
  case 508: return "Loop Detected";
  case 510: return "Not Extended";
  case 511: return "Network Authentication Required";

  default:
  case 500: return "Internal Server Error";
  }
}

struct Request {
  RequestMethod method;
  std::string url;
  QueryStrings queryStrings;
  HttpVersion version;
  Headers headers;
  bool keepAlive = false;

  std::smatch match;

  inline bool isKeyInHeader(const std::string& key) {
    return headers.find(key) != headers.end();
  }

  inline const std::string& getHeaderValue(const std::string& key) {
    auto iter = headers.find(key);
    if (iter == headers.end()) {
      throw std::runtime_error("passed key not in header");
    }
    return iter->second;
  }
};

struct Response {
  HttpVersion version = HttpVersion::HTTP1_1;
  int statusCode = -1;

  Headers headers;
  bool keepAlive = false;

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

  void listen(const std::string& host, u_short port) {
    Socket listenSocket;
    listenSocket.bind(host, port);
    listenSocket.listen(maxQueueSize);

    epoll.addEvent(listenSocket.getSocket());

    while (true) {
      socket_t timeOutSocket = timer.getNextTimeOutSocket();
      while (timeOutSocket != -1) {
        {
        std::unique_lock<std::mutex> clientSocketsLock(clientSocketsMutex);
        clientSockets.erase(timeOutSocket);
        }
        timeOutSocket = timer.getNextTimeOutSocket();
      }

      int nextTriggerTime = timer.getNextTriggerTime();

      int eventNumber = epoll.wait(20, nextTriggerTime <= 0 ? -1 : nextTriggerTime);
      if (eventNumber < 0) {
        return;
      }
      if (eventNumber == 0) {
        continue;
      }
      for (int i = 0; i < eventNumber; ++i) {
        if (epoll.getEventFd(i) == listenSocket.getSocket() && (epoll.getEvents(i) & EPOLLIN)) {
          socket_t clientSocketFd = accept(listenSocket.getSocket(), nullptr, nullptr);
          if (clientSocketFd < 0) {
            continue;
          }
          timer.insert(clientSocketFd, DEFAULT_TIME_OUT);
          epoll.addEvent(clientSocketFd);
          std::unique_lock<std::mutex> clientSocketsLock(clientSocketsMutex);
          clientSockets[clientSocketFd] = std::make_unique<Socket>(clientSocketFd);
        } else if (epoll.getEvents(i) & (EPOLLRDHUP | EPOLLERR)) {
          socket_t clientSocketFd = epoll.getEventFd(i);
          timer.removeSocket(clientSocketFd);
          epoll.removeEvent(clientSocketFd);
          std::unique_lock<std::mutex> clientSocketsLock(clientSocketsMutex);
          clientSockets.erase(clientSocketFd);
        } else if (epoll.getEvents(i) & EPOLLIN) {
          socket_t clientSocketFd = epoll.getEventFd(i);
          timer.removeSocket(clientSocketFd);
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

  std::mutex clientSocketsMutex;
  std::unordered_map<socket_t, std::unique_ptr<Socket>> clientSockets;

  const int DEFAULT_TIME_OUT = 60000;
  Timer timer;

  static inline bool isValidDirectroy(const char* dir) {
    struct stat dirStatus;
    return stat(dir, &dirStatus) >= 0 && S_ISDIR(dirStatus.st_mode);
  }

  static inline bool isValidFile(const char* dir) {
    struct stat dirStatus;
    return stat(dir, &dirStatus) >= 0 && S_ISREG(dirStatus.st_mode);
  }

  static std::string getFileType(std::string& path) {
    static std::unordered_map<std::string, std::string> totalType = {
      {"css", "text/css"},
      {"csv", "text/csv"},
      {"htm", "text/html"},
      {"html", "text/html"},
      {"js", "text/javascript"},
      {"mjs", "text/javascript"},
      {"txt", "text/plain"},
      {"vtt", "text/vtt"},

      {"apng", "image/apng"},
      {"avif", "image/avif"},
      {"bmp", "image/bmp"},
      {"gif", "image/gif"},
      {"png", "image/png"},
      {"svg", "image/svg+xml"},
      {"webp", "image/webp"},
      {"ico", "image/x-icon"},
      {"tif", "image/tiff"},
      {"tiff", "image/tiff"},
      {"jpg", "image/jpeg"},
      {"jpeg", "image/jpeg"},
      {"mp4", "video/mp4"},
      {"mpeg", "video/mpeg"},
      {"webm", "video/webm"},

      {"mp3", "audio/mp3"},
      {"mpga", "audio/mpeg"},
      {"weba", "audio/webm"},
      {"wav", "audio/wave"},

      {"otf", "font/otf"},
      {"ttf", "font/ttf"},
      {"woff", "font/woff"},
      {"woff2", "font/woff2"},

      {"7z", "application/x-7z-compressed"},
      {"atom", "application/atom+xml"},
      {"pdf", "application/pdf"},
      {"json", "application/json"},
      {"rss", "application/rss+xml"},
      {"tar", "application/x-tar"},
      {"xht", "application/xhtml+xml"},
      {"xhtml", "application/xhtml+xml"},
      {"xslt", "application/xslt+xml"},
      {"xml", "application/xml"},
      {"gz", "application/gzip"},
      {"zip", "application/zip"},
      {"wasm", "application/wasm"},
    };
    static std::string defaultType = "application/octet-stream";

    for (int i = path.size() - 1; i >= 0; --i) {
      if (path[i] == '.') {
        std::string ext = path.substr(i + 1);
        if (totalType.find(ext) == totalType.end()) {
          return defaultType;
        }
        return totalType.find(ext)->second;
      }
    }
    return defaultType;
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

    ++cur;
    while (cur < len && isblank(s[cur])) {
      ++cur;
    }
    begin = cur;

    while (cur < len && s[cur] != '\r' && s[cur] != '\n') {
      ++cur;
    }
    std::string value(s + begin, cur - begin);

    request.headers.emplace(std::move(key), std::move(value));
    return true;
  }

  static bool parseRequest(Buffer& buffer, Request& request) {
    auto line = buffer.readline();

    if (line.first == nullptr) return false;

    if (!parseRequestLine(line.first, line.second, request)) return false;

    while (true) {
      line = buffer.readline();
      if (line.second == 2 && *line.first == '\r' && *(line.first + 1) == '\n') {
        break;
      }
      if (!parseRequestHeader(line.first, line.second, request)) return false;
    }
    line = buffer.readline();

    if (request.isKeyInHeader("Connection")) {
      std::cerr << request.getHeaderValue("Connection") << std::endl;
    }

    if ((request.isKeyInHeader("Connection") && request.getHeaderValue("Connection") == "keep-alive") || 
        (!request.isKeyInHeader("Connection") && request.version == HttpVersion::HTTP1_1)) {
      request.keepAlive = true;
    }
    return true;
  }

  static bool writeResponse(Buffer& buffer, Response& response) {
    // response status line
    buffer.bufferedWriteFd("%s %d %s\r\n", getHttpVersionString(response.version), response.statusCode, getStatusMessage(response.statusCode));

    // set keep-alive or not
    if (response.keepAlive) {
      response.headers["Connection"] = "keep-alive";
    } else {
      response.headers["Connection"] = "close";
    }

    // response header
    for (auto& header : response.headers) {
      buffer.bufferedWriteFd("%s: %s\r\n", header.first.c_str(), header.second.c_str());
    }
    buffer.bufferedWriteFd("\r\n");
    buffer.flushWrite();

    //response body
    if (response.contentProvider) {
      response.contentProvider(buffer);
    } else {
      buffer.bufferedWriteFd("%s", response.body.c_str());
    }

    buffer.flushWrite();

    return true;
  }

  static bool routeStaticFiles(Request& request, Response& response, BaseDirs& baseDirs) {
    for (const auto& baseDir : baseDirs) {
      if (request.url.compare(0, baseDir.second.length(), baseDir.second) != 0) {
        continue;
      }
      std::string filePath = baseDir.first + request.url.substr(baseDir.second.length());
      if (!isValidFile(filePath.c_str())) {
        continue;
      }

      response.statusCode = 200;
      std::shared_ptr mmap = std::make_shared<Mmap>(filePath.c_str());
      response.headers.emplace("Content-Length", std::to_string(mmap->size()));
      response.headers.emplace("Content-Type", getFileType(filePath));
      if (request.method == RequestMethod::GET) {
        response.contentProvider = [mmap](Buffer& buffer) -> bool {
          return buffer.writeFd(mmap->data(), mmap->size());
        };
      }

      return true;
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
    case RequestMethod::HEAD:
      return routeHandlers(request, response, server.getHandlers);
    case RequestMethod::POST:
      return routeHandlers(request, response, server.postHandlers);
    case RequestMethod::PUT:
      return routeHandlers(request, response, server.putHandlers);
    case RequestMethod::DELETE:
      return routeHandlers(request, response, server.deleteHandlers);
    case RequestMethod::OPTIONS:
      return routeHandlers(request, response, server.optionsHandlers);
    case RequestMethod::PATCH:
      return routeHandlers(request, response, server.patchHandlers);
    }

    response.statusCode = 400;
    return false;
  }

  static void processClient(socket_t clientSocketFd, HttpServer& server, Epoll& epoll) {
    Buffer buffer(clientSocketFd);
    Request request;
    Response response;
    if (!parseRequest(buffer, request)) {
      writeResponse(buffer, response);
      return;
    }

    response.keepAlive = request.keepAlive;

    if (!route(request, response, server)) {
      if (response.statusCode == -1) {
        response.statusCode = 404;
      }
    } else {
      if (response.statusCode == -1) {
        response.statusCode = 200;
      }
    }

    writeResponse(buffer, response);
    if (response.keepAlive) {
      server.timer.insert(clientSocketFd, server.DEFAULT_TIME_OUT);
      epoll.addEvent(clientSocketFd);
    } else {
      {
      std::unique_lock<std::mutex>(server.clientSocketsMutex);
      server.clientSockets.erase(clientSocketFd);
      }
      server.timer.removeSocket(clientSocketFd);
    }
  }
};

}

#endif