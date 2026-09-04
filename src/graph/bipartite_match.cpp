#include "safetrail/graph/bipartite_match.hpp"

#include <cfloat>
#include <cmath>

namespace safetrail::graph {

// ── Kuhn's algorithm ──────────────────────────────────────────────────────────
namespace {
// Try to find an augmenting path from left vertex u, flipping matches along it.
bool try_augment(int32_t u, const std::vector<std::vector<int32_t>>& adj,
                 std::vector<int32_t>& match_right, std::vector<char>& seen) {
  for (int32_t v : adj[size_t(u)]) {
    if (seen[size_t(v)]) continue;
    seen[size_t(v)] = 1;
    // v is free, or the vertex currently holding v can be rehomed elsewhere.
    if (match_right[size_t(v)] < 0 ||
        try_augment(match_right[size_t(v)], adj, match_right, seen)) {
      match_right[size_t(v)] = u;
      return true;
    }
  }
  return false;
}
}  // namespace

std::vector<int32_t> kuhn_matching(int32_t left_n, int32_t right_n,
                                   const std::vector<std::vector<int32_t>>& adj,
                                   int32_t* matched_count) {
  std::vector<int32_t> match_right(size_t(right_n), -1);
  int32_t count = 0;
  for (int32_t u = 0; u < left_n; ++u) {
    std::vector<char> seen(size_t(right_n), 0);
    if (try_augment(u, adj, match_right, seen)) ++count;
  }
  if (matched_count) *matched_count = count;
  return match_right;
}

// ── Hungarian algorithm (Kuhn-Munkres), O(n^3) potentials method ──────────────
//
// The classic shortest-augmenting-path formulation with dual potentials u, v.
// Internally 1-indexed (index 0 is the sentinel the augmenting path terminates
// on); the public interface is 0-indexed. Handles rectangular n <= m: every left
// row gets a column, surplus columns are left unassigned.
const char* to_string(Assignment::Status s) {
  switch (s) {
    case Assignment::Status::Ok: return "ok";
    case Assignment::Status::EmptyInput: return "empty input";
    case Assignment::Status::Ragged: return "ragged cost matrix";
    case Assignment::Status::MoreRowsThanCols: return "more rows than columns";
    case Assignment::Status::NonFiniteCost: return "non-finite cost entry";
  }
  return "?";
}

Assignment hungarian(const std::vector<std::vector<double>>& cost) {
  Assignment result;
  const int n = int(cost.size());
  if (n == 0) { result.status = Assignment::Status::EmptyInput; return result; }
  const int m = int(cost[0].size());
  if (m == 0) { result.status = Assignment::Status::Ragged; return result; }
  for (const auto& row : cost) {
    if (int(row.size()) != m) { result.status = Assignment::Status::Ragged; return result; }
    for (double c : row)
      if (!std::isfinite(c)) { result.status = Assignment::Status::NonFiniteCost; return result; }
  }
  if (m < n) { result.status = Assignment::Status::MoreRowsThanCols; return result; }

  const double INF = DBL_MAX / 4.0;
  // a[1..n][1..m], 1-indexed copy.
  std::vector<std::vector<double>> a(size_t(n + 1), std::vector<double>(size_t(m + 1), 0.0));
  for (int i = 1; i <= n; ++i)
    for (int j = 1; j <= m; ++j) a[size_t(i)][size_t(j)] = cost[size_t(i - 1)][size_t(j - 1)];

  std::vector<double> u(size_t(n + 1), 0.0), v(size_t(m + 1), 0.0);
  std::vector<int>    p(size_t(m + 1), 0), way(size_t(m + 1), 0);

  for (int i = 1; i <= n; ++i) {
    p[0] = i;
    int j0 = 0;
    std::vector<double> minv(size_t(m + 1), INF);
    std::vector<char>   used(size_t(m + 1), 0);
    do {
      used[size_t(j0)] = 1;
      const int i0 = p[size_t(j0)];
      double delta = INF;
      int j1 = -1;
      for (int j = 1; j <= m; ++j)
        if (!used[size_t(j)]) {
          const double cur = a[size_t(i0)][size_t(j)] - u[size_t(i0)] - v[size_t(j)];
          if (cur < minv[size_t(j)]) { minv[size_t(j)] = cur; way[size_t(j)] = j0; }
          if (minv[size_t(j)] < delta) { delta = minv[size_t(j)]; j1 = j; }
        }
      for (int j = 0; j <= m; ++j) {
        if (used[size_t(j)]) { u[size_t(p[size_t(j)])] += delta; v[size_t(j)] -= delta; }
        else                   minv[size_t(j)] -= delta;
      }
      if (j1 < 0) { result.status = Assignment::Status::NonFiniteCost; return result; }
      j0 = j1;
    } while (p[size_t(j0)] != 0);
    // Unwind the augmenting path.
    do {
      const int j1 = way[size_t(j0)];
      p[size_t(j0)] = p[size_t(j1)];
      j0 = j1;
    } while (j0);
  }

  result.row_to_col.assign(size_t(n), -1);
  for (int j = 1; j <= m; ++j)
    if (p[size_t(j)] != 0) result.row_to_col[size_t(p[size_t(j)] - 1)] = j - 1;

  double total = 0.0;
  for (int i = 0; i < n; ++i)
    if (result.row_to_col[size_t(i)] >= 0)
      total += cost[size_t(i)][size_t(result.row_to_col[size_t(i)])];
  result.total_cost = total;
  return result;
}

}  // namespace safetrail::graph
