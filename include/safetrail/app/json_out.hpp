#pragma once
// A streaming JSON writer for API responses. Commas and nesting are tracked by
// the writer, so a handler cannot emit malformed JSON by forgetting a separator;
// strings go through util::Json::escape and numbers through util::Json::number,
// so control characters are escaped and NaN/inf come out as null.
#include <cstdint>
#include <string>
#include <vector>

#include "safetrail/util/json.hpp"

namespace safetrail::app {

class JsonOut {
 public:
  JsonOut& obj() { sep(); s_ += '{'; first_.push_back(true); return *this; }
  JsonOut& end_obj() { s_ += '}'; first_.pop_back(); return *this; }
  JsonOut& arr() { sep(); s_ += '['; first_.push_back(true); return *this; }
  JsonOut& end_arr() { s_ += ']'; first_.pop_back(); return *this; }

  JsonOut& key(const char* k) {
    sep();
    s_ += util::Json::escape(k);
    s_ += ':';
    after_key_ = true;
    return *this;
  }
  JsonOut& str(const std::string& v) { sep(); s_ += util::Json::escape(v); return *this; }
  JsonOut& num(double v) { sep(); s_ += util::Json::number(v); return *this; }
  JsonOut& integer(int64_t v) { sep(); s_ += std::to_string(v); return *this; }
  JsonOut& uint(uint64_t v) { sep(); s_ += std::to_string(v); return *this; }
  JsonOut& boolean(bool v) { sep(); s_ += v ? "true" : "false"; return *this; }
  JsonOut& null() { sep(); s_ += "null"; return *this; }
  // An id that may be "none" (UINT32_MAX) is written as null.
  JsonOut& id(uint32_t v) { return v == UINT32_MAX ? null() : uint(v); }
  JsonOut& raw(const std::string& json) { sep(); s_ += json; return *this; }

  // key + value shorthands
  JsonOut& kv(const char* k, const std::string& v) { return key(k).str(v); }
  JsonOut& kv(const char* k, const char* v) { return key(k).str(v); }
  JsonOut& kn(const char* k, double v) { return key(k).num(v); }
  JsonOut& ki(const char* k, int64_t v) { return key(k).integer(v); }
  JsonOut& ku(const char* k, uint64_t v) { return key(k).uint(v); }
  JsonOut& kb(const char* k, bool v) { return key(k).boolean(v); }
  JsonOut& kid(const char* k, uint32_t v) { return key(k).id(v); }

  const std::string& text() const { return s_; }
  std::string take() { return std::move(s_); }

 private:
  std::string s_;
  std::vector<bool> first_;
  bool after_key_ = false;

  void sep() {
    if (after_key_) { after_key_ = false; return; }
    if (!first_.empty()) {
      if (!first_.back()) s_ += ',';
      first_.back() = false;
    }
  }
};

}  // namespace safetrail::app
