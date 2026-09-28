#!/usr/bin/env python3
"""Mutation check: do the tests actually catch broken code?

Each mutant below is one deliberate, realistic bug in the core -- a pruning test
that skips a child, a refit that forgets a subtree, a state machine step that is
never run. For every mutant this script copies the tree, applies the edit,
rebuilds only what changed, and runs the named tests. A mutant is KILLED if any
of its tests fails; it SURVIVES if they all pass. Any survivor fails the run.

Why it exists: a green suite proves nothing on its own. If a test cannot fail
when the code it guards is wrong, it is decoration. This is the check on the
checks, and it is how the gaps behind several regression tests in this
repository were confirmed before the fixes went in.

Usage:  python3 tools/mutation_check.py [substring-filter]
        make mutation
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (name, file, exact text to replace, replacement, tests that must catch it)
MUTANTS = [
    # ── spatial indexes ─────────────────────────────────────────────────────
    ("quadtree query skips the NE child",
     "src/index/quadtree.cpp",
     "if (!n->leaf()) for (int i = 0; i < 4; ++i) query_node(n->kids[i].get(), q, out);",
     "if (!n->leaf()) for (int i = 0; i < 3; ++i) query_node(n->kids[i].get(), q, out);",
     ["index/differential_test", "index/equivalence_test"]),
    ("quadtree collapse loses grandchildren",
     "src/index/quadtree.cpp",
     "  if (!n->leaf()) for (int i = 0; i < 4; ++i) gather(n->kids[i].get(), out);",
     "",
     ["index/differential_test", "index/churn_test"]),
    ("quadtree root expansion misplaces the old root",
     "src/index/quadtree.cpp",
     "const int slot = (north ? 0 : 2) + (east ? 0 : 1);",
     "const int slot = (north ? 2 : 0) + (east ? 0 : 1);",
     ["index/differential_test", "index/churn_test"]),
    ("r-tree insert does not grow the node box",
     "src/index/rtree.cpp",
     "                                       size_t max_entries, size_t min_entries) {\n  n->box.expand(box);",
     "                                       size_t max_entries, size_t min_entries) {",
     ["index/differential_test"]),
    ("r-tree remove drops orphans instead of reinserting",
     "src/index/rtree.cpp",
     "    insert(e.first, e.second);\n    --count_;",
     "    (void)e;\n    --count_;",
     ["index/differential_test", "index/churn_test"]),
    # ── interval tree ───────────────────────────────────────────────────────
    ("interval tree max_high ignores the right subtree",
     "include/safetrail/ds/interval_tree.hpp",
     "    if (mh(n.right) > n.max_high) n.max_high = mh(n.right);\n  }",
     "  }",
     ["ds/interval_tree_test", "index/persistence_differential_test"]),
    ("interval tree skips the left-right double rotation",
     "include/safetrail/ds/interval_tree.hpp",
     "if (h(nodes_[size_t(l)].left) < h(nodes_[size_t(l)].right))",
     "if (false)",
     ["ds/interval_tree_test"]),
    ("interval tree prune is off by one",
     "include/safetrail/ds/interval_tree.hpp",
     "if (n.max_high <= low) return;",
     "if (n.max_high <= low + 1) return;",
     ["ds/interval_tree_test"]),
    # ── persistent quadtree ─────────────────────────────────────────────────
    ("persistent index filters history with TODAY's validity",
     "src/index/versioned_index.cpp",
     "const ValidityRecord* r = record_as_of(id, v);       // ← as of THAT version",
     "const ValidityRecord* r = record_as_of(id, latest_version());",
     ["index/persistence_differential_test", "index/versioned_index_test"]),
    ("persistent index accepts out-of-order commit times",
     "src/index/versioned_index.cpp",
     "return at < version_times_.back() ? version_times_.back() : at;",
     "return at;",
     ["index/persistence_differential_test"]),
    ("persistent index re-add duplicates instead of replacing",
     "src/index/versioned_index.cpp",
     "if (found) base = std::move(without);",
     "(void)without;",
     ["index/persistence_differential_test"]),
    # ── evaluator / state machine ───────────────────────────────────────────
    ("evaluator never reconciles zones outside the candidate window",
     "src/fence/evaluator.cpp",
     "  reconcile(t, epoch, now_ms, out);",
     "  (void)epoch;",
     ["fence/state_reconciliation_test"]),
    ("evaluator processes candidates in index order",
     "src/fence/evaluator.cpp",
     "  std::sort(candidate_buf_.begin(), candidate_buf_.end());\n\n  if (candidate_buf_.size() > cfg_.max_candidates) {",
     "\n  if (candidate_buf_.size() > cfg_.max_candidates) {",
     ["fence/index_independence_test"]),
    ("evaluator ignores validity windows",
     "src/fence/evaluator.cpp",
     "    if (!z->validity.active_at(now_ms)) continue;",
     "",
     ["fence/state_reconciliation_test"]),
    ("hysteresis lets an Uncertain fix complete a pending exit",
     "src/fence/hysteresis.cpp",
     "        phase_ = Phase::Inside;                   // cancel it, report no change\n        return geo::Containment::Inside;",
     "        return geo::Containment::Uncertain;",
     ["fence/hysteresis_test"]),
    ("hysteresis passes the Uncertain band straight through",
     "src/fence/hysteresis.cpp",
     "      if (from == Phase::Outside) return geo::Containment::Outside;",
     "      phase_ = Phase::Outside; return geo::Containment::Outside;",
     ["fence/hysteresis_test"]),
    # ── geometry ────────────────────────────────────────────────────────────
    ("ray casting ignores holes",
     "src/geo/containment.cpp",
     "    c += ring_crossings(h, p, &on_boundary);",
     "    (void)h;",
     ["geo/polygon_holes_test", "geo/ray_casting_test"]),
    ("containment ignores the GPS uncertainty radius",
     "src/geo/containment.cpp",
     "  return classify(signed_distance_m(poly, p.pos), p.accuracy_m);",
     "  return classify(signed_distance_m(poly, p.pos), 0.0);",
     ["geo/containment_uncertainty_test", "geo/ray_casting_test"]),
    ("on-boundary tolerance back to an absolute cross product",
     "src/geo/segment.cpp",
     "if (std::fabs(cross) > kOnEdgeEps * std::sqrt(dx * dx + dy * dy)) return false;",
     "if (std::fabs(cross) > 1e-11) return false;",
     ["geo/ray_casting_test"]),
    ("query box drops the 1/cos(lat) longitude stretch",
     "src/geo/bbox.cpp",
     "std::asin(std::sin(delta) / std::cos(phi))",
     "std::asin(std::sin(delta))",
     ["geo/bbox_around_test"]),
    ("sweep line looks at only the immediate neighbour",
     "src/geo/sweep_line.cpp",
     "constexpr int kReach = 3;",
     "constexpr int kReach = 1;",
     ["geo/sweep_line_test"]),
    ("sweep line tests only the first non-exempt neighbour on insert",
     "src/geo/sweep_line.cpp",
     "      for (int i = 0; i < kReach; ++i)\n        if (test(e.si, lo[i]) || test(e.si, hi[i])) return true;",
     "      for (int i = 0; i < kReach; ++i) if (lo[i] == SIZE_MAX || eligible(e.si, lo[i])) { if (test(e.si, lo[i])) return true; break; }\n"
     "      for (int i = 0; i < kReach; ++i) if (hi[i] == SIZE_MAX || eligible(e.si, hi[i])) { if (test(e.si, hi[i])) return true; break; }",
     ["geo/sweep_line_test"]),
]


def run(cmd, cwd, env=None):
    return subprocess.run(cmd, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                          env=env).returncode


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else ""
    mutants = [m for m in MUTANTS if only in m[0]]
    work = tempfile.mkdtemp(prefix="safetrail-mutation-")
    try:
        for d in ("include", "src", "tests", "apps", "data", "tools"):
            shutil.copytree(os.path.join(ROOT, d), os.path.join(work, d))
        shutil.copy(os.path.join(ROOT, "Makefile"), work)
        jobs = str(os.cpu_count() or 4)
        tests = sorted({t for m in mutants for t in m[4]})
        targets = ["build/test/" + t for t in tests]

        # The unmutated tree must build and pass, or a "kill" would mean nothing.
        if run(["make", "-s", "-j" + jobs] + targets, work) != 0:
            print("baseline build failed"); return 2
        for t in tests:
            if run(["./build/test/" + t], work) != 0:
                print("baseline test fails: " + t); return 2

        survived = errors = 0
        for name, rel, old, new, kill_tests in mutants:
            path = os.path.join(work, rel)
            original = open(path).read()
            if original.count(old) != 1:
                print("  ERROR     %s  (pattern matches %d times)" % (name, original.count(old)))
                errors += 1
                continue
            open(path, "w").write(original.replace(old, new))
            try:
                if run(["make", "-s", "-j" + jobs] + ["build/test/" + t for t in kill_tests], work) != 0:
                    print("  ERROR     %s  (mutant does not compile)" % name)
                    errors += 1
                    continue
                killer = next((t for t in kill_tests if run(["./build/test/" + t], work) != 0), None)
                if killer:
                    print("  killed    %-58s by %s" % (name, killer))
                else:
                    print("  SURVIVED  %s" % name)
                    survived += 1
            finally:
                open(path, "w").write(original)     # fresh mtime -> make rebuilds it

        total = len(mutants)
        print("\nmutation check: %d/%d killed, %d survived, %d errors"
              % (total - survived - errors, total, survived, errors))
        return 0 if survived == 0 and errors == 0 else 1
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
