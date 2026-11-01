#!/usr/bin/env python3
"""Figures for docs/final: architecture and data-flow diagrams, and charts drawn
from the committed benchmark CSVs in bench/results/. Writes PNG and SVG to
docs/final/images/. Every number on a chart comes from a CSV; nothing is typed.

Usage: python3 tools/make_figures.py      (needs matplotlib)
"""
import csv
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "final", "images")
RES = os.path.join(ROOT, "bench", "results")

BG, PANEL, LINE, TEXT, MUTED = "#0d1116", "#161d25", "#2c3846", "#e3e8ee", "#a7b1bd"
TEAL, AMBER, ORANGE, RED, BLUE, VIOLET = "#3fb6a8", "#d6a440", "#e07b39", "#e5484d", "#6c9bd2", "#8e7cd8"

plt.rcParams.update({
    "font.family": "sans-serif", "font.size": 10.5, "text.color": TEXT, "axes.labelcolor": MUTED,
    "axes.edgecolor": LINE, "xtick.color": MUTED, "ytick.color": MUTED, "axes.facecolor": BG,
    "figure.facecolor": BG, "savefig.facecolor": BG, "grid.color": "#222b36", "legend.frameon": False,
})


def save(fig, name):
    os.makedirs(OUT, exist_ok=True)
    for ext in ("png", "svg"):
        fig.savefig(os.path.join(OUT, name + "." + ext), dpi=200, bbox_inches="tight")
    plt.close(fig)


def rows(name):
    with open(os.path.join(RES, name)) as f:
        return list(csv.DictReader(f))


def box(ax, x, y, w, h, title, lines=(), edge=LINE, fill=PANEL, title_col=TEXT, step=0.034, top=0.022, fs=8.6):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.004,rounding_size=0.01",
                                linewidth=1.2, edgecolor=edge, facecolor=fill))
    ax.text(x + 0.012, y + h - top, title, fontsize=fs + 1.6, fontweight="bold", color=title_col, va="top")
    for i, t in enumerate(lines):
        ax.text(x + 0.012, y + h - top - 0.042 - i * step, t, fontsize=fs, color=MUTED, va="top")


def arrow(ax, x0, y0, x1, y1, label=None, col=MUTED):
    ax.annotate("", xy=(x1, y1), xytext=(x0, y0), arrowprops=dict(arrowstyle="-|>", color=col, lw=1.2))
    if label:
        ax.text((x0 + x1) / 2 + 0.008, (y0 + y1) / 2, label, fontsize=8, color=col, va="center")


def architecture():
    fig, ax = plt.subplots(figsize=(12, 9))
    ax.set_xlim(0, 1); ax.set_ylim(0, 1); ax.axis("off")
    ax.text(0, 0.995, "SafeTrail architecture", fontsize=14, fontweight="bold", va="top")
    box(ax, 0.0, 0.83, 1.0, 0.115, "Browser — web/app (no build step, no third-party code)",
        ["Operations console: Overview · Live Monitoring · Users · Groups · Hazard Zones · Alerts · Incidents · Responders · Investigation · Analytics · Settings",
         "Data Structures Lab (draws what the C++ returned) · Tourist: My Safety · Responder: My Assignments"], edge=BLUE)
    arrow(ax, 0.5, 0.83, 0.5, 0.79, "HTTP, same origin · session cookie + X-SafeTrail-Request", BLUE)
    box(ax, 0.0, 0.5, 1.0, 0.285, "safetrail_server — one process, one thread (poll loop)", [], edge=TEAL, title_col=TEAL)
    inner = [(0.015, 0.22, "net::Server", ["request limits", "no keep-alive, no chunked", "idle timeouts"]),
             (0.25, 0.22, "app::Api", ["PBKDF2 login, hashed sessions", "CSRF header, roles", "audited location reads"]),
             (0.485, 0.21, "ScenarioRunner · Lab", ["simulated people, seeded", "fixes via ingest_fix", "lab: real structures"]),
             (0.71, 0.275, "app::Platform", ["users · consent · sessions", "ingestion · alerts · incidents", "groups · responders · zones", "history · evidence · offline"])]
    for x, w, t, ls in inner:
        box(ax, x, 0.515, w, 0.205, t, ls)
    for x0, x1 in ((0.235, 0.25), (0.47, 0.485), (0.695, 0.71)):
        arrow(ax, x0, 0.62, x1, 0.62)
    arrow(ax, 0.85, 0.5, 0.85, 0.455)
    box(ax, 0.0, 0.0, 1.0, 0.45, "Engine — C++17, no dependencies", [], edge=AMBER, title_col=AMBER)
    eng = [
        ("fence::Evaluator", ["prune · validity · containment", "hysteresis · prediction", "reconciliation"]),
        ("index", ["R-tree (STR) · quadtree", "geohash · brute force", "persistent quadtree + AVL"]),
        ("geo", ["three-valued containment", "sweep-line validation", "projections"]),
        ("alert", ["correlator (union-find)", "escalation: timer wheel + hash map", "triage: binary heap"]),
        ("graph · dispatch", ["road graph + k-d tree", "Dijkstra · A*", "Hungarian assignment"]),
        ("group · sync · evidence", ["cohesion (rollback DSU)", "Lamport · offline queue", "SHA-256 · Merkle log"]),
    ]
    for i, (t, ls) in enumerate(eng):
        c, r = i % 3, i // 3
        box(ax, 0.015 + c * 0.33, 0.215 - r * 0.2, 0.315, 0.18, t, ls)
    save(fig, "fig-architecture")


def dataflow():
    fig, ax = plt.subplots(figsize=(12, 6.2))
    ax.set_xlim(0, 1); ax.set_ylim(0, 1); ax.axis("off")
    ax.text(0, 0.99, "One GPS fix, end to end", fontsize=14, fontweight="bold", va="top")
    steps = [
        ("1  Phone / scenario", ["POST /api/users/7/fix", "lat, lon, ±accuracy, t"], BLUE),
        ("2  Ingestion", ["consent + session?", "duplicate? late? jump?", "time-sorted trail append"], TEAL),
        ("3  Spatial prune", ["R-tree query O(log n + k)", "validity check O(1) each"], AMBER),
        ("4  Exact + hysteresis", ["inside / outside / uncertain", "margins, 3 agreeing fixes"], AMBER),
        ("5  Alerts", ["enter · approach · dwell", "straggler · signal lost", "escalation: timer wheel"], ORANGE),
        ("6  Incident + dispatch", ["union-find correlation", "Dijkstra recommendation", "A* route on assignment"], RED),
        ("7  Evidence", ["Merkle log entry", "ids only, no coordinates", "access audit"], VIOLET),
    ]
    pos = [(0.0, 0.56), (0.255, 0.56), (0.51, 0.56), (0.765, 0.56), (0.0, 0.17), (0.255, 0.17), (0.51, 0.17)]
    w, hgt = 0.235, 0.3
    for (t, ls, col), (x, y) in zip(steps, pos):
        box(ax, x, y, w, hgt, t, ls, edge=col, title_col=col, step=0.06, top=0.04, fs=9.5)
    for i in range(6):
        (x0, y0), (x1, y1) = pos[i], pos[i + 1]
        if y0 == y1:
            arrow(ax, x0 + w, y0 + hgt / 2, x1, y1 + hgt / 2)
        else:
            arrow(ax, x0 + w / 2, y0, x1 + w / 2, y1 + hgt)
    ax.text(0.765, 0.32, "Measured: about 3 µs per fix\nthrough this whole path,\nflat from 10 to 5,000 users\n(bench section 19).\n\nEach user owns its own\nengine state: no sharing.",
            fontsize=9.5, color=MUTED, va="center")
    save(fig, "fig-dataflow")


def chart_scaling():
    r = rows("index_scaling.csv")
    z = [int(x["zones"]) for x in r]
    fig, ax = plt.subplots(figsize=(8, 4.4))
    for key, name, col in (("brute_us", "brute force", ORANGE), ("quad_us", "quadtree", BLUE), ("rtree_us", "R-tree (STR)", TEAL)):
        ax.plot(z, [float(x[key]) for x in r], marker="o", color=col, label=name, lw=2)
    ax.set_xscale("log"); ax.set_yscale("log"); ax.grid(True, which="major")
    ax.set_xlabel("zones (one district)"); ax.set_ylabel("µs per range query (median)")
    ax.set_title("Range query latency vs zone count", loc="left", color=TEXT, fontsize=12)
    last = r[-1]
    ax.annotate("%.0f× / %.0f×" % (float(last["quad_speedup"]), float(last["rtree_speedup"])),
                xy=(z[-1], float(last["rtree_us"])), xytext=(-70, 20), textcoords="offset points", color=MUTED, fontsize=9)
    ax.legend()
    save(fig, "chart-index-scaling")


def chart_multi_user():
    r = rows("multi_user.csv")
    fig, ax = plt.subplots(figsize=(8, 4.2))
    for pat, col in (("in-order", TEAL), ("user-batched", BLUE), ("noisy", AMBER)):
        pts = [x for x in r if x["pattern"] == pat]
        ax.plot([int(x["users"]) for x in pts], [float(x["us_per_fix"]) for x in pts], marker="o", color=col, label=pat, lw=2)
    ax.set_xscale("log"); ax.set_ylim(0, None); ax.grid(True)
    ax.set_xlabel("independent users (60 fixes each)"); ax.set_ylabel("µs per fix, whole ingest path")
    ax.set_title("Platform ingestion cost is flat in the number of users", loc="left", color=TEXT, fontsize=12)
    ax.legend()
    save(fig, "chart-multi-user")


def chart_routing():
    r = rows("routing.csv")
    fig, ax = plt.subplots(figsize=(8, 4.0))
    n = [int(x["nodes"]) for x in r]
    ax.plot(n, [float(x["dijkstra_expanded"]) for x in r], marker="o", color=ORANGE, label="Dijkstra", lw=2)
    ax.plot(n, [float(x["astar_expanded"]) for x in r], marker="o", color=TEAL, label="A*", lw=2)
    ax.grid(True); ax.set_xlabel("graph junctions"); ax.set_ylabel("junctions settled per query (mean)")
    ax.set_title("A* settles far fewer junctions than Dijkstra", loc="left", color=TEXT, fontsize=12)
    ax.legend()
    save(fig, "chart-routing")


def chart_sharing():
    r = rows("versioned_index.csv")
    fig, ax = plt.subplots(figsize=(8, 4.0))
    v = [int(x["versions"]) for x in r]
    ax.plot(v, [int(x["full_copies"]) for x in r], marker="o", color=ORANGE, label="full copy per version", lw=2)
    ax.plot(v, [int(x["allocated"]) for x in r], marker="o", color=TEAL, label="path copying (allocated)", lw=2)
    ax.set_xscale("log"); ax.set_yscale("log"); ax.grid(True)
    ax.set_xlabel("versions"); ax.set_ylabel("quadtree nodes")
    ax.set_title("Persistent quadtree: structural sharing", loc="left", color=TEXT, fontsize=12)
    ax.legend()
    save(fig, "chart-sharing")


def chart_speedups():
    sc, dn = rows("index_scaling.csv")[-1], rows("index_density.csv")[-1]
    fig, ax = plt.subplots(figsize=(8, 4.6))
    groups = ["One district (k ≈ %.0f)" % float(sc["candidates"]), "Constant density (k ≈ %.0f)" % float(dn["candidates"])]
    q = [float(sc["quad_speedup"]), float(dn["quad_speedup"])]
    r = [float(sc["rtree_speedup"]), float(dn["rtree_speedup"])]
    x = [0, 1]
    b1 = ax.bar([i - 0.19 for i in x], q, 0.36, color=BLUE, label="quadtree")
    b2 = ax.bar([i + 0.19 for i in x], r, 0.36, color=TEAL, label="R-tree (STR)")
    for bars in (b1, b2):
        for b in bars:
            ax.text(b.get_x() + b.get_width() / 2, b.get_height() * 1.02, "%.0f×" % b.get_height(),
                    ha="center", va="bottom", color=TEXT, fontsize=12, fontweight="bold")
    ax.set_xticks(x); ax.set_xticklabels(groups)
    ax.set_yticks([]); ax.spines[["left", "top", "right"]].set_visible(False)
    ax.set_ylim(0, max(r) * 1.15)
    ax.set_title("Speedup over brute force at %s zones" % format(int(sc["zones"]), ","), loc="left", color=TEXT, fontsize=12)
    ax.legend(loc="upper left")
    save(fig, "chart-speedups")


if __name__ == "__main__":
    architecture(); dataflow(); chart_scaling(); chart_speedups(); chart_multi_user(); chart_routing(); chart_sharing()
    print("figures written to", os.path.relpath(OUT, ROOT))
