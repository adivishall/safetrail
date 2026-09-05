#pragma once
// A zone's geometry: one outer ring plus zero or more holes.
//
// Holes are not decoration — an exempt village inside a restricted forest block is
// a hole, and ray casting must count crossings in holes so parity flips correctly.
// Getting that right is the difference between a correct implementation and one
// that happens to work on convex test data.
#include <vector>
#include "safetrail/geo/bbox.hpp"
#include "safetrail/geo/point.hpp"

namespace safetrail::geo {

using Ring = std::vector<LatLon>;

// Shoelace area of a single ring. The SIGN is the ring's winding direction:
// positive counter-clockwise, negative clockwise. Exposed because containment
// needs it to normalise hole winding -- see contains_winding() in
// containment.cpp -- and a second private copy would be one more place for the
// two containment implementations to drift apart.
double ring_signed_area(const Ring& r);

// ── Ring self-intersection ────────────────────────────────────────────────────
//
// Two implementations of one predicate, both kept on purpose.
//
//   ring_self_intersects_pairwise   every non-adjacent edge pair, O(V^2). The
//                                   reference. Simple enough to be obviously
//                                   right, which is what makes it useful as the
//                                   oracle the sweep is checked against.
//   ring_self_intersects_sweep      Shamos-Hoey sweep line over a hand-written
//                                   AVL status structure, O(V log V). See
//                                   sweep_line.hpp.
//   ring_self_intersects            what Polygon::validate() calls: the sweep at
//                                   or above kSweepThresholdVertices, the
//                                   pairwise scan below it.
//
// The threshold is measured, not assumed. Section 12 of `make bench` times both
// on simple rings from 8 to 2048 vertices; on the reference machine the sweep is
// 0.89x at 48 vertices and 1.11x at 64, so the crossover is around 56 and the
// constant sits there. Below it the sweep loses on constants -- it sorts 2V
// events and builds a balanced tree in order to skip a few dozen orientation
// tests. Above it the gap widens: 1.6x at 128 vertices, 8.9x at 2048, which is
// the range a simplified OSM district boundary actually lives in.
//
// The exact value is not load-bearing for correctness -- both branches return the
// same verdict, which the tests assert on rings either side of it -- so it can be
// retuned from a new measurement without anything else having to change.
constexpr size_t kSweepThresholdVertices = 56;

bool ring_self_intersects_pairwise(const Ring& r);
bool ring_self_intersects(const Ring& r);

class Polygon {
 public:
  Polygon() = default;
  explicit Polygon(Ring outer);

  const Ring& outer() const { return outer_; }
  const std::vector<Ring>& holes() const { return holes_; }
  void add_hole(Ring h);

  // Cached, recomputed on mutation. Every hot-path caller bbox-rejects before
  // touching the O(V) geometry, so this must never be computed lazily per query.
  const Bbox& bbox() const { return bbox_; }

  size_t vertex_count() const;

  // ── Metrics, and what they mean with holes ────────────────────────────────
  //
  // A polygon with holes is a REGION, not a ring, so every metric is defined on
  // the region -- otherwise "area" silently means two different things depending
  // on whether the zone happens to have an exempt village in it.
  //
  //   signed_area()  outer ring's signed area MINUS each hole's |area|. The sign
  //                  still encodes the outer ring's winding direction, so a
  //                  clockwise outer ring gives a negative result; |signed_area()|
  //                  is the true enclosed area. Hole winding is normalised away
  //                  (we subtract |hole|), so a GeoJSON file that gets hole
  //                  orientation wrong -- most do -- still yields the right area.
  //   centroid()     area-weighted centroid of the region: the outer ring's
  //                  first moment minus each hole's. This is the centroid of the
  //                  material that is actually inside the zone, which is what the
  //                  operator UI should label and what the correlator should
  //                  cluster on.
  //   perimeter_m()  total boundary length INCLUDING hole boundaries. A hole's
  //                  edge is a real edge of the zone -- you can walk out of the
  //                  zone by crossing it.
  //
  // Degenerate cases: a polygon whose region area is ~0 (a bowtie, or holes that
  // exactly consume the outer ring) has no meaningful centroid; we fall back to
  // the outer ring's first vertex rather than dividing by zero.
  double signed_area() const;
  LatLon centroid() const;
  double perimeter_m() const;

  // Outer-ring-only variants. Kept because jurisdiction nesting compares OUTER
  // extents (a district containing a block is a statement about their outlines,
  // not about their exempt enclaves), and because the difference between the two
  // is worth being able to show.
  double outer_signed_area() const;
  double outer_perimeter_m() const;

  // ── Validity  [GAP 10] ────────────────────────────────────────────────────
  // Operators are police and tourism staff, not GIS analysts. They will draw
  // self-intersecting shapes, and ray casting on a self-intersecting polygon
  // returns arbitrary results — so this is a correctness gate, not a nicety.
  //
  // Self-intersection detection is Shamos–Hoey, O(V log V), on any ring with at
  // least kSweepThresholdVertices vertices, and the O(V^2) pairwise scan below
  // that. (It is Shamos–Hoey, not Bentley–Ottmann: this answers EXISTENCE, which
  // is all a validity gate needs, and does it in O(V log V) with no dependence on
  // the number of crossings. Bentley–Ottmann enumerates all k of them in
  // O((V+k) log V) and we do not need them.) Holes go through the same predicate
  // as the outer ring.
  //
  // Boundary-touching policy, stated once because it is a real decision and the
  // functions have to agree on it:
  //
  //   WITHIN a ring   touching counts as self-intersection. A ring that grazes
  //                   itself at a non-adjacent vertex makes ray casting's parity
  //                   rule ill-defined at that point, so it is refused. Edges
  //                   that share a vertex because they are consecutive are of
  //                   course exempt.
  //   HOLE vs OUTER   touching is REFUSED (HoleCrossesOuter). A hole tangent to
  //                   the outer boundary pinches the region to zero width there;
  //                   whether the pinch point is inside is genuinely ambiguous,
  //                   and both containment implementations would have to agree on
  //                   an answer that has no right one. Draw the hole a
  //                   millimetre in and the question disappears.
  //   HOLE vs HOLE    touching is REFUSED (HolesOverlap), for the same reason.
  //   A POINT on a boundary   is INSIDE. This is the opposite convention and it
  //                   is deliberate: containment is asked about GPS fixes, where
  //                   the boundary case must resolve one way and "inside" is the
  //                   safe direction for a hazard zone. Validation is about the
  //                   geometry being well defined; containment is about a point
  //                   in already-valid geometry. See docs/GEOMETRY_EDGE_CASES.md.
  //
  // Holes are validated to the same standard as the outer ring, and then for the
  // three ways a hole can be geometrically incoherent: outside the outer ring,
  // straddling its boundary, or overlapping another hole. Ray casting's parity
  // rule assumes holes are disjoint sub-regions strictly inside the outer ring;
  // when they are not, containment answers are arbitrary in exactly the same way
  // a self-intersecting ring makes them arbitrary. So this is the same kind of
  // correctness gate, not extra strictness.
  enum class Validity {
    Ok,
    TooFewVertices,
    SelfIntersecting,
    ZeroArea,
    HoleTooFewVertices,
    HoleSelfIntersecting,
    HoleZeroArea,
    HoleOutsideOuter,      // a hole vertex lies outside the outer ring
    HoleCrossesOuter,      // a hole edge crosses the outer boundary
    HolesOverlap,          // two holes intersect or nest
  };
  Validity validate() const;
  static const char* to_string(Validity v);

  void recompute_bbox();

 private:
  Ring              outer_;
  std::vector<Ring> holes_;
  Bbox              bbox_;
};

}  // namespace safetrail::geo
