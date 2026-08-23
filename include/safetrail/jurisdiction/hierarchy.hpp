#pragma once
// Jurisdiction nesting hierarchy.  [GAP 11]
//
// Administrative boundaries nest: a state contains districts, a district contains
// blocks, a block contains a restricted forest compartment. An alert raised at a
// point is owned by the *deepest* jurisdiction covering it -- the block, not the
// state -- because that is who dispatches and who files the report.
//
// The structure is a containment forest built from raw polygons: each region's
// parent is the smallest region that strictly contains it. Once built, resolving
// a point to its owning jurisdiction is a walk down the tree -- O(depth) polygon
// tests instead of testing every region.
//
// Build is O(n^2 * V): for each pair, does one contain the other (an all-vertices
// point-in-polygon test, O(V)). Fine -- boundaries are authored rarely and number
// in the dozens, not millions. Hand-written: parents/children are index vectors,
// no std::map.
#include <cstdint>
#include <string>
#include <vector>
#include "safetrail/geo/containment.hpp"
#include "safetrail/geo/polygon.hpp"

namespace safetrail::jurisdiction {

using JurisdictionId = uint32_t;
constexpr JurisdictionId kNoJurisdiction = UINT32_MAX;

class Hierarchy {
 public:
  // Register a region. Ids are assigned in insertion order starting at 0.
  JurisdictionId add(std::string name, geo::Polygon shape);

  // Compute parent/child links from geometric nesting. Call after all add()s.
  void build();

  // The deepest (smallest) jurisdiction containing p, or kNoJurisdiction if none.
  // Walks down the tree: O(depth * V).
  JurisdictionId resolve(const geo::LatLon& p) const;

  // Tree navigation.
  JurisdictionId parent(JurisdictionId id) const;
  const std::vector<JurisdictionId>& children(JurisdictionId id) const;
  const std::vector<JurisdictionId>& roots() const { return roots_; }
  int depth(JurisdictionId id) const;   // 0 at a root

  const std::string& name(JurisdictionId id) const { return nodes_[id].name; }
  size_t size() const { return nodes_.size(); }

 private:
  struct Node {
    std::string    name;
    geo::Polygon   shape;
    double         area = 0.0;          // |signed area|, the nesting key
    JurisdictionId parent = kNoJurisdiction;
    std::vector<JurisdictionId> children;
  };

  std::vector<Node>           nodes_;
  std::vector<JurisdictionId> roots_;
  std::vector<JurisdictionId> empty_;   // returned for invalid ids

  // Does `outer` strictly contain `inner` (every inner vertex inside, larger area)?
  bool strictly_contains(JurisdictionId outer, JurisdictionId inner) const;
};

}  // namespace safetrail::jurisdiction
