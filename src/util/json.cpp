#include "safetrail/util/json.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace safetrail::util {

namespace {
struct P {
  const char* s; const char* end; std::string err;
  void ws() { while (s < end && (*s==' '||*s=='\t'||*s=='\n'||*s=='\r')) ++s; }
  bool lit(const char* w) { size_t n = strlen(w);
    if (size_t(end - s) < n || strncmp(s, w, n)) return false; s += n; return true; }
  bool value(Json& j);
  bool string_(std::string& out);
};

bool P::string_(std::string& out) {
  if (s >= end || *s != '"') { err = "expected string"; return false; }
  ++s;
  while (s < end && *s != '"') {
    if (*s == '\\' && s + 1 < end) {
      ++s;
      switch (*s) {
        case 'n': out += '\n'; break; case 't': out += '\t'; break;
        case 'r': out += '\r'; break; case 'b': out += '\b'; break;
        case 'f': out += '\f'; break; case 'u': s += 4; out += '?'; break;
        default: out += *s;
      }
      ++s;
    } else out += *s++;
  }
  if (s >= end) { err = "unterminated string"; return false; }
  ++s; return true;
}

bool P::value(Json& j) {
  ws();
  if (s >= end) { err = "unexpected end"; return false; }
  if (*s == '{') {
    ++s; j.type = Json::Type::Object; ws();
    if (s < end && *s == '}') { ++s; return true; }
    for (;;) {
      ws(); std::string k;
      if (!string_(k)) return false;
      ws(); if (s >= end || *s != ':') { err = "expected :"; return false; }
      ++s; Json v; if (!value(v)) return false;
      j.obj.emplace_back(std::move(k), std::move(v));
      ws(); if (s < end && *s == ',') { ++s; continue; }
      if (s < end && *s == '}') { ++s; return true; }
      err = "expected , or }"; return false;
    }
  }
  if (*s == '[') {
    ++s; j.type = Json::Type::Array; ws();
    if (s < end && *s == ']') { ++s; return true; }
    for (;;) {
      Json v; if (!value(v)) return false;
      j.arr.push_back(std::move(v));
      ws(); if (s < end && *s == ',') { ++s; continue; }
      if (s < end && *s == ']') { ++s; return true; }
      err = "expected , or ]"; return false;
    }
  }
  if (*s == '"') { j.type = Json::Type::String; return string_(j.str); }
  if (lit("true"))  { j.type = Json::Type::Bool; j.b = true;  return true; }
  if (lit("false")) { j.type = Json::Type::Bool; j.b = false; return true; }
  if (lit("null"))  { j.type = Json::Type::Null; return true; }
  { char* e = nullptr; double d = strtod(s, &e);
    if (e == s) { err = "bad number"; return false; }
    j.type = Json::Type::Number; j.num = d; s = e; return true; }
}
}  // namespace

bool Json::parse(const std::string& text, Json& out, std::string* err) {
  P p{text.data(), text.data() + text.size(), ""};
  if (!p.value(out)) { if (err) *err = p.err; return false; }
  return true;
}

bool Json::parse_file(const std::string& path, Json& out, std::string* err) {
  std::ifstream f(path);
  if (!f) { if (err) *err = "cannot open " + path; return false; }
  std::stringstream ss; ss << f.rdbuf();
  return parse(ss.str(), out, err);
}

const Json* Json::find(const std::string& key) const {
  for (const auto& kv : obj) if (kv.first == key) return &kv.second;
  return nullptr;
}

}  // namespace safetrail::util
