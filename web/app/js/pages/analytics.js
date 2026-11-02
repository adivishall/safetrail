import { get, api } from '../api.js';
import { h, mount, kpi, panel, int, fixed, table } from '../util.js';

// Two kinds of numbers, never mixed: LIVE counters from the running platform,
// and MEASURED benchmark results committed in bench/results/ by `make bench`
// (with the machine they were measured on). Nothing here is typed in by hand.
export function render(root, ctx) {
  const liveBox = h('div');
  const kindsBox = h('div');
  const benchBox = h('div', { class: 'stack' });
  const envBox = h('div');
  mount(root, h('div', { class: 'page' },
    h('div', { class: 'page-head' }, h('div', null, h('h1', null, 'Analytics'),
      h('p', null, 'Live counters come from the running platform. Benchmark charts are drawn from the CSV files `make bench` writes to bench/results/ — the same files the reports quote.'))),
    h('h2', { style: { margin: '6px 0 10px' } }, 'Live platform'), liveBox,
    h('div', { class: 'grid cols-2', style: { marginTop: '12px' } }, panel('Events by kind (this run)', kindsBox), panel('Benchmark environment', envBox)),
    h('h2', { style: { margin: '22px 0 10px' } }, 'Measured benchmarks'), benchBox));

  async function loadLive() {
    try {
      const s = await get('/api/stats');
      mount(liveBox, h('div', { class: 'kpis' },
        kpi('Fixes accepted', int(s.ingest.accepted), int(s.ingest.duplicates) + ' duplicates, ' + int(s.ingest.out_of_order) + ' late'),
        kpi('Evaluation', fixed(s.evaluator.mean_us, 2) + ' µs', 'mean wall time per fix, ' + int(s.evaluator.calls) + ' calls'),
        kpi('Candidates/fix', s.evaluator.evaluations ? fixed(s.evaluator.candidates_examined / s.evaluator.evaluations, 2) : '—', 'after the ' + s.index.kind + ' prune'),
        kpi('Flaps suppressed', int(s.evaluator.flaps_suppressed), 'by hysteresis'),
        kpi('Alerts → incidents', int(s.correlation.alerts_ingested) + ' → ' + int(s.correlation.incidents_opened), 'compression ' + fixed(s.correlation.compression, 2) + '×'),
        kpi('Escalations', int(s.alerts.escalations), 'timer-wheel deadlines that fired'),
        kpi('Cohesion pass', fixed(s.cohesion.mean_us, 1) + ' µs', int(s.cohesion.passes) + ' passes'),
        kpi('History sharing', fixed(s.history.sharing_ratio, 2) + '×', int(s.history.versions) + ' versions, ' + int(s.history.nodes_allocated) + ' nodes'),
        kpi('Sync', int(s.sync.merged) + ' merged', int(s.sync.duplicates_rejected) + ' duplicates rejected'),
        kpi('Road graph', int(s.roads.nodes), int(s.roads.edges) + ' directed edges')));
      const kinds = Object.entries(s.events_by_kind).sort((a, b) => b[1] - a[1]);
      const max = Math.max(1, ...kinds.map(k => k[1]));
      mount(kindsBox, kinds.length ? h('div', { class: 'bars' }, kinds.map(([k, v]) => h('div', { class: 'bar-row' },
        h('span', { class: 'small' }, k.replace(/_/g, ' ')), h('div', { class: 'bar-track' }, h('div', { class: 'bar-fill', style: { width: (100 * v / max) + '%' } })),
        h('span', { class: 'num small', style: { textAlign: 'right' } }, int(v))))) : h('div', { class: 'empty' }, 'No events yet.'));
    } catch (e) { mount(liveBox, h('div', { class: 'error-box' }, e.message)); }
  }

  async function csv(name) {
    const text = await api('GET', '/api/bench/' + name, undefined, true);
    const lines = text.trim().split('\n');
    const head = lines[0].split(',');
    return lines.slice(1).map(l => { const c = l.split(','); const o = {}; head.forEach((k, i) => { o[k] = isNaN(Number(c[i])) ? c[i] : Number(c[i]); }); return o; });
  }

  async function loadBench() {
    try { mount(envBox, h('pre', { class: 'mono small', style: { margin: 0, whiteSpace: 'pre-wrap' } }, await api('GET', '/api/bench/environment.txt', undefined, true))); }
    catch (e) { mount(envBox, h('div', { class: 'muted' }, 'No environment file: run make bench.')); }
    const parts = [];
    try {
      const s = await csv('index_scaling.csv');
      parts.push(panel('Range query latency vs zone count (one district, log–log)', h('div', { class: 'stack' },
        lineChart(s.map(r => r.zones), [['brute force', s.map(r => r.brute_us), '#e07b39'], ['quadtree', s.map(r => r.quad_us), '#6c9bd2'], ['R-tree (STR)', s.map(r => r.rtree_us), '#3fb6a8']], 'zones', 'µs per query', true),
        table(['Zones', 'Brute µs', 'Quadtree µs', 'R-tree µs', 'Quad ×', 'R-tree ×', 'Candidates'], s.map(r => h('tr', null, [r.zones, r.brute_us, r.quad_us, r.rtree_us, r.quad_speedup, r.rtree_speedup, r.candidates].map((v, i) => h('td', { class: 'num' }, i === 0 ? int(v) : String(v)))))),
        h('p', { class: 'small muted' }, 'Speedup flattens because about 100 zones overlap each query at 100,000 zones in one district: every index must return them all. The answer size k bounds O(log n + k).'))));
    } catch (e) { parts.push(h('div', { class: 'muted' }, 'index_scaling.csv not found')); }
    try {
      const s = await csv('index_density.csv');
      parts.push(panel('Constant density (k ≈ 1): where the logarithm shows', lineChart(s.map(r => r.zones), [['brute force', s.map(r => r.brute_us), '#e07b39'], ['quadtree', s.map(r => r.quad_us), '#6c9bd2'], ['R-tree', s.map(r => r.rtree_us), '#3fb6a8']], 'zones', 'µs per query', true)));
    } catch (e) { /* optional */ }
    try {
      const s = await csv('index_build.csv');
      parts.push(panel('R-tree: insertion-built vs STR bulk-loaded', table(['Zones', 'Insert build ms', 'STR build ms', 'Insert nodes', 'STR nodes', 'Insert query µs', 'STR query µs', 'Query gain'],
        s.map(r => h('tr', null, [r.zones, r.incremental_build_ms, r.str_build_ms, r.incremental_nodes, r.str_nodes, r.incremental_us, r.str_us, r.query_gain + '×'].map(v => h('td', { class: 'num' }, String(v))))))));
    } catch (e) { /* optional */ }
    try {
      const s = await csv('multi_user.csv');
      parts.push(panel('Multi-user evaluation through the platform', h('div', { class: 'stack' },
        table(Object.keys(s[0]), s.map(r => h('tr', null, Object.values(r).map(v => h('td', { class: 'num' }, String(v)))))),
        h('p', { class: 'small muted' }, 'Fixes pushed through Platform::ingest_fix — validation, ordering checks, trail, evaluation, alerting — for N independent users. Written by `make bench` section 19.'))));
    } catch (e) { parts.push(panel('Multi-user evaluation', h('div', { class: 'muted' }, 'multi_user.csv not present yet: run make bench.'))); }
    try {
      const s = await csv('interval_tree.csv');
      parts.push(panel('AVL interval tree vs linear scan', table(['Regime', 'Intervals', 'Scan µs', 'Tree µs', 'Speedup', 'Hits/stab', 'Height', 'AVL bound'],
        s.map(r => h('tr', null, [r.regime, r.intervals, r.scan_us, r.tree_us, r.speedup + '×', r.hits_per_stab, r.height, r.avl_bound].map(v => h('td', { class: 'num' }, String(v))))))));
    } catch (e) { /* optional */ }
    try {
      const s = await csv('versioned_index.csv');
      parts.push(panel('Persistent quadtree: structural sharing', table(['Versions', 'Nodes allocated', 'Full copies would need', 'Sharing', 'Past query µs', 'Present µs'],
        s.map(r => h('tr', null, [r.versions, r.allocated, r.full_copies, r.sharing_ratio + '×', r.query_past_us, r.query_now_us].map(v => h('td', { class: 'num' }, String(v))))))));
    } catch (e) { /* optional */ }
    try {
      const s = await csv('routing.csv');
      parts.push(panel('Dijkstra vs A*: nodes settled', lineChart(s.map(r => r.nodes), [['Dijkstra', s.map(r => r.dijkstra_expanded), '#e07b39'], ['A*', s.map(r => r.astar_expanded), '#3fb6a8']], 'graph nodes', 'nodes settled', true)));
    } catch (e) { /* optional */ }
    try {
      const s = await csv('dispatch.csv');
      parts.push(panel('Greedy vs Hungarian dispatch', table(['Size', 'Greedy avg m', 'Optimal avg m', 'Saved', 'Optimal never worse'],
        s.map(r => h('tr', null, [r.size, r.greedy_avg_m, r.optimal_avg_m, r.saved_pct + ' %', r.optimal_never_worse ? 'yes' : 'NO'].map(v => h('td', { class: 'num' }, String(v))))))));
    } catch (e) { /* optional */ }
    try {
      const s = await csv('power.csv');
      parts.push(panel('Adaptive sampling vs fixed 1 Hz', h('div', { class: 'stack' }, table(['Profile', 'Continuous fixes', 'Adaptive fixes', 'Battery saved', 'Near-zone recall'],
        s.map(r => h('tr', null, [r.profile, r.continuous_fixes, r.adaptive_fixes, r.battery_saved_pct + ' %', r.near_zone_recall_pct + ' %'].map(v => h('td', { class: 'num' }, String(v)))))),
        h('p', { class: 'small muted' }, 'Battery saving is a model: energy proportional to fixes taken. Radio, screen and CPU are not modelled.'))));
    } catch (e) { /* optional */ }
    mount(benchBox, parts);
  }

  loadLive(); loadBench();
  let n = 0;
  const off = ctx.onLive(() => { if (++n % 3 === 0) loadLive(); });
  return { destroy() { off(); } };
}

export function lineChart(xs, series, xLabel, yLabel, log) {
  const W = 640, H = 260, L = 58, R = 120, T = 14, B = 36;
  const ys = series.flatMap(s => s[1]).filter(v => v > 0);
  const tx = log ? Math.log10 : (v) => v, ty = log ? Math.log10 : (v) => v;
  const x0 = tx(Math.min(...xs)), x1 = tx(Math.max(...xs)), y0 = ty(Math.min(...ys)), y1 = ty(Math.max(...ys));
  const X = (v) => L + (tx(v) - x0) / Math.max(1e-9, x1 - x0) * (W - L - R);
  const Y = (v) => H - B - (ty(v) - y0) / Math.max(1e-9, y1 - y0) * (H - T - B);
  const ns = 'http://www.w3.org/2000/svg';
  const el = (t, a, text) => { const e = document.createElementNS(ns, t); for (const k in a) e.setAttribute(k, a[k]); if (text !== undefined) e.textContent = text; return e; };
  const svg = el('svg', { viewBox: `0 0 ${W} ${H}`, role: 'img', 'aria-label': yLabel + ' against ' + xLabel });
  const gridVals = log ? [] : [];
  if (log) for (let p = Math.floor(y0); p <= Math.ceil(y1); ++p) gridVals.push(Math.pow(10, p));
  for (const v of gridVals) {
    if (ty(v) < y0 - 1e-9 || ty(v) > y1 + 1e-9) continue;
    svg.append(el('line', { x1: L, x2: W - R, y1: Y(v), y2: Y(v), stroke: '#273140' }));
    svg.append(el('text', { x: L - 6, y: Y(v) + 4, 'text-anchor': 'end' }, v >= 1 ? String(v) : v.toPrecision(1)));
  }
  for (const v of xs) svg.append(el('text', { x: X(v), y: H - B + 16, 'text-anchor': 'middle' }, v >= 1000 ? (v / 1000) + 'k' : String(v)));
  svg.append(el('text', { x: (L + W - R) / 2, y: H - 4, 'text-anchor': 'middle' }, xLabel));
  svg.append(el('text', { x: 12, y: T + 8 }, yLabel));
  for (const [name, vals, colour] of series) {
    const pts = vals.map((v, i) => v > 0 ? X(xs[i]) + ',' + Y(v) : null).filter(Boolean).join(' ');
    svg.append(el('polyline', { points: pts, fill: 'none', stroke: colour, 'stroke-width': 2 }));
    vals.forEach((v, i) => { if (v > 0) svg.append(el('circle', { cx: X(xs[i]), cy: Y(v), r: 3, fill: colour })); });
    const last = vals.length - 1;
    svg.append(el('text', { x: W - R + 8, y: Y(vals[last]) + 4, fill: colour }, name));
  }
  return h('div', { class: 'chart' }, svg);
}
