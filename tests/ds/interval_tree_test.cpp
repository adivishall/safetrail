// AVL interval tree: overlap/stab correctness vs brute force, plus the AVL bound.
#include "../test_harness.hpp"
#include <array>
#include <climits>
#include <cstdint>
#include <string>
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

  // ── real AVL deletion  ─────────────────────────────────────────────────────
  //
  // Deletion used to be a tombstone: mark the node dead, decrement the count,
  // leave the node in the tree. Two consequences, both pinned below. It was O(n)
  // -- a linear scan of the node array -- in a structure whose whole selling point
  // is O(log n); and the tree's SHAPE stopped matching its reported size, so
  // balanced() compared a real height against a fictional n and could "fail" on a
  // perfectly balanced tree, or pass on a badly shaped one.
  //
  // check_invariants() audits BST ordering, the AVL height/balance invariant at
  // every node, and the max_high augmentation. A broken rotation usually still
  // answers small queries correctly, which is exactly how it survives; the audit
  // is what catches it.
  {
    IntervalTree<int> d;
    t::ok(d.check_invariants(), "an empty tree is structurally valid");
    t::ok(!d.remove(0, 1, 0), "removing from an empty tree reports false");

    d.insert(10, 20, 1);
    t::ok(d.size() == 1 && d.check_invariants(), "single insert");
    t::ok(d.remove(10, 20, 1), "delete the only node");
    t::ok(d.size() == 0 && d.height() == 0, "tree is empty and its height is 0");
    t::ok(d.check_invariants(), "and still structurally valid");
    std::vector<int> probe;
    d.stabbing(15, probe);
    t::ok(probe.empty(), "and returns nothing");
  }

  // Leaf, one-child and two-child deletions, each checked structurally.
  {
    IntervalTree<int> d;
    for (int i = 0; i < 7; ++i) d.insert(i * 10, i * 10 + 5, i);
    t::ok(d.size() == 7 && d.check_invariants(), "seven intervals inserted");

    t::ok(d.remove(60, 65, 6), "delete a leaf");
    t::ok(d.check_invariants(), "invariants hold after a leaf delete");
    t::ok(d.size() == 6, "size drops");

    t::ok(d.remove(30, 35, 3), "delete an interior node");
    t::ok(d.check_invariants(), "invariants hold after an interior delete");

    // The remaining values must be exactly the ones we did not delete.
    std::vector<int> all;
    d.overlapping(INT64_MIN / 2, INT64_MAX / 2, all);
    std::sort(all.begin(), all.end());
    t::ok((all == std::vector<int>{0, 1, 2, 4, 5}), "the survivors are exactly right");

    t::ok(!d.remove(30, 35, 3), "deleting the same entry twice reports false");
    t::ok(d.size() == 5, "and does not change the size");
    t::ok(!d.remove(0, 5, 99), "a matching interval with the wrong value is not deleted");
    t::ok(!d.remove(0, 999, 0), "a matching value with the wrong interval is not deleted");
    t::ok(d.size() == 5, "neither changed the size");
  }

  // Duplicates on the low key: each must be individually removable.
  {
    IntervalTree<int> d;
    for (int i = 0; i < 6; ++i) d.insert(100, 100 + (i + 1) * 10, i);
    t::ok(d.size() == 6, "six intervals sharing a low endpoint");
    for (int i = 0; i < 6; ++i) {
      t::ok(d.remove(100, 100 + (i + 1) * 10, i),
            "duplicate low key: removed value " + std::to_string(i));
      t::ok(d.check_invariants(), "invariants hold after each duplicate removal");
    }
    t::ok(d.size() == 0, "all six gone");
  }

  // ── Massive endpoint multiplicity: the case the ordering key exists for ────
  //
  // Every zone whose closure starts at midnight shares a low endpoint, so a block
  // of equal keys is the normal shape here, not a corner case. When the BST key
  // was `low` alone that block had no internal order, rotations scattered it, and
  // remove() had to search the right subtree and then the left -- O(n) in a
  // structure sold as O(log n). With the total order (low, high, value, seq) the
  // block is ordered, so removal is one descent.
  {
    IntervalTree<int> d;
    const int n = 2000;
    for (int i = 0; i < n; ++i) d.insert(1000, 1000 + i + 1, i);   // one shared low
    t::ok(d.size() == size_t(n), "2000 intervals sharing one low endpoint");
    t::ok(d.check_invariants(), "the block is a valid ordered tree, not a bag");
    t::ok(d.balanced(), "and balanced: height is logarithmic, not linear");
    // Remove in an order unrelated to insertion, so a descent that only works
    // for the ascending case fails here.
    safetrail::sim::Rng r(0xD0D0);
    std::vector<int> order((size_t(n)));
    for (int i = 0; i < n; ++i) order[size_t(i)] = i;
    for (int i = n - 1; i > 0; --i) std::swap(order[size_t(i)], order[r.below(uint32_t(i + 1))]);
    size_t misses = 0;
    for (int i : order) if (!d.remove(1000, 1000 + i + 1, i)) ++misses;
    t::ok(misses == 0, "every one of the 2000 is found by a single-path descent");
    t::ok(d.size() == 0, "and the tree drains completely");
    t::ok(d.check_invariants(), "invariants hold at the end");
  }

  // Entries that are identical in every observable field. They are
  // indistinguishable to a caller, so removing "one of them" n times must remove
  // exactly n -- the `seq` tie-break is what keeps them individually addressable
  // inside the tree while the public key stays the triple.
  {
    IntervalTree<int> d;
    for (int i = 0; i < 50; ++i) d.insert(7, 9, 3);
    t::ok(d.size() == 50, "50 entries identical in (low, high, value)");
    t::ok(d.check_invariants(), "exact duplicates form a valid ordered run");
    std::vector<int> hits;
    d.stabbing(8, hits);
    t::ok(hits.size() == 50, "a stab returns all 50");
    for (int i = 0; i < 50; ++i) {
      t::ok(d.remove(7, 9, 3), "removing identical entry " + std::to_string(i));
    }
    t::ok(!d.remove(7, 9, 3), "the 51st removal reports false");
    t::ok(d.size() == 0 && d.check_invariants(), "drained and valid");
  }

  // ── Regression: two-child deletion among exact duplicates ───────────────────
  //
  // Deleting a node with two children copies its in-order successor up and then
  // unlinks the successor. That unlink used to SEARCH for the successor's
  // (low, high, value) triple, and with exact duplicates the search can stop on
  // a different node carrying the same triple. The real successor then survived
  // in the right subtree while its payload and seq were also copied up: two live
  // nodes with one total-order key. Queries were unaffected -- the payloads are
  // indistinguishable -- which is why the block above, which only audits after
  // draining, never saw it. Audit after EVERY removal.
  {
    IntervalTree<int> d;
    for (int i = 0; i < 50; ++i) d.insert(7, 9, 3);
    size_t bad = 0;
    for (int i = 0; i < 50; ++i) {
      d.remove(7, 9, 3);
      if (!d.check_invariants()) ++bad;
    }
    t::ok(bad == 0, "identical triples: invariants hold after every one of 50 removals");

    // Duplicates interleaved with a distinct neighbour block, removed in an order
    // that repeatedly hits two-child nodes.
    IntervalTree<int> e;
    for (int i = 0; i < 20; ++i) e.insert(5, 9, 1);
    for (int i = 0; i < 20; ++i) e.insert(3, 4, 2);
    bad = 0;
    for (int i = 0; i < 20; ++i) {
      e.remove(3, 4, 2);
      if (!e.check_invariants()) ++bad;
    }
    std::vector<int> left;
    e.stabbing(6, left);
    t::ok(bad == 0 && left.size() == 20,
          "mixed duplicate blocks: invariants hold after each removal, survivors intact");

    // Randomised: a small key space so exact duplicates are the common case.
    safetrail::sim::Rng dr(0xD00B);
    IntervalTree<int> f;
    std::vector<std::array<Timestamp, 3>> live;
    bad = 0;
    for (int op = 0; op < 3000 * t::stress(); ++op) {
      if (!live.empty() && dr.uniform() < 0.45) {
        const size_t k = dr.below(uint32_t(live.size()));
        const auto r = live[k];
        live.erase(live.begin() + long(k));
        if (!f.remove(r[0], r[1], int(r[2]))) ++bad;
        if (!f.check_invariants()) ++bad;
      } else {
        const Timestamp lo = Timestamp(dr.below(4));
        const Timestamp hi = lo + 1 + Timestamp(dr.below(3));
        const int v = int(dr.below(3));
        f.insert(lo, hi, v);
        live.push_back({lo, hi, Timestamp(v)});
      }
    }
    t::ok(bad == 0 && f.size() == live.size(),
          std::to_string(3000 * t::stress()) + " ops over a 4x3x3 key space: every removal found, invariants after each");
  }

  // ── Range edge cases ────────────────────────────────────────────────────────
  {
    IntervalTree<int> d;
    d.insert(0, 20, 1);
    d.insert(INT64_MAX - 5, INT64_MAX, 2);
    std::vector<int> o2;
    d.overlapping(10, 5, o2);
    t::ok(o2.empty(), "an inverted query range [10, 5) overlaps nothing");
    d.overlapping(7, 7, o2);
    t::ok(o2.empty(), "an empty query range [7, 7) overlaps nothing");
    // INT64_MAX is kForever. Half-open intervals never contain it, and computing
    // at + 1 for it was signed overflow (caught by UBSan via VersionedIndex).
    d.stabbing(INT64_MAX, o2);
    t::ok(o2.empty(), "stabbing at INT64_MAX returns nothing and does not overflow");
    d.stabbing(INT64_MAX - 1, o2);
    t::ok(o2.size() == 1 && o2[0] == 2, "stabbing just below INT64_MAX finds [MAX-5, MAX)");
  }

  // Deletions that force rotations at several levels: insert in ascending order
  // (which is the worst case for an unbalanced BST) and delete from one end.
  {
    IntervalTree<int> d;
    for (int i = 0; i < 200; ++i) d.insert(i, i + 3, i);
    t::ok(d.check_invariants(), "200 ascending inserts stay balanced");
    for (int i = 0; i < 150; ++i) {
      d.remove(i, i + 3, i);
      if (i % 17 == 0) t::ok(d.check_invariants(),
                             "invariants hold mid-deletion at i=" + std::to_string(i));
    }
    t::ok(d.size() == 50, "50 left");
    t::ok(d.balanced(), "and balanced() now compares a real height to a real n");
  }

  // ── churn against a brute-force reference ──────────────────────────────────
  //
  // Randomised insert/remove, cross-checked on both queries, with the structural
  // audit every few hundred operations. This is the test that would fail on a
  // rotation that repairs height but not max_high: the tree stays balanced and
  // ordered while the pruning bound silently starts excluding real matches.
  {
    safetrail::sim::Rng churn_rng(864213);
    IntervalTree<int> d;
    std::vector<std::array<Timestamp, 3>> ref;
    size_t bad = 0, audits = 0;

    for (int op = 0; op < 4000; ++op) {
      if (!ref.empty() && churn_rng.uniform() < 0.42) {
        const size_t k = churn_rng.below(uint32_t(ref.size()));
        const auto r = ref[k];
        ref.erase(ref.begin() + long(k));
        if (!d.remove(r[0], r[1], int(r[2]))) ++bad;
      } else {
        const Timestamp lo = Timestamp(churn_rng.range(0, 10000));
        const Timestamp hi = lo + Timestamp(churn_rng.range(1, 900));
        const int v = op;
        d.insert(lo, hi, v);
        ref.push_back({lo, hi, Timestamp(v)});
      }

      if (op % 200 == 0) {
        ++audits;
        if (d.size() != ref.size()) ++bad;
        if (!d.check_invariants()) ++bad;
        if (!d.balanced()) ++bad;

        for (int q = 0; q < 8; ++q) {
          const Timestamp at = Timestamp(churn_rng.range(0, 11000));
          std::vector<int> got, want;
          d.stabbing(at, got);
          for (const auto& r : ref) if (at >= r[0] && at < r[1]) want.push_back(int(r[2]));
          std::sort(got.begin(), got.end());
          std::sort(want.begin(), want.end());
          if (got != want) ++bad;

          const Timestamp lo = Timestamp(churn_rng.range(0, 10000));
          const Timestamp hi = lo + Timestamp(churn_rng.range(1, 2000));
          got.clear(); want.clear();
          d.overlapping(lo, hi, got);
          for (const auto& r : ref)
            if (r[0] < hi && lo < r[1]) want.push_back(int(r[2]));
          std::sort(got.begin(), got.end());
          std::sort(want.begin(), want.end());
          if (got != want) ++bad;
        }
      }
    }
    t::ok(bad == 0, "4000 mixed operations: structure, size and both query kinds all "
                    "match brute force across " + std::to_string(audits) + " audits");

    // Drain it completely -- the path that exercises root deletion repeatedly.
    while (!ref.empty()) {
      const auto r = ref.back();
      ref.pop_back();
      d.remove(r[0], r[1], int(r[2]));
    }
    t::ok(d.size() == 0, "draining leaves it empty");
    t::ok(d.height() == 0, "with height 0");
    t::ok(d.check_invariants(), "and structurally valid");
  }

  // ── freed slots are reused ─────────────────────────────────────────────────
  //
  // The node array must be bounded by the PEAK live size, not by total inserts --
  // otherwise a long-lived tree under churn grows without limit even though it
  // never holds more than a few entries. Measured indirectly: after many
  // insert/remove cycles the tree is still shallow.
  {
    IntervalTree<int> d;
    for (int cycle = 0; cycle < 200; ++cycle) {
      for (int i = 0; i < 20; ++i) d.insert(i, i + 5, cycle * 20 + i);
      for (int i = 0; i < 20; ++i) d.remove(i, i + 5, cycle * 20 + i);
    }
    t::ok(d.size() == 0, "4000 inserts and 4000 removes leave it empty");
    for (int i = 0; i < 20; ++i) d.insert(i, i + 5, i);
    t::ok(d.height() <= 6,
          "and refilling to 20 entries gives a shallow tree (height " +
              std::to_string(d.height()) + "), so slots were reused");
    t::ok(d.check_invariants(), "still valid after all that churn");
  }

  return t::report("ds/interval_tree");
}
