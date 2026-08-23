#pragma once
// A* shortest path -- Dijkstra guided by an admissible heuristic.
//
// For a single source->target query, A* orders the frontier by f = g + h, where
// g is the cost so far and h is a lower bound on the cost remaining. Here h is the
// great-circle distance from a node to the target.
//
// Admissibility (why the answer is still optimal): every edge weight is the
// great-circle length of its segment, and the straight-line distance to the
// target can never exceed the length of any actual road path to it (triangle
// inequality on the sphere). So h never over-estimates -> A* returns the true
// shortest path, identical to Dijkstra's, while settling fewer nodes. The report
// compares nodes_expanded between the two: same distance, less work.
//
// If edge weights were travel *time* rather than distance, the heuristic would
// need dividing by the max road speed to stay admissible -- noted so the trade is
// explicit rather than a latent bug.
#include <vector>
#include "safetrail/graph/road_graph.hpp"

namespace safetrail::graph {

struct AStarResult {
  bool                found = false;
  double              cost  = 0.0;     // total path cost (valid only if found)
  std::vector<NodeId> path;            // source..target inclusive (empty if none)
  size_t              nodes_expanded = 0;
};

AStarResult astar(const RoadGraph& g, NodeId source, NodeId target);

}  // namespace safetrail::graph
