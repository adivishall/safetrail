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
  // Detection is Bentley-Ottmann sweep line, O((n+k) log n). See sweep_line.hpp.
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
