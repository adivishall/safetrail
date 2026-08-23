#pragma once
// Road network -- a weighted directed graph stored as an adjacency list.
//
// The adjacency list is the right representation here and the textbook one: road
// networks are sparse (a junction has 2-4 exits, never V-1), so the O(V + E)
// space of an adjacency list beats the O(V^2) of a matrix by three orders of
// magnitude at city scale, and Dijkstra/A* iterate neighbours, which is exactly
// what a list makes cheap.
//
// Nodes carry a geographic position so that A* has a straight-line heuristic and
// so the dispatch layer can snap a responder's GPS fix to the nearest junction.
// Edge weight is metres of road (haversine length of the segment); swapping in
// travel time is a per-edge scalar change, nothing structural.
//
// Where does the graph come from? `data/osm/` is empty in this repo -- we did not
// ship a road extract -- so the honest default is `grid()`, a deterministic
// synthetic lattice over a bounding box. It is explicitly synthetic; it exists so
// the shortest-path and dispatch code has something realistic and reproducible to
// run and benchmark on. A real OSM adjacency list drops in behind the same
// interface via add_node/add_edge.
#include <cstdint>
#include <vector>
#include "safetrail/geo/bbox.hpp"
#include "safetrail/geo/point.hpp"

namespace safetrail::graph {

using NodeId = int32_t;
constexpr NodeId kNoNode = -1;

class RoadGraph {
 public:
  struct Edge {
    NodeId to;
    double weight_m;      // road length (or travel cost) along this segment
  };

  // ── construction ──────────────────────────────────────────────────────────
  NodeId add_node(geo::LatLon pos);

  // Directed edge u -> v with an explicit weight.
  void add_edge(NodeId u, NodeId v, double weight_m);

  // Undirected road: both directions, weight = great-circle length of the segment.
  // This is the normal way to build a road; add_edge is for one-way streets and
  // for tests that want a specific asymmetric weight.
  void add_road(NodeId u, NodeId v);

  // ── queries ───────────────────────────────────────────────────────────────
  size_t node_count() const { return nodes_.size(); }
  size_t edge_count() const { return edge_count_; }

  const std::vector<Edge>& neighbors(NodeId u) const { return adj_[size_t(u)]; }
  const geo::LatLon&       pos(NodeId u)       const { return nodes_[size_t(u)]; }
  bool valid(NodeId u) const { return u >= 0 && size_t(u) < nodes_.size(); }

  // Snap a free position to the closest junction. Linear O(V) -- fine for the
  // dispatch cost matrix, and the documented place a k-d tree would slot in
  // (see docs/DATA_STRUCTURES.md, the nearest-responder row).
  NodeId nearest_node(geo::LatLon p) const;

  // ── synthetic generator ───────────────────────────────────────────────────
  // A jittered rows x cols lattice over `area`, 4-connected (grid streets) plus a
  // reproducible sprinkling of diagonals so it is not a perfect manifold. Fully
  // deterministic in `seed`: the same seed yields byte-identical graphs across
  // platforms, which the golden tests and benchmarks rely on.
  static RoadGraph grid(const geo::Bbox& area, int rows, int cols,
                        uint64_t seed = 1, double jitter_frac = 0.25,
                        double diagonal_probability = 0.15);

 private:
  std::vector<geo::LatLon>        nodes_;
  std::vector<std::vector<Edge>>  adj_;
  size_t                          edge_count_ = 0;
};

}  // namespace safetrail::graph
