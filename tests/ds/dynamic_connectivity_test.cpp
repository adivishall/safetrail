// RollbackDSU vs the O(n^2) flood-fill oracle, plus the rollback property that
// plain path-compressed DSU cannot provide.
#include "../test_harness.hpp"
#include "safetrail/ds/dynamic_connectivity.hpp"
#include "safetrail/sim/mobility.hpp"
#include <algorithm>

using namespace safetrail::ds;

static std::vector<std::vector<size_t>> norm(std::vector<std::vector<size_t>> c) {
  for (auto& v : c) std::sort(v.begin(), v.end());
  std::sort(c.begin(), c.end());
  return c;
}

int main() {
  safetrail::sim::Rng rng(5150);

  for (int trial = 0; trial < 40; ++trial) {
    const size_t n = 6 + rng.below(30);
    RollbackDSU dsu(n);
    std::vector<std::pair<size_t, size_t>> edges;
    const size_t m = rng.below(uint32_t(n * 2));
    for (size_t i = 0; i < m; ++i) {
      const size_t a = rng.below(uint32_t(n)), b = rng.below(uint32_t(n));
      if (a == b) continue;
      edges.emplace_back(a, b);
      dsu.unite(a, b);
    }
    t::ok(norm(dsu.components()) == norm(components_bruteforce(n, edges)),
          "DSU matches flood-fill oracle, trial " + std::to_string(trial));
  }

  // The rollback property. This is why we gave up path compression.
  RollbackDSU d(10);
  d.unite(0, 1); d.unite(2, 3);
  const size_t mark = d.snapshot();
  const size_t before = d.component_count();
  d.unite(0, 2); d.unite(4, 5); d.unite(6, 7);
  t::ok(d.connected(1, 3), "after unions, 1 and 3 connected via 0-2");
  t::ok(d.component_count() == before - 3, "component count dropped by 3");
  d.rollback_to(mark);
  t::ok(!d.connected(1, 3), "rollback disconnected 1 and 3");
  t::ok(!d.connected(4, 5), "rollback undid 4-5");
  t::ok(d.connected(0, 1), "rollback preserved unions before the mark");
  t::ok(d.connected(2, 3), "rollback preserved 2-3");
  t::ok(d.component_count() == before, "component count restored");

  // Sizes must be restored, not just parents -- a common rollback bug.
  t::ok(d.component_size(0) == 2, "component size restored after rollback");

  // unite() on an already-connected pair must push nothing, so rollback is a no-op.
  const size_t m2 = d.snapshot();
  t::ok(!d.unite(0, 1), "unite on connected pair returns false");
  t::ok(d.snapshot() == m2, "no-op unite pushed nothing to the undo stack");

  return t::report("ds/dynamic_connectivity");
}
