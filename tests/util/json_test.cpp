// The handwritten JSON parser, held to the grammar it claims to accept.
//
// Two halves: what it must accept (so GeoJSON keeps loading), and what it must
// REJECT. The rejection half is the point -- a parser that silently accepts a
// truncated file is worse than no parser, because the failure surfaces later as
// a zone that quietly does not exist.
#include "../test_harness.hpp"

#include <string>

#include "safetrail/util/json.hpp"

using namespace safetrail::util;

static bool accepts(const std::string& text) {
  Json j;
  std::string err;
  return Json::parse(text, j, &err);
}

int main() {
  // ── the grammar it must accept ─────────────────────────────────────────────
  {
    Json j;
    std::string err;
    t::ok(Json::parse(R"({"a":1,"b":[true,false,null],"c":"x"})", j, &err),
          "object with every value type parses");
    t::ok(j.type == Json::Type::Object, "top level is an object");
    t::ok(j.find("a") && j.find("a")->number_or(0) == 1.0, "number value");
    t::ok(j.find("b") && j.find("b")->arr.size() == 3, "array of three");
    t::ok(j.find("b")->arr[0].bool_or(false) == true, "true parses");
    t::ok(j.find("b")->arr[2].type == Json::Type::Null, "null parses");
    t::ok(j.find("c") && j.find("c")->string_or("") == "x", "string value");
    t::ok(j.find("zzz") == nullptr, "missing key returns nullptr");

    t::ok(accepts("[]") && accepts("{}"), "empty array and object");
    t::ok(accepts("  \t\n {\"a\" : \n 1 } \r\n "), "whitespace everywhere legal");
    t::ok(accepts("-0"), "negative zero");
    t::ok(accepts("1e10") && accepts("1E+10") && accepts("1.5e-3"), "exponent forms");
    t::ok(accepts("0.5"), "fraction");
  }

  // ── numbers: the strict grammar, not strtod's ──────────────────────────────
  {
    t::ok(!accepts("01"), "leading zero rejected");
    t::ok(!accepts("+1"), "leading plus rejected");
    t::ok(!accepts(".5"), "bare leading dot rejected");
    t::ok(!accepts("1."), "trailing dot rejected");
    t::ok(!accepts("1e"), "exponent with no digits rejected");
    t::ok(!accepts("0x1f"), "hex literal rejected");
    t::ok(!accepts("inf"), "inf rejected");
    t::ok(!accepts("nan"), "nan rejected");
    t::ok(!accepts("--1"), "double minus rejected");
  }

  // ── trailing garbage ───────────────────────────────────────────────────────
  {
    t::ok(!accepts("{} {}"), "two documents concatenated is an error");
    t::ok(!accepts("[1,2]]"), "stray closing bracket is an error");
    t::ok(!accepts("1 junk"), "trailing word is an error");
    t::ok(accepts("[1,2]   \n"), "trailing whitespace is fine");
    t::ok(!accepts("{\"a\":1"), "truncated object is an error");
    t::ok(!accepts("[1,"), "truncated array is an error");
    t::ok(!accepts("\"abc"), "unterminated string is an error");
  }

  // ── escapes ────────────────────────────────────────────────────────────────
  {
    Json j;
    std::string err;
    t::ok(Json::parse(R"("a\"b")", j, &err) && j.str == "a\"b", "escaped quote");
    t::ok(Json::parse(R"("a\\b")", j, &err) && j.str == "a\\b", "escaped backslash");
    t::ok(Json::parse(R"("a\nb")", j, &err) && j.str == "a\nb", "escaped newline");
    t::ok(Json::parse(R"("a\/b")", j, &err) && j.str == "a/b", "escaped solidus");

    // \u, properly decoded to UTF-8 rather than replaced with '?'.
    t::ok(Json::parse(R"("\u0041")", j, &err) && j.str == "A", "\\u ASCII");
    t::ok(Json::parse(R"("\u00e9")", j, &err) && j.str == "\xc3\xa9", "\\u Latin-1 -> 2-byte UTF-8");
    t::ok(Json::parse(R"("\u0915")", j, &err) && j.str == "\xe0\xa4\x95", "\\u Devanagari -> 3-byte UTF-8");
    // Surrogate pair: U+1F600.
    t::ok(Json::parse(R"("\ud83d\ude00")", j, &err) && j.str == "\xf0\x9f\x98\x80",
          "surrogate pair -> 4-byte UTF-8");

    t::ok(!accepts(R"("\q")"), "unknown escape rejected");
    t::ok(!accepts(R"("\u12")"), "truncated \\u rejected");
    t::ok(!accepts(R"("\u12zz")"), "non-hex \\u rejected");
    t::ok(!accepts(R"("\ud83d")"), "unpaired high surrogate rejected");
    t::ok(!accepts(R"("\udc00")"), "unpaired low surrogate rejected");
    t::ok(!accepts(std::string("\"a\x01" "b\"")), "raw control character rejected");
  }

  // ── documented behaviours ──────────────────────────────────────────────────
  {
    Json j;
    std::string err;
    t::ok(Json::parse(R"({"k":1,"k":2})", j, &err), "duplicate keys parse");
    t::ok(j.obj.size() == 2, "both duplicate pairs retained in document order");
    t::ok(j.find("k")->number_or(0) == 1.0, "find() returns the FIRST duplicate");

    // Nesting bound: legal at the limit, rejected past it, and no stack overflow.
    std::string deep_ok, deep_bad;
    for (int i = 0; i < Json::kMaxDepth; ++i) deep_ok += "[";
    for (int i = 0; i < Json::kMaxDepth; ++i) deep_ok += "]";
    for (int i = 0; i < Json::kMaxDepth + 5; ++i) deep_bad += "[";
    for (int i = 0; i < Json::kMaxDepth + 5; ++i) deep_bad += "]";
    t::ok(accepts(deep_ok), "nesting at the documented limit is accepted");
    t::ok(!accepts(deep_bad), "nesting past the limit is rejected, not a crash");
  }

  // ── the writer, and that the pair round-trips ──────────────────────────────
  {
    const std::string nasty = "Nohkalikai \"Falls\"\\Trail\nline2\ttab\x01ctrl \xe0\xa4\x95";
    const std::string doc = "{\"name\":" + Json::escape(nasty) + "}";
    Json j;
    std::string err;
    t::ok(Json::parse(doc, j, &err), "escaped nasty string re-parses: " + err);
    t::ok(j.find("name") && j.find("name")->string_or("") == nasty,
          "quote, backslash, newline, tab, control char and UTF-8 all survive");

    t::ok(Json::number(1.0) == "1", "number writes 1 as 1");
    t::ok(Json::number(0.0 / 1.0) == "0", "zero");
    const double huge = 1e308 * 10;                       // +inf
    t::ok(Json::number(huge) == "null", "non-finite writes as null, never 'inf'");
    // Round-trips a double exactly.
    Json rt;
    const double v = 25.5701234567891;
    t::ok(Json::parse(Json::number(v), rt, &err) && rt.num == v,
          "double survives write -> parse exactly");
  }

  return t::report("util/json");
}
