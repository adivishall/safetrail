#pragma once
// Minimal JSON reader -- just enough for GeoJSON zone files and scenario configs.
//
// Hand-written for the same reason everything else here is: adding a dependency
// for 120 lines of parsing would be the wrong trade in a project whose whole
// premise is that we implement our own machinery.
#include <cstdint>
#include <string>
#include <vector>

namespace safetrail::util {

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool b = false;
  double num = 0.0;
  std::string str;
  std::vector<Json> arr;
  std::vector<std::pair<std::string, Json>> obj;

  static bool parse(const std::string& text, Json& out, std::string* err);
  static bool parse_file(const std::string& path, Json& out, std::string* err);

  const Json* find(const std::string& key) const;
  const Json& operator[](size_t i) const { return arr[i]; }
  size_t size() const { return type == Type::Array ? arr.size() : obj.size(); }

  double number_or(double d) const { return type == Type::Number ? num : d; }
  std::string string_or(const std::string& d) const { return type == Type::String ? str : d; }
  bool bool_or(bool d) const { return type == Type::Bool ? b : d; }
};

}  // namespace safetrail::util
