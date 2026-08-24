// Jurisdiction nesting hierarchy.  [GAP 11]
//
// resolve() walks the containment tree; the oracle is "smallest-area polygon
// containing the point" by linear scan. They must agree -- because regions nest
// by area, the deepest tree node is exactly the smallest containing region.
#include <cmath>
#include <vector>
#include "safetrail/jurisdiction/hierarchy.hpp"
#include "../test_harness.hpp"

using namespace safetrail;
using namespace safetrail::jurisdiction;

namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  double range(double lo, double hi) { return lo + (hi - lo) * (double(next() >> 11) * (1.0 / 9007199254740992.0)); }
};

// Axis-aligned square as a polygon, centred at (cy,cx) with half-size h.
geo::Polygon square(double cy, double cx, double h) {
  return geo::Polygon(geo::Ring{{cy - h, cx - h}, {cy - h, cx + h},
                                {cy + h, cx + h}, {cy + h, cx - h}});
}
}  // namespace

int main() {
  // ── Hand-built three-level nest ─────────────────────────────────────────────
  {
    Hierarchy h;
    auto state    = h.add("state",    square(25.5, 91.9, 1.0));    // biggest
    auto district = h.add("district", square(25.5, 91.9, 0.5));
    auto block    = h.add("block",    square(25.5, 91.9, 0.1));    // smallest
    h.build();

    t::ok(h.parent(block) == district, "block's parent is district");
    t::ok(h.parent(district) == state, "district's parent is state");
    t::ok(h.parent(state) == kNoJurisdiction, "state is a root");
    t::ok(h.depth(block) == 2, "block is at depth 2");
    t::ok(h.roots().size() == 1, "one root");

    // A point deep inside resolves to the block.
    t::ok(h.resolve({25.5, 91.9}) == block, "deep point -> block");
    // A point between district and block edges resolves to the district.
    t::ok(h.resolve({25.5, 91.9 + 0.3}) == district, "mid point -> district");
    // A point inside the state but outside the district -> state.
    t::ok(h.resolve({25.5, 91.9 + 0.7}) == state, "outer point -> state");
    // A point outside everything -> none.
    t::ok(h.resolve({0.0, 0.0}) == kNoJurisdiction, "far point -> no jurisdiction");
  }

  // ── Two disjoint districts under one state, each with a block ───────────────
  {
    Hierarchy h;
    auto state = h.add("state", square(25.5, 91.9, 2.0));
    auto d_w   = h.add("west",  square(25.5, 91.0, 0.5));
    auto d_e   = h.add("east",  square(25.5, 92.8, 0.5));
    auto b_w   = h.add("west-block", square(25.5, 91.0, 0.1));
    auto b_e   = h.add("east-block", square(25.5, 92.8, 0.1));
    h.build();
    t::ok(h.parent(d_w) == state && h.parent(d_e) == state, "both districts under state");
    t::ok(h.parent(b_w) == d_w && h.parent(b_e) == d_e, "blocks under their own districts");
    t::ok(h.children(state).size() == 2, "state has two district children");
    t::ok(h.resolve({25.5, 91.0}) == b_w, "west point -> west block");
    t::ok(h.resolve({25.5, 92.8}) == b_e, "east point -> east block");
  }

  // ── resolve() == smallest-containing-polygon oracle, random nests ──────────
  {
    Rng rng(0x101);
    int agree = 0, trials = 200;
    for (int trial = 0; trial < trials; ++trial) {
      Hierarchy h;
      std::vector<geo::Polygon> polys;
      const double cy = 25.5, cx = 91.9;
      // A chain of concentric squares with random shrinking half-sizes.
      double hsize = 1.0;
      const int levels = 2 + int(rng.next() % 5);
      for (int i = 0; i < levels; ++i) {
        polys.push_back(square(cy, cx, hsize));
        h.add("lvl" + std::to_string(i), square(cy, cx, hsize));
        hsize *= rng.range(0.4, 0.8);
      }
      h.build();

      for (int q = 0; q < 5; ++q) {
        geo::LatLon p{cy + rng.range(-1.2, 1.2), cx + rng.range(-1.2, 1.2)};
        // Oracle: smallest-area polygon containing p.
        JurisdictionId oracle = kNoJurisdiction;
        double best_area = 1e300;
        for (size_t i = 0; i < polys.size(); ++i)
          if (geo::contains(polys[i], p)) {
            const double a = std::fabs(polys[i].signed_area());
            if (a < best_area) { best_area = a; oracle = JurisdictionId(i); }
          }
        if (h.resolve(p) == oracle) ++agree;
      }
    }
    t::ok(agree == trials * 5, "resolve() == smallest-containing oracle (" +
                                std::to_string(agree) + "/" + std::to_string(trials * 5) + ")");
  }

  return t::report("jurisdiction/hierarchy");
}
