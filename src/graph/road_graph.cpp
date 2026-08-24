#include "safetrail/graph/road_graph.hpp"

#include <cfloat>
#include <cstdio>

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

bool RoadGraph::save_file(const std::string& path) const {
  std::FILE* f = std::fopen(path.c_str(), "w");
  if (!f) return false;
  std::fprintf(f, "safetrail-roads 1\n%zu\n", nodes_.size());
  for (const auto& n : nodes_) std::fprintf(f, "%.9f %.9f\n", n.lat, n.lon);
  // Emit each undirected road once (u < v), dropping the mirrored back-edge.
  size_t undirected = 0;
  for (size_t u = 0; u < adj_.size(); ++u)
    for (const auto& e : adj_[u]) if (u < size_t(e.to)) ++undirected;
  std::fprintf(f, "%zu\n", undirected);
  for (size_t u = 0; u < adj_.size(); ++u)
    for (const auto& e : adj_[u]) if (u < size_t(e.to)) std::fprintf(f, "%zu %d\n", u, e.to);
  std::fclose(f);
  return true;
}

bool RoadGraph::load_file(const std::string& path, std::string* err) {
  std::FILE* f = std::fopen(path.c_str(), "r");
  if (!f) { if (err) *err = "cannot open " + path; return false; }
  auto fail = [&](const char* m) { if (err) *err = m; std::fclose(f); return false; };

  int version = 0;
  if (std::fscanf(f, "safetrail-roads %d", &version) != 1 || version != 1)
    return fail("bad header or version");

  nodes_.clear(); adj_.clear(); edge_count_ = 0;
  size_t nc = 0;
  if (std::fscanf(f, "%zu", &nc) != 1) return fail("bad node count");
  for (size_t i = 0; i < nc; ++i) {
    double lat = 0, lon = 0;
    if (std::fscanf(f, "%lf %lf", &lat, &lon) != 2) return fail("bad node line");
    add_node({lat, lon});
  }
  size_t ec = 0;
  if (std::fscanf(f, "%zu", &ec) != 1) return fail("bad edge count");
  for (size_t i = 0; i < ec; ++i) {
    long u = 0, v = 0;
    if (std::fscanf(f, "%ld %ld", &u, &v) != 2) return fail("bad edge line");
    if (!valid(NodeId(u)) || !valid(NodeId(v))) return fail("edge references a missing node");
    add_road(NodeId(u), NodeId(v));
  }
  std::fclose(f);
  return true;
}

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
