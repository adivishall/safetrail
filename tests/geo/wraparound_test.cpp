// Longitude wraparound at the antimeridian.
//
// Nothing in the Shillong scenario ever crosses 180 degrees, so none of this is
// reachable from the demo. It is tested anyway because a geometry primitive that
// is wrong by 40,000 km under some input is a latent bug, and because "it never
// happens with our data" is the reasoning that ships it.
#include "../test_harness.hpp"

#include <cmath>

#include "safetrail/geo/point.hpp"
#include "safetrail/geo/projection.hpp"

using namespace safetrail::geo;

int main() {
  // ── distance across the antimeridian ───────────────────────────────────────
  //
  // 179.9 E to 179.9 W is 0.2 degrees apart, not 359.8. The haversine form needs
  // no explicit normalisation for this -- the delta enters only through
  // sin(dl/2)^2, which is periodic -- and that is the property being pinned here,
  // so a "simplification" that replaces it with a naive difference fails.
  {
    const LatLon a{0.0, 179.9}, b{0.0, -179.9};
    const double d = distance_m(a, b);
    const double expected = distance_m({0.0, 0.0}, {0.0, 0.2});
    t::near(d, expected, 1.0, "179.9E -> 179.9W is 0.2 degrees, not 359.8");
    t::ok(d < 25000.0, "the short way round, not the long way");

    // Symmetric.
    t::near(distance_m(b, a), d, 1e-9, "distance is symmetric across the seam");

    // And at a realistic latitude, where the longitude scale is compressed.
    const LatLon c{25.57, 179.95}, e{25.57, -179.95};
    t::near(distance_m(c, e), distance_m({25.57, 0.0}, {25.57, 0.1}), 1.0,
            "same, at Shillong's latitude");
  }

  // ── bearing across the antimeridian ────────────────────────────────────────
  {
    const double east = bearing_deg({0.0, 179.9}, {0.0, -179.9});
    t::near(east, 90.0, 0.5, "heading east across the seam reads as ~90 degrees");
    const double west = bearing_deg({0.0, -179.9}, {0.0, 179.9});
    t::near(west, 270.0, 0.5, "heading west across the seam reads as ~270 degrees");
  }

  // ── offset() must PRODUCE a normalised longitude ───────────────────────────
  //
  // This is the one function in point.cpp that emits a longitude rather than
  // consuming one, and it is where the real bug was: l1 is in (-180, 180] and
  // atan2 returns (-pi, pi], so their sum could land at 180.05 -- a value no other
  // function in the engine expects, and which a bbox test silently gets wrong.
  {
    const LatLon from{0.0, 179.95};
    const LatLon to = offset(from, 90.0, 20000.0);          // 20 km due east
    t::ok(to.lon >= -180.0 && to.lon <= 180.0,
          "offset() east across the seam stays in range");
    t::ok(to.lon < 0.0, "and lands in the western hemisphere");
    t::near(distance_m(from, to), 20000.0, 1.0, "and is 20 km away");

    const LatLon from2{0.0, -179.95};
    const LatLon to2 = offset(from2, 270.0, 20000.0);       // 20 km due west
    t::ok(to2.lon >= -180.0 && to2.lon <= 180.0,
          "offset() west across the seam stays in range");
    t::ok(to2.lon > 0.0, "and lands in the eastern hemisphere");

    // Round trip: go out and come back.
    const LatLon back = offset(to, 270.0, 20000.0);
    t::near(distance_m(back, from), 0.0, 1.0, "out and back returns to the start");
  }

  // ── the local plane across the seam ────────────────────────────────────────
  {
    const LocalPlane plane({0.0, 179.95});
    const Meters m = plane.to_meters({0.0, -179.95});
    t::ok(m.x > 0.0, "a point just past the seam is EAST of the anchor, not far west");
    t::ok(std::fabs(m.x) < 20000.0, "and is ~11 km east, not 40,000 km west");

    const LatLon back = plane.to_latlon(m);
    t::ok(back.lon >= -180.0 && back.lon <= 180.0, "to_latlon() normalises its output");
    t::near(distance_m(back, {0.0, -179.95}), 0.0, 0.1, "and round-trips");
  }

  // ── ordinary longitudes are untouched ──────────────────────────────────────
  //
  // The normalisation must not perturb the case the project actually runs.
  {
    const LatLon a{25.5700, 91.8800}, b{25.5710, 91.8820};
    const double d = distance_m(a, b);
    t::ok(d > 100.0 && d < 300.0, "a normal Shillong pair still measures normally");
    const LatLon o = offset(a, 45.0, 500.0);
    t::near(distance_m(a, o), 500.0, 0.01, "offset() is unchanged away from the seam");
    t::near(bearing_deg(a, o), 45.0, 0.01, "bearing round-trips away from the seam");
  }

  return t::report("geo/wraparound");
}
