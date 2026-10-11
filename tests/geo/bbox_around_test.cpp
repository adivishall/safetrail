// Bbox::around is the index's filter, so it must be CONSERVATIVE with respect to
// the metric the exact geometry uses (haversine, geo::distance_m): every point
// within the radius has to be inside the box, or the index silently drops a zone
// the exact test would have reported. It must also be TIGHT, or the filter stops
// pruning. Both are properties over the whole sphere, so they are sampled.
#include "../test_harness.hpp"

#include <cmath>
#include <string>

#include "safetrail/geo/bbox.hpp"
#include "safetrail/geo/point.hpp"
#include "safetrail/sim/mobility.hpp"

using namespace safetrail;

// The index does not wrap longitude (a documented limitation), so a box built
// near the antimeridian can extend past +/-180 while a point's longitude is
// normalised into (-180, 180]. Compare on the box's side of the seam.
static bool box_holds(const geo::Bbox& b, geo::LatLon p) {
  if (b.contains(p)) return true;
  p.lon += 360.0;
  if (b.contains(p)) return true;
  p.lon -= 720.0;
  return b.contains(p);
}

int main() {
  // ── Regression: the east-west undershoot ────────────────────────────────────
  // The old formula used 111,320 m per degree of longitude at the equator while
  // distance_m() uses a 6,371,008.8 m sphere (~111,195 m per degree), so the box
  // was 0.11% too narrow east-west. A point 999.5 m due east of the centre of a
  // 1000 m query fell outside it.
  {
    const geo::LatLon c{25.5, 91.9};
    const geo::Bbox b = geo::Bbox::around(c, 1000.0);
    t::ok(b.contains(geo::offset(c, 90.0, 999.5)), "a point 999.5 m due east is inside a 1000 m box");
    t::ok(b.contains(geo::offset(c, 270.0, 999.5)), "and 999.5 m due west");
    t::ok(b.contains(geo::offset(c, 0.0, 999.5)) && b.contains(geo::offset(c, 180.0, 999.5)),
          "and 999.5 m due north and south");
  }

  // ── Conservative: every sampled point within r is inside the box ───────────
  {
    sim::Rng rng(0xB0C5);
    size_t misses = 0, samples = 0;
    for (int i = 0; i < 20000 * t::stress(); ++i) {
      const geo::LatLon c{rng.range(-80.0, 80.0), rng.range(-179.0, 179.0)};
      const double r = std::exp(rng.range(0.0, std::log(100000.0)));   // 1 m .. 100 km
      const geo::Bbox b = geo::Bbox::around(c, r);
      for (int k = 0; k < 8; ++k) {
        // Half the samples sit in the outer 0.1% of the radius, where an
        // undershooting box fails; the rest are spread through the disc.
        const double d = (k & 1) ? r * rng.range(0.999, 1.0) : r * rng.range(0.0, 1.0);
        const geo::LatLon p = geo::offset(c, rng.range(0.0, 360.0), d);
        ++samples;
        if (geo::distance_m(c, p) <= r && !box_holds(b, p)) ++misses;
      }
    }
    t::ok(misses == 0, "every one of " + std::to_string(samples) +
                           " points within the radius is inside the box (" +
                           std::to_string(misses) + " misses)");
  }

  // ── Tight: the box does not grow beyond the cap it bounds ──────────────────
  // Latitude extent must equal the angular radius; longitude half-width must be
  // within a hair of the exact asin(sin(r/R)/cos(lat)).
  {
    sim::Rng rng(0x7167);
    size_t loose = 0;
    for (int i = 0; i < 5000; ++i) {
      const geo::LatLon c{rng.range(-75.0, 75.0), rng.range(-170.0, 170.0)};
      const double r = rng.range(10.0, 50000.0);
      const geo::Bbox b = geo::Bbox::around(c, r);
      // The farthest point due north is exactly on the max_lat edge.
      const double north = geo::offset(c, 0.0, r).lat;
      if (std::fabs(b.max_lat - north) > 1e-7) ++loose;
      // Sweep bearings for the easternmost reachable longitude; the box edge may
      // exceed it only by rounding.
      double east = -1e9;
      for (int k = 0; k < 3600; ++k) east = std::fmax(east, geo::offset(c, k * 0.1, r).lon);
      if (b.max_lon - east > 1e-6 * (b.max_lon - c.lon) + 1e-9) ++loose;
    }
    t::ok(loose == 0, "the box is tight: no edge sits measurably beyond the cap (" +
                          std::to_string(loose) + " loose)");
  }

  // ── Poles: a cap that contains a pole spans every longitude ────────────────
  {
    const geo::Bbox b = geo::Bbox::around({89.99, 10.0}, 5000.0);
    t::ok(b.min_lon == -180.0 && b.max_lon == 180.0 && b.max_lat == 90.0,
          "a 5 km cap around 89.99N covers the pole and every longitude");
    const geo::Bbox s = geo::Bbox::around({-89.995, -45.0}, 2000.0);
    t::ok(s.min_lon == -180.0 && s.min_lat == -90.0, "and likewise at the south pole");
    t::ok(box_holds(b, geo::offset({89.99, 10.0}, 0.0, 4999.0)),
          "a point over the pole within the radius is inside");
  }

  // ── Degenerate radius ──────────────────────────────────────────────────────
  {
    const geo::LatLon c{-33.9, 151.2};                        // southern/eastern hemisphere
    const geo::Bbox z = geo::Bbox::around(c, 0.0);
    t::ok(z.contains(c) && z.min_lat <= z.max_lat, "radius 0 is the point itself");
    const geo::Bbox n = geo::Bbox::around(c, -5.0);
    t::ok(n.contains(c), "a negative radius is treated as 0, not as an inverted box");
  }

  return t::report("geo/bbox_around");
}
