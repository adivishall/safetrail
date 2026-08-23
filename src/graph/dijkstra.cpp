#include "safetrail/graph/dijkstra.hpp"

#include <algorithm>
#include "safetrail/ds/priority_queue.hpp"

namespace safetrail::graph {

std::vector<NodeId> ShortestPaths::path_to(NodeId target) const {
  std::vector<NodeId> path;
  if (target < 0 || size_t(target) >= dist.size() || !reachable(target)) return path;
  for (NodeId v = target; v != kNoNode; v = parent[size_t(v)]) path.push_back(v);
  std::reverse(path.begin(), path.end());
  return path;
}

namespace {
// Frontier entry. operator< orders by distance so ds::BinaryHeap (a min-heap on
// `<`) pops the closest unsettled node -- Dijkstra's invariant.
struct QItem {
  double dist;
  NodeId node;
  bool operator<(const QItem& o) const { return dist < o.dist; }
};

ShortestPaths run(const RoadGraph& g, NodeId source, NodeId target) {
  ShortestPaths sp;
  sp.source = source;
  sp.dist.assign(g.node_count(), kUnreachable);
  sp.parent.assign(g.node_count(), kNoNode);
  if (!g.valid(source)) return sp;

  ds::BinaryHeap<QItem> frontier;
  sp.dist[size_t(source)] = 0.0;
  frontier.push({0.0, source});

  while (!frontier.empty()) {
    const QItem cur = frontier.pop();
    // Stale entry: a shorter path to `node` was found after this was queued.
    if (cur.dist > sp.dist[size_t(cur.node)]) continue;
    ++sp.nodes_expanded;
    if (cur.node == target) break;   // early exit (kNoNode target never matches)

    for (const auto& e : g.neighbors(cur.node)) {
      const double nd = cur.dist + e.weight_m;
      if (nd < sp.dist[size_t(e.to)]) {
        sp.dist[size_t(e.to)]   = nd;
        sp.parent[size_t(e.to)] = cur.node;
        frontier.push({nd, e.to});
      }
    }
  }
  return sp;
}
}  // namespace

ShortestPaths dijkstra(const RoadGraph& g, NodeId source) {
  return run(g, source, kNoNode);
}

ShortestPaths dijkstra(const RoadGraph& g, NodeId source, NodeId target) {
  return run(g, source, target);
}

}  // namespace safetrail::graph
