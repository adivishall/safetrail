#pragma once
// A small hand-written JSON reader/writer.
//
// Hand-written for the same reason everything else here is: adding a dependency
// for ~250 lines of parsing would be the wrong trade in a project whose whole
// premise is that we implement our own machinery.
//
// ── What it actually accepts, stated precisely ───────────────────────────────
//
// The full RFC 8259 grammar for values -- objects, arrays, strings with all
// escapes including \uXXXX surrogate pairs, the strict number grammar, true,
// false, null -- with these documented deviations, all of which are the parser
// being STRICTER than the spec or explicit about something the spec leaves open:
//
//   * Nesting is limited to kMaxDepth. Unbounded nesting means unbounded
//     recursion, and a 100 kB file of open brackets should be a parse error, not
//     a stack overflow. The spec explicitly permits an implementation limit.
//   * Trailing content after the top-level value is an error. It is the single
//     most common way a truncated or concatenated file gets silently half-read.
//   * Numbers are stored as double. Integers beyond 2^53 lose precision; nothing
//     in this project carries one.
//   * Duplicate keys are all retained, in document order; find() returns the
//     FIRST. The spec leaves this undefined, so it is pinned here and tested.
//   * Raw control characters (U+0000..U+001F) inside a string are rejected, as
//     the spec requires. Invalid escapes are rejected rather than passed through.
//
// It is a parser for JSON, not for a private subset -- and equally, it is not a
// streaming or a schema-validating one. escape()/write are the matching
// serialiser, so a value that round-trips through this file survives unchanged.
#include <cstdint>
#include <string>
#include <vector>

namespace safetrail::util {

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  // Recursion bound. 64 is far past anything GeoJSON produces (a MultiPolygon
  // bottoms out around 5) and shallow enough that the deepest legal document
  // cannot come near a default stack.
  static constexpr int kMaxDepth = 64;

  Type type = Type::Null;
  bool b = false;
  double num = 0.0;
  std::string str;
  std::vector<Json> arr;
  std::vector<std::pair<std::string, Json>> obj;

  static bool parse(const std::string& text, Json& out, std::string* err);
  static bool parse_file(const std::string& path, Json& out, std::string* err);

  // First value for `key`, or nullptr. See the duplicate-key note above.
  const Json* find(const std::string& key) const;
  const Json& operator[](size_t i) const { return arr[i]; }
  size_t size() const { return type == Type::Array ? arr.size() : obj.size(); }

  double number_or(double d) const { return type == Type::Number ? num : d; }
  std::string string_or(const std::string& d) const { return type == Type::String ? str : d; }
  bool bool_or(bool d) const { return type == Type::Bool ? b : d; }

  // ── Writing ───────────────────────────────────────────────────────────────
  //
  // Quote and escape `s` as a JSON string, INCLUDING the surrounding quotes.
  // Handles the two-character escapes, and emits \u00XX for control characters.
  // A zone named   Nohkalikai "Falls" \ Trail<newline>   must survive a save and
  // reload; before this existed, save_geojson() concatenated the raw name into
  // the output and produced a file its own loader rejected.
  static std::string escape(const std::string& s);

  // A JSON number with enough precision to round-trip a double exactly (17
  // significant digits), and never in a form JSON does not accept -- no "inf",
  // no "nan", no trailing ".". Non-finite input is written as null.
  static std::string number(double v);
};

}  // namespace safetrail::util
