#include "safetrail/net/http.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <exception>

namespace safetrail::net {

namespace {

std::string lower(std::string s) {
  for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return s;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
  return s.substr(a, b - a);
}

bool token_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         std::strchr("!#$%&'*+-.^_`|~", c) != nullptr;
}

int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

const std::string* Request::header(const std::string& name) const {
  for (const auto& h : headers) if (h.first == name) return &h.second;
  return nullptr;
}

std::string Request::query_param(const std::string& name) const {
  size_t i = 0;
  while (i <= query.size()) {
    size_t amp = query.find('&', i);
    if (amp == std::string::npos) amp = query.size();
    const std::string part = query.substr(i, amp - i);
    const size_t eq = part.find('=');
    const std::string k = url_decode(part.substr(0, eq), true);
    if (k == name) return eq == std::string::npos ? std::string() : url_decode(part.substr(eq + 1), true);
    i = amp + 1;
  }
  return {};
}

std::string Request::cookie(const std::string& name) const {
  const std::string* c = header("cookie");
  if (!c) return {};
  size_t i = 0;
  while (i < c->size()) {
    size_t semi = c->find(';', i);
    if (semi == std::string::npos) semi = c->size();
    const std::string part = trim(c->substr(i, semi - i));
    const size_t eq = part.find('=');
    if (eq != std::string::npos && part.substr(0, eq) == name) return part.substr(eq + 1);
    i = semi + 1;
  }
  return {};
}

std::string url_decode(const std::string& s, bool plus_is_space) {
  std::string out;
  out.reserve(s.size());
  auto hexv = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      const int hi = hexv(s[i + 1]), lo = hexv(s[i + 2]);
      if (hi >= 0 && lo >= 0) { out.push_back(char(hi * 16 + lo)); i += 2; continue; }
    }
    out.push_back(plus_is_space && s[i] == '+' ? ' ' : s[i]);
  }
  return out;
}

Parse parse_request(const std::string& buf, const ParseLimits& lim, Request* out,
                    size_t* consumed, int* status) {
  const size_t end = buf.find("\r\n\r\n");
  if (end == std::string::npos) {
    if (buf.size() > lim.max_header_bytes) { *status = 431; return Parse::Error; }
    return Parse::Incomplete;
  }
  if (end + 4 > lim.max_header_bytes) { *status = 431; return Parse::Error; }

  Request r;
  size_t line_end = buf.find("\r\n");
  const std::string first = buf.substr(0, line_end);
  // METHOD SP TARGET SP VERSION, each part non-empty.
  const size_t s1 = first.find(' ');
  const size_t s2 = s1 == std::string::npos ? std::string::npos : first.find(' ', s1 + 1);
  if (s1 == std::string::npos || s2 == std::string::npos || first.find(' ', s2 + 1) != std::string::npos) {
    *status = 400; return Parse::Error;
  }
  r.method = first.substr(0, s1);
  r.target = first.substr(s1 + 1, s2 - s1 - 1);
  r.version = first.substr(s2 + 1);
  if (r.method.empty() || r.target.empty() || r.target[0] != '/') { *status = 400; return Parse::Error; }
  for (char c : r.method) if (!(c >= 'A' && c <= 'Z')) { *status = 400; return Parse::Error; }
  if (r.version != "HTTP/1.1" && r.version != "HTTP/1.0") { *status = 400; return Parse::Error; }
  for (char c : r.target)
    if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f) { *status = 400; return Parse::Error; }
  const size_t q = r.target.find('?');
  r.path = url_decode(r.target.substr(0, q), false);
  r.query = q == std::string::npos ? std::string() : r.target.substr(q + 1);

  size_t pos = line_end + 2;
  while (pos < end) {
    const size_t le = buf.find("\r\n", pos);
    const std::string line = buf.substr(pos, le - pos);
    pos = le + 2;
    if (line.empty()) break;
    if (line[0] == ' ' || line[0] == '\t') { *status = 400; return Parse::Error; }   // obsolete folding
    const size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) { *status = 400; return Parse::Error; }
    const std::string name = line.substr(0, colon);
    for (char c : name) if (!token_char(c)) { *status = 400; return Parse::Error; }
    if (r.headers.size() >= lim.max_headers) { *status = 431; return Parse::Error; }
    r.headers.emplace_back(lower(name), trim(line.substr(colon + 1)));
  }

  size_t body_len = 0;
  if (const std::string* te = r.header("transfer-encoding")) {
    (void)te;
    *status = 501; return Parse::Error;                    // chunked: refused, not guessed at
  }
  int cl_count = 0;
  for (const auto& h : r.headers) if (h.first == "content-length") ++cl_count;
  if (cl_count > 1) { *status = 400; return Parse::Error; } // request smuggling shape
  if (const std::string* cl = r.header("content-length")) {
    if (cl->empty() || cl->size() > 12) { *status = 400; return Parse::Error; }
    for (char c : *cl) if (c < '0' || c > '9') { *status = 400; return Parse::Error; }
    body_len = size_t(std::stoull(*cl));
    if (body_len > lim.max_body_bytes) { *status = 413; return Parse::Error; }
  } else if (r.method == "POST" || r.method == "PUT" || r.method == "PATCH") {
    *status = 411; return Parse::Error;
  }
  if (buf.size() < end + 4 + body_len) return Parse::Incomplete;
  r.body = buf.substr(end + 4, body_len);
  *consumed = end + 4 + body_len;
  *out = std::move(r);
  return Parse::Complete;
}

std::string status_text(int s) {
  switch (s) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
  }
  return "Status";
}

std::string serialize(const Response& r) {
  std::string s = "HTTP/1.1 " + std::to_string(r.status) + " " + status_text(r.status) + "\r\n";
  s += "Content-Type: " + r.content_type + "\r\n";
  s += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
  s += "Connection: close\r\n";
  for (const auto& h : r.headers) s += h.first + ": " + h.second + "\r\n";
  s += "\r\n";
  s += r.body;
  return s;
}

// ─── Server ─────────────────────────────────────────────────────────────────

Server::~Server() {
  if (fd_ >= 0) ::close(fd_);
}

bool Server::listen(const std::string& host, int port, std::string* err) {
  std::signal(SIGPIPE, SIG_IGN);
  fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd_ < 0) { if (err) *err = std::strerror(errno); return false; }
  int one = 1;
  ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(uint16_t(port));
  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    if (err) *err = "bad host address " + host;
    return false;
  }
  if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(fd_, 64) != 0) {
    if (err) *err = std::string("cannot listen: ") + std::strerror(errno);
    return false;
  }
  socklen_t len = sizeof addr;
  ::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
  port_ = ntohs(addr.sin_port);
  ::fcntl(fd_, F_SETFL, ::fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
  return true;
}

void Server::run(const Handler& handler, const Tick& tick, const volatile int* stop) {
  struct Conn {
    int fd = -1;
    std::string in, out;
    size_t written = 0;
    bool responding = false;
    int64_t opened = 0, last = 0;
    std::string remote;
  };
  std::vector<Conn> conns;
  int wait_ms = 50;
  while (!*stop) {
    std::vector<pollfd> pfds;
    pfds.push_back({fd_, POLLIN, 0});
    for (const auto& c : conns)
      pfds.push_back({c.fd, short(c.responding ? POLLOUT : POLLIN), 0});
    const int rc = ::poll(pfds.data(), nfds_t(pfds.size()), std::max(1, std::min(wait_ms, 250)));
    if (rc < 0 && errno != EINTR) break;

    const int64_t t = now_ms();
    // New connections.
    if (rc > 0 && (pfds[0].revents & POLLIN)) {
      for (;;) {
        sockaddr_in a{};
        socklen_t al = sizeof a;
        const int cfd = ::accept(fd_, reinterpret_cast<sockaddr*>(&a), &al);
        if (cfd < 0) break;
        if (conns.size() >= max_connections) { ::close(cfd); continue; }
#ifdef SO_NOSIGPIPE
        int one = 1;
        ::setsockopt(cfd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        ::fcntl(cfd, F_SETFL, ::fcntl(cfd, F_GETFL, 0) | O_NONBLOCK);
        Conn c;
        c.fd = cfd;
        c.opened = c.last = t;
        char ip[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
        c.remote = ip;
        conns.push_back(std::move(c));
      }
    }

    for (size_t i = 0; i < conns.size(); ++i) {
      Conn& c = conns[i];
      const short rev = i + 1 < pfds.size() && pfds[i + 1].fd == c.fd ? pfds[i + 1].revents : short(0);
      bool close_it = false;
      if (!c.responding && (rev & (POLLIN | POLLHUP | POLLERR))) {
        char buf[65536];
        const ssize_t n = ::recv(c.fd, buf, sizeof buf, 0);
        if (n <= 0) {
          close_it = true;
        } else {
          c.in.append(buf, size_t(n));
          c.last = t;
          Request req;
          size_t used = 0;
          int st = 400;
          const Parse p = parse_request(c.in, limits, &req, &used, &st);
          if (p == Parse::Complete) {
            req.remote = c.remote;
            // One process holds every user's state in memory, so a request
            // that throws (out of memory, say) must cost that request, not
            // the server.
            try {
              c.out = serialize(handler(req));
            } catch (const std::exception&) {
              Response r;
              r.status = 500;
              r.body = "{\"error\":\"Internal Server Error\"}";
              c.out = serialize(r);
            }
            c.responding = true;
          } else if (p == Parse::Error) {
            Response r;
            r.status = st;
            r.body = "{\"error\":\"" + status_text(st) + "\"}";
            c.out = serialize(r);
            c.responding = true;
          }
        }
      }
      if (c.responding && !close_it) {
        const ssize_t n = ::send(c.fd, c.out.data() + c.written, c.out.size() - c.written, 0);
        if (n > 0) { c.written += size_t(n); c.last = t; }
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) close_it = true;
        if (c.written >= c.out.size()) close_it = true;
      }
      if (t - c.last > idle_timeout_ms) close_it = true;
      if (!c.responding && t - c.opened > request_timeout_ms) close_it = true;
      if (close_it) { ::close(c.fd); c.fd = -1; }
    }
    conns.erase(std::remove_if(conns.begin(), conns.end(), [](const Conn& c) { return c.fd < 0; }),
                conns.end());
    wait_ms = tick();
  }
  for (auto& c : conns) ::close(c.fd);
}

}  // namespace safetrail::net
