#include "safetrail/index/versioned_index.hpp"
#include <algorithm>

namespace safetrail::index {

// Immutable node. Children are shared_ptr<const Node> so any number of versions
// can point at the same subtree; nothing is ever mutated in place.
struct VersionedIndex::Node {
  geo::Bbox region;
  uint8_t depth = 0;
  std::vector<std::pair<ZoneId, geo::Bbox>> items;
  std::shared_ptr<const Node> kids[4];
  bool leaf() const { return !kids[0]; }
};

static constexpr size_t kCap = 8;
static constexpr uint8_t kMaxDepth = 14;

VersionedIndex::VersionedIndex() {
  auto root = std::make_shared<Node>();
  root->region = {-90.0, -180.0, 90.0, 180.0};
  ++nodes_allocated_;
  roots_.push_back(root);
  version_times_.push_back(0);        // version 0 = empty index at t=0
}
VersionedIndex::~VersionedIndex() = default;

static geo::Bbox quadrant(const geo::Bbox& r, int i) {
  const double mlat = (r.min_lat + r.max_lat) / 2, mlon = (r.min_lon + r.max_lon) / 2;
  switch (i) {
    case 0: return {r.min_lat, r.min_lon, mlat, mlon};
    case 1: return {r.min_lat, mlon, mlat, r.max_lon};
    case 2: return {mlat, r.min_lon, r.max_lat, mlon};
    default: return {mlat, mlon, r.max_lat, r.max_lon};
  }
}
static bool fully_contains(const geo::Bbox& o, const geo::Bbox& i) {
  return i.min_lat >= o.min_lat && i.max_lat <= o.max_lat &&
         i.min_lon >= o.min_lon && i.max_lon <= o.max_lon;
}

// ── Path copying ─────────────────────────────────────────────────────────────
//
// The heart of persistence. Returns a NEW node for this position; the three
// children we did not descend into are copied as shared_ptr, which is a refcount
// bump, not a deep copy. So one insert allocates O(depth) nodes and shares
// everything else with the previous version.
static std::shared_ptr<const VersionedIndex::Node> insert_copy(
    const VersionedIndex::Node* n, ZoneId id, const geo::Bbox& box, size_t& allocated) {
  using Node = VersionedIndex::Node;
  auto copy = std::make_shared<Node>();
  ++allocated;
  copy->region = n->region;
  copy->depth = n->depth;
  copy->items = n->items;
  for (int i = 0; i < 4; ++i) copy->kids[i] = n->kids[i];   // ← structural sharing

  if (!n->leaf()) {
    for (int i = 0; i < 4; ++i)
      if (fully_contains(copy->kids[i]->region, box)) {
        copy->kids[i] = insert_copy(copy->kids[i].get(), id, box, allocated);
        return copy;
      }
    copy->items.emplace_back(id, box);       // straddles a split, stays here
    return copy;
  }

  copy->items.emplace_back(id, box);
  if (copy->items.size() > kCap && copy->depth < kMaxDepth) {
    std::shared_ptr<Node> kids[4];
    for (int i = 0; i < 4; ++i) {
      kids[i] = std::make_shared<Node>();
      ++allocated;
      kids[i]->region = quadrant(copy->region, i);
      kids[i]->depth = uint8_t(copy->depth + 1);
    }
    std::vector<std::pair<ZoneId, geo::Bbox>> stay;
    for (const auto& it : copy->items) {
      bool moved = false;
      for (int i = 0; i < 4; ++i)
        if (fully_contains(kids[i]->region, it.second)) {
          kids[i]->items.push_back(it); moved = true; break;
        }
      if (!moved) stay.push_back(it);
    }
    copy->items = std::move(stay);
    for (int i = 0; i < 4; ++i) copy->kids[i] = kids[i];
  }
  return copy;
}

// Removal also path-copies: the old version must keep seeing the zone.
static std::shared_ptr<const VersionedIndex::Node> remove_copy(
    const VersionedIndex::Node* n, ZoneId id, size_t& allocated, bool& found) {
  using Node = VersionedIndex::Node;
  auto copy = std::make_shared<Node>();
  ++allocated;
  copy->region = n->region;
  copy->depth = n->depth;
  copy->items = n->items;
  for (int i = 0; i < 4; ++i) copy->kids[i] = n->kids[i];

  for (size_t i = 0; i < copy->items.size(); ++i)
    if (copy->items[i].first == id) {
      copy->items.erase(copy->items.begin() + long(i));
      found = true;
      return copy;
    }
  if (!copy->leaf())
    for (int i = 0; i < 4; ++i) {
      bool f = false;
      auto nk = remove_copy(copy->kids[i].get(), id, allocated, f);
      if (f) { copy->kids[i] = nk; found = true; return copy; }
    }
  return copy;
}

static void query_node(const VersionedIndex::Node* n, const geo::Bbox& q,
                       std::vector<ZoneId>& out) {
  if (!n->region.intersects(q)) return;
  for (const auto& it : n->items) if (it.second.intersects(q)) out.push_back(it.first);
  if (!n->leaf()) for (int i = 0; i < 4; ++i) query_node(n->kids[i].get(), q, out);
}
static void count_node(const VersionedIndex::Node* n, size_t& items, size_t& nodes) {
  ++nodes; items += n->items.size();
  if (!n->leaf()) for (int i = 0; i < 4; ++i) count_node(n->kids[i].get(), items, nodes);
}

VersionId VersionedIndex::commit(std::shared_ptr<const Node> root, Timestamp at) {
  roots_.push_back(std::move(root));
  version_times_.push_back(at);
  return VersionId(roots_.size() - 1);
}

VersionId VersionedIndex::add_zone(ZoneId id, const geo::Bbox& box, Validity v, Timestamp at) {
  if (validity_.size() <= id) { validity_.resize(id + 1); live_.resize(id + 1, 0); }
  validity_[id] = v;
  live_[id] = 1;
  validity_tree_.insert(v.from, v.to, id);
  auto root = insert_copy(roots_.back().get(), id, box, nodes_allocated_);
  const VersionId ver = commit(std::move(root), at);
  changelog_.push_back({ver, at, id, Change::Kind::Added});
  return ver;
}

VersionId VersionedIndex::remove_zone(ZoneId id, Timestamp at) {
  bool found = false;
  auto root = remove_copy(roots_.back().get(), id, nodes_allocated_, found);
  const VersionId ver = commit(std::move(root), at);
  if (id < live_.size()) live_[id] = 0;
  if (id < validity_.size()) validity_tree_.remove(validity_[id].from, validity_[id].to, id);
  changelog_.push_back({ver, at, id, Change::Kind::Removed});
  return ver;
}

VersionId VersionedIndex::update_validity(ZoneId id, Validity v, Timestamp at) {
  if (id < validity_.size()) {
    validity_tree_.remove(validity_[id].from, validity_[id].to, id);
    validity_[id] = v;
    validity_tree_.insert(v.from, v.to, id);
  }
  // Geometry is unchanged, so the new version SHARES the entire tree with the
  // previous one -- zero new nodes. Exactly the case persistence is good at.
  const VersionId ver = commit(roots_.back(), at);
  changelog_.push_back({ver, at, id, Change::Kind::ValidityChanged});
  return ver;
}

VersionId VersionedIndex::version_at(Timestamp t) const {
  // Latest version created at or before t.
  auto it = std::upper_bound(version_times_.begin(), version_times_.end(), t);
  if (it == version_times_.begin()) return 0;
  return VersionId((it - version_times_.begin()) - 1);
}

void VersionedIndex::query_at(Timestamp t, const geo::Bbox& box,
                              std::vector<ZoneId>& out) const {
  const VersionId v = version_at(t);
  std::vector<ZoneId> spatial;
  query_node(roots_[v].get(), box, spatial);
  for (ZoneId id : spatial)
    if (id < validity_.size() && validity_[id].active_at(t)) out.push_back(id);
}

void VersionedIndex::query_now(const geo::Bbox& box, std::vector<ZoneId>& out) const {
  query_node(roots_.back().get(), box, out);
}

void VersionedIndex::active_at(Timestamp t, std::vector<ZoneId>& out) const {
  validity_tree_.stabbing(t, out);
}

std::vector<VersionedIndex::Change> VersionedIndex::history_for(ZoneId id) const {
  std::vector<Change> v;
  for (const auto& c : changelog_) if (c.zone == id) v.push_back(c);
  return v;
}
std::vector<VersionedIndex::Change> VersionedIndex::changes_between(Timestamp f,
                                                                   Timestamp t) const {
  std::vector<Change> v;
  for (const auto& c : changelog_) if (c.at >= f && c.at < t) v.push_back(c);
  return v;
}

VersionId VersionedIndex::latest_version() const { return VersionId(roots_.size() - 1); }
size_t VersionedIndex::version_count() const { return roots_.size(); }

size_t VersionedIndex::zone_count_at(Timestamp t) const {
  size_t items = 0, nodes = 0;
  count_node(roots_[version_at(t)].get(), items, nodes);
  return items;
}

VersionedIndex::ShareStats VersionedIndex::share_stats() const {
  ShareStats s;
  s.total_nodes_allocated = nodes_allocated_;
  // What a naive "copy the whole tree per version" scheme would have cost.
  for (const auto& r : roots_) {
    size_t items = 0, nodes = 0;
    count_node(r.get(), items, nodes);
    s.nodes_if_full_copies += nodes;
  }
  return s;
}

}  // namespace safetrail::index
