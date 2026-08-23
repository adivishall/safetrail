#include "safetrail/graph/astar.hpp"

#include <algorithm>
#include "safetrail/ds/priority_queue.hpp"
#include "safetrail/graph/dijkstra.hpp"   // kUnreachable

namespace safetrail::graph {

namespace {
struct QItem {
  double f;        // g + h, the A* priority
  double g;        // cost so far (kept so we relax on true cost, not on f)
  NodeId node;
  bool operator<(const QItem& o) const { return f < o.f; }
};
}  // namespace

AStarResult astar(const RoadGraph& g, NodeId source, NodeId target) {
  AStarResult out;
  if (!g.valid(source) || !g.valid(target)) return out;

  const geo::LatLon goal = g.pos(target);
  auto h = [&](NodeId n) { return geo::distance_m(g.pos(n), goal); };

  std::vector<double> best_g(g.node_count(), kUnreachable);
  std::vector<NodeId> parent(g.node_count(), kNoNode);

  ds::BinaryHeap<QItem> frontier;
  best_g[size_t(source)] = 0.0;
  frontier.push({h(source), 0.0, source});

  while (!frontier.empty()) {
    const QItem cur = frontier.pop();
    if (cur.g > best_g[size_t(cur.node)]) continue;   // stale
    ++out.nodes_expanded;

    if (cur.node == target) {
      out.found = true;
      out.cost  = cur.g;
      for (NodeId v = target; v != kNoNode; v = parent[size_t(v)]) out.path.push_back(v);
      std::reverse(out.path.begin(), out.path.end());
      return out;
    }

    for (const auto& e : g.neighbors(cur.node)) {
      const double ng = cur.g + e.weight_m;
      if (ng < best_g[size_t(e.to)]) {
        best_g[size_t(e.to)] = ng;
        parent[size_t(e.to)] = cur.node;
        frontier.push({ng + h(e.to), ng, e.to});
      }
    }
  }
  return out;   // target unreachable
}

}  // namespace safetrail::graph
