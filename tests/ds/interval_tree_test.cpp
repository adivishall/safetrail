// AVL interval tree: overlap/stab correctness vs brute force, plus the AVL bound.
#include "../test_harness.hpp"
#include "safetrail/ds/interval_tree.hpp"
#include "safetrail/sim/mobility.hpp"
#include <algorithm>
#include <vector>
using namespace safetrail::ds;
using safetrail::Timestamp;

int main() {
  // hand-checked basics
  IntervalTree<int> t;
  t.insert(0, 10, 1);
  t.insert(5, 15, 2);
  t.insert(20, 30, 3);
  std::vector<int> o;
  o.clear(); t.stabbing(7, o);  std::sort(o.begin(),o.end());
  t::ok((o == std::vector<int>{1,2}), "stab at 7 hits [0,10) and [5,15)");
  o.clear(); t.stabbing(25, o);
  t::ok((o == std::vector<int>{3}), "stab at 25 hits only [20,30)");
  o.clear(); t.stabbing(17, o);
  t::ok(o.empty(), "stab at 17 hits nothing (gap)");
  o.clear(); t.overlapping(9, 21, o); std::sort(o.begin(),o.end());
  t::ok((o == std::vector<int>{1,2,3}), "overlap [9,21) hits all three");

  // half-open boundary: [0,10) does NOT contain 10
  o.clear(); t.stabbing(10, o);
  t::ok(std::find(o.begin(),o.end(),1)==o.end(), "half-open: 10 not in [0,10)");

  // randomised vs brute force + AVL balance check
  safetrail::sim::Rng rng(9001);
  for (int trial = 0; trial < 30; ++trial) {
    IntervalTree<int> it;
    std::vector<std::array<Timestamp,3>> ref;   // low, high, id
    const int n = 5 + int(rng.below(200));
    for (int i = 0; i < n; ++i) {
      Timestamp lo = Timestamp(rng.below(1000));
      Timestamp hi = lo + 1 + Timestamp(rng.below(200));
      it.insert(lo, hi, i);
      ref.push_back({lo, hi, i});
    }
    // stab correctness at random points
    int bad = 0;
    for (int q = 0; q < 20; ++q) {
      Timestamp at = Timestamp(rng.below(1200));
      std::vector<int> got; it.stabbing(at, got);
      std::vector<int> want;
      for (auto& r : ref) if (at >= r[0] && at < r[1]) want.push_back(int(r[2]));
      std::sort(got.begin(),got.end()); std::sort(want.begin(),want.end());
      if (got != want) ++bad;
    }
    t::ok(bad == 0, "trial " + std::to_string(trial) + ": stab == brute force");
    t::ok(it.balanced(), "trial " + std::to_string(trial) +
          ": AVL height within 1.44 log2(n+2)");
  }
  return t::report("ds/interval_tree");
}
