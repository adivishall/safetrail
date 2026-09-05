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
#include <memory>
#include <string>
#include <vector>
#include "safetrail/geo/bbox.hpp"
#include "safetrail/geo/point.hpp"
#include "safetrail/index/kd_tree.hpp"

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

  // Directed edge u -> v with an explicit weight. Rejects (returns false)
  // out-of-range node ids and weights that are negative, NaN, or infinite --
  // Dijkstra's correctness rests on non-negative finite weights, so an invalid
  // one must be refused at the boundary rather than silently producing a wrong
  // shortest path deep inside the frontier loop. See dijkstra.hpp.
  bool add_edge(NodeId u, NodeId v, double weight_m);

  // Undirected road: both directions, weight = great-circle length of the segment.
  // This is the normal way to build a road; add_edge is for one-way streets and
  // for tests that want a specific asymmetric weight.
  bool add_road(NodeId u, NodeId v);

  // ── queries ───────────────────────────────────────────────────────────────
  size_t node_count() const { return nodes_.size(); }
  size_t edge_count() const { return edge_count_; }

  const std::vector<Edge>& neighbors(NodeId u) const { return adj_[size_t(u)]; }
  const geo::LatLon&       pos(NodeId u)       const { return nodes_[size_t(u)]; }
  bool valid(NodeId u) const { return u >= 0 && size_t(u) < nodes_.size(); }

  // Snap a free position to the closest junction.
  //
  // Backed by the k-d tree (index/kd_tree.hpp), built lazily on first use and
  // invalidated by add_node. O(log V) expected per query instead of the O(V) scan
  // this used to do -- which mattered: building the dispatch cost matrix snaps
  // every responder and every incident, and the simulator re-snaps each time it
  // dispatches. Section 14 of `make bench` measures it: 10x at 64 junctions,
  // ~480x at 10,000.
  //
  // ── What "closest" means here ─────────────────────────────────────────────
  //
  // Closest in the k-d tree's metric -- the local tangent plane anchored at the
  // graph's centroid latitude, which is the same linearisation geo/projection.hpp
  // documents and budgets. NOT haversine. The distinction is invisible almost
  // always and is stated because it once was not: nearest_node_linear() scanned
  // with geo::distance_m, so on a 4096-node grid the "oracle" and the tree
  // disagreed on a handful of the 2000 probes -- two junctions that the plane and
  // the great circle rank differently, both of them right about their own
  // question. Both now use the tree's metric, so they agree node-for-node, and
  // the modelling difference is measured in its own right rather than showing up
  // as a phantom search bug. tests/graph/road_graph_io_test.cpp holds both.
  //
  // Ties: when two junctions are equidistant, the tree and the scan both break on
  // the lower NodeId. Determinism here is load-bearing -- a different snap
  // changes the whole dispatch plan, and the golden replay is compared byte for
  // byte.
  NodeId nearest_node(geo::LatLon p) const;

  // The O(V) reference. Kept permanently as the correctness oracle, in the same
  // spirit as BruteForceIndex. Uses the k-d tree's metric -- see the note above
  // for why an oracle that used a different one is not an oracle.
  NodeId nearest_node_linear(geo::LatLon p) const;

  // ── file I/O ──────────────────────────────────────────────────────────────
  // A plain-text road network, so a real OpenStreetMap extract (produced by
  // tools/osm_to_roads.py) can replace the synthetic grid behind the same
  // interface. Format:
  //   safetrail-roads 2
  //   <node_count>
  //   <lat> <lon>                 (x node_count)
  //   <edge_count>
  //   <u> <v> <weight_m>          (x edge_count; each line is ONE DIRECTED edge)
  //
  // Version 2 exists because version 1 was lossy in a way that quietly broke the
  // graph. v1 wrote one line per unordered pair and reloaded it with add_road(),
  // which re-derives the weight from the great-circle distance and inserts BOTH
  // directions. So a one-way street came back two-way, and any edge whose weight
  // was not its geometric length (a slow road, a travel-time edge, a test's
  // deliberately asymmetric pair) came back with a different number. Round-trip
  // an asymmetric graph through v1 and the shortest paths change.
  //
  // v2 writes every directed edge with its own weight, so save->load is
  // semantically identity. v1 files still load -- data/osm/roads.txt is one --
  // and are interpreted exactly as before, undirected with derived weights,
  // which is what they meant when they were written.
  static constexpr int kFileVersion = 2;
  bool save_file(const std::string& path) const;
  bool load_file(const std::string& path, std::string* err = nullptr);

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

  // Lazily built, invalidated whenever a node is added. mutable because
  // nearest_node() is logically const -- it observes the graph, it does not
  // change it -- and rebuilding a cache is the textbook case for mutable.
  mutable std::unique_ptr<index::KdTree<NodeId>> snap_index_;
  void ensure_snap_index() const;
};

}  // namespace safetrail::graph
