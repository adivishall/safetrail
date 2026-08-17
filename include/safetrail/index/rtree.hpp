#pragma once
// R-tree with quadratic node split (Guttman 1984).
//
// The second real index, and the interesting comparison against the quadtree.
// The structural difference that matters:
//
//   Quadtree partitions SPACE. Node regions are fixed by subdivision, so an item
//   straddling a split settles high in the tree regardless of how small it is.
//
//   R-tree partitions ITEMS. Node boxes are the tight envelope of whatever they
//   contain, so nothing is forced upward -- but boxes may overlap, which means a
//   query can descend several branches.
//
// Prediction to test: R-tree wins on irregular zone extents (real polygons),
// quadtree wins on uniform ones. bench/results/index_scaling.csv settles it.
//
// Complexity: query O(log n + k), insert O(log n) amortised, build O(n log n).
#include <memory>
#include "safetrail/index/spatial_index.hpp"

namespace safetrail::index {

class RTree final : public SpatialIndex {
 public:
  explicit RTree(size_t max_entries = 8);
  ~RTree();

  const char* name() const override { return "r-tree"; }
  void build(const std::vector<std::pair<ZoneId, geo::Bbox>>& items) override;
  void insert(ZoneId id, const geo::Bbox& box) override;
  bool remove(ZoneId id) override;
  void query(const geo::Bbox& q, std::vector<ZoneId>& out) const override;
  void nearest(const geo::LatLon& p, size_t k, std::vector<ZoneId>& out) const override;
  size_t size() const override { return count_; }
  IndexStats stats() const override;
  void reset_counters() override;

  struct Node;

 private:
  std::unique_ptr<Node> root_;
  size_t max_entries_, min_entries_, count_ = 0;
  mutable IndexStats st_{};
};

}  // namespace safetrail::index
