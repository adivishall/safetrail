#pragma once
// The O(n) baseline. NEVER DELETED.
//
// Two jobs, both permanent:
//   1. Correctness oracle. tests/index/equivalence_test.cpp asserts every other
//      index returns identical results to this on randomised input. Without it, a
//      benchmark can compare a correct slow thing against a fast wrong thing.
//   2. The denominator in every speedup figure we report.
#include "safetrail/index/spatial_index.hpp"

namespace safetrail::index {

class BruteForceIndex final : public SpatialIndex {
 public:
  const char* name() const override { return "brute-force"; }
  void build(const std::vector<std::pair<ZoneId, geo::Bbox>>& items) override;
  void insert(ZoneId id, const geo::Bbox& box) override;
  bool remove(ZoneId id) override;
  void query(const geo::Bbox& q, std::vector<ZoneId>& out) const override;
  void nearest(const geo::LatLon& p, size_t k, std::vector<ZoneId>& out) const override;
  size_t size() const override { return items_.size(); }
  IndexStats stats() const override;
  void reset_counters() override;

 private:
  std::vector<std::pair<ZoneId, geo::Bbox>> items_;
  mutable IndexStats st_{};
};

}  // namespace safetrail::index
