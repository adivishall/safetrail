#include "safetrail/app/lab.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "safetrail/app/json_out.hpp"
#include "safetrail/ds/circular_buffer.hpp"
#include "safetrail/ds/dynamic_connectivity.hpp"
#include "safetrail/ds/hash_table.hpp"
#include "safetrail/ds/interval_tree.hpp"
#include "safetrail/ds/priority_queue.hpp"
#include "safetrail/ds/timer_wheel.hpp"
#include "safetrail/evidence/merkle_log.hpp"
#include "safetrail/geo/containment.hpp"
#include "safetrail/graph/astar.hpp"
#include "safetrail/graph/bipartite_match.hpp"
#include "safetrail/graph/dijkstra.hpp"
#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/geohash.hpp"
#include "safetrail/index/kd_tree.hpp"
#include "safetrail/index/quadtree.hpp"
#include "safetrail/index/rtree.hpp"
#include "safetrail/index/versioned_index.hpp"
#include "safetrail/sim/mobility.hpp"
#include "safetrail/sync/lamport.hpp"

namespace safetrail::app {

namespace {

struct BadInput { std::string msg; };

double num(const util::Json& j, const char* k, double d) {
  const util::Json* v = j.find(k);
  return v ? v->number_or(d) : d;
}
int64_t inum(const util::Json& j, const char* k, int64_t d, int64_t lo, int64_t hi) {
  const double v = num(j, k, double(d));
  if (!std::isfinite(v) || v < double(lo) || v > double(hi))
    throw BadInput{std::string(k) + " must be between " + std::to_string(lo) + " and " + std::to_string(hi)};
  return int64_t(v);
}
std::string sval(const util::Json& j, const char* k, const std::string& d) {
  const util::Json* v = j.find(k);
  return v ? v->string_or(d) : d;
}
const std::vector<util::Json>& ops_of(const util::Json& j, size_t max_ops) {
  static const std::vector<util::Json> none;
  const util::Json* o = j.find("ops");
  if (!o) return none;
  if (o->type != util::Json::Type::Array) throw BadInput{"ops must be an array"};
  if (o->arr.size() > max_ops) throw BadInput{"at most " + std::to_string(max_ops) + " operations"};
  return o->arr;
}

void box(JsonOut& w, const geo::Bbox& b) {
  w.arr().num(b.min_lat).num(b.min_lon).num(b.max_lat).num(b.max_lon).end_arr();
}

template <typename F>
double time_us(F f, int reps) {
  std::vector<double> t;
  for (int i = 0; i < reps; ++i) {
    const auto a = std::chrono::steady_clock::now();
    f();
    t.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - a).count());
  }
  std::sort(t.begin(), t.end());
  return t[t.size() / 2];
}

// ── Spatial indexes ─────────────────────────────────────────────────────────
//
// The lab works in a 1 x 1 degree square (about 111 km a side at the equator),
// which is enough room for the structures to show their shape.
std::vector<std::pair<ZoneId, geo::Bbox>> lab_boxes(size_t n, uint64_t seed, const std::string& dist) {
  sim::Rng rng(seed);
  std::vector<std::pair<ZoneId, geo::Bbox>> v;
  std::vector<geo::LatLon> centres;
  for (int c = 0; c < 5; ++c) centres.push_back({rng.range(0.15, 0.85), rng.range(0.15, 0.85)});
  for (size_t i = 0; i < n; ++i) {
    double la = 0, lo = 0;
    if (dist == "clustered") {
      const auto& c = centres[size_t(rng.below(5))];
      la = c.lat + rng.normal() * 0.06;
      lo = c.lon + rng.normal() * 0.06;
    } else {
      la = rng.range(0.0, 1.0);
      lo = rng.range(0.0, 1.0);
    }
    la = std::min(0.98, std::max(0.0, la));
    lo = std::min(0.98, std::max(0.0, lo));
    const double h = rng.range(0.004, 0.025), w = rng.range(0.004, 0.025);
    v.push_back({ZoneId(i), {la, lo, la + h, lo + w}});
  }
  return v;
}

std::string lab_spatial(const util::Json& req) {
  const size_t n = size_t(inum(req, "n", 300, 1, 3000));
  const uint64_t seed = uint64_t(inum(req, "seed", 7, 0, 1'000'000));
  const std::string dist = sval(req, "distribution", "uniform");
  const size_t cap = size_t(inum(req, "capacity", 8, 2, 64));
  geo::Bbox q{num(req, "q_min_lat", 0.40), num(req, "q_min_lon", 0.40),
              num(req, "q_max_lat", 0.55), num(req, "q_max_lon", 0.58)};
  if (!(q.min_lat <= q.max_lat && q.min_lon <= q.max_lon)) throw BadInput{"query box is inverted"};
  const auto items = lab_boxes(n, seed, dist);

  JsonOut w;
  w.obj().kv("experiment", "spatial").ku("n", n).kv("distribution", dist);
  w.key("query"); box(w, q);
  w.key("items").arr();
  for (const auto& it : items) box(w, it.second);
  w.end_arr();

  index::BruteForceIndex brute;
  brute.build(items);
  std::vector<ZoneId> truth;
  brute.query(q, truth);
  std::sort(truth.begin(), truth.end());

  auto report = [&](const char* name, index::SpatialIndex& idx, double build_us,
                    const std::vector<index::NodeTrace>* nodes,
                    const std::vector<index::NodeTrace>* visited, std::vector<ZoneId> traced) {
    std::vector<ZoneId> got;
    idx.query(q, got);
    std::sort(got.begin(), got.end());
    std::sort(traced.begin(), traced.end());
    std::vector<ZoneId> scratch;
    const double qus = time_us([&] { scratch.clear(); idx.query(q, scratch); }, 101);
    size_t mismatch = 0;
    for (ZoneId id : truth) if (!std::binary_search(got.begin(), got.end(), id)) ++mismatch;
    for (ZoneId id : got) if (!std::binary_search(truth.begin(), truth.end(), id)) ++mismatch;
    const auto st = idx.stats();
    w.key(name).obj();
    w.kn("build_us", build_us).kn("query_us", qus).ku("results", got.size()).ku("mismatches_vs_brute", mismatch);
    w.ku("node_count", st.node_count).ku("max_depth", st.max_depth).ku("bytes", st.bytes);
    if (nodes) {
      w.kb("trace_matches_query", traced == got);
      w.key("nodes").arr();
      for (const auto& nd : *nodes) {
        w.arr().num(nd.box.min_lat).num(nd.box.min_lon).num(nd.box.max_lat).num(nd.box.max_lon)
            .uint(nd.depth).boolean(nd.leaf).uint(nd.items).end_arr();
      }
      w.end_arr();
      size_t hits = 0;
      w.key("visited").arr();
      for (const auto& nd : *visited) {
        hits += nd.hit ? 1 : 0;
        w.arr().num(nd.box.min_lat).num(nd.box.min_lon).num(nd.box.max_lat).num(nd.box.max_lon)
            .uint(nd.depth).boolean(nd.hit).end_arr();
      }
      w.end_arr();
      w.ku("nodes_visited", visited->size()).ku("nodes_descended", hits);
    }
    w.key("result_ids").arr();
    for (ZoneId id : got) w.uint(id);
    w.end_arr();
    w.end_obj();
  };

  {
    index::BruteForceIndex b2;
    const double bus = time_us([&] { b2.build(items); }, 3);
    report("brute", b2, bus, nullptr, nullptr, {});
  }
  {
    index::Quadtree qt(cap, 12);
    const double bus = time_us([&] { qt.build(items); }, 3);
    std::vector<index::NodeTrace> nodes, visited;
    std::vector<ZoneId> traced;
    qt.describe(nodes);
    qt.trace_query(q, traced, visited);
    report("quadtree", qt, bus, &nodes, &visited, traced);
  }
  {
    index::RTree rt(cap);
    const double bus = time_us([&] { rt.build(items); }, 3);
    std::vector<index::NodeTrace> nodes, visited;
    std::vector<ZoneId> traced;
    rt.describe(nodes);
    rt.trace_query(q, traced, visited);
    report("rtree_str", rt, bus, &nodes, &visited, traced);
  }
  {
    index::RTree rt(cap);
    const double bus = time_us([&] { rt.build_incremental(items); }, 3);
    std::vector<index::NodeTrace> nodes, visited;
    std::vector<ZoneId> traced;
    rt.describe(nodes);
    rt.trace_query(q, traced, visited);
    report("rtree_insert", rt, bus, &nodes, &visited, traced);
  }
  {
    index::Geohash gh;
    const double bus = time_us([&] { gh.build(items); }, 3);
    report("geohash", gh, bus, nullptr, nullptr, {});
  }
  w.end_obj();
  return w.take();
}

// ── k-d tree ────────────────────────────────────────────────────────────────
std::string lab_kdtree(const util::Json& req) {
  const size_t n = size_t(inum(req, "n", 60, 1, 600));
  const uint64_t seed = uint64_t(inum(req, "seed", 3, 0, 1'000'000));
  const geo::LatLon q{num(req, "q_lat", 0.5), num(req, "q_lon", 0.5)};
  sim::Rng rng(seed);
  std::vector<index::KdTree<uint32_t>::Item> items;
  for (size_t i = 0; i < n; ++i) items.push_back({uint32_t(i), {rng.range(0.02, 0.98), rng.range(0.02, 0.98)}});
  index::KdTree<uint32_t> kd;
  kd.build(items);
  uint32_t best = 0;
  std::vector<int32_t> visited;
  kd.nearest_traced(q, best, visited);
  uint32_t lin = 0;
  double lin_d = 1e300;
  for (const auto& it : items) {
    const double d = kd.distance_m(q, it.pos);
    if (d < lin_d || (d == lin_d && it.id < lin)) { lin_d = d; lin = it.id; }
  }
  JsonOut w;
  w.obj().kv("experiment", "kdtree").ki("root", kd.root_index());
  w.key("nodes").arr();
  for (const auto& nv : kd.view())
    w.arr().uint(nv.id).num(nv.pos.lat).num(nv.pos.lon).uint(nv.axis).integer(nv.left).integer(nv.right).end_arr();
  w.end_arr();
  w.key("visited").arr();
  for (int32_t v : visited) w.integer(v);
  w.end_arr();
  w.ku("nearest", best).ku("linear_scan_nearest", lin).kb("agree", best == lin);
  w.ku("nodes_visited", visited.size()).ku("n", n);
  w.end_obj();
  return w.take();
}

// ── Z-order ─────────────────────────────────────────────────────────────────
std::string lab_zorder(const util::Json& req) {
  const int bits = int(inum(req, "bits", 3, 1, 5));
  const int side = 1 << bits;
  JsonOut w;
  w.obj().kv("experiment", "zorder").ki("side", side);
  // Cells of a small grid inside the lab square, keyed by the SAME morton()
  // the geohash index sorts on. Sorting by key gives the Z-curve order.
  struct Cell { int r, c; uint64_t key; };
  std::vector<Cell> cells;
  for (int r = 0; r < side; ++r)
    for (int c = 0; c < side; ++c) {
      const geo::LatLon p{-45.0 + 90.0 * (r + 0.5) / side, -90.0 + 180.0 * (c + 0.5) / side};
      cells.push_back({r, c, index::Geohash::morton(p)});
    }
  std::stable_sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) { return a.key < b.key; });
  w.key("order").arr();
  for (const auto& c : cells) w.arr().integer(c.r).integer(c.c).str(std::to_string(c.key)).end_arr();
  w.end_arr().end_obj();
  return w.take();
}

// ── AVL interval tree ───────────────────────────────────────────────────────
std::string lab_avl(const util::Json& req) {
  ds::IntervalTree<uint32_t> tree;
  JsonOut w;
  w.obj().kv("experiment", "avl").key("steps").arr();
  uint32_t next_id = 0;
  for (const auto& op : ops_of(req, 80)) {
    const std::string kind = sval(op, "op", "insert");
    const int64_t lo = inum(op, "low", 0, -1'000'000, 1'000'000);
    const int64_t hi = inum(op, "high", lo + 1, -1'000'000, 1'000'000);
    if (lo >= hi) throw BadInput{"each interval needs low < high"};
    const uint64_t rl = tree.rotations_left(), rr = tree.rotations_right();
    bool ok = true;
    if (kind == "insert") tree.insert(lo, hi, next_id++);
    else if (kind == "remove") {
      ok = false;
      for (const auto& [i, nv] : tree.view())
        if (nv.low == lo && nv.high == hi) { ok = tree.remove(lo, hi, nv.value); (void)i; break; }
    } else throw BadInput{"op must be insert or remove"};
    w.obj().kv("op", kind).ki("low", lo).ki("high", hi).kb("applied", ok);
    w.ku("rotations_left", tree.rotations_left() - rl).ku("rotations_right", tree.rotations_right() - rr);
    w.ki("root", tree.root_index()).ku("height", tree.height()).ku("size", tree.size());
    w.kb("balanced", tree.balanced()).kb("invariants", tree.check_invariants());
    w.key("nodes").arr();
    for (const auto& [i, nv] : tree.view())
      w.arr().integer(i).integer(nv.low).integer(nv.high).integer(nv.max_high).integer(nv.height)
          .integer(nv.left).integer(nv.right).end_arr();
    w.end_arr().end_obj();
  }
  w.end_arr();
  const int64_t at = inum(req, "stab", 50, -1'000'000, 1'000'000);
  std::vector<uint32_t> hits;
  std::vector<int32_t> visited;
  tree.stabbing_traced(at, hits, visited);
  size_t linear = 0;
  for (const auto& [i, nv] : tree.view()) { (void)i; if (nv.low <= at && at < nv.high) ++linear; }
  w.key("stab").obj().ki("at", at).ku("results", hits.size()).ku("linear_scan_results", linear);
  w.key("visited").arr();
  for (int32_t v : visited) w.integer(v);
  w.end_arr().end_obj();
  w.end_obj();
  return w.take();
}

// ── Persistent quadtree ─────────────────────────────────────────────────────
std::string lab_persistent(const util::Json& req) {
  const size_t n = size_t(inum(req, "n", 24, 2, 200));
  const uint64_t seed = uint64_t(inum(req, "seed", 11, 0, 1'000'000));
  const std::string edit = sval(req, "edit", "move");
  const size_t zone = size_t(inum(req, "zone", 3, 0, int64_t(n) - 1));
  const auto items = lab_boxes(n, seed, "uniform");
  index::VersionedIndex vi;
  Timestamp t = 0;
  for (const auto& [id, b] : items) vi.add_zone(id, b, {}, t += 1000);
  if (edit == "move") {
    geo::Bbox b = items[zone].second;
    const double d = 0.3;
    b = {std::fmod(b.min_lat + d, 0.97), b.min_lon, std::fmod(b.min_lat + d, 0.97) + (b.max_lat - b.min_lat), b.max_lon};
    vi.add_zone(ZoneId(zone), b, {}, t += 1000);
  } else if (edit == "validity") {
    vi.update_validity(ZoneId(zone), {0, 5000}, t += 1000);
  } else if (edit == "remove") {
    vi.remove_zone(ZoneId(zone), t += 1000);
  } else if (edit == "add") {
    vi.add_zone(ZoneId(n), {0.45, 0.45, 0.47, 0.48}, {}, t += 1000);
  } else {
    throw BadInput{"edit must be move, validity, remove or add"};
  }
  const auto versions = vi.viz_versions(1000);
  const size_t take = std::min<size_t>(2, versions.size());
  JsonOut w;
  w.obj().kv("experiment", "persistent").kv("edit", edit).ku("zone", zone);
  w.ku("version_count", vi.version_count()).ku("validity_records", vi.validity_records());
  const auto ss = vi.share_stats();
  w.ku("nodes_allocated", ss.total_nodes_allocated).ku("nodes_if_full_copies", ss.nodes_if_full_copies);
  w.kn("sharing_ratio", ss.sharing_ratio());
  w.key("versions").arr();
  for (size_t k = versions.size() - take; k < versions.size(); ++k) {
    const auto& v = versions[k];
    w.obj().ku("version", v.version).ki("at", v.at).ku("new_nodes", v.new_nodes).ku("shared_nodes", v.shared_nodes);
    w.key("nodes").arr();
    for (const auto& nd : v.nodes)
      w.arr().num(nd.region.min_lat).num(nd.region.min_lon).num(nd.region.max_lat).num(nd.region.max_lon)
          .uint(nd.depth).boolean(nd.leaf).boolean(nd.shared).integer(nd.items)
          .integer(nd.kids[0]).integer(nd.kids[1]).integer(nd.kids[2]).integer(nd.kids[3]).end_arr();
    w.end_arr().end_obj();
  }
  w.end_arr();
  // The past is still answerable: the zone count at each version time.
  w.key("history").arr();
  for (Timestamp at : {Timestamp(1000), Timestamp(int64_t(n / 2) * 1000), t - 1000, t})
    w.arr().integer(at).uint(vi.zone_count_at(at)).end_arr();
  w.end_arr();
  w.key("items").arr();
  for (const auto& it : items) box(w, it.second);
  w.end_arr();
  w.end_obj();
  return w.take();
}

// ── Graph search ────────────────────────────────────────────────────────────
std::string lab_graph(const util::Json& req) {
  const int rows = int(inum(req, "rows", 12, 3, 30));
  const int cols = int(inum(req, "cols", 16, 3, 30));
  const uint64_t seed = uint64_t(inum(req, "seed", 5, 0, 1'000'000));
  const graph::RoadGraph g = graph::RoadGraph::grid({25.55, 91.85, 25.60, 91.92}, rows, cols, seed);
  const int64_t nn = int64_t(g.node_count());
  const auto src = graph::NodeId(inum(req, "source", 0, 0, nn - 1));
  const auto dst = graph::NodeId(inum(req, "target", nn - 1, 0, nn - 1));
  const size_t snaps = size_t(inum(req, "snapshots", 400, 0, 2000));

  graph::SearchTrace dt, at;
  dt.snapshot_limit = at.snapshot_limit = snaps;
  const auto dj = graph::dijkstra(g, src, dst, &dt);
  const auto as = graph::astar(g, src, dst, &at);

  JsonOut w;
  w.obj().kv("experiment", "graph").ki("source", src).ki("target", dst);
  w.ku("nodes", g.node_count()).ku("edges", g.edge_count());
  w.kb("heuristic_admissible", graph::heuristic_is_admissible(g));
  w.key("positions").arr();
  for (graph::NodeId i = 0; i < graph::NodeId(g.node_count()); ++i)
    w.arr().num(g.pos(i).lat).num(g.pos(i).lon).end_arr();
  w.end_arr();
  w.key("adjacency").arr();   // the adjacency list itself
  for (graph::NodeId i = 0; i < graph::NodeId(g.node_count()); ++i) {
    w.arr();
    for (const auto& e : g.neighbors(i)) w.arr().integer(e.to).num(std::round(e.weight_m * 10) / 10).end_arr();
    w.end_arr();
  }
  w.end_arr();
  auto trace = [&](const char* k, const graph::SearchTrace& tr, const std::vector<graph::NodeId>& path,
                   double cost, size_t expanded) {
    w.key(k).obj().kn("cost_m", cost).ku("expanded", expanded).ku("pushes", tr.pushes).ku("stale_pops", tr.stale_pops);
    w.key("settled").arr();
    for (auto v : tr.settled) w.integer(v);
    w.end_arr();
    w.key("frontier_size").arr();
    for (auto v : tr.frontier_size) w.uint(v);
    w.end_arr();
    w.key("frontier").arr();
    for (const auto& snap : tr.frontier) {
      w.arr();
      for (const auto& [key, node] : snap) w.arr().num(std::round(key * 10) / 10).integer(node).end_arr();
      w.end_arr();
    }
    w.end_arr();
    w.key("path").arr();
    for (auto v : path) w.integer(v);
    w.end_arr().end_obj();
  };
  trace("dijkstra", dt, dj.path_to(dst), dj.reachable(dst) ? dj.dist[size_t(dst)] : -1.0, dj.nodes_expanded);
  trace("astar", at, as.path, as.found ? as.cost : -1.0, as.nodes_expanded);
  w.kb("same_cost", as.found && dj.reachable(dst) && std::fabs(as.cost - dj.dist[size_t(dst)]) < 1e-6);
  w.end_obj();
  return w.take();
}

// ── Binary heap ─────────────────────────────────────────────────────────────
std::string lab_heap(const util::Json& req) {
  ds::BinaryHeap<int64_t> h;
  JsonOut w;
  w.obj().kv("experiment", "heap").key("steps").arr();
  for (const auto& op : ops_of(req, 100)) {
    const std::string kind = sval(op, "op", "push");
    w.obj().kv("op", kind);
    if (kind == "push") {
      const int64_t v = inum(op, "value", 0, -1'000'000, 1'000'000);
      h.push(v);
      w.ki("value", v);
    } else if (kind == "pop") {
      if (h.empty()) w.key("popped").null();
      else w.ki("popped", h.pop());
    } else throw BadInput{"op must be push or pop"};
    w.key("array").arr();
    for (int64_t v : h.data()) w.integer(v);
    w.end_arr().end_obj();
  }
  w.end_arr().end_obj();
  return w.take();
}

// ── Assignment ──────────────────────────────────────────────────────────────
std::string lab_assignment(const util::Json& req) {
  const size_t rows = size_t(inum(req, "rows", 5, 1, 10));
  const size_t cols = size_t(inum(req, "cols", 6, 1, 10));
  if (rows > cols) throw BadInput{"rows (responders) must not exceed cols (incidents)"};
  const uint64_t seed = uint64_t(inum(req, "seed", 9, 0, 1'000'000));
  sim::Rng rng(seed);
  std::vector<std::vector<double>> cost(rows, std::vector<double>(cols));
  for (auto& r : cost) for (auto& c : r) c = double(100 + rng.below(1900));
  const auto opt = graph::hungarian(cost);
  // Greedy: the rule dispatch::assign_greedy applies -- repeatedly take the
  // cheapest remaining (row, column) pair.
  std::vector<int32_t> greedy(rows, -1);
  std::vector<bool> row_used(rows, false), col_used(cols, false);
  double greedy_total = 0;
  for (size_t k = 0; k < rows; ++k) {
    double best = 1e300; size_t br = 0, bc = 0;
    for (size_t r = 0; r < rows; ++r) if (!row_used[r])
      for (size_t c = 0; c < cols; ++c) if (!col_used[c] && cost[r][c] < best) { best = cost[r][c]; br = r; bc = c; }
    row_used[br] = col_used[bc] = true;
    greedy[br] = int32_t(bc);
    greedy_total += best;
  }
  JsonOut w;
  w.obj().kv("experiment", "assignment").key("cost").arr();
  for (const auto& r : cost) { w.arr(); for (double c : r) w.num(c); w.end_arr(); }
  w.end_arr();
  w.kv("status", graph::to_string(opt.status));
  w.key("optimal").arr();
  for (auto c : opt.row_to_col) w.integer(c);
  w.end_arr().kn("optimal_total", opt.total_cost);
  w.key("greedy").arr();
  for (auto c : greedy) w.integer(c);
  w.end_arr().kn("greedy_total", greedy_total);
  w.end_obj();
  return w.take();
}

// ── Union-find with rollback ────────────────────────────────────────────────
std::string lab_dsu(const util::Json& req) {
  const size_t n = size_t(inum(req, "n", 10, 2, 40));
  ds::RollbackDSU d(n);
  JsonOut w;
  w.obj().kv("experiment", "dsu").ku("n", n).key("steps").arr();
  for (const auto& op : ops_of(req, 100)) {
    const std::string kind = sval(op, "op", "unite");
    w.obj().kv("op", kind);
    if (kind == "unite") {
      const auto a = size_t(inum(op, "a", 0, 0, int64_t(n) - 1));
      const auto b = size_t(inum(op, "b", 0, 0, int64_t(n) - 1));
      w.ku("a", a).ku("b", b).kb("merged", d.unite(a, b));
    } else if (kind == "snapshot") {
      w.ku("mark", d.snapshot());
    } else if (kind == "rollback") {
      const auto mark = size_t(inum(op, "mark", 0, 0, int64_t(d.snapshot())));
      d.rollback_to(mark);
      w.ku("mark", mark);
    } else throw BadInput{"op must be unite, snapshot or rollback"};
    w.key("parent").arr();
    for (size_t i = 0; i < n; ++i) w.uint(d.parent_of(i));
    w.end_arr().key("rank").arr();
    for (size_t i = 0; i < n; ++i) w.uint(d.rank_of(i));
    w.end_arr().ku("components", d.component_count()).ku("undo_depth", d.snapshot()).end_obj();
  }
  w.end_arr().end_obj();
  return w.take();
}

// ── Open-addressing hash table ──────────────────────────────────────────────
std::string lab_hash(const util::Json& req) {
  ds::HashMap<uint32_t, uint32_t> m;
  JsonOut w;
  w.obj().kv("experiment", "hash").key("steps").arr();
  for (const auto& op : ops_of(req, 120)) {
    const std::string kind = sval(op, "op", "put");
    const auto key = uint32_t(inum(op, "key", 0, 0, 1'000'000));
    // The probe sequence the lookup will walk, read from the table BEFORE the
    // operation: home slot, then linear steps until the key or an empty slot.
    std::vector<size_t> probe;
    {
      const size_t cap = m.bucket_count();
      size_t i = m.home_slot(key);
      for (size_t k = 0; k < cap; ++k) {
        probe.push_back(i);
        const auto sv = m.slot_view(i);
        if (sv.state == 0 || (sv.state == 1 && sv.key == key)) break;
        i = (i + 1) & (cap - 1);
      }
    }
    const size_t cap_before = m.bucket_count();
    w.obj().kv("op", kind).ku("key", key).ku("home", m.home_slot(key));
    if (kind == "put") w.kb("inserted", m.put(key, key * 10));
    else if (kind == "erase") w.kb("erased", m.erase(key));
    else if (kind == "get") w.kb("found", m.contains(key));
    else throw BadInput{"op must be put, erase or get"};
    w.kb("resized", m.bucket_count() != cap_before);
    w.key("probe").arr();
    for (size_t p : probe) w.uint(p);
    w.end_arr();
    w.ku("capacity", m.bucket_count()).ku("size", m.size()).ku("tombstones", m.tombstones())
        .ku("growths", m.growths()).ku("rehashes", m.rehashes());
    w.key("slots").arr();
    for (size_t i = 0; i < m.bucket_count(); ++i) {
      const auto sv = m.slot_view(i);
      if (sv.state == 1) w.uint(sv.key);
      else if (sv.state == 2) w.str("T");
      else w.null();
    }
    w.end_arr().end_obj();
  }
  w.end_arr().end_obj();
  return w.take();
}

// ── Timer wheel ─────────────────────────────────────────────────────────────
std::string lab_wheel(const util::Json& req) {
  const size_t slots = size_t(inum(req, "slots", 8, 2, 32));
  ds::TimerWheel<uint32_t> wh(1000, slots, 0);
  JsonOut w;
  w.obj().kv("experiment", "wheel").ku("slots", slots).ki("tick_ms", 1000).key("steps").arr();
  for (const auto& op : ops_of(req, 100)) {
    const std::string kind = sval(op, "op", "schedule");
    w.obj().kv("op", kind);
    if (kind == "schedule" || kind == "cancel") {
      const auto id = uint32_t(inum(op, "id", 0, 0, 1'000'000));
      const int64_t dl = inum(op, "deadline", 0, 0, 10'000'000);
      w.ku("id", id).ki("deadline", dl);
      if (kind == "schedule") wh.schedule(id, dl);
      else w.kb("cancelled", wh.cancel(id, dl));
    } else if (kind == "advance") {
      const int64_t to = inum(op, "to", 0, 0, 10'000'000);
      w.ki("to", to).key("fired").arr();
      for (uint32_t id : wh.advance(to)) w.uint(id);
      w.end_arr();
    } else throw BadInput{"op must be schedule, cancel or advance"};
    w.ki("tick", wh.current_tick()).ku("pending", wh.pending()).key("wheel").arr();
    for (size_t s = 0; s < wh.slot_count(); ++s) {
      w.arr();
      for (const auto& [id, et] : wh.slot_items(s)) w.arr().uint(id).integer(et).end_arr();
      w.end_arr();
    }
    w.end_arr().end_obj();
  }
  w.end_arr().end_obj();
  return w.take();
}

// ── Circular buffer ─────────────────────────────────────────────────────────
std::string lab_ring(const util::Json& req) {
  ds::CircularBuffer<int64_t, 8> b;
  JsonOut w;
  w.obj().kv("experiment", "ring").ku("capacity", b.capacity()).key("steps").arr();
  for (const auto& op : ops_of(req, 100)) {
    const int64_t v = inum(op, "value", 0, -1'000'000, 1'000'000);
    b.push(v);
    w.obj().ki("pushed", v).ku("head", b.head()).ku("size", b.size()).key("physical").arr();
    for (size_t i = 0; i < b.capacity(); ++i) w.integer(i < b.size() || b.size() == b.capacity() ? b.physical(i) : 0);
    w.end_arr().key("newest_first").arr();
    for (size_t i = 0; i < b.size(); ++i) w.integer(b[i]);
    w.end_arr().end_obj();
  }
  w.end_arr().end_obj();
  return w.take();
}

// ── Merkle tree ─────────────────────────────────────────────────────────────
//
// The tree is drawn from leaf hashes with the RFC 6962 split (the largest power
// of two below n goes left), using the same leaf_hash/sha256_pair the log uses;
// the response carries both that root and MerkleLog::root() so the viewer can
// see they are the same value.
evidence::Hash draw_tree(JsonOut& w, const std::vector<evidence::Hash>& lv, size_t lo, size_t hi) {
  if (hi - lo == 1) {
    w.obj().kv("hash", evidence::to_hex(lv[lo]).substr(0, 12)).ku("lo", lo).ku("hi", hi).end_obj();
    return lv[lo];
  }
  size_t k = 1;
  while (k * 2 < hi - lo) k *= 2;
  w.obj().ku("lo", lo).ku("hi", hi).key("kids").arr();
  const evidence::Hash l = draw_tree(w, lv, lo, lo + k);
  const evidence::Hash r = draw_tree(w, lv, lo + k, hi);
  const evidence::Hash h = evidence::sha256_pair(l, r);
  w.end_arr().kv("hash", evidence::to_hex(h).substr(0, 12)).end_obj();
  return h;
}

std::string lab_merkle(const util::Json& req) {
  std::vector<std::string> leaves;
  if (const util::Json* l = req.find("leaves")) {
    if (l->type != util::Json::Type::Array || l->arr.size() > 32) throw BadInput{"leaves: up to 32 strings"};
    for (const auto& x : l->arr) {
      if (x.type != util::Json::Type::String || x.str.size() > 200) throw BadInput{"each leaf is a string of at most 200 characters"};
      leaves.push_back(x.str);
    }
  }
  if (leaves.empty()) for (int i = 0; i < 7; ++i) leaves.push_back("event " + std::to_string(i));
  const auto idx = size_t(inum(req, "prove", 2, 0, int64_t(leaves.size()) - 1));
  const int64_t tamper = inum(req, "tamper", -1, -1, int64_t(leaves.size()) - 1);

  evidence::MerkleLog log;
  for (const auto& s : leaves) log.append(s);
  std::vector<evidence::Hash> lv;
  for (const auto& s : leaves) lv.push_back(evidence::leaf_hash(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
  JsonOut w;
  w.obj().kv("experiment", "merkle").key("tree");
  const evidence::Hash drawn = draw_tree(w, lv, 0, lv.size());
  const evidence::Hash root = log.root();
  w.kv("root", evidence::to_hex(root)).kb("drawn_root_matches_log", drawn == root);
  const auto proof = log.prove(idx);
  w.ku("prove", idx).key("proof").arr();
  for (const auto& h : proof.path) w.str(evidence::to_hex(h).substr(0, 12));
  w.end_arr();
  const evidence::Hash leaf = lv[idx];
  w.kb("proof_verifies", proof.verify(leaf, root));
  if (tamper >= 0) {
    std::string forged = leaves[size_t(tamper)] + " (edited)";
    const auto fl = evidence::leaf_hash(reinterpret_cast<const uint8_t*>(forged.data()), forged.size());
    evidence::MerkleLog log2;
    for (size_t i = 0; i < leaves.size(); ++i) log2.append(i == size_t(tamper) ? forged : leaves[i]);
    w.ki("tampered", tamper).kv("tampered_root", evidence::to_hex(log2.root()));
    w.kb("tampered_leaf_verifies_against_original_root",
         log.prove(size_t(tamper)).verify(fl, root));
  }
  w.end_obj();
  return w.take();
}

// ── Lamport clocks ──────────────────────────────────────────────────────────
std::string lab_lamport(const util::Json& req) {
  std::vector<sync::LamportClock> dev(3);
  for (size_t i = 0; i < dev.size(); ++i) dev[i].device = DeviceId(i + 1);
  struct Ev { sync::LamportClock stamp; std::string label; };
  std::vector<Ev> events;
  std::vector<uint64_t> in_flight;   // message stamps by message index
  JsonOut w;
  w.obj().kv("experiment", "lamport").key("steps").arr();
  for (const auto& op : ops_of(req, 60)) {
    const auto d = size_t(inum(op, "device", 0, 0, 2));
    const std::string kind = sval(op, "op", "local");
    w.obj().ku("device", d + 1).kv("op", kind);
    if (kind == "local") {
      dev[d].tick();
    } else if (kind == "send") {
      dev[d].tick();
      in_flight.push_back(dev[d].counter);
      w.ku("message", in_flight.size() - 1);
    } else if (kind == "recv") {
      const auto m = size_t(inum(op, "message", 0, 0, int64_t(in_flight.size()) - 1));
      if (in_flight.empty()) throw BadInput{"no message to receive"};
      dev[d].observe(in_flight[m]);
      w.ku("message", m);
    } else throw BadInput{"op must be local, send or recv"};
    events.push_back({dev[d], kind + "@" + std::to_string(d + 1)});
    w.key("clocks").arr();
    for (const auto& c : dev) w.uint(c.counter);
    w.end_arr().ku("stamp", dev[d].counter).end_obj();
  }
  w.end_arr();
  std::vector<size_t> order(events.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(),
                   [&](size_t a, size_t b) { return events[a].stamp.before(events[b].stamp); });
  w.key("total_order").arr();
  for (size_t i : order) w.arr().uint(i).uint(events[i].stamp.counter).uint(events[i].stamp.device).end_arr();
  w.end_arr().end_obj();
  return w.take();
}

// ── Geometry ────────────────────────────────────────────────────────────────
std::string lab_geometry(const util::Json& req) {
  const util::Json* ring = req.find("ring");
  geo::Ring r;
  if (ring && ring->type == util::Json::Type::Array) {
    if (ring->arr.size() > 64) throw BadInput{"at most 64 vertices"};
    for (const auto& p : ring->arr)
      if (p.type == util::Json::Type::Array && p.arr.size() == 2) r.push_back({p.arr[0].num, p.arr[1].num});
  }
  if (r.size() < 3) r = {{25.580, 91.880}, {25.580, 91.884}, {25.583, 91.884}, {25.5815, 91.882}, {25.583, 91.880}};
  geo::Polygon poly(r);
  const geo::UncertainPoint p{{num(req, "lat", 25.5815), num(req, "lon", 91.8812)},
                              num(req, "accuracy_m", 20.0), 0};
  double sd = 0;
  const geo::Containment c = geo::evaluate(poly, p, &sd);
  JsonOut w;
  w.obj().kv("experiment", "geometry").kv("validity", geo::Polygon::to_string(poly.validate()));
  w.kb("ray_casting", geo::contains(poly, p.pos)).kb("winding_number", geo::contains_winding(poly, p.pos));
  w.kn("signed_distance_m", sd).kv("containment", geo::to_string(c)).kn("accuracy_m", p.accuracy_m);
  const geo::LatLon nb = geo::nearest_boundary_point(poly, p.pos);
  w.key("nearest_boundary").arr().num(nb.lat).num(nb.lon).end_arr();
  w.key("ring").arr();
  for (const auto& v : r) w.arr().num(v.lat).num(v.lon).end_arr();
  w.end_arr().end_obj();
  return w.take();
}

}  // namespace

std::vector<std::string> lab_experiments() {
  return {"spatial", "kdtree", "zorder", "avl", "persistent", "graph", "heap", "assignment",
          "dsu", "hash", "wheel", "ring", "merkle", "lamport", "geometry"};
}

std::string run_lab(const std::string& name, const util::Json& req, int* status) {
  *status = 200;
  try {
    if (name == "spatial") return lab_spatial(req);
    if (name == "kdtree") return lab_kdtree(req);
    if (name == "zorder") return lab_zorder(req);
    if (name == "avl") return lab_avl(req);
    if (name == "persistent") return lab_persistent(req);
    if (name == "graph") return lab_graph(req);
    if (name == "heap") return lab_heap(req);
    if (name == "assignment") return lab_assignment(req);
    if (name == "dsu") return lab_dsu(req);
    if (name == "hash") return lab_hash(req);
    if (name == "wheel") return lab_wheel(req);
    if (name == "ring") return lab_ring(req);
    if (name == "merkle") return lab_merkle(req);
    if (name == "lamport") return lab_lamport(req);
    if (name == "geometry") return lab_geometry(req);
  } catch (const BadInput& e) {
    *status = 400;
    return "{\"error\":" + util::Json::escape(e.msg) + "}";
  }
  *status = 404;
  return "{\"error\":\"unknown experiment\"}";
}

}  // namespace safetrail::app
