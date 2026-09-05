#pragma once
// Sweep-line segment-intersection detection (Shamos–Hoey).  [GAP 10]
//
// Polygon::validate() used to answer "does this ring self-intersect?" by testing
// every pair of edges -- O(V^2). This is the sweep-line replacement, and it is
// wired in: validate() dispatches to it for any ring at or above
// geo::kSweepThresholdVertices and keeps the pairwise version below that, where
// V^2 is a handful of comparisons and the sweep's event sort and tree
// construction cost more than they save. See src/geo/polygon.cpp for the measured
// crossover. The pairwise version is not merely legacy -- it stays as the
// correctness oracle, in the same spirit as BruteForceIndex.
//
// Why it matters here rather than being a nicety: a simplified OSM district
// boundary runs to several hundred vertices, the zone editor re-validates on
// every vertex drag, and validation is a correctness gate that runs before a
// polygon is allowed into the index.
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
// Complexity: the active set is a hand-written balanced BST (AVL, in the .cpp),
// giving O(log n) insert, erase, predecessor, and successor -- so the whole sweep
// is O((n+k) log n): O(n) events, each doing O(log n) tree work plus O(1)
// amortised neighbour-cross tests.
//
// A subtlety worth stating for the report: the BST is ordered by each segment's
// y-coordinate AT THE SWEEP LINE, which moves. Two ring-adjacent edges sharing a
// vertex reach the exact same y at the moment they meet -- a legitimate touch,
// not a crossing -- and naively evaluating the comparator exactly at that x can
// flip their recorded order, which corrupts later erase/neighbour queries (they
// search using the *current* comparator for a node placed under the *old* one).
// The fix is to evaluate ordering a hair before the sweep position rather than
// exactly at it, so a coincidental meeting point never flips a decision already
// baked into the tree's shape.
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

// Self-intersection of a single closed ring, via the sweep. This is the entry
// point Polygon::validate() calls for large rings -- outer ring and holes alike,
// which is why it takes a Ring rather than a Polygon. Verdict is identical to the
// O(V^2) pairwise reference (geo::ring_self_intersects_pairwise); the two are
// asserted equal on randomised rings in tests/geo/sweep_line_test.cpp, at sizes
// on both sides of the dispatch threshold.
bool ring_self_intersects_sweep(const Ring& r);

// Self-intersection of a polygon's outer ring. Thin wrapper over the above.
bool polygon_self_intersects(const Polygon& poly);

}  // namespace safetrail::geo
