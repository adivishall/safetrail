#include "safetrail/index/quadtree.hpp"

#include <algorithm>

#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/geohash.hpp"
#include "safetrail/index/rtree.hpp"

namespace safetrail::index {

// Depth is NOT stored on the node. It used to be, and that made root expansion
// (below) an O(n) rewrite of every node in the tree just to renumber them. Depth
// is a property of the path, so it is passed down the recursion instead -- which
// also means there is no stored value that can drift out of sync with the shape.
struct Quadtree::Node {
  geo::Bbox region;
  std::vector<std::pair<ZoneId, geo::Bbox>> items;
  std::unique_ptr<Node> kids[4];
  bool leaf() const { return !kids[0]; }
};

Quadtree::Quadtree(size_t cap, size_t max_depth)
    : cap_(cap < 1 ? 1 : cap), max_depth_(max_depth) {
  root_ = std::make_unique<Node>();
  root_->region = {-90.0, -180.0, 90.0, 180.0};
}
Quadtree::~Quadtree() = default;

static geo::Bbox quadrant(const geo::Bbox& r, int i) {
  const double mlat = (r.min_lat + r.max_lat) / 2, mlon = (r.min_lon + r.max_lon) / 2;
  switch (i) {
    case 0: return {r.min_lat, r.min_lon, mlat, mlon};   // SW
    case 1: return {r.min_lat, mlon, mlat, r.max_lon};   // SE
    case 2: return {mlat, r.min_lon, r.max_lat, mlon};   // NW
    default: return {mlat, mlon, r.max_lat, r.max_lon};  // NE
  }
}

static bool fully_contains(const geo::Bbox& outer, const geo::Bbox& inner) {
  return inner.min_lat >= outer.min_lat && inner.max_lat <= outer.max_lat &&
         inner.min_lon >= outer.min_lon && inner.max_lon <= outer.max_lon;
}

namespace {

void subdivide(Quadtree::Node* n) {
  for (int i = 0; i < 4; ++i) {
    n->kids[i] = std::make_unique<Quadtree::Node>();
    n->kids[i]->region = quadrant(n->region, i);
  }
}

void insert_into(Quadtree::Node* n, ZoneId id, const geo::Bbox& box,
                 size_t cap, size_t max_depth, size_t depth) {
  // Descend only if a single child fully contains the box.
  if (!n->leaf()) {
    for (int i = 0; i < 4; ++i)
      if (fully_contains(n->kids[i]->region, box))
        return insert_into(n->kids[i].get(), id, box, cap, max_depth, depth + 1);
    n->items.emplace_back(id, box);           // straddles a split -> stays here
    return;
  }
  n->items.emplace_back(id, box);
  if (n->items.size() > cap && depth < max_depth) {
    subdivide(n);
    auto staying = std::move(n->items);
    n->items.clear();
    for (auto& it : staying) {
      bool moved = false;
      for (int i = 0; i < 4; ++i)
        if (fully_contains(n->kids[i]->region, it.second)) {
          insert_into(n->kids[i].get(), it.first, it.second, cap, max_depth, depth + 1);
          moved = true; break;
        }
      if (!moved) n->items.push_back(it);
    }
  }
}

void query_node(const Quadtree::Node* n, const geo::Bbox& q, std::vector<ZoneId>& out) {
  if (!n->region.intersects(q)) return;                  // prune whole subtree
  for (const auto& it : n->items)
    if (it.second.intersects(q)) out.push_back(it.first);
  if (!n->leaf()) for (int i = 0; i < 4; ++i) query_node(n->kids[i].get(), q, out);
}

// Total items in this subtree, giving up as soon as the count exceeds `limit`.
// The early exit is what makes the collapse check below cheap: it never walks
// more than `limit + 1` items, regardless of how large the subtree is.
size_t count_upto(const Quadtree::Node* n, size_t limit) {
  size_t total = n->items.size();
  if (total > limit) return total;
  if (!n->leaf())
    for (int i = 0; i < 4; ++i) {
      total += count_upto(n->kids[i].get(), limit - std::min(limit, total));
      if (total > limit) return total;
    }
  return total;
}

void gather(Quadtree::Node* n, std::vector<std::pair<ZoneId, geo::Bbox>>& out) {
  for (auto& it : n->items) out.push_back(it);
  if (!n->leaf()) for (int i = 0; i < 4; ++i) gather(n->kids[i].get(), out);
}

// ── Collapse  ────────────────────────────────────────────────────────────────
//
// After a deletion, an interior node whose whole subtree now holds no more than
// `cap` items is pure overhead: four child nodes (and their descendants) that a
// query must still descend into, to find items that would fit in this one node.
// Deletion used to leave those behind forever, so a workload that inserts 1000
// and deletes 900 kept a tree shaped for 1000 -- node count and memory never
// recovered, and every query paid to traverse subdivisions that were empty.
//
// Collapsing pulls the subtree's items up and drops the children. It is the exact
// inverse of the split rule in insert_into, so the shape after
// insert-then-delete-back-down matches the shape of a fresh tree with the same
// contents. bench/results/index_churn.csv measures the node count and query time
// with and without it -- it was measured before being kept, not assumed.
bool try_collapse(Quadtree::Node* n, size_t cap) {
  if (n->leaf()) return false;
  if (count_upto(n, cap) > cap) return false;
  std::vector<std::pair<ZoneId, geo::Bbox>> all;
  gather(n, all);
  for (int i = 0; i < 4; ++i) n->kids[i].reset();
  n->items = std::move(all);
  return true;
}

bool remove_node(Quadtree::Node* n, ZoneId id, size_t cap) {
  for (size_t i = 0; i < n->items.size(); ++i)
    if (n->items[i].first == id) {
      n->items.erase(n->items.begin() + long(i));
      try_collapse(n, cap);
      return true;
    }
  if (!n->leaf())
    for (int i = 0; i < 4; ++i)
      if (remove_node(n->kids[i].get(), id, cap)) {
        try_collapse(n, cap);          // unwinding: collapse from the bottom up
        return true;
      }
  return false;
}

void walk(const Quadtree::Node* n, size_t depth, size_t& nodes, size_t& maxd,
          std::vector<geo::Bbox>* boxes) {
  ++nodes;
  maxd = std::max(maxd, depth);
  if (boxes) boxes->push_back(n->region);
  if (!n->leaf())
    for (int i = 0; i < 4; ++i) walk(n->kids[i].get(), depth + 1, nodes, maxd, boxes);
}

}  // namespace

void Quadtree::build(const std::vector<std::pair<ZoneId, geo::Bbox>>& items) {
  root_ = std::make_unique<Node>();
  count_ = 0;

  // Fit the root to the DATA, not to the whole planet.
  //
  // A world-rooted quadtree (-90..90, -180..180) burns about 11 levels of depth
  // before its cells are even district-sized, so with max_depth=12 there is almost
  // no useful subdivision left where the zones actually are. Fitting the root to
  // the data extent recovers all of that depth. The index overlay in the dashboard
  // is what made this visible -- the cells were kilometres across.
  if (items.empty()) {
    root_->region = {-90.0, -180.0, 90.0, 180.0};
    return;
  }
  geo::Bbox ext = geo::Bbox::empty();
  for (const auto& it : items) ext.expand(it.second);
  const double padlat = (ext.max_lat - ext.min_lat) * 0.02 + 1e-6;
  const double padlon = (ext.max_lon - ext.min_lon) * 0.02 + 1e-6;
  root_->region = {ext.min_lat - padlat, ext.min_lon - padlon,
                   ext.max_lat + padlat, ext.max_lon + padlon};

  for (const auto& it : items) insert(it.first, it.second);
}

// ── Root expansion  ──────────────────────────────────────────────────────────
//
// A root fitted to the initial data does not cover a later insert outside that
// extent. The previous fix was `root_->region.expand(box)` -- widen the rectangle
// in place. Queries stayed CORRECT (the root still contains everything, and items
// that fit no child stay at the root), but the tree stopped being a quadtree: the
// four children no longer tiled their parent, so a large region of the root's
// rectangle was covered by no child at all, and everything landing there piled up
// in the root's own item list. Pruning degraded silently toward a linear scan,
// and the subdivision drawn in the dashboard overlay was a lie about where the
// data was.
//
// This is the standard alternative: grow the root by DOUBLING, so the old root
// becomes exactly one quadrant of a new root. The subdivision invariant holds at
// every level, the old subtree is reused untouched (no rebuild, no renumbering --
// depth is not stored, see Node), and the loop terminates because each pass
// doubles both extents, so the number of expansions is logarithmic in how far
// outside the box lies.
void Quadtree::expand_root_to_cover(const geo::Bbox& box) {
  // A degenerate root (a single point, from a one-item build) can never grow to
  // cover anything by doubling zero. Give it a real extent first.
  if (root_->region.max_lat <= root_->region.min_lat ||
      root_->region.max_lon <= root_->region.min_lon) {
    if (count_ == 0) {
      const double padlat = (box.max_lat - box.min_lat) * 0.02 + 1e-6;
      const double padlon = (box.max_lon - box.min_lon) * 0.02 + 1e-6;
      root_->region = {box.min_lat - padlat, box.min_lon - padlon,
                       box.max_lat + padlat, box.max_lon + padlon};
      return;
    }
    root_->region.max_lat = root_->region.min_lat + 1e-6;
    root_->region.max_lon = root_->region.min_lon + 1e-6;
  }

  int guard = 0;
  while (!fully_contains(root_->region, box) && guard++ < 64) {
    const geo::Bbox& r = root_->region;
    const double h = r.max_lat - r.min_lat, w = r.max_lon - r.min_lon;

    // Grow toward the side the box overflows. When it overflows both (or
    // neither, which cannot happen here), grow north/east by convention so the
    // choice is deterministic.
    const bool north = box.max_lat > r.max_lat || !(box.min_lat < r.min_lat);
    const bool east  = box.max_lon > r.max_lon || !(box.min_lon < r.min_lon);

    geo::Bbox nr;
    nr.min_lat = north ? r.min_lat : r.min_lat - h;
    nr.max_lat = north ? r.max_lat + h : r.max_lat;
    nr.min_lon = east ? r.min_lon : r.min_lon - w;
    nr.max_lon = east ? r.max_lon + w : r.max_lon;

    auto nroot = std::make_unique<Node>();
    nroot->region = nr;
    subdivide(nroot.get());
    // Which quadrant of the new root is the old root? If we grew north the old
    // root is the southern half; if we grew east it is the western half.
    const int slot = (north ? 0 : 2) + (east ? 0 : 1);
    nroot->kids[slot] = std::move(root_);
    root_ = std::move(nroot);
  }

  // Extremely defensive: if doubling somehow failed to cover the box (a
  // non-finite coordinate, say), fall back to widening so the index stays
  // CORRECT even in the pathological case. Correct-but-poorly-pruned beats
  // missing results.
  if (!fully_contains(root_->region, box)) root_->region.expand(box);
}

void Quadtree::insert(ZoneId id, const geo::Bbox& box) {
  if (!fully_contains(root_->region, box)) expand_root_to_cover(box);
  insert_into(root_.get(), id, box, cap_, max_depth_, 0);
  ++count_;
}

bool Quadtree::remove(ZoneId id) {
  if (remove_node(root_.get(), id, cap_)) { --count_; return true; }
  return false;
}

void Quadtree::query(const geo::Bbox& q, std::vector<ZoneId>& out) const {
  ++st_.queries;
  const size_t before = out.size();
  query_node(root_.get(), q, out);
  st_.candidates_returned += out.size() - before;
}

IndexStats Quadtree::stats() const {
  size_t nodes = 0, depth = 0;
  walk(root_.get(), 0, nodes, depth, nullptr);
  st_.node_count = nodes;
  st_.max_depth = depth;
  st_.bytes = nodes * sizeof(Node) + count_ * sizeof(std::pair<ZoneId, geo::Bbox>);
  return st_;
}
void Quadtree::reset_counters() { st_.queries = 0; st_.candidates_returned = 0; }

void Quadtree::collect_node_boxes(std::vector<geo::Bbox>& out) const {
  size_t nodes = 0, depth = 0;
  walk(root_.get(), 0, nodes, depth, &out);
}

geo::Bbox Quadtree::root_region() const { return root_->region; }

std::unique_ptr<SpatialIndex> make_index(IndexKind kind) {
  switch (kind) {
    case IndexKind::Quadtree: return std::make_unique<Quadtree>();
    case IndexKind::RTree: return std::make_unique<RTree>();
    case IndexKind::Geohash: return std::make_unique<Geohash>();
    default: return std::make_unique<BruteForceIndex>();
  }
}
const char* to_string(IndexKind k) {
  switch (k) {
    case IndexKind::BruteForce: return "brute-force";
    case IndexKind::Quadtree: return "quadtree";
    case IndexKind::RTree: return "r-tree";
    case IndexKind::Geohash: return "geohash";
  }
  return "?";
}

}  // namespace safetrail::index
