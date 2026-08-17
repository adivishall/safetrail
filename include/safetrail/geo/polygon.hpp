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
  double signed_area() const;              // sign gives winding direction
  LatLon centroid() const;
  double perimeter_m() const;

  // ── Validity  [GAP 10] ────────────────────────────────────────────────────
  // Operators are police and tourism staff, not GIS analysts. They will draw
  // self-intersecting shapes, and ray casting on a self-intersecting polygon
  // returns arbitrary results — so this is a correctness gate, not a nicety.
  // Detection is Bentley-Ottmann sweep line, O((n+k) log n). See sweep_line.hpp.
  enum class Validity { Ok, TooFewVertices, SelfIntersecting, ZeroArea, HoleOutsideOuter };
  Validity validate() const;
  static const char* to_string(Validity v);

  void recompute_bbox();

 private:
  Ring              outer_;
  std::vector<Ring> holes_;
  Bbox              bbox_;
};

}  // namespace safetrail::geo
