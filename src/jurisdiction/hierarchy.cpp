#include "safetrail/jurisdiction/hierarchy.hpp"

#include <cmath>

namespace safetrail::jurisdiction {

JurisdictionId Hierarchy::add(std::string name, geo::Polygon shape) {
  Node n;
  n.area = std::fabs(shape.signed_area());
  n.name = std::move(name);
  n.shape = std::move(shape);
  nodes_.push_back(std::move(n));
  return JurisdictionId(nodes_.size() - 1);
}

bool Hierarchy::strictly_contains(JurisdictionId outer, JurisdictionId inner) const {
  if (outer == inner) return false;
  const Node& o = nodes_[outer];
  const Node& i = nodes_[inner];
  if (o.area <= i.area) return false;                 // a container must be larger
  // Every vertex of the inner ring must lie inside (or on) the outer polygon.
  for (const auto& v : i.shape.outer())
    if (!geo::contains(o.shape, v)) return false;
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
