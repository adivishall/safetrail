// The local tangent-plane projection, and the approximation error it costs.
//
// The point of this file is not that the projection "works" -- it is to put a
// NUMBER on how wrong the flat-earth assumption is at this project's scale, so
// the claim in geo/projection.hpp is measured rather than asserted, and so a
// future change that quietly widens the error fails here.
#include "../test_harness.hpp"

#include <cmath>
#include <cstdio>

#include "safetrail/geo/point.hpp"
#include "safetrail/geo/projection.hpp"
#include "safetrail/sim/mobility.hpp"

using namespace safetrail;
using namespace safetrail::geo;

int main() {
  const LatLon origin{25.5700, 91.8800};        // Shillong
  const LocalPlane plane(origin);

  // ── round trip ─────────────────────────────────────────────────────────────
  {
    const Meters o = plane.to_meters(origin);
    t::near(o.x, 0.0, 1e-9, "the origin projects to (0,0) east");
    t::near(o.y, 0.0, 1e-9, "the origin projects to (0,0) north");

    sim::Rng rng(99);
    double worst = 0.0;
    for (int i = 0; i < 2000; ++i) {
      const LatLon p{rng.range(25.45, 25.70), rng.range(91.75, 92.00)};
      const LatLon back = plane.to_latlon(plane.to_meters(p));
      worst = std::max(worst, distance_m(p, back));
    }
    t::ok(worst < 1e-6, "lat/lon -> metres -> lat/lon round-trips to under a micron");
  }

  // ── the error budget, quantified at three scales ──────────────────────────
  //
  // The residual is curvature error, and it grows with distance from the plane's
  // ORIGIN -- not with the separation being measured. Measured here at three
  // anchor radii, which is what makes that concrete and is the reason the API
  // takes an origin instead of exposing one district-wide plane.
  //
  // The observed law is err ~ 6.4e-8 * r^2 metres, r = anchor radius: quadrupling
  // the radius multiplies the error by sixteen. The bounds below are set a factor
  // of ~2 above the measured worst case, so ordinary noise does not flap the test
  // but a real regression in the model does.
  {
    sim::Rng rng(4242);
    auto measure = [&](double radius_m, const char* label, double abs_bound) {
      double worst_abs = 0.0, worst_at = 0.0;
      for (int i = 0; i < 4000; ++i) {
        // Anchor, then two points within `radius_m` of it.
        const LatLon anchor{rng.range(25.50, 25.64), rng.range(91.80, 91.96)};
        const LocalPlane lp(anchor);
        const LatLon a = offset(anchor, rng.range(0, 360), rng.range(0, radius_m));
        const LatLon b = offset(anchor, rng.range(0, 360), rng.range(0, radius_m));
        const double truth = distance_m(a, b);
        const double planar = lp.distance_m(lp.to_meters(a), lp.to_meters(b));
        const double err = std::fabs(planar - truth);
        if (err > worst_abs) { worst_abs = err; worst_at = truth; }
      }
      // Relative to the ANCHOR RADIUS, not to the separation: two points a metre
      // apart but 200 m from the anchor carry the 200 m error, so dividing by
      // their separation would report a huge "relative error" for a sub-millimetre
      // absolute one and measure nothing useful.
      std::printf("       anchor radius %8.0f m : worst %.5f m (points %.0f m apart), "
                  "%.2e of the radius\n",
                  radius_m, worst_abs, worst_at, worst_abs / radius_m);
      t::ok(worst_abs < abs_bound,
            std::string(label) + ": absolute error within budget");
    };

    // Containment scale: the plane is anchored on the polygon edge being tested,
    // so this is the row that governs every containment decision. Millimetres.
    measure(200.0, "200 m anchor radius", 0.006);
    // A long zone boundary, or a segment spanning a couple of kilometres.
    measure(2000.0, "2 km anchor radius", 0.55);
    // District-wide: what you get if you anchor ONE plane for the whole map and
    // use it everywhere. Asserted as bad on purpose -- the number is the argument
    // for anchoring locally, and tens of metres is well past GPS noise.
    measure(25000.0, "25 km anchor radius", 90.0);
    t::ok(true, "budget rows above are printed for the report");
  }

  // ── why degree-space arithmetic is not good enough ─────────────────────────
  //
  // The thing the projection replaced. Computing the segment-projection parameter
  // t in raw degrees treats one degree of longitude as equal to one degree of
  // latitude; at 25.57 N the true ratio is cos(lat) = 0.902, an 11% skew on the
  // east-west axis. This test shows that skew producing a real error in a real
  // primitive rather than arguing about it.
  {
    // A 45-degree-in-degree-space segment running north-east, and a point off to
    // one side. In true metres the segment is NOT at 45 degrees, so the closest
    // point on it -- and the distance to it -- differ from the naive answer.
    const LatLon a{25.5700, 91.8800};
    const LatLon b{25.5800, 91.8900};
    const LatLon p{25.5800, 91.8800};

    const double good = point_segment_distance_m(p, a, b);

    // The old degree-space computation, reproduced here so the comparison is
    // concrete and this file documents what was actually wrong.
    const double dlat = b.lat - a.lat, dlon = b.lon - a.lon;
    const double len2 = dlat * dlat + dlon * dlon;
    double t_deg = ((p.lat - a.lat) * dlat + (p.lon - a.lon) * dlon) / len2;
    t_deg = t_deg < 0 ? 0 : (t_deg > 1 ? 1 : t_deg);
    const double naive = distance_m(p, {a.lat + t_deg * dlat, a.lon + t_deg * dlon});

    std::printf("       segment distance: local-metres %.3f m vs degree-space %.3f m "
                "(%.1f%% apart)\n", good, naive, 100.0 * std::fabs(naive - good) / good);
    t::ok(std::fabs(naive - good) > 1.0,
          "degree-space projection is off by more than a metre on a slanted edge");
    t::ok(good > 0.0, "the projected distance is positive");

    // Sanity: the closest point really is on the segment and really is that far.
    LatLon closest{};
    const double d2 = point_segment_distance_m(p, a, b, &closest);
    t::near(d2, good, 1e-9, "both overloads agree on the distance");
    // 5 cm: the closest point is converted back to lat/lon and re-measured with
    // haversine, so this carries one round trip of the ~1 km-anchor-radius error
    // from the budget above.
    t::near(distance_m(p, closest), good, 0.05,
            "the reported closest point is at the reported distance");
  }

  // ── endpoint clamping ──────────────────────────────────────────────────────
  {
    const LatLon a{25.5700, 91.8800}, b{25.5710, 91.8800};
    // Beyond the far end: must clamp to b, not extrapolate.
    const LatLon beyond{25.5730, 91.8800};
    LatLon closest{};
    const double d = point_segment_distance_m(beyond, a, b, &closest);
    t::near(d, distance_m(beyond, b), 0.01, "past the end clamps to the endpoint");
    t::near(distance_m(closest, b), 0.0, 0.01, "the closest point IS the endpoint");

    // Degenerate segment: both ends coincide.
    const double dd = point_segment_distance_m(beyond, a, a);
    t::near(dd, distance_m(beyond, a), 0.01, "a zero-length segment is just a point");
  }

  return t::report("geo/projection");
}
