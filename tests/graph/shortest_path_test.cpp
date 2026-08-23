// Shortest paths: Dijkstra and A* against a Floyd-Warshall oracle.
//
// The oracle is the point. Floyd-Warshall is O(V^3) and dead simple to get right,
// so it is the brute-force truth that Dijkstra (the fast, fiddly one) is checked
// against -- the same "keep the slow correct thing as an oracle" discipline the
// spatial indexes use against brute force.
#include <vector>
#include "safetrail/graph/astar.hpp"
#include "safetrail/graph/dijkstra.hpp"
#include "safetrail/graph/road_graph.hpp"
#include "../test_harness.hpp"

using namespace safetrail;
using namespace safetrail::graph;

// A tiny xorshift so the test is self-contained and reproducible.
namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  double unit() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
  int below(int n) { return int(next() % uint64_t(n)); }
};

// All-pairs shortest paths, the O(V^3) way. Ground truth.
std::vector<std::vector<double>> floyd_warshall(const RoadGraph& g) {
  const size_t n = g.node_count();
  std::vector<std::vector<double>> d(n, std::vector<double>(n, kUnreachable));
  for (size_t i = 0; i < n; ++i) d[i][i] = 0.0;
  for (size_t u = 0; u < n; ++u)
    for (const auto& e : g.neighbors(NodeId(u)))
      d[u][size_t(e.to)] = std::min(d[u][size_t(e.to)], e.weight_m);
  for (size_t k = 0; k < n; ++k)
    for (size_t i = 0; i < n; ++i) {
      if (d[i][k] >= kUnreachable) continue;
      for (size_t j = 0; j < n; ++j)
        if (d[k][j] < kUnreachable && d[i][k] + d[k][j] < d[i][j])
          d[i][j] = d[i][k] + d[k][j];
    }
  return d;
}

// Sum the edge weights actually traversed by a reconstructed path, verifying
// every consecutive pair is a real edge. Returns -1 on a broken path.
double walk_cost(const RoadGraph& g, const std::vector<NodeId>& path) {
  if (path.empty()) return -1.0;
  double total = 0.0;
  for (size_t i = 0; i + 1 < path.size(); ++i) {
    double w = kUnreachable;
    for (const auto& e : g.neighbors(path[i]))
      if (e.to == path[i + 1]) { w = std::min(w, e.weight_m); }
    if (w >= kUnreachable) return -1.0;   // path uses a non-edge
    total += w;
  }
  return total;
}
}  // namespace

int main() {
  // ── 1. Dijkstra == Floyd-Warshall on random sparse digraphs ────────────────
  Rng rng(0xD1D1);
  for (int trial = 0; trial < 40; ++trial) {
    const int n = 4 + rng.below(24);
    RoadGraph g;
    for (int i = 0; i < n; ++i)
      g.add_node({25.5 + rng.unit() * 0.1, 91.8 + rng.unit() * 0.1});
    // Random directed edges with positive weights.
    const int m = n + rng.below(n * 3);
    for (int e = 0; e < m; ++e) {
      NodeId u = rng.below(n), v = rng.below(n);
      if (u != v) g.add_edge(u, v, 1.0 + rng.unit() * 100.0);
    }

    const auto oracle = floyd_warshall(g);
    for (int s = 0; s < n; ++s) {
      const auto sp = dijkstra(g, NodeId(s));
      bool all_match = true;
      for (int v = 0; v < n; ++v)
        if (std::fabs(sp.dist[size_t(v)] - oracle[size_t(s)][size_t(v)]) > 1e-6 &&
            !(sp.dist[size_t(v)] >= kUnreachable && oracle[size_t(s)][size_t(v)] >= kUnreachable))
          all_match = false;
      t::ok(all_match, "dijkstra matches floyd-warshall (trial " +
                       std::to_string(trial) + ", src " + std::to_string(s) + ")");
    }
  }

  // ── 2. On a synthetic road grid: reconstructed paths are real and optimal ──
  {
    geo::Bbox area{25.50, 91.80, 25.62, 91.95};
    RoadGraph g = RoadGraph::grid(area, 8, 8, /*seed=*/42);
    const auto sp = dijkstra(g, 0);
    bool paths_ok = true;
    for (NodeId v = 0; v < NodeId(g.node_count()); ++v) {
      if (!sp.reachable(v)) continue;
      const auto path = sp.path_to(v);
      t::ok(!path.empty() && path.front() == 0 && path.back() == v,
            "path endpoints correct");
      const double wc = walk_cost(g, path);
      if (v == 0) continue;
      if (wc < 0 || std::fabs(wc - sp.dist[size_t(v)]) > 1e-6) paths_ok = false;
    }
    t::ok(paths_ok, "reconstructed path cost == dijkstra distance, all edges real");
  }

  // ── 3. A* == Dijkstra distance, and never expands more nodes ───────────────
  {
    geo::Bbox area{25.50, 91.80, 25.62, 91.95};
    RoadGraph g = RoadGraph::grid(area, 10, 10, /*seed=*/7);
    Rng r2(0xA57A2F);
    int astar_leq = 0, trials = 30;
    for (int i = 0; i < trials; ++i) {
      NodeId s = r2.below(int(g.node_count()));
      NodeId d = r2.below(int(g.node_count()));
      const auto sp = dijkstra(g, s, d);
      const auto as = astar(g, s, d);
      const bool reach = sp.reachable(d);
      t::ok(reach == as.found, "A* and dijkstra agree on reachability");
      if (reach && as.found) {
        t::near(as.cost, sp.dist[size_t(d)], 1e-6, "A* cost == dijkstra cost");
        t::ok(!as.path.empty() && as.path.front() == s && as.path.back() == d,
              "A* path endpoints correct");
        t::near(walk_cost(g, as.path), as.cost, 1e-6, "A* path cost consistent");
        if (as.nodes_expanded <= sp.nodes_expanded) ++astar_leq;
      } else {
        ++astar_leq;   // vacuously fine when unreachable
      }
    }
    // The heuristic should never make A* do *more* work than Dijkstra here.
    t::ok(astar_leq == trials, "A* expands <= dijkstra on every query (guidance helps)");
  }

  return t::report("graph/shortest_path");
}
