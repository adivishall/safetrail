#pragma once
// Sweep-line segment-intersection detection (Shamos–Hoey).  [GAP 10]
//
// Polygon::validate() answers "does this ring self-intersect?" by testing every
// pair of edges -- O(n^2). That is fine once per zone at authoring time, but for
// bulk validation of an imported boundary set it is the bottleneck. This is the
// sweep-line upgrade.
//
// A vertical line sweeps left to right. Segments enter the active set at their
// left endpoint and leave at their right; the active set is kept ordered by the
// y-coordinate at the sweep line. The insight (Shamos–Hoey): if any two segments
// cross, then just left of the leftmost crossing the two culprits are *adjacent*
// in that vertical order -- so it suffices to test a segment only against its
// immediate neighbours as it is inserted, and to test the two segments that
// become newly adjacent when one is removed. That is O(n) intersection tests
// instead of O(n^2), and it answers the existence question exactly.
//
// Complexity note, stated honestly: the active set here is a sorted std::vector,
// so insert/erase are O(n) and the whole thing is O(n^2) worst case -- but it
// performs only O(n) of the expensive segment-cross tests (vs O(n^2) for the
// all-pairs check), which is the cost that dominates. A balanced-BST active set is
// the drop-in that takes it to a true O(n log n); the algorithm above is unchanged.
#include <vector>
#include "safetrail/geo/point.hpp"
#include "safetrail/geo/polygon.hpp"

namespace safetrail::geo {

struct Segment { LatLon a, b; };

// True if any two segments in the set properly cross or touch. By default all
// pairs are eligible; pass `ring_adjacency = true` to treat the segments as a
// closed ring's edges (segment i spans vertex i to i+1) and exempt consecutive
// edges, which legitimately share a vertex -- matching Polygon::validate()'s rule.
bool any_intersection(const std::vector<Segment>& segs, bool ring_adjacency = false);

// Self-intersection of a polygon's outer ring, via the sweep. Returns the same
// verdict as Polygon::validate()'s SelfIntersecting check, faster.
bool polygon_self_intersects(const Polygon& poly);

}  // namespace safetrail::geo
