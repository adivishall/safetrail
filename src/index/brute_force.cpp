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
  for (const auto& it : items_)
    if (it.second.intersects(q)) out.push_back(it.first);
  st_.candidates_returned += out.size();
}

void BruteForceIndex::nearest(const geo::LatLon& p, size_t k,
                              std::vector<ZoneId>& out) const {
  ++st_.queries;
  std::vector<std::pair<double, ZoneId>> d;
  d.reserve(items_.size());
  for (const auto& it : items_) d.emplace_back(it.second.min_distance_m(p), it.first);
  const size_t n = std::min(k, d.size());
  std::partial_sort(d.begin(), d.begin() + long(n), d.end());
  for (size_t i = 0; i < n; ++i) out.push_back(d[i].second);
  st_.candidates_returned += n;
}

IndexStats BruteForceIndex::stats() const {
  st_.node_count = items_.size();
  st_.max_depth = 1;
  st_.bytes = items_.size() * sizeof(items_[0]);
  return st_;
}
void BruteForceIndex::reset_counters() { st_.queries = 0; st_.candidates_returned = 0; }

}  // namespace safetrail::index
