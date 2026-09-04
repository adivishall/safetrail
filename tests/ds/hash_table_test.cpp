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


  // ── tombstones under churn: the table must stay bounded ────────────────────
  //
  // The bug: a rehash triggered by tombstones always DOUBLED the table. Insert
  // and erase the same 100 keys forever and the table doubled forever, holding
  // 100 entries in a structure that had grown to make room for corpses. Memory
  // climbed, locality collapsed, and lookups got slower the longer it ran.
  //
  // The policy now asks what actually filled the table: the trigger counts
  // count + tombstones (it must -- probe length depends on occupied slots), but
  // the DECISION is made on count alone. Live entries justify a bigger table;
  // tombstones justify a same-size rebuild that sweeps them out.
  {
    ds::HashMap<uint32_t, uint32_t> churn;
    for (uint32_t i = 0; i < 100; ++i) churn.put(i, i);
    const size_t buckets_at_100 = churn.bucket_count();
    const size_t growths_at_100 = churn.growths();   // the initial fill legitimately grew
    const size_t rehashes_at_100 = churn.rehashes();

    // 50,000 insert/erase pairs on a live set that never exceeds ~100 entries.
    for (uint32_t round = 0; round < 500; ++round)
      for (uint32_t i = 0; i < 100; ++i) {
        const uint32_t key = 1000 + round * 100 + i;
        churn.put(key, key);
        churn.erase(key);
      }

    t::ok(churn.size() == 100, "the live set is still exactly 100 entries");
    t::ok(churn.bucket_count() == buckets_at_100,
          "and the table has NOT grown (" + std::to_string(buckets_at_100) + " -> " +
              std::to_string(churn.bucket_count()) + " buckets after 50,000 churned keys)");
    t::ok(churn.rehashes() > rehashes_at_100,
          "same-size rebuilds did happen (" +
              std::to_string(churn.rehashes() - rehashes_at_100) + " of them)");
    t::ok(churn.growths() == growths_at_100,
          "and the churn caused NO growth at all (the initial fill's " +
              std::to_string(growths_at_100) + " were the only ones)");
    t::ok(churn.tombstones() < churn.bucket_count(),
          "tombstones are bounded by the table size, not unbounded");

    // Everything still resolves after all that churn.
    bool intact = true;
    for (uint32_t i = 0; i < 100; ++i) {
      const uint32_t* v = churn.get(i);
      if (!v || *v != i) intact = false;
    }
    t::ok(intact, "all 100 live keys still resolve correctly after the churn");

    // And a key that was erased stays erased.
    t::ok(churn.get(1000) == nullptr, "an erased key is not resurrected by a rehash");
  }

  // Growth still happens when it is the LIVE set that needs room. The fix must
  // not have traded unbounded growth for no growth at all.
  {
    ds::HashMap<uint32_t, uint32_t> grow;
    const size_t initial = grow.bucket_count();
    for (uint32_t i = 0; i < 4000; ++i) grow.put(i, i);
    t::ok(grow.bucket_count() > initial * 100,
          "4000 live entries do grow the table (" + std::to_string(initial) + " -> " +
              std::to_string(grow.bucket_count()) + ")");
    t::ok(grow.growths() > 0, "and they are recorded as growths");
    t::ok(grow.size() == 4000, "with every entry present");
  }

  // A rebuild must not break a probe chain that ran through a tombstone: erase
  // colliding keys, then look up the survivor. Keys i and i + bucket_count()
  // collide under any power-of-two masking of a multiplicative hash.
  {
    ds::HashMap<uint32_t, uint32_t> probe;
    std::vector<uint32_t> keys;
    for (uint32_t i = 0; i < 400; ++i) { probe.put(i, i); keys.push_back(i); }
    // Erase every other key, forcing many tombstones inside live probe chains.
    for (size_t i = 0; i < keys.size(); i += 2) probe.erase(keys[i]);
    bool ok2 = true;
    for (size_t i = 1; i < keys.size(); i += 2) {
      const uint32_t* v = probe.get(keys[i]);
      if (!v || *v != keys[i]) ok2 = false;
    }
    t::ok(ok2, "survivors are still reachable through tombstoned probe chains");
    // Force a rebuild by inserting until the threshold trips, then re-check.
    for (uint32_t i = 10000; i < 10400; ++i) probe.put(i, i);
    ok2 = true;
    for (size_t i = 1; i < keys.size(); i += 2) {
      const uint32_t* v = probe.get(keys[i]);
      if (!v || *v != keys[i]) ok2 = false;
    }
    t::ok(ok2, "and still reachable after the rebuild that swept the tombstones");
  }

  return t::report("ds/hash_table");
}
