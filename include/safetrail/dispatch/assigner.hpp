#pragma once
// Responder -> incident assignment: greedy vs optimal, kept side by side.
//
// This is the payoff of the graph stack. Given a set of open incidents and a pool
// of available responders, who goes where? The cost of sending responder r to
// incident k is the shortest road distance between them -- one Dijkstra per
// responder over the road graph (dijkstra.hpp) fills the cost matrix.
//
// Then two strategies, and the report compares them:
//   - GREEDY: repeatedly take the cheapest available (responder, incident) pair.
//     Fast, intuitive, and provably suboptimal -- an early cheap pick can strand a
//     later incident with only a distant responder left.
//   - OPTIMAL: the Hungarian algorithm (bipartite_match.hpp) minimises the TOTAL
//     travel distance over all assignments at once.
//
// The measurement the roadmap asks for is exactly `optimal.total_m` vs
// `greedy.total_m`: the same responders, the same incidents, two dispatch policies,
// one number of difference.
#include <vector>
#include "safetrail/dispatch/responder.hpp"
#include "safetrail/graph/road_graph.hpp"
#include "safetrail/types.hpp"

namespace safetrail::dispatch {

struct Incident {
  IncidentId    id = kNoId;
  geo::LatLon   pos;
  graph::NodeId node = graph::kNoNode;   // snapped junction
};

struct Dispatch {
  ResponderId responder = kNoId;
  IncidentId  incident  = kNoId;
  double      travel_m  = 0.0;   // shortest road distance, or a large sentinel if unreachable
};

struct Plan {
  std::vector<Dispatch> dispatches;
  double total_m   = 0.0;        // sum of travel distances (the objective)
  double makespan_m = 0.0;       // the single longest response (worst tourist's wait)
  size_t unassigned = 0;         // incidents left uncovered (more incidents than responders)
};

// Snap incidents to the graph in place (nearest junction to each position).
void snap_incidents(std::vector<Incident>& incidents, const graph::RoadGraph& g);

// Build the |responders| x |incidents| road-distance cost matrix. Unreachable
// pairs get `kUnreachableCost`. Exposed because both planners and the tests use it.
constexpr double kUnreachableCost = 1e9;
std::vector<std::vector<double>> cost_matrix(const ResponderPool& pool,
                                             const std::vector<Incident>& incidents,
                                             const graph::RoadGraph& g);

// Greedy: cheapest available pair first, until responders or incidents run out.
Plan assign_greedy(const ResponderPool& pool,
                   const std::vector<Incident>& incidents,
                   const graph::RoadGraph& g);

// Optimal: minimum total travel distance via the Hungarian algorithm.
Plan assign_optimal(const ResponderPool& pool,
                    const std::vector<Incident>& incidents,
                    const graph::RoadGraph& g);

}  // namespace safetrail::dispatch
