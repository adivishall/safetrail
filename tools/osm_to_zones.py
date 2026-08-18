#!/usr/bin/env python3
"""Convert real OpenStreetMap features into safetrail GeoJSON zones.

Source: Overpass API, features around Shillong, Meghalaya. This replaces the
hand-drawn demo zones with real geography -- real reservoirs, forests, and
quarries at their actual coordinates.

Two things the converter must guarantee, because ZoneStore::load_geojson rejects
the entire file on the first invalid polygon:
  1. every exported ring is simple (non-self-intersecting)  -- GAP 10 validator
  2. every ring has >= 3 distinct vertices and non-zero area

It also simplifies (Ramer-Douglas-Peucker) so a 400-vertex forest boundary does
not dominate the O(V) geometry, and caps the zone count so the map stays legible.
"""
import json, sys, math

# ── OSM tag -> our zone semantics ────────────────────────────────────────────
def classify(tags):
    lu, nat = tags.get('landuse'), tags.get('natural')
    leis, bnd = tags.get('leisure'), tags.get('boundary')
    if lu == 'quarry':
        return 'restricted', 5, 'quarry edge - fall hazard'
    if nat == 'cliff':
        return 'restricted', 5, 'cliff'
    if nat == 'water' or lu == 'reservoir':
        return 'caution', 4, 'deep water'
    if leis == 'nature_reserve' or bnd == 'protected_area':
        return 'advisory', 2, 'protected area'
    if nat == 'wood' or lu == 'forest':
        return 'advisory', 2, 'dense forest - weak signal'
    return 'caution', 3, ''

# ── geometry helpers (mirror src/geo, so the C++ validator agrees) ───────────
def rdp(pts, eps):
    if len(pts) < 3: return pts
    def pdist(p, a, b):
        if a == b: return math.hypot(p[0]-a[0], p[1]-a[1])
        t = ((p[0]-a[0])*(b[0]-a[0]) + (p[1]-a[1])*(b[1]-a[1])) / ((b[0]-a[0])**2+(b[1]-a[1])**2)
        t = max(0, min(1, t))
        return math.hypot(p[0]-(a[0]+t*(b[0]-a[0])), p[1]-(a[1]+t*(b[1]-a[1])))
    dmax, idx = 0, 0
    for i in range(1, len(pts)-1):
        d = pdist(pts[i], pts[0], pts[-1])
        if d > dmax: dmax, idx = d, i
    if dmax > eps:
        return rdp(pts[:idx+1], eps)[:-1] + rdp(pts[idx:], eps)
    return [pts[0], pts[-1]]

def orient(a, b, c):
    v = (b[0]-a[0])*(c[1]-a[1]) - (b[1]-a[1])*(c[0]-a[0])
    return 1 if v > 1e-14 else (-1 if v < -1e-14 else 0)

def on_seg(a, b, p):
    return min(a[0],b[0])-1e-14 <= p[0] <= max(a[0],b[0])+1e-14 and \
           min(a[1],b[1])-1e-14 <= p[1] <= max(a[1],b[1])+1e-14

def cross(a, b, c, d):
    o1,o2,o3,o4 = orient(a,b,c),orient(a,b,d),orient(c,d,a),orient(c,d,b)
    if o1!=o2 and o3!=o4: return True
    if o1==0 and on_seg(a,b,c): return True
    if o2==0 and on_seg(a,b,d): return True
    if o3==0 and on_seg(c,d,a): return True
    if o4==0 and on_seg(c,d,b): return True
    return False

def self_intersects(ring):
    n = len(ring)
    for i in range(n):
        a, b = ring[i], ring[(i+1)%n]
        for j in range(i+1, n):
            # edges i(i->i+1) and j(j->j+1) are adjacent when they share a vertex:
            # j==i+1, or the wrap-around pair (i==0, j==n-1). Matches src/geo.
            if j == i+1 or (i == 0 and j == n-1): continue
            if cross(a, b, ring[j], ring[(j+1)%n]): return True
    return False

def signed_area(ring):
    a = 0
    n = len(ring)
    for i in range(n):
        j = (i-1)%n
        a += ring[j][0]*ring[i][1] - ring[i][0]*ring[j][1]
    return a/2

# ── convert ──────────────────────────────────────────────────────────────────
osm = json.load(open('/tmp/osm.json'))
feats, generic_counts, stats = [], {}, {'kept':0, 'self_intersect':0, 'degenerate':0, 'tiny':0}

# Priority so the interesting, real, named hazards survive the cap rather than
# 40 identical forest blobs. Score: named features first, then water/cliff/quarry
# (the safety-relevant kinds) over forest, then larger extent as a tiebreak.
els = [e for e in osm['elements'] if e.get('geometry') and len(e['geometry']) >= 4]
def extent(e):
    la=[p['lat'] for p in e['geometry']]; lo=[p['lon'] for p in e['geometry']]
    return (max(la)-min(la))*(max(lo)-min(lo))
KIND_RANK={'quarry':4,'cliff':4,'water':3,'reservoir':3,'nature_reserve':1,
           'protected_area':1,'wood':0,'forest':0}
def score(e):
    t=e.get('tags',{})
    k=t.get('landuse') or t.get('natural') or t.get('leisure') or t.get('boundary') or ''
    named = 1 if t.get('name') else 0
    return (named*10 + KIND_RANK.get(k,0), extent(e))
els.sort(key=score, reverse=True)

# Cap total, and separately cap plain forests so the map is not monochrome.
CAP = 38
FOREST_CAP = 18
forest_kept = 0
for e in els:
    tags = e.get('tags', {})
    kind, sev, desc = classify(tags)
    ring = [(p['lon'], p['lat']) for p in e['geometry']]
    if ring[0] == ring[-1]: ring = ring[:-1]          # drop OSM closing vertex
    ring = rdp(ring, 0.00012)                          # ~13 m tolerance
    # dedupe consecutive
    dd = [ring[0]]
    for p in ring[1:]:
        if abs(p[0]-dd[-1][0])>1e-9 or abs(p[1]-dd[-1][1])>1e-9: dd.append(p)
    ring = dd
    if len(ring) < 3: stats['degenerate']+=1; continue
    if abs(signed_area(ring)) < 5e-8: stats['tiny']+=1; continue
    if self_intersects(ring): stats['self_intersect']+=1; continue

    is_forest = (kind == 'advisory' and not tags.get('name'))
    if is_forest and forest_kept >= FOREST_CAP:
        continue
    if is_forest: forest_kept += 1

    name = tags.get('name', '')
    if not name:
        # Unnamed OSM features get a distinguishable label instead of 20 identical
        # "Deep Water" rows in the operator panel.
        base = desc.title() if desc else kind.title()
        generic_counts[base] = generic_counts.get(base, 0) + 1
        name = '%s %d' % (base, generic_counts[base])
    feats.append({
        'type':'Feature',
        'properties':{'name':name, 'kind':kind, 'severity':sev,
                      'osm_id':e['id'], 'source':'OpenStreetMap'},
        'geometry':{'type':'Polygon','coordinates':[[[round(x,6),round(y,6)] for x,y in ring]]}
    })
    stats['kept']+=1
    if stats['kept'] >= CAP: break

# ── temporal windows for the GAP 3 demo, applied to real named features ──────
# These are illustrative operational rules layered onto real geography.
def set_validity(substr, frm, to, note):
    for f in feats:
        if substr.lower() in f['properties']['name'].lower():
            f['properties']['active_from_s'] = frm
            if to is not None: f['properties']['active_to_s'] = to
            f['properties']['rule_note'] = note
            return f['properties']['name']
    return None

applied = []
# first reservoir/water -> seasonal spillway risk
for f in feats:
    if f['properties']['kind']=='caution' and 'active_from_s' not in f['properties']:
        f['properties']['active_from_s']=1800; f['properties']['active_to_s']=5400
        f['properties']['rule_note']='spillway discharge window'
        applied.append(f['properties']['name']); break
# first quarry -> blasting hours
for f in feats:
    if 'quarry' in f['properties']['name'].lower() and 'active_from_s' not in f['properties']:
        f['properties']['active_from_s']=0; f['properties']['active_to_s']=3600
        f['properties']['rule_note']='blasting hours, lifted after'
        applied.append(f['properties']['name']); break
# one forest -> activates after rainfall (landslide risk)
for f in feats:
    if f['properties']['kind']=='advisory' and 'active_from_s' not in f['properties']:
        f['properties']['active_from_s']=2700
        f['properties']['rule_note']='landslide risk after rainfall'
        applied.append(f['properties']['name']); break

out = {'type':'FeatureCollection',
       'metadata':{'source':'OpenStreetMap via Overpass API',
                   'region':'Shillong, Meghalaya',
                   'bbox':[25.50,91.78,25.72,91.98],
                   'note':'real geographic features; validity windows are illustrative operational rules'},
       'features':feats}
json.dump(out, open('data/zones/shillong_osm.geojson','w'), indent=1)

print('kept %d zones (cap %d)' % (stats['kept'], CAP))
print('dropped: %d self-intersecting, %d degenerate, %d tiny' %
      (stats['self_intersect'], stats['degenerate'], stats['tiny']))
from collections import Counter
print('by kind:', dict(Counter(f['properties']['kind'] for f in feats)))
print('named real features:', sum(1 for f in feats if f['properties'].get('osm_id') and not f['properties']['name'][0].isupper()==False)[:0] if False else
      sum(1 for f in feats if 'name' in f['properties']))
print('validity windows applied to:', [a for a in applied if a])
named = [f['properties']['name'] for f in feats if f['properties']['name'] not in
         ('Deep Water','Dense Forest - Weak Signal','Protected Area','Advisory','Caution','Restricted')][:8]
print('sample real names:', named)
