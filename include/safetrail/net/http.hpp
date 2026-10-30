#pragma once
//
// A small HTTP/1.1 server on POSIX sockets. No framework, no dependency.
//
// Deliberately narrow, because every feature of a protocol parser is attack
// surface:
//   * one request per connection ("Connection: close") -- no keep-alive, no
//     pipelining, so a connection's state machine has three states;
//   * Content-Length bodies only -- chunked transfer encoding is refused with
//     411/501 rather than half-implemented;
//   * hard limits on header bytes, header count and body bytes, checked BEFORE
//     the bytes are buffered (ParseLimits);
//   * idle connections are closed after a timeout, and the number of open
//     connections is capped, so a slow client cannot pin the server.
//
// Single-threaded: the loop multiplexes every socket with poll(2) and calls
// the handler on the same thread, between ticks of the simulation clock. That
// is the concurrency model of the whole platform -- one writer, so every
// update is applied in one total order without a lock anywhere -- and it is
// what makes a demo replayable.
//
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace safetrail::net {

struct Request {
  std::string method, target, path, query, version;
  std::vector<std::pair<std::string, std::string>> headers;   // names lower-cased
  std::string body;
  std::string remote;

  const std::string* header(const std::string& lower_name) const;
  std::string query_param(const std::string& name) const;     // url-decoded, "" if absent
  std::string cookie(const std::string& name) const;
};

struct Response {
  int status = 200;
  std::string content_type = "application/json; charset=utf-8";
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
};

struct ParseLimits {
  size_t max_header_bytes = 16 * 1024;
  size_t max_headers = 64;
  size_t max_body_bytes = 4 * 1024 * 1024;
};

enum class Parse { Incomplete, Complete, Error };

// Parse one request from the front of `buf`. Complete -> *consumed is set.
// Error -> *status is the HTTP status to answer with (400, 411, 413, 431, 501).
Parse parse_request(const std::string& buf, const ParseLimits& lim, Request* out,
                    size_t* consumed, int* status);

std::string serialize(const Response& r);
std::string status_text(int status);
std::string url_decode(const std::string& s, bool plus_is_space);

class Server {
 public:
  using Handler = std::function<Response(const Request&)>;
  // Called whenever poll() wakes; returns how many ms until it wants to run
  // again (the loop never sleeps longer than that).
  using Tick = std::function<int()>;

  Server() = default;
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  bool listen(const std::string& host, int port, std::string* err);
  int port() const { return port_; }
  // Runs until *stop becomes true (set it from a signal handler).
  void run(const Handler& handler, const Tick& tick, const volatile int* stop);

  ParseLimits limits{};
  size_t max_connections = 256;
  int idle_timeout_ms = 15'000;

 private:
  int fd_ = -1;
  int port_ = 0;
};

}  // namespace safetrail::net
