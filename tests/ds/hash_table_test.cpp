// Open-addressed hash table, against a std::vector reference map.
#include <algorithm>
#include <string>
#include <vector>
#include "safetrail/ds/hash_table.hpp"
#include "../test_harness.hpp"

using namespace safetrail;

namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  uint32_t below(uint32_t n) { return uint32_t(next() % n); }
};
}  // namespace

int main() {
  // ── Randomised put/get/erase vs a linear reference ─────────────────────────
  Rng rng(0xABCDEF);
  ds::HashMap<uint32_t, uint32_t> h;
  std::vector<std::pair<uint32_t, uint32_t>> ref;   // linear-scan oracle

  auto ref_find = [&](uint32_t k) -> int64_t {
    for (size_t i = 0; i < ref.size(); ++i) if (ref[i].first == k) return int64_t(i);
    return -1;
  };

  for (int op = 0; op < 20000; ++op) {
    const uint32_t k = rng.below(500);              // small key space -> many collisions/overwrites
    const int action = int(rng.below(3));
    if (action == 0) {                              // put
      const uint32_t v = rng.next() & 0xFFFF;
      h.put(k, v);
      const int64_t i = ref_find(k);
      if (i < 0) ref.push_back({k, v}); else ref[size_t(i)].second = v;
    } else if (action == 1) {                       // erase
      const bool had = h.erase(k);
      const int64_t i = ref_find(k);
      t::ok(had == (i >= 0), "erase return value matches presence");
      if (i >= 0) ref.erase(ref.begin() + i);
    } else {                                        // get
      const uint32_t* got = h.get(k);
      const int64_t i = ref_find(k);
      if (i < 0) { if (got != nullptr) t::ok(false, "get should miss"); }
      else if (got == nullptr || *got != ref[size_t(i)].second)
        t::ok(false, "get value mismatch");
    }
    if ((op & 0x7FF) == 0) t::ok(h.size() == ref.size(), "size tracks reference");
  }
  t::ok(h.size() == ref.size(), "final size matches reference");

  // Full sweep: every reference key is found with the right value, and keys never
  // inserted are absent.
  bool all_match = true;
  for (const auto& kv : ref) {
    const uint32_t* v = h.get(kv.first);
    if (!v || *v != kv.second) all_match = false;
  }
  t::ok(all_match, "every live key resolves to its value");

  // ── for_each visits exactly the live set ───────────────────────────────────
  {
    size_t seen = 0;
    bool ok = true;
    h.for_each([&](uint32_t k, uint32_t v) {
      ++seen;
      const int64_t i = ref_find(k);
      if (i < 0 || ref[size_t(i)].second != v) ok = false;
    });
    t::ok(seen == ref.size() && ok, "for_each visits the live set exactly once");
  }

  // ── String keys work (FNV-1a path) ─────────────────────────────────────────
  {
    ds::HashMap<std::string, int> s;
    s.put("shillong", 1); s.put("cherrapunji", 2); s.put("shillong", 3);
    t::ok(s.size() == 2, "string overwrite does not grow size");
    t::ok(*s.get("shillong") == 3, "string overwrite updates value");
    t::ok(s.get("mawsynram") == nullptr, "absent string key misses");
  }

  // ── Growth: many distinct keys survive rehashing ───────────────────────────
  {
    ds::HashMap<uint32_t, uint32_t> big;
    for (uint32_t i = 0; i < 5000; ++i) big.put(i * 7 + 1, i);
    bool ok = big.size() == 5000;
    for (uint32_t i = 0; i < 5000 && ok; ++i) {
      const uint32_t* v = big.get(i * 7 + 1);
      if (!v || *v != i) ok = false;
    }
    t::ok(ok, "5000 keys survive repeated growth/rehash");
  }

  return t::report("ds/hash_table");
}
