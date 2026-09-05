#include "safetrail/graph/road_graph.hpp"

#include <cfloat>
#include <cmath>
#include <cstdio>

namespace safetrail::graph {

NodeId RoadGraph::add_node(geo::LatLon pos) {
  nodes_.push_back(pos);
  adj_.emplace_back();
  snap_index_.reset();                  // the snap index no longer covers the graph
  return NodeId(nodes_.size() - 1);
}

// Validated at the boundary rather than trusted. Dijkstra and A* both assume
// non-negative finite weights; a NaN weight in particular is corrosive, because
// every comparison against it is false, so it neither relaxes nor fails to relax
// and the resulting distances are silently wrong rather than obviously wrong.
bool RoadGraph::add_edge(NodeId u, NodeId v, double weight_m) {
  if (!valid(u) || !valid(v)) return false;
  if (!(weight_m >= 0.0) || std::isinf(weight_m)) return false;   // catches NaN too
  adj_[size_t(u)].push_back(Edge{v, weight_m});
  ++edge_count_;
  return true;
}

bool RoadGraph::add_road(NodeId u, NodeId v) {
  if (!valid(u) || !valid(v)) return false;
  const double w = geo::distance_m(nodes_[size_t(u)], nodes_[size_t(v)]);
  return add_edge(u, v, w) && add_edge(v, u, w);
}

void RoadGraph::ensure_snap_index() const {
  if (snap_index_ || nodes_.empty()) return;
  std::vector<index::KdTree<NodeId>::Item> items;
  items.reserve(nodes_.size());
  for (size_t i = 0; i < nodes_.size(); ++i) items.push_back({NodeId(i), nodes_[i]});
  snap_index_ = std::make_unique<index::KdTree<NodeId>>();
  snap_index_->build(std::move(items));
}

NodeId RoadGraph::nearest_node(geo::LatLon p) const {
  if (nodes_.empty()) return kNoNode;
  ensure_snap_index();
  NodeId out = kNoNode;
  return snap_index_->nearest(p, out) ? out : kNoNode;
}

// The O(V) reference implementation.
//
// Two things make it an exact oracle rather than an approximate one, and both
// were wrong here before:
//
//   METRIC     it scans with the k-d tree's own metric (the local tangent plane,
//              index/kd_tree.hpp), not with haversine. Scanning with haversine
//              compares two DIFFERENT questions: on a few-thousand-node grid some
//              query always has two junctions the plane and the great circle rank
//              differently, and the scan then "disagrees" with a tree that is
//              working perfectly. A benchmark over 2000 probes on a 4096-node
//              grid is what surfaced it.
//   TIES       strict `<` keeps the FIRST node seen at a given distance, and the
//              scan runs in ascending NodeId order, so ties break on the lower
//              id -- which is what the k-d tree's query does too.
//
// The two therefore agree node-for-node, which is what lets the test assert
// equality rather than "equal distance". What this does NOT test is the modelling
// approximation itself; that is measured separately in
// tests/graph/road_graph_io_test.cpp.
NodeId RoadGraph::nearest_node_linear(geo::LatLon p) const {
  if (nodes_.empty()) return kNoNode;
  ensure_snap_index();
  NodeId best = kNoNode;
  double best_d = DBL_MAX;
  for (size_t i = 0; i < nodes_.size(); ++i) {
    const double d = snap_index_->distance_m(p, nodes_[i]);
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
  std::fprintf(f, "safetrail-roads %d\n%zu\n", kFileVersion, nodes_.size());
  // %.9f on coordinates is ~0.1 mm; %.6f on a weight in metres is a micron. Both
  // are far finer than anything the data means, and both are exact enough that
  // save->load->save is byte-identical, which the round-trip test asserts.
  for (const auto& n : nodes_) std::fprintf(f, "%.9f %.9f\n", n.lat, n.lon);
  std::fprintf(f, "%zu\n", edge_count_);
  // One line per DIRECTED edge, carrying its own weight. See the format note in
  // road_graph.hpp for why v1's "one line per unordered pair" was lossy.
  for (size_t u = 0; u < adj_.size(); ++u)
    for (const auto& e : adj_[u])
      std::fprintf(f, "%zu %d %.6f\n", u, e.to, e.weight_m);
  std::fclose(f);
  return true;
}

bool RoadGraph::load_file(const std::string& path, std::string* err) {
  std::FILE* f = std::fopen(path.c_str(), "r");
  if (!f) { if (err) *err = "cannot open " + path; return false; }
  auto fail = [&](const char* m) { if (err) *err = m; std::fclose(f); return false; };

  int version = 0;
  if (std::fscanf(f, "safetrail-roads %d", &version) != 1)
    return fail("bad header");
  if (version != 1 && version != 2)
    return fail("unsupported road-file version");

  // Parse into a fresh graph and only adopt it on success, so a truncated or
  // malformed file leaves the caller's existing network untouched rather than
  // half-replaced.
  RoadGraph loaded;
  size_t nc = 0;
  if (std::fscanf(f, "%zu", &nc) != 1) return fail("bad node count");
  for (size_t i = 0; i < nc; ++i) {
    double lat = 0, lon = 0;
    if (std::fscanf(f, "%lf %lf", &lat, &lon) != 2) return fail("bad node line");
    if (!geo::LatLon{lat, lon}.valid()) return fail("node coordinates out of range");
    loaded.add_node({lat, lon});
  }
  size_t ec = 0;
  if (std::fscanf(f, "%zu", &ec) != 1) return fail("bad edge count");
  for (size_t i = 0; i < ec; ++i) {
    long u = 0, v = 0;
    if (std::fscanf(f, "%ld %ld", &u, &v) != 2) return fail("bad edge line");
    if (!loaded.valid(NodeId(u)) || !loaded.valid(NodeId(v)))
      return fail("edge references a missing node");
    if (version == 1) {
      // Legacy: undirected, weight re-derived from the geometry. That is exactly
      // what a v1 file meant when it was written, so reading it this way is
      // faithful -- it is only WRITING that way that lost information.
      if (!loaded.add_road(NodeId(u), NodeId(v))) return fail("bad legacy edge");
    } else {
      double w = 0;
      if (std::fscanf(f, "%lf", &w) != 1) return fail("bad edge weight");
      if (!loaded.add_edge(NodeId(u), NodeId(v), w))
        return fail("edge weight must be finite and non-negative");
    }
  }
  std::fclose(f);
  *this = std::move(loaded);
  snap_index_.reset();
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
