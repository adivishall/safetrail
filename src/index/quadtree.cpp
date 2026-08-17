#include "safetrail/index/quadtree.hpp"
#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/rtree.hpp"
#include <algorithm>

namespace safetrail::index {

struct Quadtree::Node {
  geo::Bbox region;
  size_t depth = 0;
  std::vector<std::pair<ZoneId, geo::Bbox>> items;
  std::unique_ptr<Node> kids[4];
  bool leaf() const { return !kids[0]; }
};

Quadtree::Quadtree(size_t cap, size_t max_depth) : cap_(cap), max_depth_(max_depth) {
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
void insert_into(Quadtree::Node* n, ZoneId id, const geo::Bbox& box,
                 size_t cap, size_t max_depth) {
  // Descend only if a single child fully contains the box.
  if (!n->leaf()) {
    for (int i = 0; i < 4; ++i)
      if (fully_contains(n->kids[i]->region, box))
        return insert_into(n->kids[i].get(), id, box, cap, max_depth);
    n->items.emplace_back(id, box);           // straddles a split -> stays here
    return;
  }
  n->items.emplace_back(id, box);
  if (n->items.size() > cap && n->depth < max_depth) {
    for (int i = 0; i < 4; ++i) {
      n->kids[i] = std::make_unique<Quadtree::Node>();
      n->kids[i]->region = quadrant(n->region, i);
      n->kids[i]->depth = n->depth + 1;
    }
    auto staying = std::move(n->items);
    n->items.clear();
    for (auto& it : staying) {
      bool moved = false;
      for (int i = 0; i < 4; ++i)
        if (fully_contains(n->kids[i]->region, it.second)) {
          insert_into(n->kids[i].get(), it.first, it.second, cap, max_depth);
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

bool remove_node(Quadtree::Node* n, ZoneId id) {
  for (size_t i = 0; i < n->items.size(); ++i)
    if (n->items[i].first == id) { n->items.erase(n->items.begin() + long(i)); return true; }
  if (!n->leaf()) for (int i = 0; i < 4; ++i) if (remove_node(n->kids[i].get(), id)) return true;
  return false;
}

void walk(const Quadtree::Node* n, size_t& nodes, size_t& depth,
          std::vector<geo::Bbox>* boxes) {
  ++nodes;
  depth = std::max(depth, n->depth);
  if (boxes) boxes->push_back(n->region);
  if (!n->leaf()) for (int i = 0; i < 4; ++i) walk(n->kids[i].get(), nodes, depth, boxes);
}

void collect_all(const Quadtree::Node* n,
                 std::vector<std::pair<ZoneId, geo::Bbox>>& out) {
  for (const auto& it : n->items) out.push_back(it);
  if (!n->leaf()) for (int i = 0; i < 4; ++i) collect_all(n->kids[i].get(), out);
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

void Quadtree::insert(ZoneId id, const geo::Bbox& box) {
  // Late inserts can fall outside a root fitted to the original data. Widening the
  // root keeps queries correct; it only costs a little pruning quality, and
  // tests/index/equivalence_test.cpp covers exactly this case.
  if (!fully_contains(root_->region, box)) root_->region.expand(box);
  insert_into(root_.get(), id, box, cap_, max_depth_);
  ++count_;
}

bool Quadtree::remove(ZoneId id) {
  if (remove_node(root_.get(), id)) { --count_; return true; }
  return false;
}

void Quadtree::query(const geo::Bbox& q, std::vector<ZoneId>& out) const {
  ++st_.queries;
  const size_t before = out.size();
  query_node(root_.get(), q, out);
  st_.candidates_returned += out.size() - before;
}

void Quadtree::nearest(const geo::LatLon& p, size_t k, std::vector<ZoneId>& out) const {
  ++st_.queries;
  // Expanding-radius search. Doubles until k found or the whole world is covered;
  // simple, and adequate because k is tiny (the adaptive sampler asks for k=1).
  double r = 500.0;
  std::vector<ZoneId> found;
  for (int attempt = 0; attempt < 20; ++attempt) {
    found.clear();
    query_node(root_.get(), geo::Bbox::around(p, r), found);
    if (found.size() >= k) break;
    r *= 2.0;
  }
  std::vector<std::pair<double, ZoneId>> d;
  std::vector<std::pair<ZoneId, geo::Bbox>> all;
  collect_all(root_.get(), all);
  for (ZoneId id : found)
    for (const auto& it : all)
      if (it.first == id) { d.emplace_back(it.second.min_distance_m(p), id); break; }
  std::sort(d.begin(), d.end());
  const size_t n = std::min(k, d.size());
  for (size_t i = 0; i < n; ++i) out.push_back(d[i].second);
  st_.candidates_returned += n;
}

IndexStats Quadtree::stats() const {
  size_t nodes = 0, depth = 0;
  walk(root_.get(), nodes, depth, nullptr);
  st_.node_count = nodes;
  st_.max_depth = depth;
  st_.bytes = nodes * sizeof(Node) + count_ * sizeof(std::pair<ZoneId, geo::Bbox>);
  return st_;
}
void Quadtree::reset_counters() { st_.queries = 0; st_.candidates_returned = 0; }

void Quadtree::collect_node_boxes(std::vector<geo::Bbox>& out) const {
  size_t nodes = 0, depth = 0;
  walk(root_.get(), nodes, depth, &out);
}

std::unique_ptr<SpatialIndex> make_index(IndexKind kind) {
  switch (kind) {
    case IndexKind::Quadtree: return std::make_unique<Quadtree>();
    case IndexKind::RTree: return std::make_unique<RTree>();
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
