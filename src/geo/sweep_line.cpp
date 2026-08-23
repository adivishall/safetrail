#include "safetrail/geo/sweep_line.hpp"

#include <algorithm>
#include <cmath>

namespace safetrail::geo {

// Coordinates: x = lon, y = lat. Same orientation/segment-cross predicate as
// Polygon::validate(), so the two agree segment-for-segment.
namespace {

int orient(double ax, double ay, double bx, double by, double cx, double cy) {
  const double v = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
  return v > 1e-14 ? 1 : (v < -1e-14 ? -1 : 0);
}
bool on_seg(double ax, double ay, double bx, double by, double px, double py) {
  return std::fmin(ax, bx) - 1e-14 <= px && px <= std::fmax(ax, bx) + 1e-14 &&
         std::fmin(ay, by) - 1e-14 <= py && py <= std::fmax(ay, by) + 1e-14;
}
bool segs_cross(const Segment& s, const Segment& t) {
  const double ax = s.a.lon, ay = s.a.lat, bx = s.b.lon, by = s.b.lat;
  const double cx = t.a.lon, cy = t.a.lat, dx = t.b.lon, dy = t.b.lat;
  const int o1 = orient(ax, ay, bx, by, cx, cy), o2 = orient(ax, ay, bx, by, dx, dy);
  const int o3 = orient(cx, cy, dx, dy, ax, ay), o4 = orient(cx, cy, dx, dy, bx, by);
  if (o1 != o2 && o3 != o4) return true;
  if (o1 == 0 && on_seg(ax, ay, bx, by, cx, cy)) return true;
  if (o2 == 0 && on_seg(ax, ay, bx, by, dx, dy)) return true;
  if (o3 == 0 && on_seg(cx, cy, dx, dy, ax, ay)) return true;
  if (o4 == 0 && on_seg(cx, cy, dx, dy, bx, by)) return true;
  return false;
}

// y of segment i at sweep x. Vertical segments report their lower endpoint.
double y_at(const Segment& s, double x) {
  const double x1 = s.a.lon, y1 = s.a.lat, x2 = s.b.lon, y2 = s.b.lat;
  if (std::fabs(x2 - x1) < 1e-18) return std::fmin(y1, y2);
  double xc = x;
  if (xc < std::fmin(x1, x2)) xc = std::fmin(x1, x2);
  if (xc > std::fmax(x1, x2)) xc = std::fmax(x1, x2);
  return y1 + (y2 - y1) * (xc - x1) / (x2 - x1);
}

// Slope, for tie-breaking two segments with equal y at the sweep line: the one
// heading downward sorts below. Vertical segments get a large slope so they sort
// above segments they share a point with -- consistent, which is all the ordering
// needs to be. This is the robustness fix for events where several edges meet at
// one x (e.g. two ring edges leaving a shared vertex).
double slope(const Segment& s) {
  const double dx = s.b.lon - s.a.lon, dy = s.b.lat - s.a.lat;
  return std::fabs(dx) < 1e-18 ? 1e18 : dy / dx;
}

// Ordering of two active segments at sweep x: by y, ties broken by slope.
bool less_at(const Segment& a, const Segment& b, double x) {
  const double ya = y_at(a, x), yb = y_at(b, x);
  if (std::fabs(ya - yb) > 1e-12) return ya < yb;
  return slope(a) < slope(b);
}

struct Event { double x; int type; size_t si; };   // type 0 = left (enter), 1 = right (leave)

// Are these two ring edges consecutive (and so allowed to share a vertex)?
bool ring_adjacent(size_t i, size_t j, size_t n) {
  size_t lo = std::min(i, j), hi = std::max(i, j);
  return (hi - lo == 1) || (lo == 0 && hi == n - 1);
}

bool sweep(const std::vector<Segment>& segs, bool ring_adjacency) {
  const size_t n = segs.size();
  if (n < 2) return false;

  // Normalise each segment so a is the left endpoint, and build the event list.
  std::vector<Segment> s = segs;
  for (auto& seg : s)
    if (seg.a.lon > seg.b.lon) std::swap(seg.a, seg.b);

  std::vector<Event> ev;
  ev.reserve(2 * n);
  for (size_t i = 0; i < n; ++i) {
    ev.push_back({s[i].a.lon, 0, i});
    ev.push_back({s[i].b.lon, 1, i});
  }
  // Left events before right at equal x, so segments meeting at a point are both
  // active and get compared.
  std::sort(ev.begin(), ev.end(), [](const Event& p, const Event& q) {
    return p.x != q.x ? p.x < q.x : p.type < q.type;
  });

  auto eligible = [&](size_t i, size_t j) {
    return !(ring_adjacency && ring_adjacent(i, j, n));
  };

  // Active set: segment indices ordered by y at the current sweep x.
  //
  // Neighbour test with an exemption twist: a ring-adjacent neighbour shares a
  // vertex legitimately and is skipped -- but skipping it must not hide a real
  // crossing behind it, so we scan past exempt neighbours to the nearest testable
  // one in each direction. A segment has at most two ring-adjacent edges, so the
  // scan is bounded.
  std::vector<size_t> active;

  // Test `seg` (sitting at index `at`) against its nearest TESTABLE neighbour in
  // each direction -- scanning past ring-adjacent (exempt) segments so an allowed
  // shared vertex cannot shield a real crossing behind it.
  auto test_neighbours = [&](size_t seg, size_t at) -> bool {
    for (size_t k = at; k-- > 0;) {                    // strictly below `at`
      if (!eligible(seg, active[k])) continue;
      if (segs_cross(s[seg], s[active[k]])) return true;
      break;
    }
    for (size_t k = at + 1; k < active.size(); ++k) {  // strictly above `at`
      if (!eligible(seg, active[k])) continue;
      if (segs_cross(s[seg], s[active[k]])) return true;
      break;
    }
    return false;
  };

  for (const auto& e : ev) {
    if (e.type == 0) {   // enter
      const double x = e.x;
      size_t at = 0;
      while (at < active.size() && less_at(s[active[at]], s[e.si], x)) ++at;
      active.insert(active.begin() + long(at), e.si);
      if (test_neighbours(e.si, at)) return true;
    } else {             // leave
      auto it = std::find(active.begin(), active.end(), e.si);
      if (it == active.end()) continue;
      const size_t at = size_t(it - active.begin());
      active.erase(it);
      // After removal the segments at at-1 and at become adjacent; test them.
      if (at > 0 && at < active.size() &&
          eligible(active[at - 1], active[at]) &&
          segs_cross(s[active[at - 1]], s[active[at]]))
        return true;
    }
  }
  return false;
}

}  // namespace

bool any_intersection(const std::vector<Segment>& segs, bool ring_adjacency) {
  return sweep(segs, ring_adjacency);
}

bool polygon_self_intersects(const Polygon& poly) {
  const Ring& r = poly.outer();
  const size_t n = r.size();
  if (n < 3) return false;
  std::vector<Segment> segs;
  segs.reserve(n);
  for (size_t i = 0; i < n; ++i) segs.push_back({r[i], r[(i + 1) % n]});
  return sweep(segs, /*ring_adjacency=*/true);
}

}  // namespace safetrail::geo
