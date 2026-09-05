#include "safetrail/geo/polygon.hpp"

#include <cmath>

#include "safetrail/geo/segment.hpp"
#include "safetrail/geo/sweep_line.hpp"

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

// Shoelace over one ring. Sign encodes winding: positive = counter-clockwise.
double ring_signed_area(const Ring& r) {
  double a = 0.0;
  const size_t n = r.size();
  if (n < 3) return 0.0;
  for (size_t i = 0, j = n - 1; i < n; j = i++)
    a += (r[j].lon * r[i].lat) - (r[i].lon * r[j].lat);
  return a / 2.0;
}

// ── Self-intersection: the O(V^2) reference ──────────────────────────────────
//
// Every non-adjacent edge pair. Kept permanently, not as legacy: it is the oracle
// the sweep is checked against (tests/geo/sweep_line_test.cpp), and it is what
// validate() actually runs on small rings, where V^2 is a dozen comparisons and
// beats building an event list and a balanced tree.
bool ring_self_intersects_pairwise(const Ring& r) {
  const size_t n = r.size();
  if (n < 4) return false;                  // a triangle cannot self-intersect
  for (size_t i = 0; i < n; ++i) {
    const LatLon& a = r[i];
    const LatLon& b = r[(i + 1) % n];
    for (size_t j = i + 1; j < n; ++j) {
      // Edges i and j share a vertex when consecutive, including the wrap-around
      // pair (n-1, 0). Only non-adjacent edges may not touch.
      if (j == i + 1 || (i == 0 && j == n - 1)) continue;
      if (segments_intersect(a, b, r[j], r[(j + 1) % n])) return true;
    }
  }
  return false;
}

// ── ...and the dispatch ──────────────────────────────────────────────────────
//
// Below the threshold the pairwise scan wins on constants; above it the sweep's
// O(V log V) wins on growth. The number is measured, not guessed -- section 12 of
// `make bench` times both on rings from 8 to 4096 vertices and prints where they
// cross. Both answer the same question and are asserted to give the same verdict
// on randomised rings at sizes either side of it.
bool ring_self_intersects(const Ring& r) {
  return r.size() >= kSweepThresholdVertices ? ring_self_intersects_sweep(r)
                                             : ring_self_intersects_pairwise(r);
}

namespace {
// First moment of a ring: (sum of 2*A*cx, 2*A*cy scaled), returned alongside its
// signed area so callers can combine rings without recomputing.
void ring_moment(const Ring& r, double* area2, double* mx, double* my) {
  double a = 0, cx = 0, cy = 0;
  const size_t n = r.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const double cross = r[j].lon * r[i].lat - r[i].lon * r[j].lat;
    a += cross;
    cx += (r[j].lon + r[i].lon) * cross;
    cy += (r[j].lat + r[i].lat) * cross;
  }
  *area2 = a; *mx = cx; *my = cy;
}

double ring_perimeter_m(const Ring& r) {
  double p = 0.0;
  const size_t n = r.size();
  if (n < 2) return 0.0;
  for (size_t i = 0, j = n - 1; i < n; j = i++) p += distance_m(r[j], r[i]);
  return p;
}

// Point-in-ring by the half-open crossing rule. Local to validation: the public
// contains() in containment.cpp works on a whole Polygon (outer + holes), and
// validation needs the single-ring question, which is a different predicate.
bool point_in_ring(const Ring& r, const LatLon& p) {
  const size_t n = r.size();
  if (n < 3) return false;
  bool in = false;
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    if (point_on_segment(r[j], r[i], p)) return true;      // boundary counts as in
    if ((r[i].lat > p.lat) != (r[j].lat > p.lat)) {
      const double t = (p.lat - r[i].lat) / (r[j].lat - r[i].lat);
      if (p.lon < r[i].lon + t * (r[j].lon - r[i].lon)) in = !in;
    }
  }
  return in;
}

bool rings_edges_cross(const Ring& a, const Ring& b) {
  const size_t na = a.size(), nb = b.size();
  for (size_t i = 0; i < na; ++i)
    for (size_t j = 0; j < nb; ++j)
      if (segments_intersect(a[i], a[(i + 1) % na], b[j], b[(j + 1) % nb])) return true;
  return false;
}
}  // namespace

double Polygon::outer_signed_area() const { return ring_signed_area(outer_); }
double Polygon::outer_perimeter_m() const { return ring_perimeter_m(outer_); }

// Region area: outer ring minus every hole. Hole winding is normalised away by
// subtracting the magnitude, so an import that got hole orientation backwards --
// which most GeoJSON producers do -- still yields the right answer.
double Polygon::signed_area() const {
  const double outer = ring_signed_area(outer_);
  double holes = 0.0;
  for (const auto& h : holes_) holes += std::fabs(ring_signed_area(h));
  const double sign = outer < 0 ? -1.0 : 1.0;
  const double magnitude = std::fabs(outer) - holes;
  return sign * (magnitude > 0.0 ? magnitude : 0.0);
}

// Area-weighted centroid of the REGION: the outer ring's first moment minus each
// hole's. Using the outer ring alone would put the label of a ring-shaped zone in
// the middle of the hole -- i.e. outside the zone.
LatLon Polygon::centroid() const {
  double a2 = 0, mx = 0, my = 0;
  ring_moment(outer_, &a2, &mx, &my);
  const double outer_sign = a2 < 0 ? -1.0 : 1.0;

  for (const auto& h : holes_) {
    double ha2 = 0, hmx = 0, hmy = 0;
    ring_moment(h, &ha2, &hmx, &hmy);
    // Force each hole to the OPPOSITE winding of the outer ring before
    // subtracting, so the moments cancel regardless of how the file was authored.
    const double flip = (ha2 * outer_sign > 0) ? -1.0 : 1.0;
    a2 += flip * ha2; mx += flip * hmx; my += flip * hmy;
  }

  if (std::fabs(a2) < 1e-18) return outer_.empty() ? LatLon{} : outer_[0];
  return {my / (3.0 * a2), mx / (3.0 * a2)};
}

// Total boundary of the region, holes included: a hole's edge is a real edge of
// the zone -- crossing it takes you out.
double Polygon::perimeter_m() const {
  double p = ring_perimeter_m(outer_);
  for (const auto& h : holes_) p += ring_perimeter_m(h);
  return p;
}

// GAP 10. Operators are police staff, not GIS analysts; a self-intersecting
// polygon makes ray casting return arbitrary results, so this is a correctness
// gate. Holes are held to the same standard, plus the three ways a hole can be
// incoherent relative to the region it punches out of.
Polygon::Validity Polygon::validate() const {
  const size_t n = outer_.size();
  if (n < 3) return Validity::TooFewVertices;

  // Self-intersection BEFORE zero-area. A bowtie has zero signed area because its
  // two lobes cancel, so checking area first misdiagnoses every bowtie as
  // degenerate and the operator never learns the real problem.
  if (ring_self_intersects(outer_)) return Validity::SelfIntersecting;
  if (std::fabs(ring_signed_area(outer_)) < 1e-16) return Validity::ZeroArea;

  for (const auto& h : holes_) {
    if (h.size() < 3) return Validity::HoleTooFewVertices;
    if (ring_self_intersects(h)) return Validity::HoleSelfIntersecting;
    if (std::fabs(ring_signed_area(h)) < 1e-16) return Validity::HoleZeroArea;

    // Every hole vertex inside the outer ring...
    for (const auto& v : h)
      if (!point_in_ring(outer_, v)) return Validity::HoleOutsideOuter;
    // ...AND no hole edge crossing the outer boundary. Vertices alone are not
    // enough: a concave outer ring can have both endpoints of a hole edge inside
    // while the edge itself exits and re-enters through a notch.
    if (rings_edges_cross(h, outer_)) return Validity::HoleCrossesOuter;
  }

  // Holes must be pairwise disjoint. Two overlapping holes double-count their
  // shared area in the shoelace sum and, worse, flip ray-casting parity twice --
  // so the intersection reads as INSIDE the zone when it is a hole.
  for (size_t i = 0; i < holes_.size(); ++i)
    for (size_t j = i + 1; j < holes_.size(); ++j) {
      if (rings_edges_cross(holes_[i], holes_[j])) return Validity::HolesOverlap;
      // Edge-disjoint but nested: one hole entirely inside another.
      if (point_in_ring(holes_[i], holes_[j][0])) return Validity::HolesOverlap;
      if (point_in_ring(holes_[j], holes_[i][0])) return Validity::HolesOverlap;
    }

  return Validity::Ok;
}

const char* Polygon::to_string(Validity v) {
  switch (v) {
    case Validity::Ok: return "ok";
    case Validity::TooFewVertices: return "too few vertices";
    case Validity::SelfIntersecting: return "self-intersecting";
    case Validity::ZeroArea: return "zero area";
    case Validity::HoleTooFewVertices: return "hole has too few vertices";
    case Validity::HoleSelfIntersecting: return "hole is self-intersecting";
    case Validity::HoleZeroArea: return "hole has zero area";
    case Validity::HoleOutsideOuter: return "hole outside outer ring";
    case Validity::HoleCrossesOuter: return "hole crosses the outer boundary";
    case Validity::HolesOverlap: return "holes overlap";
  }
  return "unknown";
}

}  // namespace safetrail::geo
