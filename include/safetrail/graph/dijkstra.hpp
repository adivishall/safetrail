#pragma once
// Dijkstra's single-source shortest paths on the road graph.
//
// Non-negative edge weights (road lengths are non-negative), so Dijkstra applies
// and gives O((V + E) log V) with a binary heap -- our hand-written ds::BinaryHeap
// (priority_queue.hpp). We use the "lazy deletion" variant: rather than a
// decrease-key, we push a fresh (dist, node) entry each time we relax and discard
// stale pops when a node's recorded distance is already better. That keeps the
// heap simple, is the standard competitive-programming form, and costs only a
// log factor in heap size (still O(E) entries, O(log E) = O(log V) per op).
//
// Results carry a parent array for path reconstruction and a nodes_expanded
// counter -- the latter is what the A* comparison in the report is measured on.
#include <cstdint>
#include <vector>
#include "safetrail/graph/road_graph.hpp"

namespace safetrail::graph {

constexpr double kUnreachable = 1e300;   // effectively +inf, but finite/printable

struct ShortestPaths {
  NodeId               source = kNoNode;
  std::vector<double>  dist;       // dist[v] = cost source->v, kUnreachable if none
  std::vector<NodeId>  parent;     // predecessor on a shortest path, kNoNode at source
  size_t               nodes_expanded = 0;  // settled nodes (popped with a fresh dist)

  bool reachable(NodeId v) const { return dist[size_t(v)] < kUnreachable; }

  // Reconstruct source -> target as a node list (empty if unreachable).
  std::vector<NodeId> path_to(NodeId target) const;
};

// Full single-source tree.
ShortestPaths dijkstra(const RoadGraph& g, NodeId source);

// Single source, single target with early exit the moment `target` settles.
// Same distances as the full run up to that point; cheaper when you need one pair.
ShortestPaths dijkstra(const RoadGraph& g, NodeId source, NodeId target);

}  // namespace safetrail::graph
