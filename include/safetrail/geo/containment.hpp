#pragma once
//
// Three-valued containment.  [GAP 1]
//
// The whole project pivots on this file. Existing systems ask
//
//     bool inside = ST_Contains(zone, point);
//
// and get a confident answer to a question the data cannot support. We ask
//
//     Containment c = evaluate(zone, uncertain_point);
//
// and accept that the honest answer is sometimes "I don't know", which the
// operator UI can render as a distinct third state and the alert policy can
// treat differently from a confirmed breach.
//
#include "safetrail/geo/point.hpp"
#include "safetrail/geo/polygon.hpp"

namespace safetrail::geo {

enum class Containment {
  Outside,     // the entire uncertainty disc lies outside the polygon
  Inside,      // the entire uncertainty disc lies inside the polygon
  Uncertain,   // the disc straddles the boundary — cannot be resolved
};

const char* to_string(Containment c);

// ─── Exact point-in-polygon ─────────────────────────────────────────────────
//
// Ray casting: cast a ray from the point and count boundary crossings. Odd means
// inside. O(V).
//
// The implementation is short. Getting it correct is not. The cases that break
// naive implementations, all of which have tests in tests/geo/:
//
//   - point exactly on an edge          (define it: we return true)
//   - ray passes exactly through a vertex   (count each edge once, not twice)
//   - horizontal edges collinear with the ray
//   - concave polygons                  (naive convex assumptions fail)
//   - polygons with holes               (crossings in holes must flip parity)
//   - self-intersecting polygons        (undefined — reject at authoring time,
//                                        see sweep_line.hpp, GAP 10)
//
// See docs/GEOMETRY_EDGE_CASES.md.
bool contains(const Polygon& poly, const LatLon& p);

// Winding number: a second, independent implementation. Kept permanently, not as
// a curiosity — it cross-validates ray casting on randomised input in
// tests/geo/equivalence_test.cpp, and the two disagree exactly where the hard
// cases live. Also benchmarked against each other.
bool contains_winding(const Polygon& poly, const LatLon& p);

// ─── Uncertainty-aware containment ──────────────────────────────────────────
//
// Resolves to Inside/Outside only when the entire disc of radius `accuracy_m`
// falls on one side. Requires the distance from the point to the nearest polygon
// edge, not just a parity test:
//
//     d = distance to nearest edge
//     if d > r  →  Inside or Outside, per the exact test
//     if d <= r →  Uncertain
//
// O(V) — same order as ray casting, but with a real distance computation per
// edge rather than a comparison, so measurably slower. Worth benchmarking.
Containment evaluate(const Polygon& poly, const UncertainPoint& p);

// Signed distance from p to the polygon boundary. Negative inside, positive
// outside. This is the workhorse: `evaluate` needs it, the predictive alerting
// path needs it (GAP 2), and the adaptive sampler needs it to decide how often
// to request a GPS fix (GAP 7).
//
// O(V). Callers on the hot path must bbox-reject before calling this.
double signed_distance_m(const Polygon& poly, const LatLon& p);

// Nearest point on the polygon boundary to p. Used by the operator UI to draw
// "2.3 km inside, nearest exit is this way" and by the predictive path.
LatLon nearest_boundary_point(const Polygon& poly, const LatLon& p);

}  // namespace safetrail::geo
