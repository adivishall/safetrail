// Every index must return exactly what brute force returns. Without this a
// benchmark can compare a correct slow thing against a fast wrong thing.
#include "../test_harness.hpp"
#include <string>
#include <vector>
#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/quadtree.hpp"
#include "safetrail/index/rtree.hpp"
#include "safetrail/index/geohash.hpp"
#include "safetrail/sim/mobility.hpp"
#include <algorithm>

using namespace safetrail;

int main() {
  sim::Rng rng(31337);
  for (size_t n : {1u, 2u, 9u, 64u, 777u, 4000u}) {
    std::vector<std::pair<index::ZoneId, geo::Bbox>> boxes;
    for (size_t i = 0; i < n; ++i) {
      const double lat = rng.range(25.4, 25.8), lon = rng.range(91.7, 92.1);
      const double r = rng.range(0.0002, 0.003);
      boxes.emplace_back(index::ZoneId(i), geo::Bbox{lat - r, lon - r, lat + r, lon + r});
    }
    index::BruteForceIndex bf; bf.build(boxes);
    index::Quadtree qt;        qt.build(boxes);
    index::RTree rt;           rt.build(boxes);
    index::Geohash gh;         gh.build(boxes);
    t::ok(bf.size() == qt.size(), "quadtree size agrees for n=" + std::to_string(n));
    t::ok(bf.size() == rt.size(), "r-tree size agrees for n=" + std::to_string(n));
    t::ok(bf.size() == gh.size(), "geohash size agrees for n=" + std::to_string(n));

    size_t badq = 0, badr = 0, badg = 0;
    for (int q = 0; q < 400; ++q) {
      const geo::LatLon p{rng.range(25.35, 25.85), rng.range(91.65, 92.15)};
      const geo::Bbox box = geo::Bbox::around(p, rng.range(50, 3000));
      std::vector<index::ZoneId> a, b, d, e;
      bf.query(box, a); qt.query(box, b); rt.query(box, d); gh.query(box, e);
      std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
      std::sort(d.begin(), d.end()); std::sort(e.begin(), e.end());
      if (a != b) ++badq;
      if (a != d) ++badr;
      if (a != e) ++badg;
    }
    t::ok(badq == 0, "quadtree == brute force, n=" + std::to_string(n));
    t::ok(badr == 0, "r-tree == brute force, n=" + std::to_string(n));
    t::ok(badg == 0, "geohash == brute force, n=" + std::to_string(n));
  }

  // Removal must keep them consistent too.
  std::vector<std::pair<index::ZoneId, geo::Bbox>> boxes;
  for (size_t i = 0; i < 200; ++i) {
    const double lat = rng.range(25.4, 25.8), lon = rng.range(91.7, 92.1);
    boxes.emplace_back(index::ZoneId(i), geo::Bbox{lat, lon, lat + 0.001, lon + 0.001});
  }
  index::BruteForceIndex bf; bf.build(boxes);
  index::Quadtree qt;        qt.build(boxes);
  for (index::ZoneId id = 0; id < 60; id += 3) { bf.remove(id); qt.remove(id); }
  t::ok(bf.size() == qt.size(), "size agrees after removals");
  size_t bad = 0;
  for (int q = 0; q < 500; ++q) {
    const geo::LatLon p{rng.range(25.4, 25.8), rng.range(91.7, 92.1)};
    std::vector<index::ZoneId> a, b;
    const geo::Bbox box = geo::Bbox::around(p, 1500);
    bf.query(box, a); qt.query(box, b);
    std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
    if (a != b) ++bad;
  }
  t::ok(bad == 0, "quadtree == brute force after removals");

  return t::report("index/equivalence");
}
