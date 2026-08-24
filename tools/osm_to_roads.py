#!/usr/bin/env python3
"""Fetch the real road network around Shillong from OpenStreetMap and write it in
safetrail's road-graph text format (the one RoadGraph::load_file reads).

This replaces the synthetic road grid the dispatch layer falls back to. Run it
when online; it needs no third-party packages (urllib is stdlib):

    python3 tools/osm_to_roads.py

Output: data/osm/roads.txt

Format (matches include/safetrail/graph/road_graph.hpp):
    safetrail-roads 1
    <node_count>
    <lat> <lon>            x node_count
    <edge_count>
    <u> <v>                x edge_count   (undirected; C++ side weights by great-circle length)
"""
import json, sys, urllib.request, urllib.parse

# South, West, North, East -- same box as the zones (data/zones/shillong_osm.geojson).
BBOX = (25.50, 91.78, 25.72, 91.98)
OVERPASS = "https://overpass-api.de/api/interpreter"

# Drivable/walkable ways only -- skip footways-into-buildings noise if desired by
# editing this set. Kept broad so responders can route on tracks and paths too.
QUERY = f"""
[out:json][timeout:90];
way["highway"]({BBOX[0]},{BBOX[1]},{BBOX[2]},{BBOX[3]});
(._;>;);
out;
"""

def fetch():
    data = urllib.parse.urlencode({"data": QUERY}).encode()
    req = urllib.request.Request(OVERPASS, data=data)
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.load(r)

def build(osm):
    # node id -> (lat, lon)
    coords = {e["id"]: (e["lat"], e["lon"]) for e in osm["elements"] if e["type"] == "node"}
    # Collect the node ids actually used by a highway way, in order per way.
    ways = [e["nodes"] for e in osm["elements"] if e["type"] == "way" and "nodes" in e]

    used = []
    seen = {}
    def idx(nid):
        if nid not in seen:
            seen[nid] = len(used)
            used.append(nid)
        return seen[nid]

    edges = set()
    for nodes in ways:
        prev = None
        for nid in nodes:
            if nid not in coords:      # a referenced node we did not receive
                prev = None
                continue
            cur = idx(nid)
            if prev is not None and prev != cur:
                edges.add((min(prev, cur), max(prev, cur)))   # undirected, dedup
            prev = cur
    return used, coords, edges

def write(path, used, coords, edges):
    with open(path, "w") as f:
        f.write("safetrail-roads 1\n")
        f.write(f"{len(used)}\n")
        for nid in used:
            lat, lon = coords[nid]
            f.write(f"{lat:.7f} {lon:.7f}\n")
        f.write(f"{len(edges)}\n")
        for u, v in sorted(edges):
            f.write(f"{u} {v}\n")

def main():
    print(f"querying Overpass for highways in bbox {BBOX} ...")
    osm = fetch()
    used, coords, edges = build(osm)
    if not used:
        print("no road nodes returned -- check connectivity or the bbox", file=sys.stderr)
        sys.exit(1)
    out = "data/osm/roads.txt"
    write(out, used, coords, edges)
    print(f"wrote {out}: {len(used)} nodes, {len(edges)} undirected edges")

if __name__ == "__main__":
    main()
