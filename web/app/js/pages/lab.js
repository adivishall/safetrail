import { post } from '../api.js';
import { h, mount, panel, field, toast, table, fixed, int } from '../util.js';

// The Data Structures Lab. Every picture here is drawn from what the C++
// structure in this repository returned for the request: its nodes, the order
// a query visited them, the rotations an insert performed, the slots a probe
// walked. The JavaScript only draws; it computes nothing about the structures.

const EXP = [
  { cat: 'Spatial indexes', id: 'spatial', title: 'Brute force · Quadtree · R-tree · Geohash',
    problem: 'Which hazard zones could contain this GPS fix? Checking every zone is O(n) per fix — at 100,000 zones and one fix per second per person that is the whole cost of geofencing.',
    why: 'A spatial index answers the range query "which boxes meet this box?" by pruning whole regions that cannot overlap. The quadtree partitions SPACE into disjoint quadrants; the R-tree partitions the ITEMS into overlapping bounding boxes; STR bulk-loading packs an R-tree from sorted slices instead of n inserts.',
    where: 'src/index/quadtree.cpp, src/index/rtree.cpp, src/index/brute_force.cpp, src/index/geohash.cpp — fence::Evaluator step 3 queries one of them for every fix (the platform uses the R-tree).',
    time: 'Query: O(log n + k) expected, O(n) worst case on adversarial data (k = results). Brute force: Θ(n). Build: quadtree O(n log n) expected; STR O(n log n).',
    space: 'O(n) for each; the quadtree may store a box in an internal node when it straddles a split.',
    alt: 'Brute force is kept permanently as the correctness oracle: every query here is also run on it and the mismatch count is shown.' },
  { cat: 'Spatial indexes', id: 'kdtree', title: 'k-d tree nearest neighbour',
    problem: 'Snap a responder or an incident to the nearest of 144,090 road junctions.',
    why: 'A 2-d tree splits alternately on latitude and longitude at the median; a nearest-neighbour search descends towards the query and only visits the far side of a split when the splitting plane is closer than the best point found so far.',
    where: 'include/safetrail/index/kd_tree.hpp, used by graph::RoadGraph::nearest_node().',
    time: 'Build O(n log n). Nearest: O(log n) expected for well-spread points, O(n) worst case.', space: 'O(n).',
    alt: 'A linear scan over every point — run alongside here, and the answers must agree.' },
  { cat: 'Spatial indexes', id: 'zorder', title: 'Z-order (Morton) curve',
    problem: 'Turn a 2-D position into one sortable key so nearby places tend to sort near each other — the basis of the geohash index that ships to an offline phone as a flat sorted array.',
    why: 'Interleaving the bits of latitude and longitude visits the grid in a recursive Z pattern; sorting by that key and binary-searching gives a compact index with no pointers at all.',
    where: 'index::Geohash::morton() in src/index/geohash.cpp.', time: 'Key: O(bits). Lookup in the sorted array: O(log n) binary search per key range.', space: 'O(n), no pointers — which is why it serialises to bytes.',
    alt: 'Row-major order: neighbours across a row boundary end up far apart in the sort.' },
  { cat: 'Trees', id: 'avl', title: 'AVL interval tree (rotations, augmentation)',
    problem: 'Which zone rules were in force at 14:32? Zones have validity windows; the history must answer "which intervals contain time t".',
    why: 'A binary search tree keyed on the interval start, kept balanced by AVL rotations (height ≤ 1.44 log₂ n), and AUGMENTED: every node stores the largest end time in its subtree, so a whole subtree is skipped when its max end is before t.',
    where: 'include/safetrail/ds/interval_tree.hpp, inside index::VersionedIndex.',
    time: 'Insert/delete O(log n) with at most two rotations per level on the way up. Stabbing query O(min(n, (k+1) log n)).', space: 'O(n).',
    alt: 'A linear scan of all intervals. The tree wins only when the query is selective — the benchmark shows where it loses.' },
  { cat: 'Trees', id: 'persistent', title: 'Persistent quadtree (path copying)',
    problem: 'Answer questions about ANY past configuration of the zone set, not just the present one, without storing a full copy per change.',
    why: 'An update copies only the nodes on the root-to-leaf path it changes; every other node is shared by reference with the previous version. Old roots stay valid forever, so each version is a complete, immutable tree.',
    where: 'src/index/versioned_index.cpp; the Investigation page queries it for "zones in force at t".',
    time: 'Update: O(depth) new nodes. Query at any version: same cost as the present.', space: 'O(depth) per change instead of O(n) per version.',
    alt: 'A full copy per version: the sharing ratio shows how many times larger that would be.' },
  { cat: 'Graphs', id: 'graph', title: 'Adjacency list · Dijkstra · A* · priority queue',
    problem: 'Route a responder to an incident along the road network.',
    why: 'Dijkstra settles nodes in order of distance using a binary-heap frontier. A* adds an admissible heuristic (straight-line distance never exceeds road distance) so it settles far fewer nodes and still finds a shortest path.',
    where: 'src/graph/dijkstra.cpp, src/graph/astar.cpp over graph::RoadGraph (an adjacency list). Platform::recommend runs Dijkstra; assign runs A*.',
    time: 'O((V + E) log V) with a binary heap, lazy deletion of stale entries.', space: 'O(V + E).',
    alt: 'Dijkstra versus A* on the same pair: compare the settled counts.' },
  { cat: 'Graphs', id: 'assignment', title: 'Hungarian assignment vs greedy',
    problem: 'Several incidents, several free responders: who goes where?',
    why: 'Greedy takes the cheapest pair first and can strand a later incident with only a distant responder. The Hungarian algorithm minimises the total over all pairings at once.',
    where: 'src/graph/bipartite_match.cpp; dispatch::assign_optimal; the Incidents page "batch dispatch".', time: 'Hungarian O(n³); greedy O(n² · n) as written.', space: 'O(n²).',
    alt: 'Greedy, shown side by side with its total.' },
  { cat: 'Graphs', id: 'dsu', title: 'Union-find with rollback',
    problem: 'Is a tour group still one connected group? Which alerts belong to the same incident?',
    why: 'Disjoint-set union with union by rank keeps trees shallow; recording each change on an undo stack allows rollback to any earlier snapshot. Path compression is deliberately absent — it would make rollback impossible.',
    where: 'src/ds/dynamic_connectivity.cpp, used by group::CohesionMonitor and alert::Correlator.', time: 'find/unite O(log n) (union by rank, no compression); rollback O(1) per undone union.', space: 'O(n) + undo stack.',
    alt: 'A flood fill over the edge list (components_bruteforce), used as the test oracle.' },
  { cat: 'Hashing and queues', id: 'heap', title: 'Binary heap',
    problem: 'Always know the next most urgent item: Dijkstra\'s nearest unsettled node, the operator\'s most urgent alert.',
    why: 'A complete binary tree stored in an array (children of i at 2i+1, 2i+2) with the heap property; push sifts up, pop sifts down.',
    where: 'include/safetrail/ds/priority_queue.hpp — Dijkstra/A* frontier and alert::TriageQueue.', time: 'push/pop O(log n); top O(1).', space: 'O(n), contiguous.',
    alt: 'A sorted array: O(n) insert.' },
  { cat: 'Hashing and queues', id: 'hash', title: 'Open-addressing hash table (linear probing)',
    problem: 'Constant-time lookup by key: usernames, alert escalation records, synced-event stamps.',
    why: 'Keys hash to a home slot; collisions walk forward to the next free slot. Deletion leaves a TOMBSTONE so probe chains stay intact; the table rehashes at 70% load (counting tombstones) and doubles only when live entries need it.',
    where: 'include/safetrail/ds/hash_table.hpp — Platform username index, EscalationTracker, Reconciler de-duplication, API sessions.', time: 'O(1) expected at bounded load; O(n) worst case.', space: 'O(capacity).',
    alt: 'A linear list: O(n) per lookup.' },
  { cat: 'Hashing and queues', id: 'wheel', title: 'Hashed timing wheel',
    problem: 'Escalate any alert left unacknowledged for 2 minutes, with thousands live, without checking every alert every tick.',
    why: 'Deadlines are dropped into the slot (deadline / tick) mod slots. Advancing one tick examines one slot; a timer due several revolutions later waits in place.',
    where: 'include/safetrail/ds/timer_wheel.hpp inside alert::EscalationTracker, run every platform tick.', time: 'schedule O(1); cancel O(slot size); advance O(Δticks + fired + held).', space: 'O(slots + timers).',
    alt: 'Scanning every open alert each tick: O(n).' },
  { cat: 'Hashing and queues', id: 'ring', title: 'Circular buffer',
    problem: 'Keep the last 64 fixes per person, forever, without allocating.',
    why: 'A fixed array with a head index that wraps around; the oldest entry is overwritten.', where: 'include/safetrail/ds/circular_buffer.hpp — track::Tourist::pings, read by speed estimation and anomaly detection.',
    time: 'push O(1); index O(1).', space: 'O(N) fixed.', alt: 'A growing vector with erase-from-front: O(n) per push.' },
  { cat: 'Integrity and time', id: 'merkle', title: 'Merkle tree and inclusion proofs',
    problem: 'Prove an evidence log entry was there and has not been edited, without trusting whoever serves the log.',
    why: 'Leaves hash with prefix 0x00, nodes with 0x01 (RFC 6962). An inclusion proof is the O(log n) sibling hashes on the path to the root; anyone with the published root can recompute it.',
    where: 'src/evidence/merkle_log.cpp, src/evidence/sha256.cpp — every platform log entry is a leaf; the Investigation page verifies proofs in the browser.', time: 'append O(1) amortised; prove/verify O(log n).', space: 'O(n).',
    alt: 'A plain hash chain: proving one entry needs every later entry.' },
  { cat: 'Integrity and time', id: 'lamport', title: 'Lamport logical clocks',
    problem: 'Order events from phones that were offline with wrong clocks.',
    why: 'Each device counts its own events; a receiver sets its counter to max(own, received) + 1. Ordering by (counter, device) is a total order consistent with causality, whatever the wall clocks say.',
    where: 'include/safetrail/sync/lamport.hpp — OfflineQueue on the device, Reconciler on the server; the offline scenario.', time: 'O(1) per event; merge sorts O(n log n).', space: 'O(1) per clock.',
    alt: 'Sorting by device wall clock, which a phone 7 minutes fast gets wrong.' },
  { cat: 'Integrity and time', id: 'geometry', title: 'Point in polygon under uncertainty',
    problem: 'Is this person inside the zone, given the fix is only good to ±20 m?',
    why: 'Ray casting (count edge crossings) and winding number must agree on the exact point; the signed distance to the boundary against the accuracy radius gives a THREE-valued answer — inside, outside, or uncertain.',
    where: 'src/geo/containment.cpp, called for each candidate the index returns.', time: 'O(V) per polygon.', space: 'O(1).',
    alt: 'A two-valued test that pretends the fix is a point.' },
];

const C = { bg: '#0a0e12', line: '#273140', text: '#a7b1bd', hi: '#e3e8ee', accent: '#3fb6a8', warn: '#d6a440', bad: '#e5484d', blue: '#6c9bd2', violet: '#8e7cd8' };

export function render(root, ctx) {
  const list = h('nav', { class: 'lab-list', 'aria-label': 'Experiments' });
  const stage = h('div', { class: 'stack' });
  mount(root, h('div', { class: 'page' },
    h('div', { class: 'page-head' }, h('div', null, h('h1', null, 'Data Structures Lab'),
      h('p', null, 'Each experiment runs the real C++ structure from this repository on the server and draws what it returned. Nothing on this page is a JavaScript re-implementation.'))),
    h('div', { class: 'lab' }, list, stage)));
  let current = (location.hash.split('/')[2]) || 'spatial';
  function drawList() {
    const parts = [];
    let cat = null;
    for (const e of EXP) {
      if (e.cat !== cat) { parts.push(h('div', { class: 'cat' }, e.cat)); cat = e.cat; }
      parts.push(h('button', { class: e.id === current ? 'on' : '', onclick: () => { current = e.id; history.replaceState(null, '', '#/lab/' + e.id); drawList(); open(); } }, e.title.split(' (')[0]));
    }
    mount(list, parts);
  }
  function open() {
    const e = EXP.find(x => x.id === current) || EXP[0];
    const body = h('div', { class: 'stack' });
    mount(stage,
      h('h2', null, e.title),
      h('div', { class: 'facts' },
        fact('Problem it solves', e.problem), fact('How it works', e.why), fact('Where SafeTrail uses it', e.where),
        fact('Time', e.time), fact('Space', e.space), fact('Compared with', e.alt)),
      body);
    (VIEWS[e.id] || (() => mount(body, h('div', null, 'missing'))))(body);
  }
  drawList(); open();
  return {};
}

function fact(t, text) { return h('div', { class: 'fact' }, h('h4', null, t), h('div', { class: 'small' }, text)); }

async function run(name, body) {
  try { return await post('/api/lab/' + name, body); }
  catch (e) { toast(e.message, 'error'); return null; }
}

function canvas(w, hgt) {
  const c = h('canvas', { class: 'lab-canvas' });
  const dpr = window.devicePixelRatio || 1;
  c.width = w * dpr; c.height = hgt * dpr;
  c.style.aspectRatio = w + ' / ' + hgt;
  const g = c.getContext('2d');
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  c.W = w; c.H = hgt;
  c.clear = () => { g.setTransform(dpr, 0, 0, dpr, 0, 0); g.fillStyle = C.bg; g.fillRect(0, 0, w, hgt); };
  c.g = g;
  c.local = (ev) => { const r = c.getBoundingClientRect(); return [(ev.clientX - r.left) * w / r.width, (ev.clientY - r.top) * hgt / r.height]; };
  return c;
}

function stepper(n, onStep, label) {
  const s = h('input', { type: 'range', min: 0, max: Math.max(0, n - 1), value: Math.max(0, n - 1), style: { flex: 1 } });
  const lab = h('span', { class: 'mono small' });
  const set = (k) => { k = Math.max(0, Math.min(n - 1, k)); s.value = k; lab.textContent = (label || 'step') + ' ' + (k + 1) + ' / ' + n; onStep(k); };
  s.oninput = () => set(Number(s.value));
  let timer = 0;
  const play = h('button', { class: 'small', onclick: () => {
    if (timer) { clearInterval(timer); timer = 0; play.textContent = 'Play'; return; }
    let k = Number(s.value) >= n - 1 ? 0 : Number(s.value);
    play.textContent = 'Stop';
    timer = setInterval(() => { set(k++); if (k >= n) { clearInterval(timer); timer = 0; play.textContent = 'Play'; } }, Math.max(30, 2400 / Math.max(1, n)));
  } }, 'Play');
  const el = h('div', { class: 'steps' }, h('button', { class: 'small', onclick: () => set(Number(s.value) - 1) }, '◀'), s,
    h('button', { class: 'small', onclick: () => set(Number(s.value) + 1) }, '▶'), play, lab);
  if (n > 0) set(n - 1);
  return el;
}

// ── Spatial ────────────────────────────────────────────────────────────────
const VIEWS = {};
VIEWS.spatial = (body) => {
  const p = { n: 400, distribution: 'clustered', capacity: 8, seed: 7, q_min_lat: 0.40, q_min_lon: 0.40, q_max_lat: 0.55, q_max_lon: 0.58 };
  let which = 'quadtree', data = null;
  const cv = canvas(560, 560);
  const out = h('div');
  const stepHost = h('div');
  const n = h('input', { type: 'number', min: 10, max: 3000, value: p.n, style: { width: '90px' } });
  const dist = h('select', null, ['clustered', 'uniform'].map(d => h('option', { value: d }, d)));
  const cap = h('input', { type: 'number', min: 2, max: 64, value: p.capacity, style: { width: '70px' } });
  const seed = h('input', { type: 'number', min: 0, value: p.seed, style: { width: '80px' } });
  const pick = h('select', { onchange: () => { which = pick.value; draw(-1); steps(); } },
    [['quadtree', 'Quadtree'], ['rtree_str', 'R-tree (STR bulk-loaded)'], ['rtree_insert', 'R-tree (built by insertion, quadratic split)']].map(([v, t]) => h('option', { value: v }, t)));
  mount(body, panel('Controls', h('div', { class: 'stack' },
    h('div', { class: 'row' }, field('Zones', n), field('Distribution', dist), field('Node capacity', cap), field('Seed', seed), field('Show structure', pick),
      h('button', { class: 'primary', onclick: go }, 'Build and query')),
    h('p', { class: 'small muted' }, 'Drag on the canvas to draw a new query box. Teal outlines: nodes the query descended into. Red dashed: nodes it pruned (the box missed them, so their whole subtree was skipped). Filled amber: zones returned.'))),
  h('div', { class: 'grid cols-2' }, h('div', { class: 'stack' }, cv, stepHost), out));
  let dragFrom = null;
  cv.onpointerdown = (e) => { dragFrom = cv.local(e); };
  cv.onpointerup = (e) => {
    if (!dragFrom) return;
    const b = cv.local(e);
    const a = dragFrom; dragFrom = null;
    if (Math.abs(a[0] - b[0]) < 4 && Math.abs(a[1] - b[1]) < 4) return;
    const toLL = ([x, y]) => [1 - y / cv.H, x / cv.W];
    const [la0, lo0] = toLL(a), [la1, lo1] = toLL(b);
    Object.assign(p, { q_min_lat: Math.min(la0, la1), q_max_lat: Math.max(la0, la1), q_min_lon: Math.min(lo0, lo1), q_max_lon: Math.max(lo0, lo1) });
    go();
  };
  async function go() {
    Object.assign(p, { n: Number(n.value), distribution: dist.value, capacity: Number(cap.value), seed: Number(seed.value) });
    data = await run('spatial', p);
    if (!data) return;
    draw(-1); steps(); results();
  }
  const X = (lon) => lon * cv.W, Y = (lat) => (1 - lat) * cv.H;
  const rect = (g, b) => g.rect(X(b[1]), Y(b[2]), (b[3] - b[1]) * cv.W, (b[2] - b[0]) * cv.H);
  function draw(upto) {
    const g = cv.g; cv.clear();
    const s = data[which];
    g.strokeStyle = '#33404e'; g.lineWidth = 1;
    for (const b of data.items) { g.beginPath(); rect(g, b); g.stroke(); }
    const depthCol = ['#6c9bd2', '#8e7cd8', '#3fb6a8', '#d6a440', '#e07b39', '#a7b1bd'];
    g.globalAlpha = 0.5;
    for (const nd of s.nodes) { g.strokeStyle = depthCol[nd[4] % depthCol.length]; g.lineWidth = 0.8; g.beginPath(); g.rect(X(nd[1]), Y(nd[2]), (nd[3] - nd[1]) * cv.W, (nd[2] - nd[0]) * cv.H); g.stroke(); }
    g.globalAlpha = 1;
    const vis = upto < 0 ? s.visited : s.visited.slice(0, upto + 1);
    for (const v of vis) {
      g.strokeStyle = v[5] ? C.accent : C.bad; g.lineWidth = v[5] ? 1.6 : 1; g.setLineDash(v[5] ? [] : [4, 3]);
      g.beginPath(); g.rect(X(v[1]), Y(v[2]), (v[3] - v[1]) * cv.W, (v[2] - v[0]) * cv.H); g.stroke(); g.setLineDash([]);
    }
    if (upto < 0 || upto >= s.visited.length - 1) {
      g.fillStyle = 'rgba(214,164,64,.55)';
      for (const id of s.result_ids) { g.beginPath(); rect(g, data.items[id]); g.fill(); }
    }
    g.strokeStyle = C.warn; g.lineWidth = 2;
    g.beginPath(); rect(g, data.query); g.stroke();
  }
  function steps() { mount(stepHost, data[which].visited.length ? stepper(data[which].visited.length, (k) => draw(k), 'node visit') : null); }
  function results() {
    const rows = ['brute', 'quadtree', 'rtree_str', 'rtree_insert', 'geohash'].map(k => { const s = data[k]; return h('tr', null,
      h('td', null, { brute: 'Brute force', quadtree: 'Quadtree', rtree_str: 'R-tree (STR)', rtree_insert: 'R-tree (insert)', geohash: 'Geohash' }[k]),
      h('td', { class: 'num' }, String(s.results)), h('td', { class: 'num', style: { color: s.mismatches_vs_brute ? C.bad : C.accent } }, String(s.mismatches_vs_brute)),
      h('td', { class: 'num' }, fixed(s.query_us, 2)), h('td', { class: 'num' }, s.nodes_visited === undefined ? (k === 'brute' ? int(data.n) + ' boxes' : '—') : String(s.nodes_visited)),
      h('td', { class: 'num' }, String(s.node_count)), h('td', { class: 'num' }, String(s.max_depth)), h('td', { class: 'num' }, fixed(s.build_us, 0)), h('td', { class: 'num' }, int(s.bytes))); });
    mount(out, panel('Measured on the server, this request', h('div', { class: 'stack' },
      table(['Index', 'Results', 'Mismatch', 'Query µs', 'Visited', 'Nodes', 'Depth', 'Build µs', 'Bytes'], rows),
      h('p', { class: 'small muted' }, 'Query µs is the median of 101 runs of query() on the server. Traced queries are checked against query(): ' +
        (['quadtree', 'rtree_str', 'rtree_insert'].every(k => data[k].trace_matches_query) ? 'all identical.' : 'MISMATCH.')),
      h('p', { class: 'small muted' }, 'On this data and query the STR-packed R-tree visited ' + data.rtree_str.nodes_visited + ' nodes and the insertion-built one ' +
        data.rtree_insert.nodes_visited + '; STR builds ' + data.rtree_str.node_count + ' nodes against ' + data.rtree_insert.node_count +
        '. One small query can go either way — the committed benchmark (Analytics, "insertion-built vs STR") measures the average over many queries at up to 100,000 zones.'))));
  }
  go();
};

// ── k-d tree ───────────────────────────────────────────────────────────────
VIEWS.kdtree = (body) => {
  const cv = canvas(520, 520);
  const out = h('div'); const stepHost = h('div');
  let q = [0.5, 0.5], data = null;
  const n = h('input', { type: 'number', min: 5, max: 600, value: 60, style: { width: '90px' } });
  mount(body, panel('Controls', h('div', { class: 'row' }, field('Points', n), h('button', { class: 'primary', onclick: go }, 'Rebuild'),
    h('span', { class: 'small muted' }, 'Click the canvas to move the query point.'))), h('div', { class: 'grid cols-2' }, h('div', { class: 'stack' }, cv, stepHost), out));
  cv.onclick = (e) => { const [x, y] = cv.local(e); q = [1 - y / cv.H, x / cv.W]; go(); };
  async function go() {
    data = await run('kdtree', { n: Number(n.value), q_lat: q[0], q_lon: q[1] });
    if (!data) return;
    mount(stepHost, stepper(data.visited.length, draw, 'visit'));
    mount(out, panel('Result', h('dl', { class: 'kv' }, h('dt', null, 'Nearest (tree)'), h('dd', null, '#' + data.nearest),
      h('dt', null, 'Nearest (linear scan)'), h('dd', null, '#' + data.linear_scan_nearest), h('dt', null, 'Agree'), h('dd', { style: { color: data.agree ? C.accent : C.bad } }, data.agree ? 'yes' : 'NO'),
      h('dt', null, 'Nodes visited'), h('dd', null, data.nodes_visited + ' of ' + data.n)), h('p', { class: 'small muted' }, 'Lines are the splits: vertical on longitude, horizontal on latitude, alternating with depth. Visited nodes light up in search order.')));
  }
  function draw(k) {
    const g = cv.g; cv.clear();
    const X = (lon) => lon * cv.W, Y = (lat) => (1 - lat) * cv.H;
    const rec = (i, a0, b0, a1, b1, d) => {
      if (i < 0) return;
      const nd = data.nodes[i];
      g.strokeStyle = nd[3] === 0 ? 'rgba(108,155,210,.6)' : 'rgba(142,124,216,.6)'; g.lineWidth = Math.max(0.5, 2 - d * 0.25);
      g.beginPath();
      if (nd[3] === 0) { g.moveTo(X(b0), Y(nd[1])); g.lineTo(X(b1), Y(nd[1])); rec(nd[4], a0, b0, nd[1], b1, d + 1); rec(nd[5], nd[1], b0, a1, b1, d + 1); }
      else { g.moveTo(X(nd[2]), Y(a0)); g.lineTo(X(nd[2]), Y(a1)); rec(nd[4], a0, b0, a1, nd[2], d + 1); rec(nd[5], a0, nd[2], a1, b1, d + 1); }
      g.stroke();
    };
    rec(data.root, 0, 0, 1, 1, 0);
    const vis = new Set(data.visited.slice(0, k + 1));
    data.nodes.forEach((nd, i) => { g.fillStyle = vis.has(i) ? C.accent : '#6f7b89'; g.beginPath(); g.arc(X(nd[2]), Y(nd[1]), vis.has(i) ? 4 : 2.5, 0, 7); g.fill(); });
    if (k >= data.visited.length - 1) { const best = data.nodes.find(nd => nd[0] === data.nearest); g.strokeStyle = C.warn; g.lineWidth = 2; g.beginPath(); g.moveTo(X(q[1]), Y(q[0])); g.lineTo(X(best[2]), Y(best[1])); g.stroke(); }
    g.fillStyle = C.warn; g.beginPath(); g.arc(X(q[1]), Y(q[0]), 5, 0, 7); g.fill();
  }
  go();
};

// ── Z-order ────────────────────────────────────────────────────────────────
VIEWS.zorder = (body) => {
  const cv = canvas(480, 480);
  const bits = h('select', null, [1, 2, 3, 4, 5].map(b => h('option', { value: b, selected: b === 3 }, (1 << b) + ' × ' + (1 << b))));
  mount(body, panel('Controls', h('div', { class: 'row' }, field('Grid', bits), h('button', { class: 'primary', onclick: go }, 'Draw'))), cv,
    h('p', { class: 'small muted' }, 'Each cell is numbered by its position after sorting on the 48-bit Morton key that Geohash::morton() computes for the cell centre; the line follows that order.'));
  async function go() {
    const d = await run('zorder', { bits: Number(bits.value) });
    if (!d) return;
    const g = cv.g; cv.clear();
    const s = cv.W / d.side;
    const centre = ([r, c]) => [c * s + s / 2, cv.H - (r * s + s / 2)];
    g.strokeStyle = C.line;
    for (let i = 0; i <= d.side; ++i) { g.beginPath(); g.moveTo(i * s, 0); g.lineTo(i * s, cv.H); g.moveTo(0, i * s); g.lineTo(cv.W, i * s); g.stroke(); }
    g.strokeStyle = C.accent; g.lineWidth = 2; g.beginPath();
    d.order.forEach((o, i) => { const [x, y] = centre(o); i ? g.lineTo(x, y) : g.moveTo(x, y); }); g.stroke();
    if (d.side <= 16) { g.fillStyle = C.hi; g.font = '11px ui-monospace, monospace'; d.order.forEach((o, i) => { const [x, y] = centre(o); g.fillText(String(i), x + 3, y - 3); }); }
  }
  go();
};

// ── Binary-tree drawing (AVL, heap) ─────────────────────────────────────────
function drawTree(cv, nodes, rootIdx, label, colourOf) {
  const g = cv.g; cv.clear();
  const byIdx = new Map(nodes.map(n => [n.idx, n]));
  const pos = new Map(); let x = 0, maxd = 0;
  const walk = (i, d) => { if (i < 0 || !byIdx.has(i)) return; const n = byIdx.get(i); walk(n.left, d + 1); pos.set(i, [x++, d]); maxd = Math.max(maxd, d); walk(n.right, d + 1); };
  walk(rootIdx, 0);
  const cols = Math.max(1, x), dx = (cv.W - 40) / cols, dy = Math.min(70, (cv.H - 50) / Math.max(1, maxd + 1));
  const P = (i) => { const [a, d] = pos.get(i); return [20 + dx * (a + 0.5), 30 + dy * d]; };
  g.strokeStyle = '#3a4757'; g.lineWidth = 1.2;
  for (const n of nodes) { if (!pos.has(n.idx)) continue; const [x0, y0] = P(n.idx); for (const c of [n.left, n.right]) if (c >= 0 && pos.has(c)) { const [x1, y1] = P(c); g.beginPath(); g.moveTo(x0, y0); g.lineTo(x1, y1); g.stroke(); } }
  for (const n of nodes) {
    if (!pos.has(n.idx)) continue;
    const [px, py] = P(n.idx);
    const lines = label(n);
    const w = Math.max(...lines.map(t => t.length)) * 6.4 + 10, hh = lines.length * 12 + 6;
    g.fillStyle = colourOf ? colourOf(n) : '#1a212a'; g.strokeStyle = '#4a5768';
    g.fillRect(px - w / 2, py - hh / 2, w, hh); g.strokeRect(px - w / 2, py - hh / 2, w, hh);
    g.fillStyle = C.hi; g.font = '10.5px ui-monospace, monospace';
    lines.forEach((t, k) => g.fillText(t, px - w / 2 + 5, py - hh / 2 + 13 + k * 12));
  }
}

VIEWS.avl = (body) => {
  const cv = canvas(760, 380);
  const out = h('div'); const stepHost = h('div');
  const presets = {
    'Ascending inserts (forces left rotations)': [10, 20, 30, 40, 50, 60, 70, 80, 90, 100].map((v, i) => ({ op: 'insert', low: v, high: v + 25 + (i % 3) * 10 })),
    'Zig-zag (forces double rotations)': [50, 20, 35, 80, 65, 72, 10, 15, 90, 85].map(v => ({ op: 'insert', low: v, high: v + 30 })),
    'Insert then delete': [40, 20, 60, 10, 30, 50, 70, 25, 35].map(v => ({ op: 'insert', low: v, high: v + 20 })).concat([{ op: 'remove', low: 20, high: 40 }, { op: 'remove', low: 10, high: 30 }, { op: 'remove', low: 25, high: 45 }]),
  };
  const pre = h('select', null, Object.keys(presets).map(k => h('option', { value: k }, k)));
  const stab = h('input', { type: 'number', value: 55, style: { width: '80px' } });
  const custom = h('input', { placeholder: 'or your own: e.g. 5-30, 12-18, 40-70, -12', style: { flex: 1 } });
  mount(body, panel('Controls', h('div', { class: 'stack' }, h('div', { class: 'row' }, field('Operations', pre), field('Stabbing query at t =', stab), h('button', { class: 'primary', onclick: go }, 'Run')),
    h('div', { class: 'row' }, custom, h('span', { class: 'small muted' }, '"a-b" inserts [a, b); "-a" removes the interval starting at a.')))), cv, stepHost, out);
  let data = null;
  async function go() {
    let ops = presets[pre.value];
    if (custom.value.trim()) {
      ops = [];
      for (const t of custom.value.split(',').map(s => s.trim()).filter(Boolean)) {
        const m = t.match(/^(-?\d+)\s*-\s*(-?\d+)$/);
        if (t.startsWith('-') && !m) {
          const low = Number(t.slice(1));
          const prev = ops.find(o => o.op === 'insert' && o.low === low);
          ops.push({ op: 'remove', low, high: prev ? prev.high : low + 1 });
        } else if (m) ops.push({ op: 'insert', low: Number(m[1]), high: Number(m[2]) });
      }
    }
    data = await run('avl', { ops, stab: Number(stab.value) });
    if (!data) return;
    mount(stepHost, stepper(data.steps.length, show, 'operation'));
  }
  function show(k) {
    const s = data.steps[k];
    const final = k === data.steps.length - 1;
    const visited = final ? new Set(data.stab.visited) : new Set();
    const nodes = s.nodes.map(n => ({ idx: n[0], low: n[1], high: n[2], max: n[3], height: n[4], left: n[5], right: n[6] }));
    const hOf = new Map(nodes.map(n => [n.idx, n.height]));
    drawTree(cv, nodes, s.root, (n) => {
      const bf = (n.left >= 0 ? hOf.get(n.left) : 0) - (n.right >= 0 ? hOf.get(n.right) : 0);
      return ['[' + n.low + ',' + n.high + ')', 'max ' + n.max + ' h' + n.height + ' bf' + (bf > 0 ? '+' : '') + bf];
    }, (n) => visited.has(n.idx) ? 'rgba(63,182,168,.35)' : '#1a212a');
    mount(out, h('div', { class: 'row small' },
      h('span', { class: 'chip' }, s.op + ' [' + s.low + ', ' + s.high + ')' + (s.applied ? '' : ' — not found')),
      h('span', { class: 'chip' }, 'rotations: ' + s.rotations_left + ' left, ' + s.rotations_right + ' right'),
      h('span', { class: 'chip' }, 'height ' + s.height + ' for ' + s.size + ' nodes'),
      h('span', { class: 'chip', style: { color: s.balanced && s.invariants ? C.accent : C.bad } }, s.balanced && s.invariants ? 'AVL + augmentation invariants hold' : 'INVARIANT BROKEN'),
      final ? h('span', { class: 'chip' }, 'stab t=' + data.stab.at + ': ' + data.stab.results + ' result(s), linear scan ' + data.stab.linear_scan_results + ', nodes entered ' + data.stab.visited.length + ' (teal)') : null));
  }
  go();
};

// ── Persistent quadtree ────────────────────────────────────────────────────
VIEWS.persistent = (body) => {
  const cv = canvas(760, 380);
  const out = h('div');
  const edit = h('select', null, [['move', 'Move one zone (geometry change)'], ['add', 'Add a zone'], ['remove', 'Remove a zone'], ['validity', 'Change validity only']].map(([v, t]) => h('option', { value: v }, t)));
  const zone = h('input', { type: 'number', min: 0, max: 199, value: 3, style: { width: '70px' } });
  const n = h('input', { type: 'number', min: 2, max: 200, value: 24, style: { width: '70px' } });
  mount(body, panel('Controls', h('div', { class: 'row' }, field('Zones', n), field('Edit', edit), field('Zone id', zone), h('button', { class: 'primary', onclick: go }, 'Apply'))),
    cv, out);
  async function go() {
    const d = await run('persistent', { n: Number(n.value), edit: edit.value, zone: Number(zone.value) });
    if (!d) return;
    const g = cv.g; cv.clear();
    const w = cv.W / 2 - 15;
    d.versions.forEach((v, vi) => {
      const ox = vi * (w + 30) + 5;
      g.fillStyle = C.text; g.font = '12px -apple-system, sans-serif';
      g.fillText('version ' + v.version + (vi === d.versions.length - 1 ? ' (after the edit)' : ' (before)') + ' — ' + v.new_nodes + ' new, ' + v.shared_nodes + ' shared', ox, 16);
      if (!v.nodes.length) return;
      const r = v.nodes[0];
      const sx = w / Math.max(1e-9, r[3] - r[1]), sy = (cv.H - 30) / Math.max(1e-9, r[2] - r[0]);
      for (const nd of v.nodes) {
        const fresh = vi === d.versions.length - 1 && !nd[6];
        g.strokeStyle = fresh ? C.warn : 'rgba(167,177,189,.35)'; g.lineWidth = fresh ? 2 : 1;
        g.strokeRect(ox + (nd[1] - r[1]) * sx, 24 + (r[2] - nd[2]) * sy, (nd[3] - nd[1]) * sx, (nd[2] - nd[0]) * sy);
      }
    });
    mount(out, h('div', { class: 'stack' }, h('div', { class: 'row small' },
      h('span', { class: 'chip' }, d.version_count + ' versions'), h('span', { class: 'chip' }, 'nodes allocated ' + int(d.nodes_allocated)),
      h('span', { class: 'chip' }, 'full copies would need ' + int(d.nodes_if_full_copies)), h('span', { class: 'chip' }, 'sharing ' + fixed(d.sharing_ratio, 2) + '×'),
      h('span', { class: 'chip' }, 'validity records ' + d.validity_records)),
      h('p', { class: 'small muted' }, 'Amber: nodes newly allocated by this edit (the copied root-to-leaf path). Grey: nodes the new version shares with the old one by reference. A validity-only change copies no nodes at all — it appends one record.'),
      table(['Time', 'Zones in force then'], d.history.map(([t, c]) => h('tr', null, h('td', { class: 'num' }, (t / 1000) + ' s'), h('td', { class: 'num' }, String(c)))))));
  }
  go();
};

// ── Graph search ───────────────────────────────────────────────────────────
VIEWS.graph = (body) => {
  const cv = canvas(760, 470);
  const out = h('div'); const stepHost = h('div'); const heapBox = h('div');
  let data = null, algo = 'astar', src = 0, dst = -1;
  const rows = h('input', { type: 'number', min: 3, max: 30, value: 14, style: { width: '70px' } });
  const cols = h('input', { type: 'number', min: 3, max: 30, value: 20, style: { width: '70px' } });
  const seed = h('input', { type: 'number', min: 0, value: 5, style: { width: '70px' } });
  const sel = h('select', { onchange: () => { algo = sel.value; steps(); } }, [['dijkstra', 'Dijkstra'], ['astar', 'A*']].map(([v, t]) => h('option', { value: v, selected: v === algo }, t)));
  mount(body, panel('Controls', h('div', { class: 'stack' }, h('div', { class: 'row' }, field('Rows', rows), field('Cols', cols), field('Seed', seed), field('Algorithm', sel), h('button', { class: 'primary', onclick: go }, 'Run')),
    h('p', { class: 'small muted' }, 'Click a junction to set the source; shift-click to set the target. The graph is a road-like jittered grid (graph::RoadGraph::grid) stored as an adjacency list; weights are segment lengths in metres.'))),
  cv, stepHost, h('div', { class: 'grid cols-2' }, out, heapBox));
  let bb = null;
  const proj = (la, lo) => [20 + (lo - bb[1]) / (bb[3] - bb[1]) * (cv.W - 40), cv.H - 20 - (la - bb[0]) / (bb[2] - bb[0]) * (cv.H - 40)];
  cv.onclick = (e) => {
    if (!data) return;
    const [x, y] = cv.local(e);
    let best = -1, bd = 1e9;
    data.positions.forEach((p, i) => { const [px, py] = proj(p[0], p[1]); const d = (px - x) ** 2 + (py - y) ** 2; if (d < bd) { bd = d; best = i; } });
    if (e.shiftKey) dst = best; else src = best;
    go();
  };
  async function go() {
    const nn = Number(rows.value) * Number(cols.value);
    if (dst < 0 || dst >= nn) dst = nn - 1;
    if (src >= nn) src = 0;
    data = await run('graph', { rows: Number(rows.value), cols: Number(cols.value), seed: Number(seed.value), source: src, target: dst, snapshots: 900 });
    if (!data) return;
    bb = [90, 180, -90, -180];
    for (const [la, lo] of data.positions) { bb[0] = Math.min(bb[0], la); bb[1] = Math.min(bb[1], lo); bb[2] = Math.max(bb[2], la); bb[3] = Math.max(bb[3], lo); }
    mount(out, panel('Same source and target', table(['', 'Dijkstra', 'A*'], [
      ['Settled nodes', data.dijkstra.expanded, data.astar.expanded], ['Heap pushes', data.dijkstra.pushes, data.astar.pushes],
      ['Stale pops (lazy deletion)', data.dijkstra.stale_pops, data.astar.stale_pops], ['Path cost (m)', fixed(data.dijkstra.cost_m, 1), fixed(data.astar.cost_m, 1)]]
      .map(r => h('tr', null, r.map((c, i) => h('td', { class: i ? 'num' : '' }, String(c)))))),
      h('p', { class: 'small muted' }, 'Same cost: ' + (data.same_cost ? 'yes' : 'NO') + '. Heuristic admissible on this graph (straight line ≤ every edge): ' + (data.heuristic_admissible ? 'yes' : 'NO') + '.')));
    steps();
  }
  function steps() { const t = data[algo]; mount(stepHost, stepper(t.settled.length, draw, 'settle')); }
  function draw(k) {
    const g = cv.g; cv.clear();
    const t = data[algo];
    g.strokeStyle = '#2a3440'; g.lineWidth = 1;
    data.adjacency.forEach((nb, u) => { const [x0, y0] = proj(...data.positions[u]); for (const [v] of nb) if (v > u) { const [x1, y1] = proj(...data.positions[v]); g.beginPath(); g.moveTo(x0, y0); g.lineTo(x1, y1); g.stroke(); } });
    const settled = new Set(t.settled.slice(0, k + 1));
    const front = t.frontier[k] || [];
    const inFront = new Set(front.map(f => f[1]));
    data.positions.forEach((p, i) => {
      const [x, y] = proj(p[0], p[1]);
      g.fillStyle = settled.has(i) ? 'rgba(63,182,168,.85)' : inFront.has(i) ? C.warn : '#3a4757';
      g.beginPath(); g.arc(x, y, settled.has(i) ? 3.6 : 2.6, 0, 7); g.fill();
    });
    if (k >= t.settled.length - 1 && t.path.length) {
      g.strokeStyle = C.hi; g.lineWidth = 3; g.beginPath();
      t.path.forEach((n, i) => { const [x, y] = proj(...data.positions[n]); i ? g.lineTo(x, y) : g.moveTo(x, y); }); g.stroke();
    }
    for (const [n, col] of [[data.source, C.blue], [data.target, C.bad]]) { const [x, y] = proj(...data.positions[n]); g.fillStyle = col; g.beginPath(); g.arc(x, y, 6, 0, 7); g.fill(); }
    mount(heapBox, panel('Binary-heap frontier before pop ' + (k + 1) + ' (' + (t.frontier_size[k] ?? 0) + ' after)', h('div', { class: 'small mono', style: { maxHeight: '220px', overflow: 'auto' } },
      front.length ? front.slice(0, 63).map((f, i) => h('span', { style: { display: 'inline-block', minWidth: '96px', color: i === 0 ? C.warn : C.text } }, '[' + i + '] ' + f[0] + '→' + f[1])) : 'snapshot not recorded for this step',
      h('div', { class: 'faint' }, 'key → node, array order: children of i at 2i+1 and 2i+2; [0] is the minimum and is popped next. Keys are ' + (algo === 'astar' ? 'f = g + h (metres).' : 'distance from the source (metres).')))));
  }
  go();
};

// ── Heap ───────────────────────────────────────────────────────────────────
VIEWS.heap = (body) => {
  const cv = canvas(760, 300);
  const arr = h('div', { class: 'cells' }); const stepHost = h('div');
  let ops = [41, 17, 88, 5, 23, 61, 9, 72, 30].map(v => ({ op: 'push', value: v })).concat([{ op: 'pop' }, { op: 'pop' }]);
  const val = h('input', { type: 'number', value: 12, style: { width: '80px' } });
  mount(body, panel('Controls', h('div', { class: 'row' }, val, h('button', { onclick: () => { ops.push({ op: 'push', value: Number(val.value) }); go(); } }, 'Push'),
    h('button', { onclick: () => { ops.push({ op: 'pop' }); go(); } }, 'Pop min'), h('button', { class: 'ghost', onclick: () => { ops = []; go(); } }, 'Clear'))), cv, arr, stepHost);
  let data = null;
  async function go() { data = await run('heap', { ops }); if (!data) return; mount(stepHost, data.steps.length ? stepper(data.steps.length, show, 'op') : null); if (!data.steps.length) { cv.clear(); mount(arr); } }
  function show(k) {
    const s = data.steps[k];
    const nodes = s.array.map((v, i) => ({ idx: i, v, left: 2 * i + 1 < s.array.length ? 2 * i + 1 : -1, right: 2 * i + 2 < s.array.length ? 2 * i + 2 : -1 }));
    if (nodes.length) drawTree(cv, nodes, 0, (n) => [String(n.v)], (n) => n.idx === 0 ? 'rgba(214,164,64,.35)' : '#1a212a'); else cv.clear();
    mount(arr, h('div', { class: 'small muted', style: { width: '100%' } }, s.op === 'push' ? 'push ' + s.value : 'pop → ' + (s.popped ?? 'empty'), ' — the same values as stored in the array:'),
      s.array.map((v, i) => h('div', { class: 'cell full' }, h('span', { class: 'ix' }, String(i)), String(v))));
  }
  go();
};

// ── Assignment ─────────────────────────────────────────────────────────────
VIEWS.assignment = (body) => {
  const out = h('div');
  const rows = h('input', { type: 'number', min: 1, max: 10, value: 5, style: { width: '70px' } });
  const cols = h('input', { type: 'number', min: 1, max: 10, value: 6, style: { width: '70px' } });
  const seed = h('input', { type: 'number', min: 0, value: 9, style: { width: '80px' } });
  mount(body, panel('Controls', h('div', { class: 'row' }, field('Responders', rows), field('Incidents', cols), field('Seed', seed), h('button', { class: 'primary', onclick: go }, 'Solve'))), out);
  async function go() {
    const d = await run('assignment', { rows: Number(rows.value), cols: Number(cols.value), seed: Number(seed.value) });
    if (!d) return;
    mount(out, h('div', { class: 'stack' }, table([''].concat(d.cost[0].map((_, j) => 'Incident ' + j)), d.cost.map((r, i) => h('tr', null, h('td', null, 'Responder ' + i),
      r.map((c, j) => { const o = d.optimal[i] === j, gr = d.greedy[i] === j;
        return h('td', { class: 'num', style: { background: o && gr ? 'rgba(63,182,168,.25)' : o ? 'rgba(63,182,168,.25)' : gr ? 'rgba(214,164,64,.25)' : '', outline: gr ? '1px solid ' + C.warn : '' } }, String(c)); })))),
    h('div', { class: 'row small' }, h('span', { class: 'chip', style: { color: C.accent } }, 'Hungarian total ' + d.optimal_total), h('span', { class: 'chip', style: { color: C.warn } }, 'Greedy total ' + d.greedy_total),
      h('span', { class: 'chip' }, 'greedy pays ' + (d.greedy_total - d.optimal_total) + ' more (' + fixed(100 * (d.greedy_total - d.optimal_total) / d.optimal_total, 1) + ' %)')),
    h('p', { class: 'small muted' }, 'Teal fill: the Hungarian choice. Amber outline: greedy\'s choice. Costs are random road distances in metres. Try several seeds: greedy is sometimes optimal, never better.')));
  }
  go();
};

// ── DSU ────────────────────────────────────────────────────────────────────
VIEWS.dsu = (body) => {
  const cv = canvas(760, 300);
  const out = h('div'); const stepHost = h('div');
  let ops = [{ op: 'unite', a: 0, b: 1 }, { op: 'unite', a: 2, b: 3 }, { op: 'snapshot' }, { op: 'unite', a: 1, b: 3 }, { op: 'unite', a: 4, b: 5 }, { op: 'unite', a: 5, b: 3 }, { op: 'rollback', mark: 2 }, { op: 'unite', a: 6, b: 7 }];
  const a = h('input', { type: 'number', min: 0, max: 9, value: 7, style: { width: '60px' } });
  const b = h('input', { type: 'number', min: 0, max: 9, value: 8, style: { width: '60px' } });
  const mark = h('input', { type: 'number', min: 0, value: 0, style: { width: '60px' } });
  mount(body, panel('Controls', h('div', { class: 'row' }, a, b, h('button', { onclick: () => { ops.push({ op: 'unite', a: Number(a.value), b: Number(b.value) }); go(); } }, 'Unite'),
    h('button', { onclick: () => { ops.push({ op: 'snapshot' }); go(); } }, 'Snapshot'), mark, h('button', { onclick: () => { ops.push({ op: 'rollback', mark: Number(mark.value) }); go(); } }, 'Roll back to mark'),
    h('button', { class: 'ghost', onclick: () => { ops = []; go(); } }, 'Clear'))), cv, stepHost, out);
  let data = null;
  async function go() { data = await run('dsu', { n: 10, ops }); if (!data) return; if (!data.steps.length) { cv.clear(); mount(stepHost); mount(out); return; } mount(stepHost, stepper(data.steps.length, show, 'op')); }
  function show(k) {
    const s = data.steps[k]; const g = cv.g; cv.clear();
    const n = data.n; const kids = Array.from({ length: n }, () => []);
    const roots = [];
    for (let i = 0; i < n; ++i) (s.parent[i] === i ? roots : kids[s.parent[i]]).push(i);
    const pos = new Map(); let x = 0;
    const lay = (i, d) => { const start = x; for (const c of kids[i]) lay(c, d + 1); const mid = kids[i].length ? (start + x - 1) / 2 : x++; pos.set(i, [mid, d]); };
    for (const r of roots) lay(r, 0);
    const P = (i) => { const [a2, d] = pos.get(i); return [30 + a2 * (cv.W - 60) / Math.max(1, n - 1), 40 + d * 70]; };
    g.strokeStyle = '#4a5768'; g.lineWidth = 1.4;
    for (let i = 0; i < n; ++i) if (s.parent[i] !== i) { const [x0, y0] = P(i), [x1, y1] = P(s.parent[i]); g.beginPath(); g.moveTo(x0, y0); g.lineTo(x1, y1); g.stroke(); }
    for (let i = 0; i < n; ++i) { const [px, py] = P(i); g.fillStyle = s.parent[i] === i ? 'rgba(63,182,168,.4)' : '#1a212a'; g.beginPath(); g.arc(px, py, 14, 0, 7); g.fill(); g.strokeStyle = '#6f7b89'; g.stroke();
      g.fillStyle = C.hi; g.font = '11px ui-monospace, monospace'; g.fillText(String(i), px - 4, py + 4); g.fillStyle = C.text; g.fillText('r' + s.rank[i], px + 15, py - 8); }
    mount(out, h('div', { class: 'row small' }, h('span', { class: 'chip' }, s.op + (s.op === 'unite' ? ' ' + s.a + ' ' + s.b + (s.merged ? ' (merged)' : ' (already joined)') : s.mark !== undefined ? ' mark ' + s.mark : '')),
      h('span', { class: 'chip' }, s.components + ' components'), h('span', { class: 'chip' }, 'undo stack depth ' + s.undo_depth),
      h('span', { class: 'small muted' }, 'Arrows point to parents; roots are teal; r = rank. No path compression, so a rollback restores the exact earlier forest.')));
  }
  go();
};

// ── Hash table ─────────────────────────────────────────────────────────────
VIEWS.hash = (body) => {
  const cells = h('div', { class: 'cells' }); const info = h('div'); const stepHost = h('div');
  let ops = [3, 19, 35, 7, 51, 23, 2].map(k => ({ op: 'put', key: k })).concat([{ op: 'erase', key: 19 }, { op: 'get', key: 35 }, { op: 'put', key: 67 }]);
  const key = h('input', { type: 'number', min: 0, value: 83, style: { width: '90px' } });
  mount(body, panel('Controls', h('div', { class: 'stack' }, h('div', { class: 'row' }, key,
    ['put', 'erase', 'get'].map(o => h('button', { onclick: () => { ops.push({ op: o, key: Number(key.value) }); go(); } }, o)),
    h('button', { onclick: () => { for (let i = 0; i < 10; ++i) ops.push({ op: 'put', key: 100 + i * 16 }); go(); } }, 'Insert 10 colliding keys'),
    h('button', { class: 'ghost', onclick: () => { ops = []; go(); } }, 'Clear')),
    h('p', { class: 'small muted' }, 'The table starts with 16 slots. Keys are hashed by Fibonacci multiplication (k × 2654435769) masked to the table size. Teal ring: home slot; amber: slots the probe walked; dashed: tombstones.'))), stepHost, info, cells);
  let data = null;
  async function go() { data = await run('hash', { ops }); if (!data) return; if (!data.steps.length) { mount(cells); mount(info); mount(stepHost); return; } mount(stepHost, stepper(data.steps.length, show, 'op')); }
  function show(k) {
    const s = data.steps[k]; const probe = new Set(s.probe);
    mount(info, h('div', { class: 'row small' }, h('span', { class: 'chip' }, s.op + ' ' + s.key + ' → ' + (s.inserted !== undefined ? (s.inserted ? 'inserted' : 'overwritten') : s.erased !== undefined ? (s.erased ? 'erased (tombstone)' : 'absent') : s.found ? 'found' : 'absent')),
      h('span', { class: 'chip' }, 'home slot ' + s.home + ', probed ' + s.probe.length), s.resized ? h('span', { class: 'chip', style: { color: C.warn } }, 'table rebuilt') : null,
      h('span', { class: 'chip' }, s.size + ' live / ' + s.capacity + ' slots, load ' + fixed(s.size / s.capacity, 2)), h('span', { class: 'chip' }, s.tombstones + ' tombstones · ' + s.growths + ' growths · ' + s.rehashes + ' same-size rehashes')));
    mount(cells, s.slots.map((v, i) => h('div', { class: 'cell' + (v === null ? '' : v === 'T' ? ' tomb' : ' full') + (i === s.home && !s.resized ? ' home' : probe.has(i) && !s.resized ? ' probe' : '') },
      h('span', { class: 'ix' }, String(i)), v === null ? '' : v === 'T' ? '†' : String(v))));
  }
  go();
};

// ── Timer wheel ────────────────────────────────────────────────────────────
VIEWS.wheel = (body) => {
  const cells = h('div', { class: 'cells' }); const info = h('div'); const stepHost = h('div');
  let ops = [{ op: 'schedule', id: 1, deadline: 3000 }, { op: 'schedule', id: 2, deadline: 5000 }, { op: 'schedule', id: 3, deadline: 11000 }, { op: 'schedule', id: 4, deadline: 4000 },
    { op: 'cancel', id: 4, deadline: 4000 }, { op: 'advance', to: 5000 }, { op: 'advance', to: 9000 }, { op: 'advance', to: 12000 }];
  const id = h('input', { type: 'number', value: 7, style: { width: '60px' } }), dl = h('input', { type: 'number', value: 14000, style: { width: '90px' } }), to = h('input', { type: 'number', value: 15000, style: { width: '90px' } });
  mount(body, panel('Controls', h('div', { class: 'row' }, field('Alert id', id), field('Deadline ms', dl), h('button', { onclick: () => { ops.push({ op: 'schedule', id: Number(id.value), deadline: Number(dl.value) }); go(); } }, 'Schedule'),
    h('button', { onclick: () => { ops.push({ op: 'cancel', id: Number(id.value), deadline: Number(dl.value) }); go(); } }, 'Cancel (acknowledge)'), field('Advance to ms', to),
    h('button', { onclick: () => { ops.push({ op: 'advance', to: Number(to.value) }); go(); } }, 'Advance'), h('button', { class: 'ghost', onclick: () => { ops = []; go(); } }, 'Clear'))), stepHost, info, cells,
  h('p', { class: 'small muted' }, '8 slots of 1 s. A deadline lands in slot (expiry tick mod 8); one that is more than a revolution away waits in place (its expiry tick is shown) and fires on a later lap.'));
  let data = null;
  async function go() { data = await run('wheel', { slots: 8, ops }); if (!data) return; if (!data.steps.length) { mount(cells); mount(info); mount(stepHost); return; } mount(stepHost, stepper(data.steps.length, show, 'op')); }
  function show(k) {
    const s = data.steps[k]; const cur = s.tick % data.slots;
    mount(info, h('div', { class: 'row small' }, h('span', { class: 'chip' }, s.op + (s.id !== undefined ? ' #' + s.id : '') + (s.deadline !== undefined ? ' @' + s.deadline + ' ms' : '') + (s.to !== undefined ? ' to ' + s.to + ' ms' : '')),
      s.fired ? h('span', { class: 'chip', style: { color: C.warn } }, 'fired: ' + (s.fired.length ? s.fired.map(x => '#' + x).join(', ') : 'none')) : null,
      s.cancelled !== undefined ? h('span', { class: 'chip' }, s.cancelled ? 'cancelled' : 'not found') : null, h('span', { class: 'chip' }, 'tick ' + s.tick + ', ' + s.pending + ' pending')));
    mount(cells, s.wheel.map((items, i) => h('div', { class: 'cell', style: { minWidth: '86px', height: '64px', alignItems: 'start', paddingTop: '14px', borderColor: i === cur ? C.accent : '' } },
      h('span', { class: 'ix' }, 'slot ' + i + (i === cur ? ' ◀ now' : '')), h('div', { class: 'small' }, items.map(([iid, et]) => h('div', null, '#' + iid + ' t' + et))))));
  }
  go();
};

// ── Ring buffer ────────────────────────────────────────────────────────────
VIEWS.ring = (body) => {
  const cells = h('div', { class: 'cells' }); const info = h('div'); const stepHost = h('div');
  const ops = Array.from({ length: 11 }, (_, i) => ({ value: (i + 1) * 10 }));
  mount(body, stepHost, info, cells, h('p', { class: 'small muted' }, 'Capacity 8. Pushing a 9th value overwrites the oldest: the head moves on and nothing is allocated or shifted. The tourist ping window is the same class with capacity 64.'));
  run('ring', { ops }).then(data => { if (!data) return; mount(stepHost, stepper(data.steps.length, (k) => {
    const s = data.steps[k];
    mount(info, h('div', { class: 'row small' }, h('span', { class: 'chip' }, 'pushed ' + s.pushed), h('span', { class: 'chip' }, 'head (next write) = slot ' + s.head), h('span', { class: 'chip' }, 'newest first: ' + s.newest_first.join(', '))));
    mount(cells, s.physical.map((v, i) => h('div', { class: 'cell' + (i < s.size ? ' full' : ''), style: i === s.head ? { boxShadow: 'inset 0 0 0 2px ' + C.warn } : null }, h('span', { class: 'ix' }, String(i)), i < s.size ? String(v) : '')));
  }, 'push')); });
};

// ── Merkle ─────────────────────────────────────────────────────────────────
VIEWS.merkle = (body) => {
  const cv = canvas(760, 340);
  const out = h('div');
  const leaves = h('input', { value: 'enter Z1, alert A1, ack A1, exit Z1, incident I1, assign R2, resolve I1', style: { flex: 1 } });
  const prove = h('input', { type: 'number', min: 0, value: 2, style: { width: '60px' } });
  const tamper = h('input', { type: 'number', min: -1, value: -1, style: { width: '60px' } });
  mount(body, panel('Controls', h('div', { class: 'row' }, field('Leaves (comma separated)', leaves), field('Prove leaf', prove), field('Tamper leaf (-1 none)', tamper), h('button', { class: 'primary', onclick: go }, 'Build'))), cv, out);
  async function go() {
    const ls = leaves.value.split(',').map(s => s.trim()).filter(Boolean);
    const d = await run('merkle', { leaves: ls, prove: Number(prove.value), tamper: Number(tamper.value) });
    if (!d) return;
    const g = cv.g; cv.clear();
    const pos = []; let leafX = 0; let maxd = 0;
    const lay = (n, d0) => { maxd = Math.max(maxd, d0); if (!n.kids) { n.x = leafX++; } else { lay(n.kids[0], d0 + 1); lay(n.kids[1], d0 + 1); n.x = (n.kids[0].x + n.kids[1].x) / 2; } n.d = d0; pos.push(n); };
    lay(d.tree, 0);
    const dx = (cv.W - 40) / Math.max(1, leafX), dy = (cv.H - 60) / Math.max(1, maxd);
    const P = (n) => [20 + dx * (n.x + 0.5), 25 + dy * n.d];
    const pidx = Number(prove.value);
    const onPath = (n) => n.lo <= pidx && pidx < n.hi;
    g.strokeStyle = '#3a4757';
    for (const n of pos) if (n.kids) for (const k of n.kids) { const [x0, y0] = P(n), [x1, y1] = P(k); g.beginPath(); g.moveTo(x0, y0); g.lineTo(x1, y1); g.stroke(); }
    const sib = new Set();
    for (const n of pos) if (n.kids && onPath(n)) for (const k of n.kids) if (!onPath(k)) sib.add(k);
    for (const n of pos) {
      const [x, y] = P(n);
      g.fillStyle = onPath(n) ? 'rgba(63,182,168,.35)' : sib.has(n) ? 'rgba(214,164,64,.35)' : '#1a212a';
      g.fillRect(x - 42, y - 11, 84, 22); g.strokeStyle = '#4a5768'; g.strokeRect(x - 42, y - 11, 84, 22);
      g.fillStyle = C.hi; g.font = '10px ui-monospace, monospace'; g.fillText(n.hash.slice(0, 11), x - 37, y + 4);
      if (!n.kids) { g.fillStyle = C.text; g.fillText((ls[n.lo] || '').slice(0, 12), x - 37, y + 22); }
    }
    mount(out, h('div', { class: 'stack' }, h('div', { class: 'row small' },
      h('span', { class: 'chip' }, 'root ' + d.root.slice(0, 16) + '…'), h('span', { class: 'chip', style: { color: d.drawn_root_matches_log ? C.accent : C.bad } }, d.drawn_root_matches_log ? 'drawn tree root = MerkleLog::root()' : 'ROOT MISMATCH'),
      h('span', { class: 'chip', style: { color: d.proof_verifies ? C.accent : C.bad } }, 'proof for leaf ' + d.prove + ' (' + d.proof.length + ' hashes): ' + (d.proof_verifies ? 'verifies' : 'FAILS'))),
      d.tampered !== undefined ? h('div', { class: 'note', style: { borderLeftColor: C.bad } }, 'Leaf ' + d.tampered + ' edited: the root becomes ' + d.tampered_root.slice(0, 16) + '…, and the edited leaf ' + (d.tampered_leaf_verifies_against_original_root ? 'STILL verifies (bug!)' : 'no longer verifies against the published root') + '.') : null,
      h('p', { class: 'small muted' }, 'Teal: the leaf and its ancestors. Amber: the sibling hashes that form the inclusion proof — all a verifier needs besides the root.')));
  }
  go();
};

// ── Lamport ────────────────────────────────────────────────────────────────
VIEWS.lamport = (body) => {
  const cv = canvas(760, 260);
  const out = h('div');
  const ops = [{ device: 0, op: 'local' }, { device: 0, op: 'send' }, { device: 1, op: 'local' }, { device: 1, op: 'local' }, { device: 1, op: 'local' },
    { device: 2, op: 'recv', message: 0 }, { device: 2, op: 'send' }, { device: 1, op: 'recv', message: 1 }, { device: 0, op: 'local' }, { device: 1, op: 'local' }];
  mount(body, cv, out);
  run('lamport', { ops }).then(d => {
    if (!d) return;
    const g = cv.g; cv.clear();
    const lane = (dv) => 50 + dv * 80;
    for (let dv = 0; dv < 3; ++dv) { g.strokeStyle = '#3a4757'; g.beginPath(); g.moveTo(80, lane(dv)); g.lineTo(cv.W - 20, lane(dv)); g.stroke(); g.fillStyle = C.text; g.font = '12px -apple-system, sans-serif'; g.fillText('device ' + (dv + 1), 12, lane(dv) + 4); }
    const X = (i) => 100 + i * (cv.W - 140) / d.steps.length;
    const sends = [];
    d.steps.forEach((s, i) => { if (s.op === 'send') sends[s.message] = [X(i), lane(s.device - 1)]; });
    d.steps.forEach((s, i) => {
      if (s.op === 'recv') { const [sx, sy] = sends[s.message]; g.strokeStyle = C.warn; g.beginPath(); g.moveTo(sx, sy); g.lineTo(X(i), lane(s.device - 1)); g.stroke(); }
    });
    d.steps.forEach((s, i) => { const x = X(i), y = lane(s.device - 1); g.fillStyle = s.op === 'local' ? C.blue : s.op === 'send' ? C.accent : C.warn; g.beginPath(); g.arc(x, y, 6, 0, 7); g.fill(); g.fillStyle = C.hi; g.font = '11px ui-monospace, monospace'; g.fillText(String(s.stamp), x - 4, y - 10); });
    mount(out, h('div', { class: 'stack' }, h('p', { class: 'small muted' }, 'Blue: local event. Teal: send. Amber: receive — the receiver jumps to max(own, message) + 1, so every receive is stamped after its send.'),
      h('div', { class: 'row small' }, 'Total order by (counter, device): ', d.total_order.map(([i, c, dv]) => h('span', { class: 'chip' }, d.steps[i].op + '@' + dv + ' (' + c + ')')))));
  });
};

// ── Geometry ───────────────────────────────────────────────────────────────
VIEWS.geometry = (body) => {
  const cv = canvas(560, 420);
  const out = h('div');
  let pt = [25.5815, 91.8812];
  const acc = h('input', { type: 'range', min: 1, max: 120, value: 20 });
  const accL = h('span', { class: 'mono small' }, '±20 m');
  acc.oninput = () => { accL.textContent = '±' + acc.value + ' m'; go(); };
  mount(body, panel('Controls', h('div', { class: 'row' }, 'GPS accuracy', acc, accL, h('span', { class: 'small muted' }, 'Click to move the fix. The polygon is concave on purpose.'))), h('div', { class: 'grid cols-2' }, cv, out));
  const bb = [25.5795, 91.8792, 25.5835, 91.8848];
  const X = (lo) => (lo - bb[1]) / (bb[3] - bb[1]) * cv.W, Y = (la) => (bb[2] - la) / (bb[2] - bb[0]) * cv.H;
  const mPerPx = (bb[3] - bb[1]) * 111320 * Math.cos(25.58 * Math.PI / 180) / cv.W;
  cv.onclick = (e) => { const [x, y] = cv.local(e); pt = [bb[2] - y / cv.H * (bb[2] - bb[0]), bb[1] + x / cv.W * (bb[3] - bb[1])]; go(); };
  async function go() {
    const d = await run('geometry', { lat: pt[0], lon: pt[1], accuracy_m: Number(acc.value) });
    if (!d) return;
    const g = cv.g; cv.clear();
    g.beginPath(); d.ring.forEach(([la, lo], i) => i ? g.lineTo(X(lo), Y(la)) : g.moveTo(X(lo), Y(la))); g.closePath();
    g.fillStyle = 'rgba(229,72,77,.14)'; g.fill(); g.strokeStyle = C.bad; g.lineWidth = 2; g.stroke();
    const col = d.containment === 'inside' ? C.bad : d.containment === 'uncertain' ? C.warn : C.accent;
    g.fillStyle = col; g.globalAlpha = 0.18; g.beginPath(); g.arc(X(pt[1]), Y(pt[0]), d.accuracy_m / mPerPx, 0, 7); g.fill(); g.globalAlpha = 1;
    g.beginPath(); g.arc(X(pt[1]), Y(pt[0]), 4, 0, 7); g.fill();
    g.strokeStyle = C.hi; g.setLineDash([4, 3]); g.beginPath(); g.moveTo(X(pt[1]), Y(pt[0])); g.lineTo(X(d.nearest_boundary[1]), Y(d.nearest_boundary[0])); g.stroke(); g.setLineDash([]);
    g.strokeStyle = 'rgba(214,164,64,.5)'; g.beginPath(); g.moveTo(X(pt[1]), Y(pt[0])); g.lineTo(cv.W, Y(pt[0])); g.stroke();
    mount(out, panel('What the engine says', h('dl', { class: 'kv' },
      h('dt', null, 'Ray casting'), h('dd', null, d.ray_casting ? 'inside' : 'outside'), h('dt', null, 'Winding number'), h('dd', null, d.winding_number ? 'inside' : 'outside'),
      h('dt', null, 'Signed distance'), h('dd', null, fixed(d.signed_distance_m, 1) + ' m (negative = inside)'),
      h('dt', null, 'With ±' + d.accuracy_m + ' m'), h('dd', { style: { color: col, fontWeight: 600 } }, d.containment.toUpperCase()),
      h('dt', null, 'Polygon check'), h('dd', null, d.validity)),
      h('p', { class: 'small muted' }, 'Uncertain means the accuracy disc crosses the boundary: |distance| < accuracy. The amber ray is the one ray casting counts crossings along.')));
  }
  go();
};
