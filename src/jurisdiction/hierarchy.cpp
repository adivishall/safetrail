#include "safetrail/jurisdiction/hierarchy.hpp"

#include <cmath>

#include "safetrail/geo/segment.hpp"

namespace safetrail::jurisdiction {

JurisdictionId Hierarchy::add(std::string name, geo::Polygon shape) {
  Node n;
  // OUTER area, deliberately. Nesting is a statement about outlines: a district
  // contains a block whether or not either has exempt enclaves punched out of it,
  // and using region area (which subtracts holes) would let a large region with
  // large holes rank as "smaller" than the block it contains and invert the tree.
  n.area = std::fabs(shape.outer_signed_area());
  n.name = std::move(name);
  n.shape = std::move(shape);
  nodes_.push_back(std::move(n));
  return JurisdictionId(nodes_.size() - 1);
}

// Containment of a REGION, not of a point set.
//
// The obvious test -- "is every vertex of the inner ring inside the outer?" -- is
// not sufficient, and the counterexample is not exotic. Take a C-shaped (concave)
// district and a block drawn as a long bar across the mouth of the C: both of the
// bar's ends can sit inside the two arms while its middle lies in the gap,
// entirely outside the district. Every vertex passes; the region is not contained.
//
// A region is contained iff BOTH hold:
//   1. every inner vertex is inside (or on) the outer polygon, and
//   2. no inner edge crosses the outer boundary.
// (1) alone admits the bar; (2) alone admits a disjoint region that never touches
// the outer ring. Together they are exactly containment for simple polygons:
// having no boundary crossing means the inner ring lies wholly in one face of the
// outer ring, and (1) identifies which face.
//
// Cost: O(V_inner * V_outer) edge tests instead of O(V_inner) point tests. Both
// are dominated by build()'s O(n^2) pairwise loop, and boundaries are authored in
// dozens, so the exact answer is affordable.
bool Hierarchy::strictly_contains(JurisdictionId outer, JurisdictionId inner) const {
  if (outer == inner) return false;
  const Node& o = nodes_[outer];
  const Node& i = nodes_[inner];
  if (o.area <= i.area) return false;                 // a container must be larger

  for (const auto& v : i.shape.outer())
    if (!geo::contains(o.shape, v)) return false;

  const auto& ir = i.shape.outer();
  const auto& orr = o.shape.outer();
  for (size_t a = 0; a < ir.size(); ++a)
    for (size_t b = 0; b < orr.size(); ++b)
      if (geo::segments_properly_cross(ir[a], ir[(a + 1) % ir.size()],
                                       orr[b], orr[(b + 1) % orr.size()]))
        return false;
  return true;
}

void Hierarchy::build() {
  roots_.clear();
  for (auto& n : nodes_) { n.parent = kNoJurisdiction; n.children.clear(); }

  // Parent = the smallest region that strictly contains this one.
  for (JurisdictionId i = 0; i < nodes_.size(); ++i) {
    JurisdictionId best = kNoJurisdiction;
    double best_area = 0.0;
    for (JurisdictionId j = 0; j < nodes_.size(); ++j) {
      if (!strictly_contains(j, i)) continue;
      if (best == kNoJurisdiction || nodes_[j].area < best_area) {
        best = j;
        best_area = nodes_[j].area;
      }
    }
    nodes_[i].parent = best;
  }

  for (JurisdictionId i = 0; i < nodes_.size(); ++i) {
    if (nodes_[i].parent == kNoJurisdiction) roots_.push_back(i);
    else nodes_[nodes_[i].parent].children.push_back(i);
  }
}

JurisdictionId Hierarchy::resolve(const geo::LatLon& p) const {
  // Descend from whichever root contains p, always into the child that also
  // contains p, until no deeper region does. Because regions nest by area, the
  // deepest containing region is the answer -- the owning jurisdiction.
  JurisdictionId current = kNoJurisdiction;

  // Find a containing root (siblings are disjoint in well-formed data; if several
  // match, take the smallest, which is the most specific).
  auto pick_containing = [&](const std::vector<JurisdictionId>& candidates) {
    JurisdictionId best = kNoJurisdiction;
    for (JurisdictionId c : candidates)
      if (geo::contains(nodes_[c].shape, p) &&
          (best == kNoJurisdiction || nodes_[c].area < nodes_[best].area))
        best = c;
    return best;
  };

  current = pick_containing(roots_);
  while (current != kNoJurisdiction) {
    const JurisdictionId deeper = pick_containing(nodes_[current].children);
    if (deeper == kNoJurisdiction) break;
    current = deeper;
  }
  return current;
}

JurisdictionId Hierarchy::parent(JurisdictionId id) const {
  return id < nodes_.size() ? nodes_[id].parent : kNoJurisdiction;
}

const std::vector<JurisdictionId>& Hierarchy::children(JurisdictionId id) const {
  return id < nodes_.size() ? nodes_[id].children : empty_;
}

int Hierarchy::depth(JurisdictionId id) const {
  int d = 0;
  for (JurisdictionId c = id; c < nodes_.size() && nodes_[c].parent != kNoJurisdiction;
       c = nodes_[c].parent)
    ++d;
  return d;
}

}  // namespace safetrail::jurisdiction
