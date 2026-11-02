// The HTTP request parser: well-formed requests, the limits it enforces before
// buffering, and the malformed shapes it must refuse rather than guess at.
#include "safetrail/net/http.hpp"
#include "test_harness.hpp"

using namespace safetrail;

namespace {
net::Parse parse(const std::string& raw, net::Request* r, int* status, size_t* used = nullptr,
                 net::ParseLimits lim = {}) {
  size_t u = 0;
  const auto p = net::parse_request(raw, lim, r, &u, status);
  if (used) *used = u;
  return p;
}
}  // namespace

int main() {
  {
    net::Request r; int st = 0; size_t used = 0;
    const std::string raw = "POST /api/users/7/fix?a=1&b=x%20y HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n"
                            "Cookie: other=1; st_session=abc\r\n\r\nhello";
    t::ok(parse(raw, &r, &st, &used) == net::Parse::Complete, "well-formed POST parses");
    t::ok(r.method == "POST" && r.path == "/api/users/7/fix", "method and path");
    t::ok(r.body == "hello" && used == raw.size(), "body by Content-Length, all bytes consumed");
    t::ok(r.query_param("b") == "x y" && r.query_param("a") == "1" && r.query_param("z").empty(), "query decoding");
    t::ok(r.cookie("st_session") == "abc" && r.cookie("missing").empty(), "cookie lookup");
    t::ok(r.header("host") && *r.header("host") == "x", "header names are lower-cased");
  }
  {
    net::Request r; int st = 0;
    t::ok(parse("GET / HTTP/1.1\r\nHost: x\r\n", &r, &st) == net::Parse::Incomplete, "no blank line yet: incomplete");
    t::ok(parse("POST /a HTTP/1.1\r\nContent-Length: 10\r\n\r\nabc", &r, &st) == net::Parse::Incomplete, "short body: incomplete");
  }
  auto refuses = [](const std::string& raw, int want, const char* what) {
    net::Request r; int st = 0;
    net::ParseLimits lim;
    lim.max_header_bytes = 1024;
    lim.max_body_bytes = 64;
    lim.max_headers = 8;
    t::ok(parse(raw, &r, &st, nullptr, lim) == net::Parse::Error && st == want, what);
  };
  refuses("POST /a HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", 501, "chunked bodies are refused");
  refuses("POST /a HTTP/1.1\r\n\r\n", 411, "POST without Content-Length is refused");
  refuses("POST /a HTTP/1.1\r\nContent-Length: 999\r\n\r\n", 413, "body over the limit refused before it arrives");
  refuses("POST /a HTTP/1.1\r\nContent-Length: 3\r\nContent-Length: 4\r\n\r\nabc", 400, "two Content-Length headers (smuggling shape)");
  refuses("POST /a HTTP/1.1\r\nContent-Length: -1\r\n\r\n", 400, "negative length");
  refuses("GET /a HTTP/2.0\r\n\r\n", 400, "unknown version");
  refuses("get /a HTTP/1.1\r\n\r\n", 400, "lower-case method");
  refuses("GET a HTTP/1.1\r\n\r\n", 400, "target not starting with /");
  refuses("GET /a  HTTP/1.1\r\n\r\n", 400, "extra space in the request line");
  refuses("GET /a HTTP/1.1\r\nBad Header: x\r\n\r\n", 400, "space in a header name");
  refuses("GET /a HTTP/1.1\r\n folded: x\r\n\r\n", 400, "obsolete line folding");
  refuses("GET /" + std::string(2000, 'a') + " HTTP/1.1\r\n\r\n", 431, "oversized header block");
  refuses(std::string(2000, 'a'), 431, "endless header with no terminator stops at the limit");
  {
    std::string many = "GET /a HTTP/1.1\r\n";
    for (int i = 0; i < 9; ++i) many += "X-" + std::to_string(i) + ": v\r\n";
    refuses(many + "\r\n", 431, "too many headers");
  }
  {
    net::Response r;
    r.status = 404;
    r.body = "{}";
    const std::string s = net::serialize(r);
    t::ok(s.rfind("HTTP/1.1 404 Not Found\r\n", 0) == 0, "status line");
    t::ok(s.find("Content-Length: 2\r\n") != std::string::npos && s.find("Connection: close") != std::string::npos,
          "length and connection headers");
  }
  t::ok(net::url_decode("%2e%2E%2f", false) == "../", "percent decoding (the static handler then refuses '..')");
  t::ok(net::url_decode("%zz%", false) == "%zz%", "malformed escapes pass through unchanged");
  return t::report("net/http_parse");
}
