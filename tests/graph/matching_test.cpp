// Bipartite matching: Kuhn's and Hungarian against brute force.
//
// Both algorithms are checked against exhaustive search on small instances --
// Kuhn's max-cardinality against a subset/permutation search, Hungarian's minimum
// cost against every permutation. Small n keeps the O(n!) oracle honest and fast.
#include <algorithm>
#include <functional>
#include <vector>
#include "safetrail/graph/bipartite_match.hpp"
#include "../test_harness.hpp"

using namespace safetrail;
using namespace safetrail::graph;

namespace {
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  double unit() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
  int below(int n) { return int(next() % uint64_t(n)); }
};

// Max cardinality by brute force: try to match left vertices greedily over every
// ordering is overkill; instead do the standard recursive "assign left i to some
// free reachable right, backtrack" and take the max -- which for small n is the
// exhaustive truth.
int brute_max_matching(int ln, int rn, const std::vector<std::vector<char>>& can) {
  std::vector<int> right_taken(size_t(rn), -1);
  int best = 0;
  // Recursive search over left vertices, each either unmatched or matched to a
  // free reachable right vertex.
  std::function<void(int, int)> rec = [&](int i, int cnt) {
    if (i == ln) { best = std::max(best, cnt); return; }
    rec(i + 1, cnt);   // leave i unmatched
    for (int j = 0; j < rn; ++j)
      if (can[size_t(i)][size_t(j)] && right_taken[size_t(j)] < 0) {
        right_taken[size_t(j)] = i;
        rec(i + 1, cnt + 1);
        right_taken[size_t(j)] = -1;
      }
  };
  rec(0, 0);
  return best;
}
}  // namespace

int main() {
  Rng rng(0x9A7C1);

  // ── Kuhn's == brute-force max cardinality ──────────────────────────────────
  for (int trial = 0; trial < 200; ++trial) {
    const int ln = 1 + rng.below(6), rn = 1 + rng.below(6);
    std::vector<std::vector<char>> can(size_t(ln), std::vector<char>(size_t(rn), 0));
    std::vector<std::vector<int32_t>> adj((size_t(ln)));
    for (int i = 0; i < ln; ++i)
      for (int j = 0; j < rn; ++j)
        if (rng.unit() < 0.45) { can[size_t(i)][size_t(j)] = 1; adj[size_t(i)].push_back(j); }

    int32_t kc = 0;
    kuhn_matching(ln, rn, adj, &kc);
    const int bc = brute_max_matching(ln, rn, can);
    t::ok(kc == bc, "kuhn == brute max matching (trial " + std::to_string(trial) +
                    ": " + std::to_string(kc) + " vs " + std::to_string(bc) + ")");
  }

  // ── Kuhn's output is a valid matching (no right vertex reused, edges real) ──
  {
    std::vector<std::vector<int32_t>> adj = {{0, 1}, {0}, {1, 2}};
    int32_t c = 0;
    auto mr = kuhn_matching(3, 3, adj, &c);
    // Every matched right points back to a left that actually lists it.
    bool valid = true;
    for (int j = 0; j < 3; ++j)
      if (mr[size_t(j)] >= 0) {
        const auto& row = adj[size_t(mr[size_t(j)])];
        if (std::find(row.begin(), row.end(), int32_t(j)) == row.end()) valid = false;
      }
    t::ok(valid, "kuhn matching uses only real edges");
    t::ok(c == 3, "kuhn finds the perfect matching here");
  }

  // ── Hungarian == brute-force minimum-cost assignment ───────────────────────
  for (int trial = 0; trial < 120; ++trial) {
    const int n = 1 + rng.below(6);
    const int m = n + rng.below(3);          // rectangular n <= m
    std::vector<std::vector<double>> cost(size_t(n), std::vector<double>((size_t)m));
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < m; ++j) cost[size_t(i)][size_t(j)] = 1.0 + rng.below(100);

    const auto asg = hungarian(cost);

    // Brute force: over all injective row->col assignments, take the min sum.
    std::vector<int> cols((size_t)m);
    for (int j = 0; j < m; ++j) cols[size_t(j)] = j;
    double best = 1e18;
    // Choose which n of the m columns to use, in order, via permutations.
    std::sort(cols.begin(), cols.end());
    do {
      double s = 0.0;
      for (int i = 0; i < n; ++i) s += cost[size_t(i)][size_t(cols[size_t(i)])];
      best = std::min(best, s);
    } while (std::next_permutation(cols.begin(), cols.end()));

    t::near(asg.total_cost, best, 1e-6,
            "hungarian == brute min-cost (trial " + std::to_string(trial) + ")");

    // The assignment is injective (no column reused).
    std::vector<char> used(size_t(m), 0);
    bool injective = true;
    for (int i = 0; i < n; ++i) {
      const int c = asg.row_to_col[size_t(i)];
      if (c < 0 || used[size_t(c)]) injective = false; else used[size_t(c)] = 1;
    }
    t::ok(injective, "hungarian assignment is injective");
  }

  // ── Hungarian beats or ties greedy (the whole reason it exists) ────────────
  {
    // Classic case where nearest-first greedy is suboptimal.
    std::vector<std::vector<double>> cost = {{1.0, 2.0}, {1.0, 100.0}};
    const auto asg = hungarian(cost);
    // Greedy: row 0 grabs its min (col 0 = 1), forcing row 1 to col 1 = 100 -> 101.
    // Optimal: row 0 -> col 1 (2), row 1 -> col 0 (1) -> 3.
    t::near(asg.total_cost, 3.0, 1e-9, "hungarian finds the non-greedy optimum (3, not 101)");
  }

  return t::report("graph/matching");
}
