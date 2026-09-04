#pragma once
// Bipartite matching -- two algorithms for the responder <-> incident problem.
//
// 1. Kuhn's algorithm (max-cardinality matching, O(V * E)). Answers "how many
//    incidents can be covered at all, given each responder can only reach some of
//    them?" -- pure feasibility, edges are yes/no. Augmenting-path method: repeatedly
//    find an alternating path that frees up a new match.
//
// 2. Hungarian algorithm (min-cost perfect assignment, O(n^3)). Answers the harder
//    question the dispatcher actually asks: "assign responders to incidents so the
//    TOTAL response time is minimised." Edges are weighted (road travel time), and
//    the greedy nearest-first heuristic is provably not optimal -- Hungarian is.
//
// Keeping both is the point of the comparison in the report: Kuhn's for the
// combinatorial core, Hungarian for the weighted optimum that greedy dispatch
// leaves on the table.
#include <cstdint>
#include <vector>

namespace safetrail::graph {

// ── Kuhn's: maximum cardinality matching in an unweighted bipartite graph ──────
// `adj[u]` lists the right-side vertices reachable from left vertex u.
// Returns match_right, where match_right[v] = the left vertex matched to right v,
// or -1 if v is unmatched. `matched_count` receives the size of the matching.
std::vector<int32_t> kuhn_matching(int32_t left_n, int32_t right_n,
                                   const std::vector<std::vector<int32_t>>& adj,
                                   int32_t* matched_count = nullptr);

// ── Hungarian: minimum-cost assignment on a rectangular cost matrix ────────────
// cost[i][j] = cost of assigning left i to right j. Requires left_n <= right_n
// (pad with dummy high-cost columns if you have more responders than incidents,
// or transpose). Returns row_to_col: for each left row, the column it is assigned
// to (always a full assignment of all left rows). `total_cost` receives the sum.
struct Assignment {
  std::vector<int32_t> row_to_col;   // size = number of rows (left vertices)
  double               total_cost = 0.0;
  // Why a call produced nothing. `Ok` with an empty row_to_col means the input
  // had zero rows, which is a legitimate no-op; anything else is malformed input.
  enum class Status {
    Ok,
    EmptyInput,        // zero rows
    Ragged,            // rows of differing widths, or a zero-width row
    MoreRowsThanCols,  // violates the documented rows <= cols contract
    NonFiniteCost,     // a NaN or infinite entry
  } status = Status::Ok;
  bool ok() const { return status == Status::Ok; }
};
const char* to_string(Assignment::Status s);

// Validates its input rather than trusting it. A ragged matrix used to read past
// the end of a short row -- undefined behaviour reached from a plain data error --
// and a NaN entry silently poisons the dual potentials so every subsequent
// comparison is false and the algorithm returns an arbitrary assignment that
// still looks well-formed. Both are now refused with a status.
//
// Determinism on equal costs: the potentials method scans columns left to right
// and takes the FIRST minimum (`cur < minv[j]`, `minv[j] < delta`, both strict),
// so among several optimal assignments it always returns the same one. That is
// asserted directly in tests/graph/matching_test.cpp -- several optima exist on
// any symmetric cost matrix, and the dispatch plan must not depend on which one
// the compiler's floating-point happened to reach first.
Assignment hungarian(const std::vector<std::vector<double>>& cost);

}  // namespace safetrail::graph
