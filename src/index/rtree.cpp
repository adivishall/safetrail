#include "safetrail/index/rtree.hpp"
#include <algorithm>
#include <limits>

namespace safetrail::index {

struct RTree::Node {
  bool leaf = true;
  geo::Bbox box = geo::Bbox::empty();
  std::vector<std::pair<ZoneId, geo::Bbox>> entries;      // leaf payload
  std::vector<std::unique_ptr<Node>> kids;                // internal children

  void refit() {
    box = geo::Bbox::empty();
    for (const auto& e : entries) box.expand(e.second);
    for (const auto& k : kids) box.expand(k->box);
  }
  size_t fill() const { return leaf ? entries.size() : kids.size(); }
};

RTree::RTree(size_t max_entries)
    : max_entries_(max_entries < 4 ? 4 : max_entries),
      min_entries_((max_entries < 4 ? 4 : max_entries) / 2) {
  root_ = std::make_unique<Node>();
}
RTree::~RTree() = default;

static double enlargement(const geo::Bbox& have, const geo::Bbox& add) {
  geo::Bbox u = have; u.expand(add);
  return u.area() - have.area();
}

namespace {

// Quadratic split. Pick the two entries whose combined bounding box wastes the
// most space as seeds -- they are the pair most worth separating -- then assign
// the rest to whichever group they enlarge least.
template <typename T>
void quadratic_split(std::vector<T>& items, const geo::Bbox* (*boxof)(const T&),
                     std::vector<T>& g1, std::vector<T>& g2, size_t min_entries) {
  size_t s1 = 0, s2 = 1;
  double worst = -1.0;
  for (size_t i = 0; i < items.size(); ++i)
    for (size_t j = i + 1; j < items.size(); ++j) {
      geo::Bbox u = *boxof(items[i]); u.expand(*boxof(items[j]));
      const double d = u.area() - boxof(items[i])->area() - boxof(items[j])->area();
      if (d > worst) { worst = d; s1 = i; s2 = j; }
    }

  geo::Bbox b1 = *boxof(items[s1]), b2 = *boxof(items[s2]);
  g1.push_back(std::move(items[s1]));
  g2.push_back(std::move(items[s2]));

  std::vector<size_t> rest;
  for (size_t i = 0; i < items.size(); ++i) if (i != s1 && i != s2) rest.push_back(i);

  for (size_t idx : rest) {
    // Honour the minimum fill: if one group must take everything left to reach it,
    // stop choosing and just fill. Skipping this is how R-trees end up with
    // 1-entry nodes and a degenerate shape.
    const size_t remaining = rest.size() - (g1.size() - 1) - (g2.size() - 1);
    if (g1.size() + remaining <= min_entries) { b1.expand(*boxof(items[idx])); g1.push_back(std::move(items[idx])); continue; }
    if (g2.size() + remaining <= min_entries) { b2.expand(*boxof(items[idx])); g2.push_back(std::move(items[idx])); continue; }

    const double e1 = enlargement(b1, *boxof(items[idx]));
    const double e2 = enlargement(b2, *boxof(items[idx]));
    if (e1 < e2 || (e1 == e2 && b1.area() <= b2.area())) {
      b1.expand(*boxof(items[idx])); g1.push_back(std::move(items[idx]));
    } else {
      b2.expand(*boxof(items[idx])); g2.push_back(std::move(items[idx]));
    }
  }
}

const geo::Bbox* box_of_entry(const std::pair<ZoneId, geo::Bbox>& e) { return &e.second; }
const geo::Bbox* box_of_kid(const std::unique_ptr<RTree::Node>& n) { return &n->box; }

// Returns a new sibling if the node split.
std::unique_ptr<RTree::Node> do_insert(RTree::Node* n, ZoneId id, const geo::Bbox& box,
                                       size_t max_entries, size_t min_entries) {
  n->box.expand(box);

  if (n->leaf) {
    n->entries.emplace_back(id, box);
    if (n->entries.size() <= max_entries) return nullptr;
    std::vector<std::pair<ZoneId, geo::Bbox>> g1, g2;
    quadratic_split(n->entries, box_of_entry, g1, g2, min_entries);
    n->entries = std::move(g1);
    n->refit();
    auto sib = std::make_unique<RTree::Node>();
    sib->leaf = true;
    sib->entries = std::move(g2);
    sib->refit();
    return sib;
  }

  // ChooseSubtree: least enlargement, area as tiebreak.
  size_t best = 0;
  double best_e = std::numeric_limits<double>::infinity(), best_a = 0;
  for (size_t i = 0; i < n->kids.size(); ++i) {
    const double e = enlargement(n->kids[i]->box, box);
    const double a = n->kids[i]->box.area();
    if (e < best_e || (e == best_e && a < best_a)) { best_e = e; best_a = a; best = i; }
  }

  if (auto sib = do_insert(n->kids[best].get(), id, box, max_entries, min_entries)) {
    n->kids.push_back(std::move(sib));
    if (n->kids.size() <= max_entries) { n->refit(); return nullptr; }
    std::vector<std::unique_ptr<RTree::Node>> g1, g2;
    quadratic_split(n->kids, box_of_kid, g1, g2, min_entries);
    n->kids = std::move(g1);
    n->refit();
    auto up = std::make_unique<RTree::Node>();
    up->leaf = false;
    up->kids = std::move(g2);
    up->refit();
    return up;
  }
  n->refit();
  return nullptr;
}

void query_node(const RTree::Node* n, const geo::Bbox& q, std::vector<ZoneId>& out) {
  if (!n->box.intersects(q)) return;
  if (n->leaf) {
    for (const auto& e : n->entries) if (e.second.intersects(q)) out.push_back(e.first);
    return;
  }
  for (const auto& k : n->kids) query_node(k.get(), q, out);
}

bool remove_node(RTree::Node* n, ZoneId id) {
  if (n->leaf) {
    for (size_t i = 0; i < n->entries.size(); ++i)
      if (n->entries[i].first == id) {
        n->entries.erase(n->entries.begin() + long(i));
        n->refit();
        return true;
      }
    return false;
  }
  for (auto& k : n->kids)
    if (remove_node(k.get(), id)) { n->refit(); return true; }
  return false;
}

void walk(const RTree::Node* n, size_t depth, size_t& nodes, size_t& maxd) {
  ++nodes; maxd = std::max(maxd, depth);
  if (!n->leaf) for (const auto& k : n->kids) walk(k.get(), depth + 1, nodes, maxd);
}
void collect(const RTree::Node* n, std::vector<std::pair<ZoneId, geo::Bbox>>& out) {
  if (n->leaf) { for (const auto& e : n->entries) out.push_back(e); return; }
  for (const auto& k : n->kids) collect(k.get(), out);
}
}  // namespace

void RTree::build(const std::vector<std::pair<ZoneId, geo::Bbox>>& items) {
  root_ = std::make_unique<Node>();
  count_ = 0;
  for (const auto& it : items) insert(it.first, it.second);
}

void RTree::insert(ZoneId id, const geo::Bbox& box) {
  if (auto sib = do_insert(root_.get(), id, box, max_entries_, min_entries_)) {
    auto nr = std::make_unique<Node>();       // grow upward: the root splits
    nr->leaf = false;
    nr->kids.push_back(std::move(root_));
    nr->kids.push_back(std::move(sib));
    nr->refit();
    root_ = std::move(nr);
  }
  ++count_;
}

bool RTree::remove(ZoneId id) {
  if (remove_node(root_.get(), id)) { --count_; return true; }
  return false;
}

void RTree::query(const geo::Bbox& q, std::vector<ZoneId>& out) const {
  ++st_.queries;
  const size_t before = out.size();
  query_node(root_.get(), q, out);
  st_.candidates_returned += out.size() - before;
}

void RTree::nearest(const geo::LatLon& p, size_t k, std::vector<ZoneId>& out) const {
  ++st_.queries;
  std::vector<std::pair<ZoneId, geo::Bbox>> all;
  collect(root_.get(), all);
  std::vector<std::pair<double, ZoneId>> d;
  d.reserve(all.size());
  for (const auto& it : all) d.emplace_back(it.second.min_distance_m(p), it.first);
  const size_t n = std::min(k, d.size());
  std::partial_sort(d.begin(), d.begin() + long(n), d.end());
  for (size_t i = 0; i < n; ++i) out.push_back(d[i].second);
  st_.candidates_returned += n;
}

IndexStats RTree::stats() const {
  size_t nodes = 0, maxd = 0;
  walk(root_.get(), 0, nodes, maxd);
  st_.node_count = nodes;
  st_.max_depth = maxd;
  st_.bytes = nodes * sizeof(Node) + count_ * sizeof(std::pair<ZoneId, geo::Bbox>);
  return st_;
}
void RTree::reset_counters() { st_.queries = 0; st_.candidates_returned = 0; }

}  // namespace safetrail::index
