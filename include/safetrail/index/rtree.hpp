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
// Complexity, stated honestly -- full table in docs/DATA_STRUCTURES.md:
//   query   O(log n + k) expected. WORST CASE O(n): node boxes may overlap, so a
//           query can be forced down several branches, and with pathological data
//           (every box covering the query point) down all of them. This is the
//           structural difference from a quadtree, not a defect.
//   insert  O(log n) expected, amortised over splits.
//   build   O(n log n), via STR bulk packing (see the .cpp) -- not by repeated
//           insertion. build_incremental() keeps the insertion-built version so
//           the two can be compared on tree quality and query time.
//   remove  O(n) to find the id (no id -> leaf map), then O(log n) to condense,
//           plus reinsertion of any orphaned entries.
#include <memory>
#include "safetrail/index/spatial_index.hpp"

namespace safetrail::index {

class RTree final : public SpatialIndex {
 public:
  explicit RTree(size_t max_entries = 8);
  ~RTree();

  const char* name() const override { return "r-tree"; }
  // STR bulk packing. See the .cpp for why this beats repeated insertion.
  void build(const std::vector<std::pair<ZoneId, geo::Bbox>>& items) override;

  // The old build: n calls to insert(). Kept as the comparison baseline for the
  // bulk-load benchmark -- "STR is better" is a claim that needs a measurement
  // next to it, and that needs both implementations to still exist.
  void build_incremental(const std::vector<std::pair<ZoneId, geo::Bbox>>& items);
  void insert(ZoneId id, const geo::Bbox& box) override;
  bool remove(ZoneId id) override;
  void query(const geo::Bbox& q, std::vector<ZoneId>& out) const override;
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
