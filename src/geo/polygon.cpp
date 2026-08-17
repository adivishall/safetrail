#include "safetrail/geo/polygon.hpp"
#include <cmath>

namespace safetrail::geo {

Polygon::Polygon(Ring outer) : outer_(std::move(outer)) { recompute_bbox(); }

void Polygon::add_hole(Ring h) { holes_.push_back(std::move(h)); recompute_bbox(); }

void Polygon::recompute_bbox() {
  bbox_ = Bbox::of(outer_.data(), outer_.size());
}

size_t Polygon::vertex_count() const {
  size_t n = outer_.size();
  for (const auto& h : holes_) n += h.size();
  return n;
}

// Shoelace. Sign encodes winding direction: positive = counter-clockwise.
double Polygon::signed_area() const {
  double a = 0.0;
  const size_t n = outer_.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++)
    a += (outer_[j].lon * outer_[i].lat) - (outer_[i].lon * outer_[j].lat);
  return a / 2.0;
}

LatLon Polygon::centroid() const {
  double a = 0, cx = 0, cy = 0;
  const size_t n = outer_.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const double cross = outer_[j].lon * outer_[i].lat - outer_[i].lon * outer_[j].lat;
    a += cross;
    cx += (outer_[j].lon + outer_[i].lon) * cross;
    cy += (outer_[j].lat + outer_[i].lat) * cross;
  }
  if (std::fabs(a) < 1e-18) return outer_.empty() ? LatLon{} : outer_[0];
  return {cy / (3.0 * a), cx / (3.0 * a)};
}

double Polygon::perimeter_m() const {
  double p = 0.0;
  const size_t n = outer_.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) p += distance_m(outer_[j], outer_[i]);
  return p;
}

// Proper orientation test, avoiding the classic (b-a)x(c-a) overflow-prone form.
static int orient(const LatLon& a, const LatLon& b, const LatLon& c) {
  const double v = (b.lon - a.lon) * (c.lat - a.lat) - (b.lat - a.lat) * (c.lon - a.lon);
  return v > 1e-14 ? 1 : (v < -1e-14 ? -1 : 0);
}

static bool on_seg(const LatLon& a, const LatLon& b, const LatLon& p) {
  return std::fmin(a.lon, b.lon) - 1e-14 <= p.lon && p.lon <= std::fmax(a.lon, b.lon) + 1e-14 &&
         std::fmin(a.lat, b.lat) - 1e-14 <= p.lat && p.lat <= std::fmax(a.lat, b.lat) + 1e-14;
}

static bool segs_cross(const LatLon& a, const LatLon& b, const LatLon& c, const LatLon& d) {
  const int o1 = orient(a, b, c), o2 = orient(a, b, d);
  const int o3 = orient(c, d, a), o4 = orient(c, d, b);
  if (o1 != o2 && o3 != o4) return true;
  if (o1 == 0 && on_seg(a, b, c)) return true;
  if (o2 == 0 && on_seg(a, b, d)) return true;
  if (o3 == 0 && on_seg(c, d, a)) return true;
  if (o4 == 0 && on_seg(c, d, b)) return true;
  return false;
}

// GAP 10. Operators are police staff, not GIS analysts; a self-intersecting
// polygon makes ray casting return arbitrary results, so this is a correctness
// gate. O(n^2) here -- fine at authoring time, once per zone. Bentley-Ottmann in
// sweep_line.hpp is the O((n+k) log n) version for bulk validation.
Polygon::Validity Polygon::validate() const {
  const size_t n = outer_.size();
  if (n < 3) return Validity::TooFewVertices;

  // Self-intersection BEFORE zero-area. A bowtie has zero signed area because its
  // two lobes cancel, so checking area first misdiagnoses every bowtie as
  // degenerate and the operator never learns the real problem.
  //
  // Edge i spans vertices (i, i+1 mod n). Two edges are adjacent -- and so
  // legitimately share a vertex -- when they are consecutive, including the
  // wrap-around pair (n-1, 0). Only non-adjacent edges may not cross.
  for (size_t i = 0; i < n; ++i) {
    const LatLon& a = outer_[i];
    const LatLon& b = outer_[(i + 1) % n];
    for (size_t j = i + 1; j < n; ++j) {
      const bool adjacent = (j == i + 1) || (i == 0 && j == n - 1);
      if (adjacent) continue;
      const LatLon& c = outer_[j];
      const LatLon& d = outer_[(j + 1) % n];
      if (segs_cross(a, b, c, d)) return Validity::SelfIntersecting;
    }
  }

  if (std::fabs(signed_area()) < 1e-16) return Validity::ZeroArea;
  return Validity::Ok;
}

const char* Polygon::to_string(Validity v) {
  switch (v) {
    case Validity::Ok: return "ok";
    case Validity::TooFewVertices: return "too few vertices";
    case Validity::SelfIntersecting: return "self-intersecting";
    case Validity::ZeroArea: return "zero area";
    case Validity::HoleOutsideOuter: return "hole outside outer ring";
  }
  return "unknown";
}

}  // namespace safetrail::geo
