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


  // ── concave containment: vertices alone are not enough ─────────────────────
  //
  // The old test was "every vertex of the inner ring is inside the outer one".
  // That is not containment. Take a C-shaped district and a block drawn as a bar
  // across the mouth of the C: both ends of the bar sit inside the two arms while
  // its middle lies in the gap, outside the district entirely. Every vertex
  // passes; the region is not contained, and treating it as contained puts the
  // bar in the wrong branch of the tree, so every alert raised in it is routed to
  // the wrong authority.
  //
  // Containment now requires BOTH: every inner vertex inside, AND no inner edge
  // crossing the outer boundary.
  {
    // C opening to the east, spanning lat [-2, 2], lon [-2, 2] with a notch.
    geo::Ring c_shape = {{-2, -2}, {-2, 2}, {-1, 2}, {-1, -1},
                         {1, -1}, {1, 2}, {2, 2}, {2, -2}};
    // A bar lying across the mouth: its ends are in the arms, its middle in the gap.
    geo::Ring bar = {{-1.6, 1.2}, {1.6, 1.2}, {1.6, 1.6}, {-1.6, 1.6}};

    Hierarchy h;
    const JurisdictionId district = h.add("C district", geo::Polygon(c_shape));
    const JurisdictionId block = h.add("bar block", geo::Polygon(bar));
    h.build();

    t::ok(h.parent(block) != district,
          "a bar across the mouth of a C is NOT nested inside it");
    t::ok(h.parent(block) == kNoJurisdiction, "so it is a root in its own right");

    // A point in the middle of the bar is outside the district entirely, and must
    // resolve to the bar, not to the district.
    const JurisdictionId owner = h.resolve({0.0, 1.4});
    t::ok(owner == block, "a point in the gap resolves to the bar, not the district");

    // Sanity: a genuinely nested block IS nested, so the stricter rule has not
    // simply started rejecting everything.
    Hierarchy h2;
    const JurisdictionId d2 = h2.add("C district", geo::Polygon(c_shape));
    geo::Ring inner = {{-1.8, -1.8}, {-1.2, -1.8}, {-1.2, -1.2}, {-1.8, -1.2}};
    const JurisdictionId b2 = h2.add("real block", geo::Polygon(inner));
    h2.build();
    t::ok(h2.parent(b2) == d2, "a block genuinely inside the C IS nested");
    t::ok(h2.resolve({-1.5, -1.5}) == b2, "and owns points inside itself");
    t::ok(h2.resolve({-1.9, -1.9}) == d2, "while the district owns the rest");
  }

  // ── shared administrative boundaries must still nest ───────────────────────
  //
  // Real boundary data shares edges constantly: a block whose northern limit IS
  // the district's northern limit is normal, correctly nested data. The crossing
  // test therefore has to be TRANSVERSAL crossing, not "do these segments touch" --
  // otherwise the stricter rule breaks the exact input it was written for.
  {
    geo::Ring district = {{0, 0}, {0, 10}, {10, 10}, {10, 0}};
    geo::Ring block = {{0, 0}, {0, 4}, {4, 4}, {4, 0}};   // shares two whole edges
    Hierarchy h;
    const JurisdictionId d = h.add("district", geo::Polygon(district));
    const JurisdictionId b = h.add("corner block", geo::Polygon(block));
    h.build();
    t::ok(h.parent(b) == d, "a block sharing edges with its district still nests");
    t::ok(h.resolve({2, 2}) == b, "and owns its own interior");
    t::ok(h.resolve({8, 8}) == d, "while the district owns the rest");
  }

  // ── a district's holes are real: an enclave is not owned by it ─────────────
  //
  // The hierarchy ranks regions by OUTER area, because nesting is a statement
  // about outlines -- a district contains a block whether or not either has exempt
  // enclaves punched out of it. Worth noting what that does NOT mean: it does not
  // make holes cosmetic. Containment itself goes through geo::contains(), which
  // respects holes, so a region sitting inside a district's exempt enclave is not
  // owned by that district, and a point in the enclave does not resolve to it.
  //
  // (Ranking by REGION area instead cannot actually invert the tree -- a
  // genuinely contained block is a subset of the container minus its holes, so
  // its area is necessarily smaller. Outer area is chosen for stability and for
  // matching what "nesting" means administratively, not to fix a bug.)
  {
    geo::Polygon district(geo::Ring{{0, 0}, {0, 10}, {10, 10}, {10, 0}});
    district.add_hole(geo::Ring{{3, 3}, {3, 7}, {7, 7}, {7, 3}});     // exempt enclave
    t::ok(district.validate() == geo::Polygon::Validity::Ok, "the holey district is valid");

    geo::Polygon inside_solid(geo::Ring{{0.5, 0.5}, {0.5, 2.5}, {2.5, 2.5}, {2.5, 0.5}});
    geo::Polygon inside_hole(geo::Ring{{4, 4}, {4, 6}, {6, 6}, {6, 4}});

    Hierarchy h;
    const JurisdictionId d = h.add("holey district", std::move(district));
    const JurisdictionId solid = h.add("block in the solid part", std::move(inside_solid));
    const JurisdictionId enclave = h.add("region inside the enclave", std::move(inside_hole));
    h.build();

    t::ok(h.parent(solid) == d, "a block in the district's solid part nests inside it");
    t::ok(h.parent(enclave) != d, "a region inside the exempt enclave does NOT");
    t::ok(h.resolve({1.5, 1.5}) == solid, "a point in the block resolves to the block");
    t::ok(h.resolve({5.0, 5.0}) == enclave,
          "a point in the enclave resolves to the enclave, not the district");
    t::ok(h.resolve({8.5, 8.5}) == d, "and the district still owns its own solid ground");
  }

  return t::report("jurisdiction/hierarchy");
}
