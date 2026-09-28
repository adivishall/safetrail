#include "safetrail/geo/sweep_line.hpp"

#include <algorithm>
#include <cmath>

#include "safetrail/geo/segment.hpp"

namespace safetrail::geo {

// Coordinates: x = lon, y = lat. Same orientation/segment-cross predicate as
// Polygon::validate(), so the two agree segment-for-segment.
namespace {

// The crossing predicate is shared with Polygon::validate() (geo/segment.hpp) so
// the sweep and the O(n^2) reference cannot disagree segment-for-segment -- which
// is what tests/geo/sweep_line_test.cpp asserts on randomised input.
bool segs_cross(const Segment& s, const Segment& t) {
  return segments_intersect(s.a, s.b, t.a, t.b);
}
double y_at(const Segment& s, double x) {
  const double x1 = s.a.lon, y1 = s.a.lat, x2 = s.b.lon, y2 = s.b.lat;
  if (std::fabs(x2 - x1) < 1e-18) return std::fmin(y1, y2);
  double xc = x;
  if (xc < std::fmin(x1, x2)) xc = std::fmin(x1, x2);
  if (xc > std::fmax(x1, x2)) xc = std::fmax(x1, x2);
  return y1 + (y2 - y1) * (xc - x1) / (x2 - x1);
}
double slope(const Segment& s) {
  const double dx = s.b.lon - s.a.lon, dy = s.b.lat - s.a.lat;
  return std::fabs(dx) < 1e-18 ? 1e18 : dy / dx;
}
// Order two active segments as of sweep position x. Evaluated a hair BEFORE x,
// not at x, and this matters: when two segments meet at a shared endpoint (two
// ring-adjacent edges touching at their common vertex -- normal, not a crossing),
// their y values become EXACTLY equal at that x, and naive tie-breaking there can
// flip which one the tree considers "above" the other. That flip is harmless for
// a one-shot comparison, but it is fatal for a balanced-BST status structure,
// whose erase/predecessor/successor queries re-run this comparator later to find
// a node that was placed using its ordering at insertion time -- if the ordering
// of that pair has flipped, the search walks the wrong way and the node is never
// found. Comparing "just before" x sidesteps the coincidence: away from an exact
// meeting point the two curves are already separated, so the order matches the
// one used when the tree was built, and the balance invariant holds throughout.
constexpr double kEpsX = 1e-9;
bool less_at(const Segment& a, const Segment& b, double x) {
  const double xb = x - kEpsX;
  const double ya = y_at(a, xb), yb = y_at(b, xb);
  if (std::fabs(ya - yb) > 1e-12) return ya < yb;
  return slope(a) < slope(b);
}

struct Event { double x; int type; size_t si; };   // type 0 = left, 1 = right

bool ring_adjacent(size_t i, size_t j, size_t n) {
  size_t lo = std::min(i, j), hi = std::max(i, j);
  return (hi - lo == 1) || (lo == 0 && hi == n - 1);
}

// ── Balanced-BST status structure (AVL, index-backed) ────────────────────────
// Orders active segments by y at the current sweep x, giving O(log n) insert,
// erase, and neighbour (predecessor/successor) lookup -- so the whole sweep is
// O((n + k) log n). Ordering is made a strict total order by breaking y/slope
// ties on the segment index, so each active segment occupies a unique node and
// erase/neighbour queries are unambiguous.
class Status {
 public:
  explicit Status(const std::vector<Segment>& segs) : segs_(segs) {}
  void set_x(double x) { x_ = x; }

  void insert(size_t seg) { root_ = insert_at(root_, seg); }
  void erase(size_t seg)  { root_ = erase_at(root_, seg); }

  // Nearest active segment below `seg` in the ordering (largest node < seg), or
  // SIZE_MAX. `seg` need not be in the tree.
  size_t predecessor(size_t seg) const {
    int32_t i = root_; size_t best = SIZE_MAX;
    while (i >= 0) {
      if (less(nodes_[size_t(i)].seg, seg)) { best = nodes_[size_t(i)].seg; i = nodes_[size_t(i)].r; }
      else i = nodes_[size_t(i)].l;
    }
    return best;
  }
  size_t successor(size_t seg) const {
    int32_t i = root_; size_t best = SIZE_MAX;
    while (i >= 0) {
      if (less(seg, nodes_[size_t(i)].seg)) { best = nodes_[size_t(i)].seg; i = nodes_[size_t(i)].l; }
      else i = nodes_[size_t(i)].r;
    }
    return best;
  }

 private:
  struct Node { size_t seg; int32_t l = -1, r = -1; int32_t h = 1; };
  const std::vector<Segment>& segs_;
  std::vector<Node> nodes_;
  int32_t root_ = -1;
  double  x_ = 0.0;

  bool less(size_t a, size_t b) const {
    if (less_at(segs_[a], segs_[b], x_)) return true;
    if (less_at(segs_[b], segs_[a], x_)) return false;
    return a < b;                                  // total order tie-break
  }
  int32_t h(int32_t i) const { return i < 0 ? 0 : nodes_[size_t(i)].h; }
  void fix(int32_t i) { nodes_[size_t(i)].h = 1 + std::max(h(nodes_[size_t(i)].l), h(nodes_[size_t(i)].r)); }
  int32_t bal(int32_t i) const { return i < 0 ? 0 : h(nodes_[size_t(i)].l) - h(nodes_[size_t(i)].r); }

  int32_t rot_r(int32_t y) {
    int32_t x = nodes_[size_t(y)].l;
    nodes_[size_t(y)].l = nodes_[size_t(x)].r;
    nodes_[size_t(x)].r = y; fix(y); fix(x); return x;
  }
  int32_t rot_l(int32_t x) {
    int32_t y = nodes_[size_t(x)].r;
    nodes_[size_t(x)].r = nodes_[size_t(y)].l;
    nodes_[size_t(y)].l = x; fix(x); fix(y); return y;
  }
  int32_t rebalance(int32_t i) {
    fix(i);
    const int32_t b = bal(i);
    if (b > 1) { if (bal(nodes_[size_t(i)].l) < 0) nodes_[size_t(i)].l = rot_l(nodes_[size_t(i)].l); return rot_r(i); }
    if (b < -1) { if (bal(nodes_[size_t(i)].r) > 0) nodes_[size_t(i)].r = rot_r(nodes_[size_t(i)].r); return rot_l(i); }
    return i;
  }
  int32_t insert_at(int32_t root, size_t seg) {
    if (root < 0) { nodes_.push_back(Node{seg}); return int32_t(nodes_.size()) - 1; }
    if (less(seg, nodes_[size_t(root)].seg)) nodes_[size_t(root)].l = insert_at(nodes_[size_t(root)].l, seg);
    else nodes_[size_t(root)].r = insert_at(nodes_[size_t(root)].r, seg);
    return rebalance(root);
  }
  int32_t min_node(int32_t i) const { while (nodes_[size_t(i)].l >= 0) i = nodes_[size_t(i)].l; return i; }
  int32_t erase_at(int32_t root, size_t seg) {
    if (root < 0) return -1;
    if (less(seg, nodes_[size_t(root)].seg)) nodes_[size_t(root)].l = erase_at(nodes_[size_t(root)].l, seg);
    else if (less(nodes_[size_t(root)].seg, seg)) nodes_[size_t(root)].r = erase_at(nodes_[size_t(root)].r, seg);
    else {
      const int32_t l = nodes_[size_t(root)].l, r = nodes_[size_t(root)].r;
      if (l < 0) return r;
      if (r < 0) return l;
      const int32_t m = min_node(r);
      nodes_[size_t(root)].seg = nodes_[size_t(m)].seg;   // copy successor's seg up
      nodes_[size_t(root)].r = erase_at(r, nodes_[size_t(m)].seg);
    }
    return rebalance(root);
  }
};

// How many neighbours an inserted segment is tested against on each side.
//
// Ring-adjacent edges are exempt from each other (they legitimately share a
// vertex), and that breaks the textbook Shamos-Hoey argument: with exempt pairs
// ignored, a segment's first eligible neighbour is no longer guaranteed to be
// the one it would meet first. Testing ONLY that first eligible neighbour missed
// touching pairs one step further out -- 118 of 800,000 fuzzed lattice rings,
// all false "simple" verdicts. Testing every eligible segment among the nearest
// kReach on each side closes that: an edge has at most two exempt partners, so
// three covers them plus one. It is a constant, so every event is still
// O(log n). The value is evidence-based, not proven: zero disagreements with the
// O(V^2) oracle over the same 800,000 rings, and tools/mutation_check.py shows
// the test suite fails if it is set to 1.
constexpr int kReach = 3;

bool sweep(const std::vector<Segment>& segs, bool ring_adjacency) {
  const size_t n = segs.size();
  if (n < 2) return false;

  std::vector<Segment> s = segs;
  for (auto& seg : s)
    if (seg.a.lon > seg.b.lon) std::swap(seg.a, seg.b);

  std::vector<Event> ev;
  ev.reserve(2 * n);
  for (size_t i = 0; i < n; ++i) { ev.push_back({s[i].a.lon, 0, i}); ev.push_back({s[i].b.lon, 1, i}); }
  // A total order. (x, type) alone left events at the same x in whatever order
  // std::sort happened to produce -- unspecified, so free to differ between
  // libc++ and libstdc++ -- and axis-aligned polygons put many events at one x.
  std::sort(ev.begin(), ev.end(), [](const Event& p, const Event& q) {
    if (p.x != q.x) return p.x < q.x;
    if (p.type != q.type) return p.type < q.type;
    return p.si < q.si;
  });

  auto eligible = [&](size_t i, size_t j) { return !(ring_adjacency && ring_adjacent(i, j, n)); };

  Status st(s);
  // The nearest kReach active segments below / above `seg`, nearest first.
  size_t lo[kReach], hi[kReach];
  auto neighbours = [&](size_t seg) {
    int k = 0;
    for (size_t p = st.predecessor(seg); p != SIZE_MAX && k < kReach; p = st.predecessor(p)) lo[k++] = p;
    for (; k < kReach; ++k) lo[k] = SIZE_MAX;
    k = 0;
    for (size_t q = st.successor(seg); q != SIZE_MAX && k < kReach; q = st.successor(q)) hi[k++] = q;
    for (; k < kReach; ++k) hi[k] = SIZE_MAX;
  };
  auto test = [&](size_t a, size_t b) {
    return a != SIZE_MAX && b != SIZE_MAX && eligible(a, b) && segs_cross(s[a], s[b]);
  };

  for (const auto& e : ev) {
    st.set_x(e.x);
    if (e.type == 0) {                 // enter: test against its near neighbours
      st.insert(e.si);
      neighbours(e.si);
      for (int i = 0; i < kReach; ++i)
        if (test(e.si, lo[i]) || test(e.si, hi[i])) return true;
    } else {                           // leave: its two neighbours become adjacent
      const size_t p = st.predecessor(e.si), q = st.successor(e.si);
      if (test(p, q)) return true;
      st.erase(e.si);
    }
  }
  return false;
}

}  // namespace

bool any_intersection(const std::vector<Segment>& segs, bool ring_adjacency) {
  return sweep(segs, ring_adjacency);
}

bool ring_self_intersects_sweep(const Ring& r) {
  const size_t n = r.size();
  if (n < 4) return false;                 // a triangle cannot self-intersect
  std::vector<Segment> segs;
  segs.reserve(n);
  for (size_t i = 0; i < n; ++i) segs.push_back({r[i], r[(i + 1) % n]});
  return sweep(segs, /*ring_adjacency=*/true);
}

bool polygon_self_intersects(const Polygon& poly) {
  return ring_self_intersects_sweep(poly.outer());
}

}  // namespace safetrail::geo
