#include "safetrail/util/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace safetrail::util {

namespace {

struct P {
  const char* s;
  const char* end;
  std::string err;
  int depth = 0;

  void ws() { while (s < end && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) ++s; }
  bool lit(const char* w) {
    const size_t n = std::strlen(w);
    if (size_t(end - s) < n || std::strncmp(s, w, n)) return false;
    s += n;
    return true;
  }
  bool value(Json& j);
  bool string_(std::string& out);
  bool number_(double& out);
  bool fail(const char* m) { if (err.empty()) err = m; return false; }
};

// Append one Unicode code point as UTF-8.
void append_utf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += char(cp);
  } else if (cp < 0x800) {
    out += char(0xC0 | (cp >> 6));
    out += char(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += char(0xE0 | (cp >> 12));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  } else {
    out += char(0xF0 | (cp >> 18));
    out += char(0x80 | ((cp >> 12) & 0x3F));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  }
}

bool hex4(const char* p, const char* end, uint32_t* out) {
  if (end - p < 4) return false;
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) {
    const char c = p[i];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
    else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
    else return false;
  }
  *out = v;
  return true;
}

bool P::string_(std::string& out) {
  if (s >= end || *s != '"') return fail("expected string");
  ++s;
  for (;;) {
    if (s >= end) return fail("unterminated string");
    const unsigned char c = static_cast<unsigned char>(*s);
    if (c == '"') { ++s; return true; }
    if (c < 0x20) return fail("raw control character in string");
    if (c != '\\') { out += char(c); ++s; continue; }

    ++s;                                     // consume the backslash
    if (s >= end) return fail("truncated escape");
    switch (*s) {
      case '"':  out += '"';  ++s; break;
      case '\\': out += '\\'; ++s; break;
      case '/':  out += '/';  ++s; break;
      case 'b':  out += '\b'; ++s; break;
      case 'f':  out += '\f'; ++s; break;
      case 'n':  out += '\n'; ++s; break;
      case 'r':  out += '\r'; ++s; break;
      case 't':  out += '\t'; ++s; break;
      case 'u': {
        // \uXXXX, and the surrogate-pair form for anything above the BMP. The
        // old implementation skipped four bytes and appended '?', which silently
        // corrupted every non-ASCII zone name -- and skipped without checking,
        // so a truncated escape walked off the end of the buffer.
        ++s;
        uint32_t cp = 0;
        if (!hex4(s, end, &cp)) return fail("bad \\u escape");
        s += 4;
        if (cp >= 0xD800 && cp <= 0xDBFF) {          // high surrogate
          uint32_t lo = 0;
          if (end - s < 6 || s[0] != '\\' || s[1] != 'u' || !hex4(s + 2, end, &lo) ||
              lo < 0xDC00 || lo > 0xDFFF)
            return fail("unpaired high surrogate");
          s += 6;
          cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
          return fail("unpaired low surrogate");
        }
        append_utf8(out, cp);
        break;
      }
      default:
        return fail("invalid escape character");
    }
  }
}

// The JSON number grammar, scanned explicitly:
//   -? (0 | [1-9][0-9]*) ( . [0-9]+ )? ( [eE] [+-]? [0-9]+ )?
// strtod is far more permissive -- it happily accepts "0x1f", "inf", "nan", a
// leading "+", and a bare "." -- so validating first is what makes "is this
// JSON?" a question this parser actually answers. strtod then does the
// conversion over the span we have already proved well-formed.
bool P::number_(double& out) {
  const char* start = s;
  if (s < end && *s == '-') ++s;
  if (s >= end || *s < '0' || *s > '9') return fail("bad number");
  if (*s == '0') {
    ++s;                                      // a leading zero admits no digits
  } else {
    while (s < end && *s >= '0' && *s <= '9') ++s;
  }
  if (s < end && *s == '.') {
    ++s;
    if (s >= end || *s < '0' || *s > '9') return fail("bad number: no digits after '.'");
    while (s < end && *s >= '0' && *s <= '9') ++s;
  }
  if (s < end && (*s == 'e' || *s == 'E')) {
    ++s;
    if (s < end && (*s == '+' || *s == '-')) ++s;
    if (s >= end || *s < '0' || *s > '9') return fail("bad number: no digits in exponent");
    while (s < end && *s >= '0' && *s <= '9') ++s;
  }
  const std::string span(start, size_t(s - start));
  out = std::strtod(span.c_str(), nullptr);
  return true;
}

bool P::value(Json& j) {
  if (++depth > Json::kMaxDepth) { --depth; return fail("nesting too deep"); }
  struct DepthGuard { int* d; ~DepthGuard() { --*d; } } guard{&depth};

  ws();
  if (s >= end) return fail("unexpected end of input");

  if (*s == '{') {
    ++s;
    j.type = Json::Type::Object;
    ws();
    if (s < end && *s == '}') { ++s; return true; }
    for (;;) {
      ws();
      std::string k;
      if (!string_(k)) return false;
      ws();
      if (s >= end || *s != ':') return fail("expected ':' after object key");
      ++s;
      Json v;
      if (!value(v)) return false;
      j.obj.emplace_back(std::move(k), std::move(v));
      ws();
      if (s < end && *s == ',') { ++s; continue; }
      if (s < end && *s == '}') { ++s; return true; }
      return fail("expected ',' or '}'");
    }
  }

  if (*s == '[') {
    ++s;
    j.type = Json::Type::Array;
    ws();
    if (s < end && *s == ']') { ++s; return true; }
    for (;;) {
      Json v;
      if (!value(v)) return false;
      j.arr.push_back(std::move(v));
      ws();
      if (s < end && *s == ',') { ++s; continue; }
      if (s < end && *s == ']') { ++s; return true; }
      return fail("expected ',' or ']'");
    }
  }

  if (*s == '"') { j.type = Json::Type::String; return string_(j.str); }
  if (lit("true"))  { j.type = Json::Type::Bool; j.b = true;  return true; }
  if (lit("false")) { j.type = Json::Type::Bool; j.b = false; return true; }
  if (lit("null"))  { j.type = Json::Type::Null; return true; }

  j.type = Json::Type::Number;
  return number_(j.num);
}

}  // namespace

bool Json::parse(const std::string& text, Json& out, std::string* err) {
  out = Json{};
  P p{text.data(), text.data() + text.size(), "", 0};
  if (!p.value(out)) { if (err) *err = p.err; return false; }
  // Trailing content is an error. Without this a truncated file, two documents
  // concatenated, or a stray brace all parse "successfully" as whatever the
  // first value happened to be -- the failure mode where half a zone set loads
  // and nobody notices until an alert does not fire.
  p.ws();
  if (p.s != p.end) {
    if (err) *err = "trailing content after the top-level value";
    return false;
  }
  return true;
}

bool Json::parse_file(const std::string& path, Json& out, std::string* err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { if (err) *err = "cannot open " + path; return false; }
  std::stringstream ss;
  ss << f.rdbuf();
  return parse(ss.str(), out, err);
}

const Json* Json::find(const std::string& key) const {
  for (const auto& kv : obj) if (kv.first == key) return &kv.second;
  return nullptr;
}

std::string Json::escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {                       // any other control character
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", unsigned(c));
          out += buf;
        } else {
          // Bytes >= 0x80 are passed through unchanged: input is assumed UTF-8,
          // and re-encoding valid UTF-8 as \u escapes would be lossless but
          // needlessly unreadable in a file a human may open.
          out += char(c);
        }
    }
  }
  out += '"';
  return out;
}

std::string Json::number(double v) {
  if (!std::isfinite(v)) return "null";       // JSON has no inf/nan
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.17g", v);
  return buf;
}

}  // namespace safetrail::util
