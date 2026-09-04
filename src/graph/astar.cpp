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
  // (f, node): same reason as Dijkstra's frontier -- a binary heap is unstable,
  // so equal-f entries must be ordered explicitly or the expansion order (and
  // the reported nodes_expanded, which the report compares against Dijkstra's)
  // becomes implementation-defined.
  bool operator<(const QItem& o) const {
    return f != o.f ? f < o.f : node < o.node;
  }
};
}  // namespace

bool heuristic_is_admissible(const RoadGraph& g) {
  for (size_t u = 0; u < g.node_count(); ++u)
    for (const auto& e : g.neighbors(NodeId(u))) {
      const double geometric = geo::distance_m(g.pos(NodeId(u)), g.pos(e.to));
      // Tolerance of a millimetre: weights written by save_file are rounded to
      // micron precision, so an exact >= would report a false violation on a
      // graph that merely made a round trip through a file.
      if (e.weight_m + 1e-3 < geometric) return false;
    }
  return true;
}

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
      } else if (e.weight_m > 0.0 && ng == best_g[size_t(e.to)] &&
                 cur.node < parent[size_t(e.to)]) {
        // The positive-weight guard is not cosmetic. Rewriting a parent on an
        // equal-cost path is only safe when the new parent is strictly closer
        // to the source; with a zero-weight edge two nodes can share a distance
        // and each become the other's parent, and path_to() then walks a cycle
        // forever. Road weights are positive, but add_edge() accepts zero, so
        // the invariant is enforced here rather than assumed.
        parent[size_t(e.to)] = cur.node;      // deterministic equal-cost parent
      }
    }
  }
  return out;   // target unreachable
}

}  // namespace safetrail::graph
