#include "safetrail/dispatch/assigner.hpp"

#include <algorithm>
#include "safetrail/graph/bipartite_match.hpp"
#include "safetrail/graph/dijkstra.hpp"

namespace safetrail::dispatch {

void snap_incidents(std::vector<Incident>& incidents, const graph::RoadGraph& g) {
  for (auto& inc : incidents) inc.node = g.nearest_node(inc.pos);
}

std::vector<std::vector<double>> cost_matrix(const ResponderPool& pool,
                                             const std::vector<Incident>& incidents,
                                             const graph::RoadGraph& g) {
  const size_t R = pool.size(), I = incidents.size();
  std::vector<std::vector<double>> c(R, std::vector<double>(I, kUnreachableCost));
  for (size_t r = 0; r < R; ++r) {
    const graph::NodeId src = pool[r].node;
    if (!g.valid(src)) continue;
    // One single-source Dijkstra per responder fills that responder's whole row.
    const auto sp = graph::dijkstra(g, src);
    for (size_t i = 0; i < I; ++i) {
      const graph::NodeId dst = incidents[i].node;
      if (g.valid(dst) && sp.reachable(dst)) c[r][i] = sp.dist[size_t(dst)];
    }
  }
  return c;
}

namespace {
void finalize(Plan& p, size_t incident_count) {
  p.total_m = 0.0;
  p.makespan_m = 0.0;
  for (const auto& d : p.dispatches) {
    p.total_m += d.travel_m;
    p.makespan_m = std::max(p.makespan_m, d.travel_m);
  }
  p.unassigned = incident_count - p.dispatches.size();
}
}  // namespace

Plan assign_greedy(const ResponderPool& pool,
                   const std::vector<Incident>& incidents,
                   const graph::RoadGraph& g) {
  Plan plan;
  const auto cost = cost_matrix(pool, incidents, g);
  const std::vector<size_t> avail = pool.available_indices();
  const size_t I = incidents.size();

  std::vector<char> resp_used(avail.size(), 0), inc_used(I, 0);

  // Repeatedly commit the globally cheapest reachable pair. O(R*I) per pick; the
  // dispatch batch is tiny, so the simple form is the right one.
  for (;;) {
    double best = kUnreachableCost;
    size_t ba = 0, bi = 0;
    bool found = false;
    for (size_t a = 0; a < avail.size(); ++a) {
      if (resp_used[a]) continue;
      for (size_t i = 0; i < I; ++i) {
        if (inc_used[i]) continue;
        // Strict <: among equal-cost pairs the first in (responder, incident)
        // index order wins. The scan order is fixed, so the greedy plan is a pure
        // function of the cost matrix -- which matters because greedy-vs-optimal
        // is a reported number, and a plan that varied run to run would make the
        // comparison meaningless.
        const double c = cost[avail[a]][i];
        if (c < best) { best = c; ba = a; bi = i; found = true; }
      }
    }
    if (!found) break;
    resp_used[ba] = 1;
    inc_used[bi] = 1;
    plan.dispatches.push_back({pool[avail[ba]].id, incidents[bi].id, best});
  }

  finalize(plan, I);
  return plan;
}

Plan assign_optimal(const ResponderPool& pool,
                    const std::vector<Incident>& incidents,
                    const graph::RoadGraph& g) {
  Plan plan;
  const auto full = cost_matrix(pool, incidents, g);
  const std::vector<size_t> avail = pool.available_indices();
  const size_t R = avail.size(), I = incidents.size();
  if (R == 0 || I == 0) { plan.unassigned = I; return plan; }

  // Restrict the cost matrix to available responders.
  std::vector<std::vector<double>> C(R, std::vector<double>(I));
  for (size_t r = 0; r < R; ++r)
    for (size_t i = 0; i < I; ++i) C[r][i] = full[avail[r]][i];

  // Hungarian needs rows <= cols. Orient so the smaller side is the rows, which is
  // also what we want semantically: match every member of the scarcer resource.
  auto commit = [&](size_t resp_idx, size_t inc_idx) {
    const double cst = full[avail[resp_idx]][inc_idx];
    if (cst < kUnreachableCost)   // drop forced-unreachable pairings
      plan.dispatches.push_back({pool[avail[resp_idx]].id, incidents[inc_idx].id, cst});
  };

  // A rejected Assignment carries an EMPTY row_to_col, so its status must be
  // checked before indexing. The orientation above guarantees rows <= cols and
  // cost_matrix() only ever writes finite values, so a failure here means a bug
  // upstream, not bad user input -- and it degrades to "nothing assigned" rather
  // than reading past the end of a vector.
  if (R <= I) {
    const auto asg = graph::hungarian(C);              // rows = responders
    if (asg.ok())
      for (size_t r = 0; r < R; ++r) {
        const int col = asg.row_to_col[r];
        if (col >= 0) commit(r, size_t(col));
      }
  } else {
    std::vector<std::vector<double>> CT(I, std::vector<double>(R));  // rows = incidents
    for (size_t i = 0; i < I; ++i)
      for (size_t r = 0; r < R; ++r) CT[i][r] = C[r][i];
    const auto asg = graph::hungarian(CT);
    if (asg.ok())
      for (size_t i = 0; i < I; ++i) {
        const int col = asg.row_to_col[i];
        if (col >= 0) commit(size_t(col), i);
      }
  }

  finalize(plan, I);
  return plan;
}

}  // namespace safetrail::dispatch
