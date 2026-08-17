#pragma once
// Point-region quadtree over bounding boxes.
//
// Items are boxes, not points, so a box may straddle a quadrant split. The rule:
// an item descends only into a child that FULLY contains it, otherwise it is
// stored at the current node. That keeps every item in exactly one place -- no
// duplication, so remove() is unambiguous and query() cannot return duplicates.
// The cost is that large boxes settle near the root, which is correct and also
// exactly why the R-tree comparison is interesting.
//
// Complexity: build O(n log n), query O(log n + k), insert O(log n) expected.
#include <memory>
#include "safetrail/index/spatial_index.hpp"

namespace safetrail::index {

class Quadtree final : public SpatialIndex {
 public:
  explicit Quadtree(size_t node_capacity = 8, size_t max_depth = 12);
  ~Quadtree();

  const char* name() const override { return "quadtree"; }
  void build(const std::vector<std::pair<ZoneId, geo::Bbox>>& items) override;
  void insert(ZoneId id, const geo::Bbox& box) override;
  bool remove(ZoneId id) override;
  void query(const geo::Bbox& q, std::vector<ZoneId>& out) const override;
  void nearest(const geo::LatLon& p, size_t k, std::vector<ZoneId>& out) const override;
  size_t size() const override { return count_; }
  IndexStats stats() const override;
  void reset_counters() override;

  // Exposed for web/js/diagnostics.js -- drawing the live subdivision over the
  // map is the clearest possible explanation of why this is fast.
  void collect_node_boxes(std::vector<geo::Bbox>& out) const;

 private:
  public:
  struct Node;
 private:
  std::unique_ptr<Node> root_;
  size_t cap_, max_depth_, count_ = 0;
  mutable IndexStats st_{};
};

}  // namespace safetrail::index
