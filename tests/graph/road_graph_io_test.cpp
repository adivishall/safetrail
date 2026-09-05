// RoadGraph: file round-trip, edge validation, and k-d-tree snapping.
//
// The round-trip half exists because the v1 format was LOSSY in a way that
// changed answers rather than merely losing labels: it wrote one line per
// unordered pair and reloaded with add_road(), which re-derives the weight from
// geometry and inserts both directions. A one-way street came back two-way; a
// travel-cost edge came back as a distance. Shortest paths through the reloaded
// graph were therefore different from shortest paths through the original.
#include "../test_harness.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "safetrail/graph/astar.hpp"
#include "safetrail/graph/dijkstra.hpp"
#include "safetrail/graph/road_graph.hpp"
#include "safetrail/sim/mobility.hpp"

using namespace safetrail;
using namespace safetrail::graph;

static std::string tmp(const char* n) { return std::string("build/test_tmp_") + n; }

int main() {
  // ── a deliberately asymmetric graph ────────────────────────────────────────
  //
  // One-way streets and weights that are NOT the geometric length: exactly what
  // v1 could not represent.
  RoadGraph g;
  const NodeId a = g.add_node({25.5700, 91.8800});
  const NodeId b = g.add_node({25.5710, 91.8810});
  const NodeId c = g.add_node({25.5720, 91.8800});
  const NodeId d = g.add_node({25.5730, 91.8815});

  t::ok(g.add_road(a, b), "a<->b two-way");
  t::ok(g.add_edge(b, c, 500.0), "b->c one-way, weight 500");
  t::ok(g.add_edge(c, b, 1500.0), "c->b the other way, weight 1500 (asymmetric)");
  t::ok(g.add_edge(c, d, 42.0), "c->d one-way only");
  const size_t edges_before = g.edge_count();
  t::ok(edges_before == 5, "five directed edges");

  const std::string path = tmp("roads.txt");
  t::ok(g.save_file(path), "save_file writes");

  RoadGraph r;
  std::string err;
  t::ok(r.load_file(path, &err), "load_file reads it back: " + err);

  // ── the round trip is semantically exact ───────────────────────────────────
  t::ok(r.node_count() == g.node_count(), "node count survives");
  t::ok(r.edge_count() == g.edge_count(), "DIRECTED edge count survives");
  for (size_t i = 0; i < g.node_count(); ++i) {
    t::near(r.pos(NodeId(i)).lat, g.pos(NodeId(i)).lat, 1e-9, "node lat survives");
    t::near(r.pos(NodeId(i)).lon, g.pos(NodeId(i)).lon, 1e-9, "node lon survives");
  }

  // Direction: c->d must exist and d->c must NOT.
  {
    bool c_to_d = false, d_to_c = false;
    for (const auto& e : r.neighbors(c)) if (e.to == d) c_to_d = true;
    for (const auto& e : r.neighbors(d)) if (e.to == c) d_to_c = true;
    t::ok(c_to_d, "the one-way edge c->d survives");
    t::ok(!d_to_c, "and is NOT mirrored back into d->c (the v1 bug)");
  }

  // Weights: the asymmetric pair must stay asymmetric, with its own numbers.
  {
    double bc = -1, cb = -1;
    for (const auto& e : r.neighbors(b)) if (e.to == c) bc = e.weight_m;
    for (const auto& e : r.neighbors(c)) if (e.to == b) cb = e.weight_m;
    t::near(bc, 500.0, 1e-4, "b->c keeps its weight");
    t::near(cb, 1500.0, 1e-4, "c->b keeps its DIFFERENT weight");
  }

  // Shortest paths are the ultimate check: same distances, same routes.
  {
    const auto sp_a = dijkstra(g, a);
    const auto sp_b = dijkstra(r, a);
    size_t mismatched = 0;
    for (size_t v = 0; v < g.node_count(); ++v)
      if (std::fabs(sp_a.dist[v] - sp_b.dist[v]) > 1e-6) ++mismatched;
    t::ok(mismatched == 0, "shortest-path distances from every source are unchanged");
    t::ok(sp_a.path_to(d) == sp_b.path_to(d), "and the reconstructed route is identical");
  }

  // save -> load -> save must be byte-identical: the format has no free choices.
  {
    const std::string path2 = tmp("roads2.txt");
    t::ok(r.save_file(path2), "second save writes");
    std::FILE* f1 = std::fopen(path.c_str(), "rb");
    std::FILE* f2 = std::fopen(path2.c_str(), "rb");
    std::string s1, s2;
    char buf[4096]; size_t n;
    while (f1 && (n = std::fread(buf, 1, sizeof buf, f1)) > 0) s1.append(buf, n);
    while (f2 && (n = std::fread(buf, 1, sizeof buf, f2)) > 0) s2.append(buf, n);
    if (f1) std::fclose(f1);
    if (f2) std::fclose(f2);
    t::ok(!s1.empty() && s1 == s2, "save -> load -> save is byte-identical");
    std::remove(path2.c_str());
  }
  std::remove(path.c_str());

  // ── v1 files still load ────────────────────────────────────────────────────
  //
  // data/osm/roads.txt is a v1 file, so dropping v1 support would break the demo.
  // A v1 file MEANT undirected with derived weights, so reading it that way is
  // faithful; only writing that way lost information.
  {
    const std::string v1 = tmp("roads_v1.txt");
    std::FILE* f = std::fopen(v1.c_str(), "w");
    std::fprintf(f, "safetrail-roads 1\n3\n"
                    "25.570000000 91.880000000\n"
                    "25.571000000 91.881000000\n"
                    "25.572000000 91.880000000\n"
                    "2\n0 1\n1 2\n");
    std::fclose(f);
    RoadGraph legacy;
    std::string e2;
    t::ok(legacy.load_file(v1, &e2), "a version-1 file still loads: " + e2);
    t::ok(legacy.node_count() == 3, "with its nodes");
    t::ok(legacy.edge_count() == 4, "and its 2 roads expanded to 4 directed edges");
    double w = -1;
    for (const auto& e : legacy.neighbors(0)) if (e.to == 1) w = e.weight_m;
    t::near(w, geo::distance_m(legacy.pos(0), legacy.pos(1)), 1e-6,
            "v1 weights are re-derived from geometry, as v1 always meant");
    std::remove(v1.c_str());
  }

  // ── malformed files are refused, without damaging the loaded graph ─────────
  {
    struct Case { const char* what; const char* text; };
    const Case cases[] = {
        {"empty file", ""},
        {"wrong header", "not-safetrail 2\n0\n0\n"},
        {"unsupported version", "safetrail-roads 99\n0\n0\n"},
        {"truncated node list", "safetrail-roads 2\n3\n25.57 91.88\n"},
        {"edge to a missing node", "safetrail-roads 2\n1\n25.57 91.88\n1\n0 7 10\n"},
        {"missing edge weight", "safetrail-roads 2\n2\n25.57 91.88\n25.58 91.89\n1\n0 1\n"},
        {"negative weight", "safetrail-roads 2\n2\n25.57 91.88\n25.58 91.89\n1\n0 1 -5\n"},
        {"nan weight", "safetrail-roads 2\n2\n25.57 91.88\n25.58 91.89\n1\n0 1 nan\n"},
        {"latitude out of range", "safetrail-roads 2\n1\n999 91.88\n0\n"},
    };
    for (const auto& tc : cases) {
      const std::string p = tmp("roads_bad.txt");
      std::FILE* f = std::fopen(p.c_str(), "w");
      std::fprintf(f, "%s", tc.text);
      std::fclose(f);

      RoadGraph victim = RoadGraph::grid({25.50, 91.80, 25.62, 91.96}, 5, 5, 1);
      const size_t nodes_before = victim.node_count();
      const size_t edges_before2 = victim.edge_count();
      std::string e3;
      t::ok(!victim.load_file(p, &e3), std::string("refused: ") + tc.what);
      t::ok(victim.node_count() == nodes_before && victim.edge_count() == edges_before2,
            std::string("and left the existing graph intact: ") + tc.what);
      std::remove(p.c_str());
    }
  }

  // ── edge validation at the API boundary ────────────────────────────────────
  {
    RoadGraph v;
    const NodeId n0 = v.add_node({25.57, 91.88});
    const NodeId n1 = v.add_node({25.58, 91.89});
    t::ok(!v.add_edge(n0, 99, 10.0), "an out-of-range destination is rejected");
    t::ok(!v.add_edge(-1, n1, 10.0), "an out-of-range source is rejected");
    t::ok(!v.add_edge(n0, n1, -1.0), "a negative weight is rejected");
    t::ok(!v.add_edge(n0, n1, 1.0 / 0.0), "an infinite weight is rejected");
    t::ok(!v.add_edge(n0, n1, std::nan("")), "a NaN weight is rejected");
    t::ok(v.edge_count() == 0, "and none of them were added");
    t::ok(v.add_edge(n0, n1, 0.0), "a zero weight IS allowed (a coincident junction)");
  }

  // ── nearest_node: k-d tree vs the O(V) oracle ──────────────────────────────
  //
  // At several sizes, not one. This test used to run a single 40x40 grid and
  // passed while the tree and the scan were minimising DIFFERENT distances -- the
  // tree on the local tangent plane, the scan on haversine. At 1600 junctions no
  // query happened to fall where the two rank a pair differently; at 4096 several
  // did, and a benchmark found what the test did not. Both now use the tree's
  // metric (see road_graph.hpp), and the sweep over sizes is what keeps a
  // rare-at-small-n disagreement from hiding again.
  {
    sim::Rng rng(31337);
    for (int side : {8, 20, 40, 64, 100}) {
      RoadGraph big = RoadGraph::grid({25.50, 91.80, 25.62, 91.96}, side, side, 99);
      size_t mismatched = 0;
      for (int i = 0; i < 3000; ++i) {
        const geo::LatLon q{rng.range(25.48, 25.64), rng.range(91.78, 91.98)};
        if (big.nearest_node(q) != big.nearest_node_linear(q)) ++mismatched;
      }
      t::ok(mismatched == 0,
            "k-d tree == linear scan on 3000 queries over " +
                std::to_string(big.node_count()) + " junctions");
    }
  }

  // ── ...and what the tangent plane actually costs, measured ─────────────────
  //
  // The test above proves the SEARCH is right. It says nothing about the metric,
  // and pretending the two questions are one is what hid the bug. So measure the
  // modelling difference directly: how often does the plane's nearest junction
  // differ from the great circle's, and when it does, how much further is it?
  //
  // No threshold is asserted on the RATE -- it depends on junction density and
  // would be a number pretending to be a law. What is asserted is the thing that
  // matters operationally: when they differ, the two candidates are within a
  // couple of metres of each other, i.e. inside GPS noise, so the choice cannot
  // change a dispatch outcome in any way a dispatcher could observe.
  {
    // This exact configuration -- 64x64, graph seed 7, probe seed 0x5AAA over the
    // benchmark's box -- is the one that exhibits a disagreement, and it is
    // pinned rather than randomised so the case cannot quietly stop being
    // exercised. It is also how the bug was found: section 14 of `make bench`
    // runs it and reported "same node: NO".
    const geo::Bbox area{25.50, 91.83, 25.62, 91.95};
    RoadGraph g = RoadGraph::grid(area, 64, 64, 7);
    sim::Rng rng(0x5AAA);
    size_t differ = 0;
    double worst_extra_m = 0.0;
    const int probes = 4000;
    for (int i = 0; i < probes; ++i) {
      const geo::LatLon q{rng.range(area.min_lat, area.max_lat),
                          rng.range(area.min_lon, area.max_lon)};
      const NodeId plane = g.nearest_node(q);
      // The haversine nearest, computed here rather than in the graph: it is not
      // the question the engine asks, so it does not belong in the API.
      NodeId hav = kNoNode;
      double best = 1e300;
      for (size_t k = 0; k < g.node_count(); ++k) {
        const double d = geo::distance_m(q, g.pos(NodeId(k)));
        if (d < best) { best = d; hav = NodeId(k); }
      }
      if (plane != hav) {
        ++differ;
        worst_extra_m = std::fmax(worst_extra_m,
                                  geo::distance_m(q, g.pos(plane)) - best);
      }
    }
    // The case must actually occur, or the bound below is vacuous and the test is
    // theatre.
    t::ok(differ > 0,
          "the plane and the great circle do pick different junctions sometimes (" +
              std::to_string(differ) + "/" + std::to_string(probes) + ")");
    // ...and when they do, the difference is millimetres. THAT is the claim worth
    // making: the metric choice cannot change a dispatch outcome, so an oracle
    // built on the tree's own metric loses nothing operationally while gaining
    // the ability to detect a real search bug.
    t::ok(worst_extra_m < 1.0,
          "and the plane's choice is at most " + std::to_string(worst_extra_m) +
              " m further -- four orders of magnitude inside GPS noise");
  }

  {
    RoadGraph big = RoadGraph::grid({25.50, 91.80, 25.62, 91.96}, 40, 40, 99);
    sim::Rng rng(31337);

    // Deterministic across repeated calls (the index is built lazily on first use).
    RoadGraph fresh = RoadGraph::grid({25.50, 91.80, 25.62, 91.96}, 40, 40, 99);
    const geo::LatLon q{25.553, 91.881};
    const NodeId first = fresh.nearest_node(q);
    bool stable = true;
    for (int i = 0; i < 50; ++i) if (fresh.nearest_node(q) != first) stable = false;
    t::ok(stable, "repeated snaps return the same node");

    // Adding a node must invalidate the cached index, not serve a stale answer.
    const geo::LatLon exact{25.5531, 91.8812};
    const NodeId added = fresh.add_node(exact);
    t::ok(fresh.nearest_node(exact) == added,
          "a node added after the index was built is found (cache invalidated)");

    // An empty graph has no nearest node.
    RoadGraph none;
    t::ok(none.nearest_node({25.5, 91.8}) == kNoNode, "an empty graph snaps to kNoNode");
  }

  // ── A* admissibility is checked, not assumed ───────────────────────────────
  {
    RoadGraph geo_weighted = RoadGraph::grid({25.50, 91.80, 25.62, 91.96}, 12, 12, 5);
    t::ok(heuristic_is_admissible(geo_weighted),
          "a distance-weighted graph satisfies the A* heuristic's assumption");

    // Now break it: a travel-TIME edge whose weight is far below its length.
    RoadGraph time_weighted;
    const NodeId p0 = time_weighted.add_node({25.5000, 91.8000});
    const NodeId p1 = time_weighted.add_node({25.6000, 91.9000});
    time_weighted.add_edge(p0, p1, 1.0);            // 1 "cost unit" over ~14 km
    t::ok(!heuristic_is_admissible(time_weighted),
          "a graph whose weights are not distances is reported as inadmissible");
  }

  return t::report("graph/road_graph_io");
}
