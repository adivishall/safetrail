// Determinism, across the whole engine.
//
// The README says determinism is non-negotiable. That is only a claim until
// something checks it, and the places it can quietly break are not obvious: a
// binary heap is not a stable container, std::nth_element gives no guarantee
// among equal elements, and std::sort is not stable either. So any structure that
// orders by a key with ties -- Dijkstra's frontier, A*'s frontier, the k-d tree's
// median partition and its nearest-neighbour candidates, the Hungarian
// assignment's equal-cost choices -- is free to return a DIFFERENT but equally
// valid answer between two builds, two standard libraries, or two optimisation
// levels. Every one of those answers feeds the golden replay.
//
// Each block below runs the same computation twice (or compares against an
// explicitly-tie-broken oracle) and requires the results to be identical, not
// merely equivalent.
#include "../test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "safetrail/alert/correlator.hpp"
#include "safetrail/dispatch/assigner.hpp"
#include "safetrail/graph/astar.hpp"
#include "safetrail/graph/bipartite_match.hpp"
#include "safetrail/graph/dijkstra.hpp"
#include "safetrail/graph/road_graph.hpp"
#include "safetrail/index/kd_tree.hpp"
#include "safetrail/index/quadtree.hpp"
#include "safetrail/sim/simulator.hpp"

using namespace safetrail;

int main() {
  // ── the whole simulation, twice ────────────────────────────────────────────
  //
  // Same seed, same config, two independent Simulator instances. Every event must
  // match field for field and in the same order -- this is the end-to-end claim,
  // and it subsumes all the structure-level ones below.
  {
    // Simulator owns unique_ptrs, so it is not copyable. Capture what we need to
    // compare instead of returning the object.
    struct Snapshot {
      std::vector<fence::Event> events;
      sim::Simulator::Summary summary;
      size_t dispatch_lines = 0;
    };
    auto run = [] {
      sim::SimConfig cfg;
      cfg.tourists = 25;
      cfg.groups = 4;
      cfg.duration_ms = 900000;              // 15 simulated minutes
      cfg.tick_ms = 1000;
      cfg.seed = 987654321;
      cfg.dispatch = true;
      cfg.responders = 6;
      cfg.roads_path.clear();                // synthetic grid: no file dependency
      sim::Simulator s(cfg);
      std::string err;
      s.load_zones("data/zones/shillong_osm.geojson", &err);
      s.spawn_tourists();
      s.run();
      return Snapshot{s.events(), s.summary(), s.dispatch_lines().size()};
    };

    const Snapshot a = run();
    const Snapshot b = run();

    t::ok(a.events.size() == b.events.size(),
          "two runs of the same seed produce the same number of events (" +
              std::to_string(a.events.size()) + ")");
    t::ok(!a.events.empty(), "and the run actually produced events");

    size_t differing = 0;
    const size_t n = std::min(a.events.size(), b.events.size());
    for (size_t i = 0; i < n; ++i) {
      const auto& x = a.events[i];
      const auto& y = b.events[i];
      if (x.kind != y.kind || x.tourist != y.tourist || x.zone != y.zone ||
          x.t_ms != y.t_ms || x.containment != y.containment ||
          std::fabs(x.depth_m - y.depth_m) != 0.0 ||
          std::fabs(x.accuracy_m - y.accuracy_m) != 0.0 ||
          std::fabs(x.eta_s - y.eta_s) != 0.0)
        ++differing;
    }
    t::ok(differing == 0, "and every event matches bit for bit, in order");

    const auto& sa = a.summary;
    const auto& sb = b.summary;
    t::ok(sa.enters == sb.enters && sa.exits == sb.exits && sa.uncertain == sb.uncertain &&
              sa.approaching == sb.approaching && sa.dwell == sb.dwell,
          "the event-kind tallies match");
    t::ok(sa.alerts == sb.alerts && sa.incidents == sb.incidents &&
              sa.anomalies == sb.anomalies && sa.cohesion_events == sb.cohesion_events,
          "the alert, incident, anomaly and cohesion counts match");
    t::ok(sa.dispatched == sb.dispatched && sa.unassigned == sb.unassigned,
          "the dispatch plan covers the same incidents");
    t::ok(sa.greedy_response_m == sb.greedy_response_m &&
              sa.optimal_response_m == sb.optimal_response_m,
          "and the greedy and optimal totals are bit-identical");
    t::ok(a.dispatch_lines == b.dispatch_lines,
          "the same number of dispatch assignments");
  }

  // ── Dijkstra and A* on a graph full of equal-weight edges ──────────────────
  //
  // The grid generator produces many exactly-equal edge lengths, so the frontier
  // is full of ties. Without an explicit tie-break the heap resolves them by
  // insertion history, and the PARENT pointers -- and therefore the reconstructed
  // route -- can differ between runs while the distances stay optimal.
  {
    graph::RoadGraph g;
    // A perfectly regular lattice: every edge the same length by construction.
    const int N = 12;
    for (int r = 0; r < N; ++r)
      for (int c = 0; c < N; ++c) g.add_node({25.50 + 0.001 * r, 91.80 + 0.001 * c});
    auto id = [&](int r, int c) { return graph::NodeId(r * N + c); };
    for (int r = 0; r < N; ++r)
      for (int c = 0; c < N; ++c) {
        // add_road() weights each edge with its great-circle length, which keeps
        // the A* heuristic admissible. Every horizontal edge has one identical
        // length and every vertical edge another, so all monotone routes from
        // corner to corner cost exactly the same -- a lattice of ties, which is
        // the point.
        if (c + 1 < N) g.add_road(id(r, c), id(r, c + 1));
        if (r + 1 < N) g.add_road(id(r, c), id(r + 1, c));
      }

    const auto d1 = graph::dijkstra(g, id(0, 0));
    const auto d2 = graph::dijkstra(g, id(0, 0));
    t::ok(d1.dist == d2.dist, "Dijkstra: identical distances");
    t::ok(d1.parent == d2.parent,
          "Dijkstra: identical PARENT tree, not merely an equally optimal one");
    t::ok(d1.nodes_expanded == d2.nodes_expanded, "Dijkstra: identical work done");
    t::ok(d1.path_to(id(N - 1, N - 1)) == d2.path_to(id(N - 1, N - 1)),
          "Dijkstra: identical reconstructed route through a lattice of ties");

    const auto a1 = graph::astar(g, id(0, 0), id(N - 1, N - 1));
    const auto a2 = graph::astar(g, id(0, 0), id(N - 1, N - 1));
    t::ok(a1.found && a2.found, "A*: both runs find a path");
    t::ok(a1.path == a2.path, "A*: identical route");
    t::ok(a1.cost == a2.cost, "A*: identical cost");
    t::ok(a1.nodes_expanded == a2.nodes_expanded, "A*: identical nodes expanded");

    // A* must also agree with Dijkstra on cost -- the heuristic is admissible on
    // this graph, so optimality is guaranteed and worth asserting.
    t::ok(graph::heuristic_is_admissible(g), "the heuristic is admissible here");
    t::near(a1.cost, d1.dist[size_t(id(N - 1, N - 1))], 1e-9,
            "A* and Dijkstra agree on the optimal cost");
  }

  // ── k-d tree: build and query are pure functions of the input SET ──────────
  //
  // std::nth_element gives no ordering guarantee among equal elements and its
  // partition is implementation-defined, so a comparator on the axis value alone
  // lets two builds of the same points produce different trees.
  {
    std::vector<index::KdTree<uint32_t>::Item> items;
    // Deliberately degenerate: many points sharing a latitude, many sharing a
    // longitude, and several exact duplicates.
    for (uint32_t i = 0; i < 200; ++i)
      items.push_back({i, {25.55 + 0.0001 * double(i % 10), 91.88 + 0.0001 * double(i / 10)}});
    for (uint32_t i = 200; i < 220; ++i) items.push_back({i, {25.5500, 91.8800}});

    index::KdTree<uint32_t> t1, t2;
    t1.build(items);
    t2.build(items);

    size_t nn_diff = 0, knn_diff = 0;
    for (int i = 0; i < 500; ++i) {
      const geo::LatLon q{25.5495 + 0.00002 * i, 91.8795 + 0.00003 * i};
      uint32_t a = 0, b = 0;
      t1.nearest(q, a);
      t2.nearest(q, b);
      if (a != b) ++nn_diff;
      if (t1.k_nearest(q, 5) != t2.k_nearest(q, 5)) ++knn_diff;
    }
    t::ok(nn_diff == 0, "k-d tree: nearest() is identical across builds, ties included");
    t::ok(knn_diff == 0, "k-d tree: k_nearest() returns the same ids in the same order");

    // Exactly-coincident points: the tie-break says the lowest id wins, so the
    // answer is a defined one rather than "whichever branch was walked first".
    uint32_t got = 0;
    t1.nearest({25.5500, 91.8800}, got);
    t::ok(got == 0,
          "a query on a pile of coincident points returns the LOWEST id (got " +
              std::to_string(got) + ")");
    uint32_t again = 0;
    bool stable = true;
    for (int i = 0; i < 20; ++i) { t1.nearest({25.5500, 91.8800}, again);
                                   if (again != got) stable = false; }
    t::ok(stable, "and the same one every time");
    uint32_t from_other_build = 0;
    t2.nearest({25.5500, 91.8800}, from_other_build);
    t::ok(from_other_build == got, "and the same one from an independently built tree");
  }

  // ── Hungarian: one optimum chosen, deterministically ───────────────────────
  //
  // A symmetric cost matrix has many optimal assignments of the same total cost.
  // Returning a different one run to run would still be "optimal" and would still
  // change the dispatch plan the report prints.
  {
    const size_t n = 6;
    std::vector<std::vector<double>> cost(n, std::vector<double>(n, 10.0));
    // All-equal costs: every permutation is optimal.
    const auto a = graph::hungarian(cost);
    const auto b = graph::hungarian(cost);
    t::ok(a.ok() && b.ok(), "an all-equal matrix is accepted");
    t::ok(a.row_to_col == b.row_to_col,
          "Hungarian picks the SAME optimum from an all-equal cost matrix");
    t::near(a.total_cost, b.total_cost, 1e-12, "with the same total");

    // Partial ties: two clearly-best options of identical cost.
    std::vector<std::vector<double>> tied = {{1.0, 1.0, 5.0}, {5.0, 1.0, 1.0}};
    const auto c = graph::hungarian(tied);
    const auto d = graph::hungarian(tied);
    t::ok(c.row_to_col == d.row_to_col, "and the same optimum when only some costs tie");
    t::near(c.total_cost, 2.0, 1e-12, "which is genuinely optimal");
  }

  // ── the dispatch plan end to end ───────────────────────────────────────────
  {
    graph::RoadGraph g = graph::RoadGraph::grid({25.50, 91.80, 25.62, 91.96}, 20, 20, 77);
    dispatch::ResponderPool pool;
    for (int i = 0; i < 5; ++i) {
      dispatch::Responder r;
      r.pos = {25.52 + 0.02 * i, 91.82 + 0.02 * i};
      pool.add(r);
    }
    pool.snap_all(g);
    std::vector<dispatch::Incident> incs;
    for (int i = 0; i < 7; ++i)
      incs.push_back({IncidentId(i), {25.53 + 0.01 * i, 91.90 - 0.01 * i}, graph::kNoNode});
    dispatch::snap_incidents(incs, g);

    const auto g1 = dispatch::assign_greedy(pool, incs, g);
    const auto g2 = dispatch::assign_greedy(pool, incs, g);
    const auto o1 = dispatch::assign_optimal(pool, incs, g);
    const auto o2 = dispatch::assign_optimal(pool, incs, g);

    auto same = [](const dispatch::Plan& x, const dispatch::Plan& y) {
      if (x.dispatches.size() != y.dispatches.size()) return false;
      for (size_t i = 0; i < x.dispatches.size(); ++i)
        if (x.dispatches[i].responder != y.dispatches[i].responder ||
            x.dispatches[i].incident != y.dispatches[i].incident ||
            x.dispatches[i].travel_m != y.dispatches[i].travel_m)
          return false;
      return x.total_m == y.total_m && x.makespan_m == y.makespan_m;
    };
    t::ok(same(g1, g2), "the greedy plan is identical across runs");
    t::ok(same(o1, o2), "the optimal plan is identical across runs");
    t::ok(o1.total_m <= g1.total_m + 1e-9,
          "and optimal is no worse than greedy (" + std::to_string(int(o1.total_m)) +
              " m vs " + std::to_string(int(g1.total_m)) + " m)");
  }

  // ── insertion order: same SET, different order, same answers ──────────────
  //
  // Where the API promises set semantics, the result must not depend on the order
  // the set was built in. A spatial index's query answers are set semantics; its
  // internal SHAPE is not, and the difference is worth being explicit about.
  {
    std::vector<std::pair<ZoneId, geo::Bbox>> items;
    for (ZoneId i = 0; i < 400; ++i) {
      const double lat = 25.50 + 0.0003 * double(i % 40);
      const double lon = 91.80 + 0.0003 * double(i / 40);
      items.push_back({i, {lat - 0.0005, lon - 0.0005, lat + 0.0005, lon + 0.0005}});
    }
    auto reversed = items;
    std::reverse(reversed.begin(), reversed.end());

    index::Quadtree a, b;
    a.build(items);
    b.build(reversed);

    size_t diff = 0;
    for (int i = 0; i < 300; ++i) {
      const geo::Bbox q = geo::Bbox::around(
          {25.50 + 0.00004 * i, 91.80 + 0.00005 * i}, 200.0);
      std::vector<ZoneId> ra, rb;
      a.query(q, ra);
      b.query(q, rb);
      std::sort(ra.begin(), ra.end());
      std::sort(rb.begin(), rb.end());
      if (ra != rb) ++diff;
    }
    t::ok(diff == 0,
          "a quadtree built in reverse order answers every query with the same SET");
  }

  return t::report("golden/determinism");
}
