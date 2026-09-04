#include "safetrail/index/rtree.hpp"

#include <algorithm>
#include <cmath>
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

void collect(const RTree::Node* n, std::vector<std::pair<ZoneId, geo::Bbox>>& out) {
  if (n->leaf) { for (const auto& e : n->entries) out.push_back(e); return; }
  for (const auto& k : n->kids) collect(k.get(), out);
}

void query_node(const RTree::Node* n, const geo::Bbox& q, std::vector<ZoneId>& out) {
  if (!n->box.intersects(q)) return;
  if (n->leaf) {
    for (const auto& e : n->entries) if (e.second.intersects(q)) out.push_back(e.first);
    return;
  }
  for (const auto& k : n->kids) query_node(k.get(), q, out);
}

// ── Deletion with condensing (Guttman's CondenseTree, simplified) ────────────
//
// Removing an entry used to just erase it and refit the boxes. Correct, and
// progressively worse: nodes drained below the minimum fill were left in place,
// so after heavy churn the tree carried a scaffold of near-empty nodes that every
// query still had to descend. Node count never recovered and the fan-out that
// makes an R-tree fast quietly went away.
//
// Condensing: after the erase, unwind the path. Any node that has fallen below
// min_entries (and is not the root) is DETACHED from its parent; the entries
// beneath it are collected and reinserted from the top afterwards.
//
// Deliberate simplification, stated plainly. Guttman reinserts an orphaned NODE
// at its original level, preserving both the height balance and the work already
// spent grouping its contents. We flatten orphans to leaf entries and reinsert
// them one by one. That is a real cost -- O(m log n) instead of O(1) relinks per
// underflow, and it can reshape more of the tree than necessary -- but it removes
// the need to track and re-enter at a specific level, which is where the fiddly
// bugs in R-tree deletion live. At this project's scale (thousands of zones,
// deletions rare and batched) the trade is worth it, and the churn benchmark
// reports what it costs rather than leaving it as an assertion.
bool remove_node(RTree::Node* n, ZoneId id, size_t min_entries,
                 std::vector<std::pair<ZoneId, geo::Bbox>>& orphans) {
  if (n->leaf) {
    for (size_t i = 0; i < n->entries.size(); ++i)
      if (n->entries[i].first == id) {
        n->entries.erase(n->entries.begin() + long(i));
        n->refit();
        return true;
      }
    return false;
  }
  for (size_t i = 0; i < n->kids.size(); ++i) {
    if (!remove_node(n->kids[i].get(), id, min_entries, orphans)) continue;
    if (n->kids[i]->fill() < min_entries) {
      collect(n->kids[i].get(), orphans);            // rescue its contents
      n->kids.erase(n->kids.begin() + long(i));      // and drop the underfull node
    }
    n->refit();
    return true;
  }
  return false;
}

// ── STR bulk loading (Sort-Tile-Recursive, Leutenegger et al. 1997) ──────────
//
// Inserting n items one at a time builds a tree whose quality depends on
// insertion order: each ChooseSubtree decision is made with no knowledge of the
// items still to come, so early splits are guesses and the resulting node boxes
// overlap more than they need to. Overlap is what forces a query to descend
// several branches, so it is the thing that actually costs query time.
//
// STR uses the fact that a bulk build knows everything up front. To pack n items
// into P = ceil(n/M) leaves, arrange the leaves as a roughly square S x S grid of
// tiles (S = ceil(sqrt(P))): sort all items by centre longitude, cut into S
// vertical slices, sort each slice by centre latitude, and cut each into leaves
// of M. The result is a near-square tiling with far less overlap than incremental
// insertion produces. Then repeat the same procedure over those leaves' boxes to
// build the level above, until one node remains.
//
// Cost: O(n log n), dominated by the sorts -- the same order as n incremental
// inserts, but with a markedly better tree at the end. build() vs
// build_incremental() is a benchmark row (bench/results/index_build.csv), which
// is the point of keeping both.
//
// Sorts break ties on the id so the packing is deterministic; std::sort is not
// stable and equal centres are common in synthetic data.
template <typename T>
void str_slice_sort(std::vector<T>& v, size_t lo, size_t hi, bool by_lon,
                    const geo::Bbox* (*boxof)(const T&), ZoneId (*idof)(const T&)) {
  std::sort(v.begin() + long(lo), v.begin() + long(hi), [&](const T& a, const T& b) {
    const geo::LatLon ca = boxof(a)->center(), cb = boxof(b)->center();
    const double va = by_lon ? ca.lon : ca.lat;
    const double vb = by_lon ? cb.lon : cb.lat;
    if (va != vb) return va < vb;
    return idof(a) < idof(b);
  });
}

ZoneId id_of_entry(const std::pair<ZoneId, geo::Bbox>& e) { return e.first; }
ZoneId id_of_kid(const std::unique_ptr<RTree::Node>& n) {
  // A node's identity for tie-breaking is its smallest contained id, which is
  // stable regardless of how the level below was packed.
  ZoneId best = UINT32_MAX;
  std::vector<std::pair<ZoneId, geo::Bbox>> all;
  collect(n.get(), all);
  for (const auto& e : all) best = std::min(best, e.first);
  return best;
}

// One STR pass: group `items` into runs of at most M, in tile order.
template <typename T>
std::vector<std::vector<T>> str_partition(std::vector<T> items, size_t M,
                                          const geo::Bbox* (*boxof)(const T&),
                                          ZoneId (*idof)(const T&)) {
  const size_t n = items.size();
  const size_t P = (n + M - 1) / M;                        // leaves needed
  const size_t S = size_t(std::ceil(std::sqrt(double(P)))); // slices
  const size_t per_slice = (n + S - 1) / S;

  str_slice_sort(items, 0, n, /*by_lon=*/true, boxof, idof);

  std::vector<std::vector<T>> groups;
  for (size_t s = 0; s < n; s += per_slice) {
    const size_t e = std::min(n, s + per_slice);
    str_slice_sort(items, s, e, /*by_lon=*/false, boxof, idof);
    for (size_t i = s; i < e; i += M) {
      const size_t j = std::min(e, i + M);
      std::vector<T> g;
      g.reserve(j - i);
      for (size_t k = i; k < j; ++k) g.push_back(std::move(items[k]));
      groups.push_back(std::move(g));
    }
  }
  return groups;
}

void walk(const RTree::Node* n, size_t depth, size_t& nodes, size_t& maxd) {
  ++nodes; maxd = std::max(maxd, depth);
  if (!n->leaf) for (const auto& k : n->kids) walk(k.get(), depth + 1, nodes, maxd);
}
}  // namespace

void RTree::build_incremental(const std::vector<std::pair<ZoneId, geo::Bbox>>& items) {
  root_ = std::make_unique<Node>();
  count_ = 0;
  for (const auto& it : items) insert(it.first, it.second);
}

void RTree::build(const std::vector<std::pair<ZoneId, geo::Bbox>>& items) {
  root_ = std::make_unique<Node>();
  count_ = items.size();
  if (items.empty()) return;

  // Level 0: pack the entries into leaves.
  auto groups = str_partition(items, max_entries_, box_of_entry, id_of_entry);
  std::vector<std::unique_ptr<Node>> level;
  level.reserve(groups.size());
  for (auto& g : groups) {
    auto leaf = std::make_unique<Node>();
    leaf->leaf = true;
    leaf->entries = std::move(g);
    leaf->refit();
    level.push_back(std::move(leaf));
  }

  // Levels above: pack the previous level's nodes the same way.
  while (level.size() > 1) {
    auto parents = str_partition(std::move(level), max_entries_, box_of_kid, id_of_kid);
    level.clear();
    for (auto& g : parents) {
      auto node = std::make_unique<Node>();
      node->leaf = false;
      node->kids = std::move(g);
      node->refit();
      level.push_back(std::move(node));
    }
  }
  root_ = std::move(level.front());
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
  std::vector<std::pair<ZoneId, geo::Bbox>> orphans;
  if (!remove_node(root_.get(), id, min_entries_, orphans)) return false;
  --count_;

  // Root collapse: an internal root left with a single child is a level of
  // indirection every query pays for and nothing gains from.
  while (!root_->leaf && root_->kids.size() == 1) {
    auto only = std::move(root_->kids[0]);
    root_ = std::move(only);
  }
  // An internal root with no children at all became a leaf.
  if (!root_->leaf && root_->kids.empty()) {
    root_->leaf = true;
    root_->refit();
  }

  for (const auto& e : orphans) {
    insert(e.first, e.second);
    --count_;                     // insert() counts it; it was already counted
  }
  return true;
}

void RTree::query(const geo::Bbox& q, std::vector<ZoneId>& out) const {
  ++st_.queries;
  const size_t before = out.size();
  query_node(root_.get(), q, out);
  st_.candidates_returned += out.size() - before;
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
