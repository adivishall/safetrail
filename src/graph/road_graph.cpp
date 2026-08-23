#include "safetrail/graph/road_graph.hpp"

#include <cfloat>

namespace safetrail::graph {

NodeId RoadGraph::add_node(geo::LatLon pos) {
  nodes_.push_back(pos);
  adj_.emplace_back();
  return NodeId(nodes_.size() - 1);
}

void RoadGraph::add_edge(NodeId u, NodeId v, double weight_m) {
  adj_[size_t(u)].push_back(Edge{v, weight_m});
  ++edge_count_;
}

void RoadGraph::add_road(NodeId u, NodeId v) {
  const double w = geo::distance_m(nodes_[size_t(u)], nodes_[size_t(v)]);
  add_edge(u, v, w);
  add_edge(v, u, w);
}

NodeId RoadGraph::nearest_node(geo::LatLon p) const {
  NodeId best = kNoNode;
  double best_d = DBL_MAX;
  for (size_t i = 0; i < nodes_.size(); ++i) {
    const double d = geo::distance_m(p, nodes_[i]);
    if (d < best_d) { best_d = d; best = NodeId(i); }
  }
  return best;
}

// Local, self-contained PRNG so the generator carries no dependency on sim::Rng
// and stays reproducible in isolation. splitmix64 -- a well-known, high-quality
// finaliser; overkill for jitter, but free and deterministic.
namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
  uint64_t next() {
    s += 0x9E3779B97F4A7C15ULL;
    uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  double unit() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }  // [0,1)
  double centered() { return unit() * 2.0 - 1.0; }                              // [-1,1)
};
}  // namespace

RoadGraph RoadGraph::grid(const geo::Bbox& area, int rows, int cols,
                          uint64_t seed, double jitter_frac,
                          double diagonal_probability) {
  RoadGraph g;
  if (rows < 1 || cols < 1) return g;

  Rng rng(seed);
  const double dlat = (area.max_lat - area.min_lat) / double(rows > 1 ? rows - 1 : 1);
  const double dlon = (area.max_lon - area.min_lon) / double(cols > 1 ? cols - 1 : 1);
  const double jlat = dlat * jitter_frac;
  const double jlon = dlon * jitter_frac;

  auto id = [cols](int r, int c) { return NodeId(r * cols + c); };

  // Place jittered lattice points. The jitter is bounded to < half a cell so the
  // grid never self-crosses -- neighbours stay neighbours.
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c) {
      geo::LatLon p{area.min_lat + r * dlat + rng.centered() * jlat * 0.5,
                    area.min_lon + c * dlon + rng.centered() * jlon * 0.5};
      g.add_node(p);
    }

  // 4-connected streets.
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c) {
      if (c + 1 < cols) g.add_road(id(r, c), id(r, c + 1));
      if (r + 1 < rows) g.add_road(id(r, c), id(r + 1, c));
    }

  // Reproducible diagonals -- a few shortcuts so A*'s heuristic has something to
  // exploit and greedy vs optimal dispatch can actually diverge.
  for (int r = 0; r + 1 < rows; ++r)
    for (int c = 0; c + 1 < cols; ++c)
      if (rng.unit() < diagonal_probability) {
        if (rng.unit() < 0.5) g.add_road(id(r, c),     id(r + 1, c + 1));
        else                  g.add_road(id(r, c + 1), id(r + 1, c));
      }

  return g;
}

}  // namespace safetrail::graph
