#include "safetrail/index/brute_force.hpp"
#include <algorithm>

namespace safetrail::index {

void BruteForceIndex::build(const std::vector<std::pair<ZoneId, geo::Bbox>>& items) {
  items_ = items;
}
void BruteForceIndex::insert(ZoneId id, const geo::Bbox& box) {
  items_.emplace_back(id, box);
}
bool BruteForceIndex::remove(ZoneId id) {
  for (size_t i = 0; i < items_.size(); ++i)
    if (items_[i].first == id) { items_.erase(items_.begin() + long(i)); return true; }
  return false;
}

void BruteForceIndex::query(const geo::Bbox& q, std::vector<ZoneId>& out) const {
  ++st_.queries;
  // `out` is an accumulating buffer the caller reuses across a whole tick, so the
  // candidate count is what THIS query appended -- not the buffer's total length.
  // Adding out.size() here inflated the brute-force candidate count by everything
  // already in the buffer, which is the denominator of every pruning-ratio and
  // speedup figure the project reports. A measurement bug in the oracle is worse
  // than one in the thing being measured.
  const size_t before = out.size();
  for (const auto& it : items_)
    if (it.second.intersects(q)) out.push_back(it.first);
  st_.candidates_returned += out.size() - before;
}


IndexStats BruteForceIndex::stats() const {
  st_.node_count = items_.size();
  st_.max_depth = 1;
  st_.bytes = items_.size() * sizeof(items_[0]);
  return st_;
}
void BruteForceIndex::reset_counters() { st_.queries = 0; st_.candidates_returned = 0; }

}  // namespace safetrail::index
