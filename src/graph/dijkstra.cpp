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
//
// The node id is a TIE-BREAK, not decoration. A binary heap is not a stable
// container: sift_up/sift_down swap equal-priority elements in an order that
// depends on insertion history, so on a graph with equal-length edges (the
// synthetic grid is full of them) two runs could settle equidistant nodes in
// different orders and produce different -- equally optimal, but different --
// parent pointers, and therefore different paths. Everything downstream reads
// those paths: the dispatch plan, the golden replay, the A*-vs-Dijkstra
// comparison. Ordering on (distance, node) makes the output a pure function of
// the graph.
struct QItem {
  double dist;
  NodeId node;
  bool operator<(const QItem& o) const {
    return dist != o.dist ? dist < o.dist : node < o.node;
  }
};

ShortestPaths run(const RoadGraph& g, NodeId source, NodeId target) {
  ShortestPaths sp;
  sp.source = source;
  sp.dist.assign(g.node_count(), kUnreachable);
  sp.parent.assign(g.node_count(), kNoNode);
  // Invalid source: return the all-unreachable result rather than indexing out of
  // bounds. An invalid target is harmless -- it simply never matches the
  // early-exit comparison, so the run degrades to a full single-source tree.
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
      // Strict improvement relaxes; an equal-cost alternative path is resolved by
      // preferring the lower-numbered predecessor, so the parent tree -- and thus
      // the reconstructed path -- is deterministic on graphs with equal-weight
      // edges rather than "whichever we happened to reach first".
      if (nd < sp.dist[size_t(e.to)]) {
        sp.dist[size_t(e.to)]   = nd;
        sp.parent[size_t(e.to)] = cur.node;
        frontier.push({nd, e.to});
      } else if (e.weight_m > 0.0 && nd == sp.dist[size_t(e.to)] &&
                 cur.node < sp.parent[size_t(e.to)]) {
        // The positive-weight guard is not cosmetic. Rewriting a parent on an
        // equal-cost path is only safe when the new parent is strictly closer
        // to the source; with a zero-weight edge two nodes can share a distance
        // and each become the other's parent, and path_to() then walks a cycle
        // forever. Road weights are positive, but add_edge() accepts zero, so
        // the invariant is enforced here rather than assumed.
        sp.parent[size_t(e.to)] = cur.node;
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
