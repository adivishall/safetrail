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
// Complexity, stated honestly -- see docs/DATA_STRUCTURES.md for the full table:
//   build   O(n log n) expected on spatially spread data
//   query   O(log n + k) expected; O(n) worst case, when the boxes are large
//           enough that most of them settle at the root and no split separates
//           them. The pathological case is real, not theoretical: give every zone
//           the same bounding box and the tree degenerates to one node.
//   insert  O(log n) expected, plus an amortised O(log R) root expansion when the
//           box falls outside the current root (R = how far outside).
//   remove  O(n) worst case -- the id is found by searching, since there is no
//           id -> node map. Followed by an O(cap) collapse check per level.
//
// Root expansion and node collapse both matter for a long-running index and both
// used to be missing; the notes on Quadtree::expand_root_to_cover and try_collapse
// in the .cpp explain what went wrong without them.
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
  size_t size() const override { return count_; }
  IndexStats stats() const override;
  void reset_counters() override;

  // Exposed for web/js/diagnostics.js -- drawing the live subdivision over the
  // map is the clearest possible explanation of why this is fast.
  void collect_node_boxes(std::vector<geo::Bbox>& out) const;

  // The root's current extent. Exposed so the expansion tests can assert the
  // doubling behaviour directly rather than inferring it from query results.
  geo::Bbox root_region() const;

  // Structural audit. The invariant that makes query() correct is that every
  // item lies inside the region of the node that stores it -- then skipping a
  // subtree whose region misses the query cannot skip a match. Also checked:
  // children TILE their parent -- child i is exactly quadrant i of the
  // parent's region, which is what makes this a quadtree rather than some tree
  // of boxes (a doubling root expansion reuses the old root as one quadrant,
  // equal up to rounding) -- a node has either four children or none, and the
  // stored item count equals size(). Called after every operation by
  // tests/index/differential_test.cpp.
  bool check_invariants() const;

  struct Node;   // public only so the .cpp's free helper functions can name it

 private:
  std::unique_ptr<Node> root_;
  size_t cap_, max_depth_, count_ = 0;
  bool root_widened_ = false;      // expand_root_to_cover's last-resort path ran
  mutable IndexStats st_{};

  // Grow the root by doubling until it covers `box`, keeping the old root as one
  // quadrant of the new one. See the long note in the .cpp.
  void expand_root_to_cover(const geo::Bbox& box);
};

}  // namespace safetrail::index
