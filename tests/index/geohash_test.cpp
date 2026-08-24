// Geohash index: equivalence with brute force, Morton encoding, serialisation.
#include <algorithm>
#include <vector>
#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/geohash.hpp"
#include "../test_harness.hpp"

using namespace safetrail;
using namespace safetrail::index;

namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  double range(double lo, double hi) { return lo + (hi - lo) * (double(next() >> 11) * (1.0 / 9007199254740992.0)); }
};
}  // namespace

int main() {
  Rng rng(0x6E0);

  // ── Morton encoding is monotone under component-wise ordering ───────────────
  // This is the property the range query relies on.
  {
    bool mono = true;
    for (int i = 0; i < 5000; ++i) {
      geo::LatLon a{rng.range(25.0, 26.0), rng.range(91.0, 92.0)};
      geo::LatLon b{a.lat + rng.range(0, 0.5), a.lon + rng.range(0, 0.5)};  // b >= a both axes
      if (Geohash::morton(a) > Geohash::morton(b)) mono = false;
    }
    t::ok(mono, "morton(a) <= morton(b) when a <= b component-wise");
  }

  // ── Equivalence with brute force on random box sets ─────────────────────────
  for (size_t n : {1u, 2u, 9u, 64u, 777u, 4000u}) {
    std::vector<std::pair<ZoneId, geo::Bbox>> boxes;
    for (size_t i = 0; i < n; ++i) {
      const double lat = rng.range(25.4, 25.8), lon = rng.range(91.7, 92.1);
      const double r = rng.range(0.0002, 0.003);
      boxes.emplace_back(ZoneId(i), geo::Bbox{lat - r, lon - r, lat + r, lon + r});
    }
    BruteForceIndex bf; bf.build(boxes);
    Geohash gh;         gh.build(boxes);
    t::ok(bf.size() == gh.size(), "geohash size agrees for n=" + std::to_string(n));

    size_t bad = 0;
    for (int q = 0; q < 400; ++q) {
      const geo::LatLon p{rng.range(25.35, 25.85), rng.range(91.65, 92.15)};
      const geo::Bbox box = geo::Bbox::around(p, rng.range(50, 3000));
      std::vector<ZoneId> a, b;
      bf.query(box, a); gh.query(box, b);
      std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
      if (a != b) ++bad;
    }
    t::ok(bad == 0, "geohash == brute force, n=" + std::to_string(n));
  }

  // ── Insert / remove keep equivalence ────────────────────────────────────────
  {
    std::vector<std::pair<ZoneId, geo::Bbox>> boxes;
    for (size_t i = 0; i < 300; ++i) {
      const double lat = rng.range(25.4, 25.8), lon = rng.range(91.7, 92.1);
      boxes.emplace_back(ZoneId(i), geo::Bbox{lat, lon, lat + 0.001, lon + 0.001});
    }
    BruteForceIndex bf; bf.build(boxes);
    Geohash gh;         gh.build(boxes);
    for (ZoneId id = 0; id < 90; id += 3) { bf.remove(id); gh.remove(id); }
    // one late insert on each
    bf.insert(9999, {25.5, 91.9, 25.503, 91.903});
    gh.insert(9999, {25.5, 91.9, 25.503, 91.903});
    t::ok(bf.size() == gh.size(), "size agrees after remove+insert");
    size_t bad = 0;
    for (int q = 0; q < 500; ++q) {
      const geo::LatLon p{rng.range(25.4, 25.8), rng.range(91.7, 92.1)};
      std::vector<ZoneId> a, b;
      const geo::Bbox box = geo::Bbox::around(p, 1500);
      bf.query(box, a); gh.query(box, b);
      std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
      if (a != b) ++bad;
    }
    t::ok(bad == 0, "geohash == brute force after mutations");
  }

  // ── Serialise -> deserialise round-trips to identical query results  [GAP 6] ─
  {
    std::vector<std::pair<ZoneId, geo::Bbox>> boxes;
    for (size_t i = 0; i < 500; ++i) {
      const double lat = rng.range(25.4, 25.8), lon = rng.range(91.7, 92.1);
      const double r = rng.range(0.0002, 0.003);
      boxes.emplace_back(ZoneId(i), geo::Bbox{lat - r, lon - r, lat + r, lon + r});
    }
    Geohash gh; gh.build(boxes);
    std::vector<uint8_t> blob;
    t::ok(gh.serialize(blob), "serialize succeeds");
    t::ok(blob.size() == gh.stats().bytes, "reported byte size matches the blob");

    Geohash loaded;
    t::ok(loaded.deserialize(blob), "deserialize succeeds");
    t::ok(loaded.size() == gh.size(), "size survives the round trip");

    size_t bad = 0;
    for (int q = 0; q < 500; ++q) {
      const geo::LatLon p{rng.range(25.35, 25.85), rng.range(91.65, 92.15)};
      const geo::Bbox box = geo::Bbox::around(p, rng.range(50, 3000));
      std::vector<ZoneId> a, b;
      gh.query(box, a); loaded.query(box, b);
      std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
      if (a != b) ++bad;
    }
    t::ok(bad == 0, "deserialized index queries identically to the original");

    // A corrupted blob is rejected, not silently loaded.
    std::vector<uint8_t> junk = {1, 2, 3, 4};
    Geohash bad_load;
    t::ok(!bad_load.deserialize(junk), "garbage blob is rejected");
  }

  return t::report("index/geohash");
}
