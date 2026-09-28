#!/usr/bin/env python3
"""Render every benchmark table in the docs from bench/results/*.csv.

The documentation never contains a hand-typed benchmark number. Tables live
between markers --

    <!-- results:NAME -->
    ...generated...
    <!-- /results:NAME -->

-- in docs/RESULTS.md and README.md, and this script regenerates them from the
CSVs that `make bench` writes. `make bench` runs it; `--check` (run by
`make validate` and CI) fails if a committed doc disagrees with the committed
CSVs, so the two cannot drift.

Run-to-run bands come from bench/results/variation/run*/ (written by
`make bench-variation`): each band is the min-max of the ratio over the main
run plus every variation run.

Usage: python3 tools/render_results.py [--check]
"""
import csv
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "bench", "results")
DOCS = [os.path.join(ROOT, "docs", "RESULTS.md"), os.path.join(ROOT, "README.md")]


def rows(name, base=RES):
    path = os.path.join(base, name)
    if not os.path.exists(path):
        return []
    with open(path) as f:
        return list(csv.DictReader(f))


def runs(name):
    """The main run plus every variation run of one CSV."""
    out = [rows(name)]
    for d in sorted(glob.glob(os.path.join(RES, "variation", "run*"))):
        r = rows(name, d)
        if r:
            out.append(r)
    return [r for r in out if r]


def band(name, key_col, key, col):
    vals = []
    for r in runs(name):
        for row in r:
            if row[key_col] == key:
                vals.append(float(row[col]))
    if len(vals) < 2:
        return ""
    return "%s–%s×" % (x(min(vals)), x(max(vals)))


def x(v):
    """A ratio, with sensible precision."""
    return "%.0f" % v if v >= 100 else ("%.1f" % v if v >= 10 else "%.2f" % v)


def us(v):
    v = float(v)
    return "%.0f" % v if v >= 100 else ("%.1f" % v if v >= 10 else ("%.2f" % v if v >= 1 else "%.3f" % v))


def n(v):
    return "{:,}".format(int(float(v)))


def table(header, body, align=None):
    align = align or ["---:"] * len(header)
    out = ["| " + " | ".join(header) + " |", "|" + "|".join(align) + "|"]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in body]
    return "\n".join(out)


def nruns(name):
    return len(runs(name))


# ── blocks ────────────────────────────────────────────────────────────────────

def block_machine():
    """One line of benchmark conditions for prose, from environment.txt."""
    p = os.path.join(RES, "environment.txt")
    if not os.path.exists(p):
        return "conditions not recorded"
    env = {}
    for line in open(p):
        if ":" in line:
            k, v = line.split(":", 1)
            env[k.strip()] = v.strip()
    load = env.get("load", "").split()
    parts = [env.get("cpu", "?"), env.get("power", "?").replace(" Power", " power")]
    if load:
        parts.append("load average %s at the start" % load[0])
    parts.append("commit %s" % env.get("commit", "?"))
    return ", ".join(parts)


def block_env():
    p = os.path.join(RES, "environment.txt")
    return "```\n" + (open(p).read().rstrip() if os.path.exists(p) else "(not recorded)") + "\n```"


def scaling_block(name):
    body = []
    for r in rows(name):
        worst = max(float(r["brute_iqr_pct"]), float(r["quad_iqr_pct"]), float(r["rtree_iqr_pct"]))
        body.append([n(r["zones"]), us(r["brute_us"]), us(r["quad_us"]), us(r["rtree_us"]),
                     "**%s×**" % x(float(r["quad_speedup"])), "**%s×**" % x(float(r["rtree_speedup"])),
                     "%.2f" % float(r["candidates"]),
                     band(name, "zones", r["zones"], "quad_speedup") or "—",
                     band(name, "zones", r["zones"], "rtree_speedup") or "—",
                     "%.0f%%" % worst])
    return table(["zones", "brute µs", "quadtree µs", "R-tree µs", "quadtree ×", "R-tree ×",
                  "k / query", "quadtree × range", "R-tree × range", "worst IQR"], body)


def block_e2e():
    body = []
    for r in rows("end_to_end.csv"):
        body.append([r["vertices"], us(r["naive_us"]), us(r["scan_us"]), us(r["quad_us"]),
                     us(r["rtree_us"]), "**%s×**" % x(float(r["quad_vs_naive"])),
                     band("end_to_end.csv", "vertices", r["vertices"], "quad_vs_naive") or "—",
                     "%s×" % x(float(r["quad_vs_scan"])),
                     band("end_to_end.csv", "vertices", r["vertices"], "quad_vs_scan") or "—",
                     "%.2f" % float(r["candidates"]), "%.2f" % float(r["hits"])])
    return table(["V", "naive µs", "bbox scan µs", "quadtree µs", "R-tree µs", "quadtree vs naive",
                  "range", "quadtree vs scan", "range", "candidates / q", "hits / q"], body)


def block_costs():
    body = [[n(r["zones"]), r["index"], "%.2f" % float(r["build_ms"]),
             "{:,.0f}".format(float(r["bytes"]) / 1024), "%.0f" % float(r["bytes_per_zone"]),
             us(r["insert_us"]), us(r["remove_us"])] for r in rows("index_costs.csv")]
    return table(["zones", "index", "build ms", "memory KB", "bytes / zone", "insert µs",
                  "remove µs"], body, ["---:", "---", "---:", "---:", "---:", "---:", "---:"])


def block_str():
    body = [[n(r["zones"]), "%.1f" % float(r["incremental_build_ms"]), "%.1f" % float(r["str_build_ms"]),
             n(r["incremental_nodes"]), n(r["str_nodes"]), us(r["incremental_us"]), us(r["str_us"]),
             "**%.2f×**" % float(r["query_gain"])] for r in rows("index_build.csv")]
    return table(["zones", "insertion build ms", "STR build ms", "insertion nodes", "STR nodes",
                  "insertion µs / query", "STR µs / query", "STR query gain"], body)


def block_interval():
    body = [[r["regime"], n(r["intervals"]), us(r["scan_us"]), us(r["tree_us"]),
             "**%s×**" % x(float(r["speedup"])), "%.1f" % float(r["hits_per_stab"]), r["height"],
             "%.1f" % float(r["avl_bound"]), r["height_after_churn"], us(r["delete_us"])]
            for r in rows("interval_tree.csv")]
    return table(["regime", "windows", "scan µs", "tree µs", "tree ×", "k / stab", "height",
                  "AVL bound", "height after churn", "delete µs"], body,
                 ["---", "---:", "---:", "---:", "---:", "---:", "---:", "---:", "---:", "---:"])


def block_persistent():
    a = table(["versions", "nodes allocated", "if full-copied", "sharing", "query @ past µs",
               "query @ now µs"],
              [[n(r["versions"]), n(r["allocated"]), n(r["full_copies"]),
                "**%.1f×**" % float(r["sharing_ratio"]), us(r["query_past_us"]), us(r["query_now_us"])]
               for r in rows("versioned_index.csv")])
    b = table(["mutation (5,000-zone index)", "nodes allocated, mean", "max"],
              [[r["mutation"], "%.1f" % float(r["avg_nodes_allocated"]), r["max_nodes_allocated"]]
               for r in rows("versioned_mutations.csv")], ["---", "---:", "---:"])
    return a + "\n\n" + b


def block_extensions():
    out = []
    h = rows("hysteresis_ab.csv")
    if h:
        out.append(["Hysteresis A/B", "§10", "; ".join(
            "%s: filter removes %.1f%% of the naive run's excess transitions and reports %s vs %s "
            "that noise-free fixes give under the same policy" % (
                r["noise_model"], float(r["excess_removed_pct"]), n(r["filtered"]), n(r["target"]))
            for r in h)])
    ro = rows("routing.csv")
    if ro:
        r = ro[-1]
        out.append(["A* vs Dijkstra", "§11", "%s fewer nodes settled at %s nodes, same path"
                    % ("%.1f%%" % float(r["work_reduction_pct"]), n(r["nodes"]))])
    d = rows("dispatch.csv")
    if d:
        r = d[-1]
        out.append(["Hungarian vs greedy dispatch", "§12", "%.1f%% less total travel at %s responders; "
                    "never worse in any of 200 layouts" % (float(r["saved_pct"]), r["size"])
                    if r["optimal_never_worse"] == "1" else
                    "%.1f%% less total travel at %s responders" % (float(r["saved_pct"]), r["size"])])
    p = rows("power.csv")
    if p:
        r = p[0]
        out.append(["Adaptive GPS sampling", "§13", "%s → %s fixes over an 8 h trek (%.1f%% fewer) "
                    "at %.1f%% near-zone recall" % (n(r["continuous_fixes"]), n(r["adaptive_fixes"]),
                                                   float(r["battery_saved_pct"]),
                                                   float(r["near_zone_recall_pct"]))])
    c = rows("index_churn.csv")
    if c:
        nodes = {r["structure"]: float(r["ratio"]) for r in c if r["metric"] in ("nodes", "buckets")}
        out.append(["Index churn (20 × add/remove 2,000)", "§14", ", ".join(
            "%s %.2f×" % (k, v) for k, v in nodes.items()) + " node count vs fresh"])
    s = rows("serialization.csv")
    if s:
        r = s[-1]
        out.append(["Geohash serialisation", "§15", "%.0f bytes / zone; %s zones read in %.2f ms"
                    % (float(r["bytes_per_zone"]), n(r["zones"]), float(r["read_ms"]))])
    si = rows("self_intersection.csv")
    if si:
        cross = next((r["vertices"] for r in si if float(r["speedup"]) >= 1.0), "?")
        r = si[-1]
        out.append(["Shamos–Hoey vs pairwise validation", "§16",
                    "sweep first wins at %s vertices; %s× at %s; verdicts agree on every ring"
                    % (cross, x(float(r["speedup"])), n(r["vertices"]))])
    k = rows("node_snap.csv")
    if k:
        r = k[-1]
        out.append(["k-d tree vs linear snap", "§17", "%s× at %s junctions, same node every time"
                    % (x(float(r["speedup"])), n(r["nodes"]))])
    return table(["Extension", "§", "Measured"], out, ["---", "---", "---"])


def block_headline():
    """README: the three questions a reader asks first, 100,000 zones."""
    def get(name, key_col, key):
        return next((r for r in rows(name) if r[key_col] == key), None)
    fa, fd = get("index_scaling.csv", "zones", "100000"), get("index_density.csv", "zones", "100000")
    e = get("end_to_end.csv", "vertices", "128")
    body = []
    if fa:
        body.append(["Range query, 100,000 zones in one district (k ≈ %.0f)" % float(fa["candidates"]),
                     us(fa["brute_us"]) + " µs",
                     "%s× (%s)" % (x(float(fa["quad_speedup"])), band("index_scaling.csv", "zones", "100000", "quad_speedup")),
                     "%s× (%s)" % (x(float(fa["rtree_speedup"])), band("index_scaling.csv", "zones", "100000", "rtree_speedup"))])
    if fd:
        body.append(["Range query, 100,000 zones at constant density (k ≈ %.0f)" % float(fd["candidates"]),
                     us(fd["brute_us"]) + " µs",
                     "%s× (%s)" % (x(float(fd["quad_speedup"])), band("index_density.csv", "zones", "100000", "quad_speedup")),
                     "%s× (%s)" % (x(float(fd["rtree_speedup"])), band("index_density.csv", "zones", "100000", "rtree_speedup"))])
    if e:
        rt_naive = float(e["naive_us"]) / float(e["rtree_us"]) if float(e["rtree_us"]) else 0
        body.append(["Point-in-zone, 5,000 polygons × 128 vertices, vs checking every polygon",
                     us(e["naive_us"]) + " µs",
                     "%s× (%s)" % (x(float(e["quad_vs_naive"])), band("end_to_end.csv", "vertices", "128", "quad_vs_naive")),
                     "%s× (ratio of medians)" % x(rt_naive)])
    return table(["Workload", "Brute force / naive", "Quadtree speedup (range over runs)",
                  "R-tree speedup (range over runs)"], body, ["---", "---:", "---:", "---:"])


BLOCKS = {
    "env": block_env,
    "machine": block_machine,
    "scaling": lambda: scaling_block("index_scaling.csv"),
    "density": lambda: scaling_block("index_density.csv"),
    "e2e": block_e2e,
    "costs": block_costs,
    "str": block_str,
    "interval": block_interval,
    "persistent": block_persistent,
    "extensions": block_extensions,
    "headline": block_headline,
    "runs": lambda: "%d" % nruns("index_scaling.csv"),
}


def render(text):
    def fill(m):
        name = m.group(1)
        if name not in BLOCKS:
            raise SystemExit("unknown results block: " + name)
        body = BLOCKS[name]()
        inline = "\n" not in body
        return "<!-- results:%s -->%s%s%s<!-- /results:%s -->" % (
            name, "" if inline else "\n", body, "" if inline else "\n", name)
    return re.sub(r"<!-- results:([a-z0-9-]+) -->.*?<!-- /results:\1 -->", fill, text, flags=re.S)


def main():
    check = "--check" in sys.argv
    stale = []
    for path in DOCS:
        if not os.path.exists(path):
            continue
        old = open(path, encoding="utf-8").read()
        new = render(old)
        if new != old:
            if check:
                stale.append(os.path.relpath(path, ROOT))
            else:
                open(path, "w", encoding="utf-8").write(new)
                print("  rendered " + os.path.relpath(path, ROOT))
    if check:
        for p in stale:
            print("  stale: %s does not match bench/results/*.csv (run: python3 tools/render_results.py)" % p)
        print("results: %s" % ("docs match the committed CSVs" if not stale else "%d stale file(s)" % len(stale)))
        return 1 if stale else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
