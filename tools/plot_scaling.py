#!/usr/bin/env python3
"""Render the index-scaling benchmark as a self-contained log-log SVG.

No dependencies (no matplotlib): reads bench/results/index_scaling.csv and writes
bench/plots/index_scaling.svg by hand, so it runs anywhere Python 3 does and keeps
the project's zero-dependency rule. Re-run after `make bench`:

    python3 tools/plot_scaling.py

The chart is the picture the numbers describe: brute force going vertical while the
two tree indexes stay flat. It is committed so the README can show it without a
build step; regenerate it whenever the CSV changes.
"""
import csv
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(ROOT, "bench", "results", "index_scaling.csv")
OUT = os.path.join(ROOT, "bench", "plots", "index_scaling.svg")

# ── read the measured data ───────────────────────────────────────────────────
rows = []
with open(CSV) as f:
    for r in csv.DictReader(f):
        rows.append((int(r["zones"]),
                     float(r["brute_us"]),
                     float(r["quad_us"]),
                     float(r["rtree_us"])))
rows.sort()
zones = [r[0] for r in rows]
series = {
    "brute force": ([r[1] for r in rows], "#d1495b"),
    "quadtree":    ([r[2] for r in rows], "#2a9d8f"),
    "R-tree (STR)":([r[3] for r in rows], "#3d5a80"),
}

# ── log-log axes ─────────────────────────────────────────────────────────────
W, H = 720, 460
ML, MR, MT, MB = 70, 150, 40, 60
PW, PH = W - ML - MR, H - MT - MB
xmin, xmax = math.log10(min(zones)), math.log10(max(zones))
all_y = [v for s, _ in series.values() for v in s if v > 0]
ymin, ymax = math.log10(min(all_y)), math.log10(max(all_y))
ymin, ymax = math.floor(ymin), math.ceil(ymax)


def sx(z):
    return ML + (math.log10(z) - xmin) / (xmax - xmin) * PW


def sy(v):
    v = max(v, 10 ** ymin)
    return MT + PH - (math.log10(v) - ymin) / (ymax - ymin) * PH


svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" '
       f'font-family="system-ui,Segoe UI,Helvetica,Arial,sans-serif">']
svg.append(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
svg.append(f'<text x="{ML}" y="24" font-size="16" font-weight="600" fill="#1d3557">'
           'Spatial index query latency vs zone count (log–log)</text>')

# y grid + labels (powers of 10, microseconds)
for e in range(ymin, ymax + 1):
    y = sy(10 ** e)
    svg.append(f'<line x1="{ML}" y1="{y:.1f}" x2="{ML+PW}" y2="{y:.1f}" '
               'stroke="#e6e6e6"/>')
    lab = f'{10**e:g} µs' if e >= 0 else f'{10**e:g} µs'
    svg.append(f'<text x="{ML-8}" y="{y+4:.1f}" font-size="11" fill="#666" '
               f'text-anchor="end">{lab}</text>')
# x labels
for z in zones:
    x = sx(z)
    svg.append(f'<line x1="{x:.1f}" y1="{MT}" x2="{x:.1f}" y2="{MT+PH}" '
               'stroke="#f2f2f2"/>')
    svg.append(f'<text x="{x:.1f}" y="{MT+PH+18:.1f}" font-size="11" fill="#666" '
               f'text-anchor="middle">{z:,}</text>')
svg.append(f'<text x="{ML+PW/2:.0f}" y="{H-14}" font-size="12" fill="#333" '
           'text-anchor="middle">number of hazard zones</text>')

# axis box
svg.append(f'<rect x="{ML}" y="{MT}" width="{PW}" height="{PH}" fill="none" '
           'stroke="#333"/>')

# series
ly = MT + 12
for name, (vals, colour) in series.items():
    pts = " ".join(f'{sx(z):.1f},{sy(v):.1f}' for z, v in zip(zones, vals))
    svg.append(f'<polyline points="{pts}" fill="none" stroke="{colour}" '
               'stroke-width="2.5"/>')
    for z, v in zip(zones, vals):
        svg.append(f'<circle cx="{sx(z):.1f}" cy="{sy(v):.1f}" r="3" '
                   f'fill="{colour}"/>')
    svg.append(f'<rect x="{ML+PW+16}" y="{ly-9}" width="14" height="4" '
               f'fill="{colour}"/>')
    svg.append(f'<text x="{ML+PW+34}" y="{ly-4}" font-size="12" fill="#222">'
               f'{name}</text>')
    ly += 22

svg.append(f'<text x="{ML+PW+16}" y="{ly+8}" font-size="10" fill="#888">'
           'median of 7, ratio to</text>')
svg.append(f'<text x="{ML+PW+16}" y="{ly+22}" font-size="10" fill="#888">'
           'our own brute force</text>')
svg.append('</svg>')

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w") as f:
    f.write("\n".join(svg))
print(f"wrote {os.path.relpath(OUT, ROOT)}  ({len(zones)} points)")
