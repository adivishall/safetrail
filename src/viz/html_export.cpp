#include "safetrail/viz/html_export.hpp"
#include "safetrail/index/quadtree.hpp"
#include "safetrail/index/rtree.hpp"
#include "safetrail/index/brute_force.hpp"
#include "safetrail/index/versioned_index.hpp"
#include "safetrail/alert/correlator.hpp"
#include "safetrail/evidence/merkle_log.hpp"
#include "safetrail/geo/haversine.hpp"
#include <algorithm>
#include <cstdio>
#include <climits>
#include <fstream>
#include <string>

namespace safetrail::viz {

void TraceRecorder::capture(const sim::Simulator& s) {
  const int64_t now = s.now_ms();
  if (last_capture_ms_ >= 0 && now - last_capture_ms_ < opt_.frame_interval_ms) return;
  last_capture_ms_ = now;

  Frame f;
  f.t_ms = now;
  for (const auto& t : s.tourists()) {
    f.lat.push_back(float(t.last_fix.pos.lat));
    f.lon.push_back(float(t.last_fix.pos.lon));
    f.acc.push_back(float(t.last_fix.accuracy_m));
    uint8_t st = 0;
    for (const auto& zs : t.zone_states) {
      if (zs.confirmed == geo::Containment::Inside) { st = 2; break; }
      if (zs.confirmed == geo::Containment::Uncertain) st = 1;
    }
    f.state.push_back(st);
  }
  frames_.push_back(std::move(f));
}

static const char* kShell = R"HTML(<!doctype html>
<meta charset="utf-8"><title>__TITLE__</title>
<style>
  :root{--bg:#0e1116;--panel:#161b22;--line:#2a323d;--fg:#e6edf3;--dim:#8b949e;
        --r:#f85149;--o:#d29922;--g:#3fb950;--b:#58a6ff;--u:#bc8cff}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--fg);
       font:13px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace}
  header{padding:10px 16px;border-bottom:1px solid var(--line);display:flex;
         gap:20px;align-items:baseline;flex-wrap:wrap}
  h1{font-size:15px;margin:0;letter-spacing:.5px}
  .sub{color:var(--dim);font-size:11px}
  #wrap{display:grid;grid-template-columns:1fr 340px;height:calc(100vh - 92px)}
  @media(max-width:900px){#wrap{grid-template-columns:1fr;height:auto}}
  #stage{position:relative;overflow:hidden}
  canvas{display:block;width:100%;height:100%}
  aside{border-left:1px solid var(--line);overflow-y:auto;padding:12px}
  @media(max-width:900px){aside{border-left:0;border-top:1px solid var(--line);height:50vh}}
  footer{padding:8px 16px;border-top:1px solid var(--line);display:flex;
         gap:12px;align-items:center}
  button{background:var(--panel);color:var(--fg);border:1px solid var(--line);
         border-radius:5px;padding:5px 12px;cursor:pointer;font:inherit}
  button:hover{border-color:var(--b)}
  button.on{border-color:var(--b);color:var(--b)}
  input[type=range]{flex:1;accent-color:var(--b)}
  .ev{border-left:2px solid var(--line);padding:5px 8px;margin-bottom:5px;
      background:var(--panel);border-radius:0 4px 4px 0}
  .ev.enter{border-left-color:var(--r)} .ev.appr{border-left-color:var(--o)}
  .ev.unc{border-left-color:var(--u)}  .ev.dwell{border-left-color:var(--b)}
  .ev .k{font-weight:700;font-size:11px;letter-spacing:.4px}
  .ev .m{color:var(--dim);font-size:11px}
  .lg{display:flex;gap:14px;flex-wrap:wrap;font-size:11px;color:var(--dim)}
  .rule{display:flex;justify-content:space-between;gap:8px;padding:3px 6px;
        border-radius:3px;font-size:11px;margin-bottom:2px;background:var(--panel)}
  .rule.off{opacity:.38}
  .rule b{font-weight:600}
  .chg{font-size:11px;padding:3px 6px;margin-bottom:2px;border-left:2px solid var(--b);
       background:var(--panel);color:var(--dim)}
  .chg.future{opacity:.35}
  .sw{display:inline-block;width:9px;height:9px;border-radius:2px;margin-right:4px}
  table{width:100%;border-collapse:collapse;font-size:11px}
  td{padding:2px 0} td:last-child{text-align:right;color:var(--b)}
  h2{font-size:11px;text-transform:uppercase;letter-spacing:1px;color:var(--dim);
     margin:14px 0 6px;font-weight:600}
  .inc{border-left:3px solid var(--r);background:var(--panel);border-radius:0 4px 4px 0;
       padding:6px 9px;margin-bottom:5px}
  .inc.s4{border-left-color:var(--o)} .inc.s3{border-left-color:var(--b)}
  .inc.s2{border-left-color:var(--dim)}
  .inc .big{font-size:17px;font-weight:700;line-height:1.2}
  .inc .m{color:var(--dim);font-size:11px}
  .saved{color:var(--g);font-weight:700}
  .mk{background:var(--panel);border-radius:5px;padding:9px;font-size:11px;line-height:1.5}
  .mk code{word-break:break-all;color:var(--b)}
  .mk .ok{color:var(--g);font-weight:700} .mk .bad{color:var(--r);font-weight:700}
  table.exp{font-size:10.5px} table.exp th{color:var(--dim);font-weight:600;text-align:right;padding:2px 4px;border-bottom:1px solid var(--line)}
  table.exp th:first-child{text-align:left}
  table.exp td{padding:2px 4px;text-align:right;color:var(--fg)} table.exp td:first-child{text-align:left;color:var(--dim)}
  table.exp tr.hl td{color:var(--fg);font-weight:700;background:rgba(88,166,255,.08)}
  table.exp .qt{color:var(--b)} table.exp .rt{color:var(--g)} table.exp .bf{color:var(--r)}
  .note{font-size:10px;color:var(--dim);margin-top:5px;line-height:1.5}
  .why b.bf{color:var(--r)} .why b.qt{color:var(--b)} .why b.rt{color:var(--g)}
  .concept{display:flex;justify-content:space-between;gap:8px;font-size:11px;padding:2px 6px;margin-bottom:2px;background:var(--panel);border-radius:3px}
  .concept b{color:var(--b);font-weight:600}
</style>
<header>
  <h1>safetrail</h1>
  <span class="sub" id="clock">00:00:00</span>
  <span class="lg">
    <span><i class="sw" style="background:var(--r)"></i>restricted</span>
    <span><i class="sw" style="background:var(--o)"></i>caution</span>
    <span><i class="sw" style="background:var(--g)"></i>safe</span>
    <span><i class="sw" style="background:var(--u)"></i>uncertain [GAP 1]</span>
  </span>
</header>
<div id="wrap">
  <div id="stage"><canvas id="c"></canvas></div>
  <aside>
    <h2>main experiment · brute force vs quadtree vs R-tree</h2><div id="experiment"></div>
    <h2>why the index works</h2><div id="why"></div>
    <h2>tracked tourist</h2><div id="track"><div class="m" style="color:var(--dim)">click a dot on the map to track a tourist</div></div>
    <h2>open incidents [GAP 5]</h2><div id="incidents"></div>
    <h2>counters</h2><table id="stats"></table>
    <h2>rules in force <span id="asof" style="color:var(--b)"></span></h2>
    <div id="rules"></div>
    <h2>zone change log [GAP 3]</h2><div id="changes"></div>
    <h2>persistent index [GAP 3]</h2><div id="versions"></div>
    <h2>course concepts demonstrated</h2><div id="concepts"></div>
    <h2>evidence log [GAP 9]</h2><div id="evidence"></div>
    <h2>event stream</h2><div id="events"></div>
  </aside>
</div>
<footer>
  <button id="play">pause</button>
  <input type="range" id="scrub" min="0" value="0">
  <button id="qt">index: off</button>
  <button id="acc">accuracy discs</button>
  <button id="disp">dispatch</button>
  <span style="color:var(--dim);font-size:10px">discs indicative, not to scale</span>
</footer>
<script>
const D = __DATA__;
const cv = document.getElementById('c'), cx = cv.getContext('2d');
// idxMode: 0 off · 1 brute force · 2 quadtree · 3 R-tree. The switch the demo asks
// for -- same data, same queries, three ways of organising the search.
let frame = 0, playing = true, idxMode = 0, showAcc = false, showDisp = false, selected = -1;
const IDX_NAMES = ['off', 'brute force', 'quadtree', 'r-tree'];
// The cells the active index would draw. Brute force has none -- it scans every
// zone -- which is the whole point of the comparison, so its overlay says so.
function activeBoxes() {
  return idxMode === 2 ? D.index_boxes : idxMode === 3 ? (D.rtree_boxes || []) : null;
}
const scrub = document.getElementById('scrub');
scrub.max = D.frames.length - 1;

// Bounds from the zones, padded. Equirectangular projection is fine for display
// at district scale -- we only need pixels, not distances.
let B = {n:-1e9, s:1e9, e:-1e9, w:1e9};
for (const z of D.zones) for (const p of z.ring) {
  B.n = Math.max(B.n, p[0]); B.s = Math.min(B.s, p[0]);
  B.e = Math.max(B.e, p[1]); B.w = Math.min(B.w, p[1]);
}
const pad = 0.012;
B.n += pad; B.s -= pad; B.e += pad; B.w -= pad;

let W = 0, H = 0, sc = 1, ox = 0, oy = 0;
function resize() {
  const r = cv.parentElement.getBoundingClientRect(), dpr = devicePixelRatio || 1;
  W = r.width; H = Math.max(r.height, 320);
  cv.width = W * dpr; cv.height = H * dpr;
  cx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const sx = W / (B.e - B.w), sy = H / (B.n - B.s);
  sc = Math.min(sx, sy) * 0.94;
  ox = (W - (B.e - B.w) * sc) / 2; oy = (H - (B.n - B.s) * sc) / 2;
}
const X = lon => ox + (lon - B.w) * sc;
const Y = lat => H - oy - (lat - B.s) * sc;

const ZC = {restricted:'#f85149', caution:'#d29922', safe:'#3fb950', advisory:'#8b949e'};

function draw() {
  cx.fillStyle = '#0e1116'; cx.fillRect(0, 0, W, H);

  if (idxMode === 1) {                 // brute force: no structure -- every zone tested
    cx.strokeStyle = 'rgba(248,81,73,.20)'; cx.lineWidth = 0.5;
    for (const z of D.zones) {          // one faint box per zone = the whole scan
      let n=1e9,s=-1e9,e=-1e9,w=1e9;
      for (const p of z.ring){n=Math.min(n,p[0]);s=Math.max(s,p[0]);w=Math.min(w,p[1]);e=Math.max(e,p[1]);}
      cx.strokeRect(X(w), Y(s), (e-w)*sc, (s-n)*sc);
    }
    cx.fillStyle = 'rgba(248,81,73,.95)'; cx.font = '12px ui-monospace';
    cx.fillText('brute force: no spatial index — all ' + D.zones.length +
                ' zones tested per query (O(n))', 14, 22);
  } else if (idxMode === 2 || idxMode === 3) {   // real node boxes, not an approximation
    const boxes = activeBoxes();
    cx.strokeStyle = idxMode === 3 ? 'rgba(63,185,80,.28)' : 'rgba(88,166,255,.20)';
    cx.lineWidth = 0.6;
    let drawn = 0;
    for (const b of boxes) {
      if (b[2] - b[0] > 20) continue;      // skip the root world box
      cx.strokeRect(X(b[1]), Y(b[2]), (b[3]-b[1])*sc, (b[2]-b[0])*sc);
      ++drawn;
    }
    cx.fillStyle = 'rgba(139,148,158,.95)'; cx.font = '12px ui-monospace';
    if (!drawn)
      cx.fillText('index has a single node at this zone count - run with --synthetic to see subdivision', 14, 22);
    else
      cx.fillText(IDX_NAMES[idxMode] + ': ' + drawn + ' node cells' +
                  (idxMode === 3 ? ' (envelopes overlap — it partitions items)'
                                 : ' (disjoint grid — it partitions space)'), 14, 22);
  }

  // Synthetic zones first, as faint outlines -- they exist to give the index a
  // realistic workload, so they should read as density rather than compete with
  // the authored zones for attention.
  cx.lineWidth = 0.5; cx.strokeStyle = 'rgba(139,148,158,.22)';
  for (const z of D.zones) {
    if (!z.syn) continue;
    cx.beginPath();
    z.ring.forEach((p, i) => i ? cx.lineTo(X(p[1]), Y(p[0])) : cx.moveTo(X(p[1]), Y(p[0])));
    cx.closePath(); cx.stroke();
  }
  // GAP 3 made visible: a zone out of force is drawn as a dashed ghost, not
  // hidden. An operator needs to see that a rule exists but is not active -- and
  // an investigator needs to see it appear and disappear as they scrub time.
  const now = D.frames[frame].t_ms;
  const inforce = z => now >= z.from && (z.to < 0 || now < z.to);
  for (const z of D.zones) {
    if (z.syn) continue;
    const col = ZC[z.kind] || '#8b949e';
    const on = inforce(z);
    cx.beginPath();
    z.ring.forEach((p, i) => i ? cx.lineTo(X(p[1]), Y(p[0])) : cx.moveTo(X(p[1]), Y(p[0])));
    cx.closePath();
    if (on) {
      cx.setLineDash([]);
      cx.fillStyle = col + '2a'; cx.fill();
      cx.strokeStyle = col + 'cc'; cx.lineWidth = z.kind === 'restricted' ? 1.8 : 1.1;
    } else {
      cx.setLineDash([4, 4]);
      cx.strokeStyle = col + '44'; cx.lineWidth = 1;
    }
    cx.stroke();
    cx.setLineDash([]);
  }

  const f = D.frames[frame];
  for (let i = 0; i < f.lat.length; i++) {
    const x = X(f.lon[i]), y = Y(f.lat[i]), st = f.state[i];
    // GAP 1 made visible. Two honest details: fixes worse than 150 m are the ones
    // the engine REJECTS as unusable, so drawing them would advertise data we throw
    // away; and a 35 m radius is sub-pixel at district zoom, so there is a minimum
    // render size. The ring is indicative, not to scale -- hence the footer note.
    if (showAcc && f.acc[i] <= 150) {
      const rpx = Math.max((f.acc[i] / 111320) * sc, 5);
      cx.beginPath(); cx.arc(x, y, rpx, 0, 6.284);
      cx.fillStyle = st === 1 ? 'rgba(188,140,255,.20)' : 'rgba(88,166,255,.10)';
      cx.fill();
      cx.strokeStyle = st === 1 ? 'rgba(188,140,255,.55)' : 'rgba(88,166,255,.28)';
      cx.lineWidth = 1; cx.stroke();
    }
    cx.beginPath(); cx.arc(x, y, st === 2 ? 4.2 : 3, 0, 6.284);
    cx.fillStyle = st === 2 ? '#f85149' : st === 1 ? '#bc8cff' : '#58a6ff';
    cx.fill();
    if (st === 2) { cx.strokeStyle = '#f8514966'; cx.lineWidth = 4; cx.stroke(); }
  }

  if (selected >= 0 && selected < f.lat.length) {
    cx.strokeStyle = 'rgba(255,255,255,.55)'; cx.lineWidth = 1.5;
    cx.beginPath();
    let started = false;
    for (let fi = 0; fi <= frame; fi++) {
      const ff = D.frames[fi];
      if (selected >= ff.lat.length) continue;
      const x = X(ff.lon[selected]), y = Y(ff.lat[selected]);
      started ? cx.lineTo(x, y) : cx.moveTo(x, y); started = true;
    }
    cx.stroke();
    const x = X(f.lon[selected]), y = Y(f.lat[selected]);
    cx.beginPath(); cx.arc(x, y, 8, 0, 6.284);
    cx.strokeStyle = '#fff'; cx.lineWidth = 2; cx.stroke();

    // The spatial prune, made visible: the query neighbourhood around this tourist
    // (white dashed) and the index cells it actually touches (amber). Every other
    // cell the tree skips -- that is the O(log n + k) story on screen. Reflects the
    // active index; in brute-force mode there are no cells to skip -- it scans all.
    const qr = 0.006, ql = f.lat[selected], qo = f.lon[selected];
    const qb = [ql - qr, qo - qr, ql + qr, qo + qr];
    const boxes = activeBoxes();
    let touched = 0;
    if (boxes) {
      cx.lineWidth = 0.9; cx.strokeStyle = 'rgba(210,153,34,.6)';
      for (const b of boxes) {
        if (b[2] - b[0] > 20) continue;                                // skip world root
        if (b[2] < qb[0] || b[0] > qb[2] || b[3] < qb[1] || b[1] > qb[3]) continue;
        cx.strokeRect(X(b[1]), Y(b[2]), (b[3]-b[1])*sc, (b[2]-b[0])*sc);
        ++touched;
      }
    }
    cx.setLineDash([5, 4]); cx.strokeStyle = 'rgba(255,255,255,.75)'; cx.lineWidth = 1.2;
    cx.strokeRect(X(qb[1]), Y(qb[2]), (qb[3]-qb[1])*sc, (qb[2]-qb[0])*sc);
    cx.setLineDash([]);
    cx.fillStyle = 'rgba(210,153,34,.9)'; cx.font = '11px ui-monospace';
    if (idxMode === 1)
      cx.fillText('brute force: all ' + D.zones.length + ' zones tested', X(qb[1]), Y(qb[2]) - 6);
    else if (boxes && touched)
      cx.fillText(touched + ' ' + IDX_NAMES[idxMode] + ' cells touched', X(qb[1]), Y(qb[2]) - 6);
  }

  if (showDisp) {
    cx.strokeStyle = 'rgba(63,185,80,.6)'; cx.lineWidth = 1.4;
    for (const l of (D.dispatch || [])) {
      cx.beginPath(); cx.moveTo(X(l[1]), Y(l[0])); cx.lineTo(X(l[3]), Y(l[2])); cx.stroke();
    }
    for (const r of (D.responders || [])) {
      const x = X(r[1]), y = Y(r[0]);
      cx.fillStyle = '#3fb950'; cx.fillRect(x - 3, y - 3, 6, 6);
      cx.strokeStyle = '#0e1116'; cx.lineWidth = 1; cx.strokeRect(x - 3, y - 3, 6, 6);
    }
  }

  const hh = n => String(n).padStart(2, '0');
  const s = Math.floor(f.t_ms / 1000);
  document.getElementById('clock').textContent =
    hh(Math.floor(s/3600)) + ':' + hh(Math.floor(s/60)%60) + ':' + hh(s%60);
}

function panel() {
  const t = D.frames[frame].t_ms;
  let ins = 0, unc = 0;
  for (const s of D.frames[frame].state) { if (s === 2) ins++; else if (s === 1) unc++; }
  const rows = [
    ['tourists', D.frames[frame].lat.length], ['inside a zone', ins],
    ['uncertain [GAP 1]', unc], ['zones', D.stats.zones],
    ['index', D.stats.index], ['candidates/query', D.stats.avg_candidates.toFixed(2)],
    ['pruning', D.stats.pruning.toFixed(0) + 'x'],
    ['flaps suppressed [GAP 8]', D.stats.flaps],
    ['alerts', D.stats.alerts], ['incidents [GAP 5]', D.stats.incidents],
    ['anomalies', D.stats.anomalies], ['responders dispatched', D.stats.dispatched],
    ['greedy travel', (D.stats.greedy_m | 0).toLocaleString() + ' m'],
    ['optimal travel [Phase 8]', (D.stats.optimal_m | 0).toLocaleString() + ' m'],
    ['saved by Hungarian', Math.max(0, (D.stats.greedy_m - D.stats.optimal_m) | 0).toLocaleString() + ' m'],
    ['index versions [GAP 3]', D.versions], ['node sharing', D.sharing.toFixed(1) + 'x'],
  ];
  document.getElementById('stats').innerHTML =
    rows.map(r => `<tr><td>${r[0]}</td><td>${r[1]}</td></tr>`).join('');

  // "What were the rules at 14:32" -- the question the persistent index exists to
  // answer. Rendered from the exported validity windows, which come straight from
  // the same Validity objects the evaluator consults each tick.
  const fmt = ms => { const s2 = Math.floor(ms/1000);
    return String(Math.floor(s2/3600)).padStart(2,'0') + ':' +
           String(Math.floor(s2/60)%60).padStart(2,'0'); };
  document.getElementById('asof').textContent = 'as of ' + fmt(t);
  // Rules in force. Authored zones only (synthetic padding is scale-test geometry,
  // not a hazard); repetitive families (Deep Water x18, ...) collapse to one row so
  // the panel reads like an operator console, not a dump.
  const authored = D.zones.filter(z => !z.syn);
  const fams = {};
  for (const z of authored) {
    const fam = z.name.replace(/\s*\d+$/, '');       // "Deep Water 5" -> "Deep Water"
    const on = t >= z.from && (z.to < 0 || t < z.to);
    if (!fams[fam]) fams[fam] = {name:fam, kind:z.kind, n:0, onN:0, win:
      (z.from === 0 && z.to < 0 ? 'always' : fmt(z.from)+'-'+(z.to<0?'end':fmt(z.to)))};
    fams[fam].n++; if (on) fams[fam].onN++;
  }
  const kr = {restricted:0, caution:1, advisory:2, safe:3};
  const rl = Object.values(fams).sort((a,b) =>
    (b.onN>0)-(a.onN>0) || (kr[a.kind]-kr[b.kind]) || a.name.localeCompare(b.name));
  document.getElementById('rules').innerHTML = rl.map(f => {
    const on = f.onN > 0;
    const nm = f.n > 1 ? `${f.name} <span class="m">×${f.n}</span>` : f.name;
    const st = f.n > 1 ? `${f.onN}/${f.n} in force`
                       : `${on?'IN FORCE':'not in force'} · ${f.win}`;
    const sw = `<i class="sw" style="background:${ZC[f.kind]||'#8b949e'}"></i>`;
    return `<div class="rule ${on?'':'off'}"><span>${sw}<b>${nm}</b></span><span>${st}</span></div>`;
  }).join('');

  // Change log: the bulk load at 00:00 is one line; the interesting entries are the
  // validity windows that open and close mid-run -- what "rewind to 14:32" queries.
  const CK = {0:'added', 1:'removed', 2:'validity changed'};
  const chs = (D.zone_changes || []);
  const bulk = chs.filter(c => c.at === 0).length;
  const later = chs.filter(c => c.at > 0).sort((a,b) => a.at - b.at);
  let clog = bulk ? `<div class="chg">@ 00:00 — ${bulk} zones loaded</div>` : '';
  clog += later.map(c => {
    const z = D.zones[c.z];
    return `<div class="chg ${c.at > t ? 'future' : ''}">@ ${fmt(c.at)} —
      ${z ? z.name : '?'} ${CK[c.k]||''}</div>`;
  }).join('');
  document.getElementById('changes').innerHTML = clog || '<div class="chg">no changes</div>';

  const KL = {0:['ENTER','enter'],1:['EXIT','exit'],2:['UNCERTAIN','unc'],
              3:['APPROACHING','appr'],4:['DWELL','dwell']};
  const vis = D.events.filter(e => e.t <= t && !(D.zones[e.z] && D.zones[e.z].syn))
                      .slice(-40).reverse();
  document.getElementById('events').innerHTML = vis.map(e => {
    const [lbl, cls] = KL[e.k] || ['?',''];
    const z = D.zones[e.z] ? D.zones[e.z].name : '?';
    const d = e.k === 3 ? `ETA ${e.eta|0}s` : `${Math.abs(e.d)|0}m, ±${e.a|0}m`;
    return `<div class="ev ${cls}"><div class="k">${lbl}</div>
      <div class="m">TID-${String(e.p).padStart(5,'0')} · ${z}</div>
      <div class="m">${d}</div></div>`;
  }).join('') || '<div class="m" style="color:var(--dim)">no events yet</div>';
}

function jdist(a, b, c, d) {
  const R = 6371008.8, rad = Math.PI / 180, dp = (c - a) * rad, dl = (d - b) * rad;
  const s = Math.sin(dp/2)**2 + Math.cos(a*rad)*Math.cos(c*rad)*Math.sin(dl/2)**2;
  return 2 * R * Math.asin(Math.sqrt(s));
}
function trackPanel() {
  const el = document.getElementById('track');
  const f = D.frames[frame];
  if (selected < 0 || selected >= f.lat.length) {
    el.innerHTML = '<div class="m" style="color:var(--dim)">click a dot on the map to track a tourist</div>';
    return;
  }
  const t = (D.tourists && D.tourists[selected]) || {did: '?', grp: '?'};
  const st = f.state[selected];
  const stName = st === 2 ? 'INSIDE zone' : st === 1 ? 'uncertain' : 'clear';
  const stCol = st === 2 ? 'var(--r)' : st === 1 ? 'var(--u)' : 'var(--g)';
  let spd = 0;
  if (frame > 0) {
    const p = D.frames[frame - 1], dt = (f.t_ms - p.t_ms) / 1000;
    if (dt > 0 && selected < p.lat.length)
      spd = jdist(f.lat[selected], f.lon[selected], p.lat[selected], p.lon[selected]) / dt;
  }
  el.innerHTML = `<table>
    <tr><td>id</td><td>${t.did}</td></tr>
    <tr><td>group</td><td>party-${t.grp}</td></tr>
    <tr><td>status</td><td style="color:${stCol}">${stName}</td></tr>
    <tr><td>speed</td><td>${spd.toFixed(1)} m/s</td></tr>
    <tr><td>accuracy</td><td>±${(f.acc[selected]||0).toFixed(0)} m</td></tr>
    <tr><td>position</td><td>${f.lat[selected].toFixed(4)}, ${f.lon[selected].toFixed(4)}</td></tr>
  </table>`;
}
cv.addEventListener('click', ev => {
  const r = cv.getBoundingClientRect(), px = ev.clientX - r.left, py = ev.clientY - r.top;
  const f = D.frames[frame];
  let best = -1, bd = 400;
  for (let i = 0; i < f.lat.length; i++) {
    const dx = X(f.lon[i]) - px, dy = Y(f.lat[i]) - py, d = dx*dx + dy*dy;
    if (d < bd) { bd = d; best = i; }
  }
  selected = best; render();
});
// ── GAP 5 headline: the biggest correlated incidents, one card each ──────────
function incidentFeed() {
  const el = document.getElementById('incidents');
  const inc = D.incidents_top || [];
  if (!inc.length) {
    el.innerHTML = '<div class="m" style="color:var(--dim)">no multi-person incidents</div>';
    return;
  }
  el.innerHTML = inc.map(i => {
    const cls = i.sev >= 5 ? '' : 's' + i.sev;
    return `<div class="inc ${cls}">
      <div class="big">${i.people} people <span class="m">· 1 card</span></div>
      <div class="m">${i.zone} · severity ${i.sev}</div>
      <div class="m">${i.alerts.toLocaleString()} alerts correlated into this incident</div>
    </div>`;
  }).join('');
}

// ── GAP 9: verify a Merkle inclusion proof in the browser (SHA-256 + RFC 6962) ─
// A self-contained SHA-256 so the proof is checked with no network and no chain,
// exactly as a responder's cached-root verification would. The recomputed root is
// compared to the exported root, so the ✓/✗ is real, not decorative.
const K256=[0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2];
function sha256Bytes(msg){
  const rr=(x,n)=>(x>>>n)|(x<<(32-n));
  let h=[0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19];
  const l=msg.length, bl=((l+8)>>6)+1, m=new Uint8Array(bl*64);
  m.set(msg); m[l]=0x80;
  const bits=l*8; const dv=new DataView(m.buffer);
  dv.setUint32(m.length-4, bits>>>0); dv.setUint32(m.length-8, Math.floor(bits/0x100000000));
  const w=new Uint32Array(64);
  for(let i=0;i<m.length;i+=64){
    for(let j=0;j<16;j++) w[j]=dv.getUint32(i+j*4);
    for(let j=16;j<64;j++){
      const s0=rr(w[j-15],7)^rr(w[j-15],18)^(w[j-15]>>>3);
      const s1=rr(w[j-2],17)^rr(w[j-2],19)^(w[j-2]>>>10);
      w[j]=(w[j-16]+s0+w[j-7]+s1)|0;
    }
    let [a,b,c,d,e,f,g,hh]=h;
    for(let j=0;j<64;j++){
      const S1=rr(e,6)^rr(e,11)^rr(e,25), ch=(e&f)^(~e&g);
      const t1=(hh+S1+ch+K256[j]+w[j])|0;
      const S0=rr(a,2)^rr(a,13)^rr(a,22), mj=(a&b)^(a&c)^(b&c);
      const t2=(S0+mj)|0;
      hh=g;g=f;f=e;e=(d+t1)|0;d=c;c=b;b=a;a=(t1+t2)|0;
    }
    h[0]=(h[0]+a)|0;h[1]=(h[1]+b)|0;h[2]=(h[2]+c)|0;h[3]=(h[3]+d)|0;
    h[4]=(h[4]+e)|0;h[5]=(h[5]+f)|0;h[6]=(h[6]+g)|0;h[7]=(h[7]+hh)|0;
  }
  const out=new Uint8Array(32), o=new DataView(out.buffer);
  for(let i=0;i<8;i++) o.setUint32(i*4, h[i]>>>0);
  return out;
}
const hex2b=h=>{const a=new Uint8Array(h.length/2);for(let i=0;i<a.length;i++)a[i]=parseInt(h.substr(i*2,2),16);return a;};
const b2hex=b=>[...b].map(x=>x.toString(16).padStart(2,'0')).join('');
function leafHash(str){const e=new TextEncoder().encode(str);const b=new Uint8Array(1+e.length);b[0]=0;b.set(e,1);return sha256Bytes(b);}
function nodeHash(l,r){const b=new Uint8Array(65);b[0]=1;b.set(l,1);b.set(r,33);return sha256Bytes(b);}
function verifyMerkle(m){
  let fn=m.index, sn=m.size-1, h=leafHash(m.entry);
  for(const p of m.path.map(hex2b)){
    if(sn===0) return {ok:false, root:b2hex(h)};
    if((fn&1)||fn===sn){ h=nodeHash(p,h); if(!(fn&1)) while(!(fn&1)){fn=Math.floor(fn/2);sn=Math.floor(sn/2);} }
    else { h=nodeHash(h,p); }
    fn=Math.floor(fn/2); sn=Math.floor(sn/2);
  }
  const root=b2hex(h);
  return {ok: sn===0 && root===m.root, root};
}
function evidencePanel(){
  const el=document.getElementById('evidence'), m=D.merkle;
  if(!m||!m.size){el.innerHTML='<div class="m" style="color:var(--dim)">no evidence log</div>';return;}
  el.innerHTML=`<div class="mk">
    <div>${m.size.toLocaleString()} events committed to a Merkle root:</div>
    <code>${m.root.slice(0,40)}…</code>
    <div style="margin-top:6px">Inclusion proof for event #${m.index} (<code>${m.entry}</code>) — ${m.path.length} hashes.</div>
    <div id="mkres" style="margin:6px 0"></div>
    <button id="mkbtn">verify offline</button>
  </div>`;
  document.getElementById('mkbtn').onclick=()=>{
    const r=verifyMerkle(m);
    document.getElementById('mkres').innerHTML = r.ok
      ? '<span class="ok">✓ verified</span> — root recomputed from leaf + proof, no network, no chain'
      : '<span class="bad">✗ mismatch</span> — got '+r.root.slice(0,16)+'…';
  };
}

// ── The main experiment, as a panel: brute force vs quadtree vs R-tree ───────
// Latency/speedup/candidates from bench/results/index_scaling.csv (the source
// docs/RESULTS.md uses); the mismatch count is measured live over this run's
// zones. Nothing here is a hand-typed constant.
function experimentPanel() {
  const el = document.getElementById('experiment');
  const rows = D.experiment || [];
  const eq = D.equivalence || {};
  if (!rows.length) { el.innerHTML = '<div class="m" style="color:var(--dim)">run <code>make bench</code> to populate this panel</div>'; return; }
  let body = '';
  for (const r of rows) {
    const hl = r.zones >= 100000 ? ' class="hl"' : '';
    body += `<tr${hl}><td>${(+r.zones).toLocaleString()}</td>` +
      `<td class="bf">${(+r.brute).toFixed(1)}</td>` +
      `<td class="qt">${(+r.quad).toFixed(2)}</td>` +
      `<td class="rt">${(+r.rtree).toFixed(2)}</td>` +
      `<td class="qt">${(+r.qgain).toFixed(0)}×</td>` +
      `<td class="rt">${(+r.rgain).toFixed(0)}×</td>` +
      `<td>${(+r.cand).toFixed(2)}</td></tr>`;
  }
  const ok = (eq.mismatches | 0) === 0;
  el.innerHTML = `<table class="exp">
    <tr><th>zones</th><th>brute µs</th><th>quad µs</th><th>R-tree µs</th><th>QT×</th><th>RT×</th><th>cand</th></tr>
    ${body}</table>
    <div class="note">Latency = median of 7, µs/query, 450 m box, 2000 probes/row —
      from <code>bench/results/index_scaling.csv</code> (the source
      <code>RESULTS.md</code> uses). Speedups are vs our own brute force. The
      <b>candidate count is identical across all three</b>: they return the same
      true positives, so the ceiling is output size <b>k</b>, exactly as
      O(log n + k) predicts.</div>
    <div class="note" style="margin-top:6px">
      <b class="${ok ? '' : 'bad'}" style="color:${ok ? 'var(--g)' : 'var(--r)'};font-weight:700">
      ${ok ? '✓' : '✗'} correctness:</b> ${(eq.queries || 0).toLocaleString()} random
      queries over ${(eq.zones || 0).toLocaleString()} zones, just measured live —
      <b style="color:${ok ? 'var(--g)' : 'var(--r)'}">${eq.mismatches | 0} mismatches</b>
      between quadtree/R-tree and the brute-force oracle.</div>`;
}

// ── Why the index works (item 10): the three strategies in one line each ──────
function whyPanel() {
  document.getElementById('why').innerHTML = `<div class="why note" style="font-size:11px">
    <div><b class="bf">brute force</b> tests <i>every</i> zone against the query — O(n).</div>
    <div><b class="qt">quadtree</b> descends only the space quadrants the query can
      overlap and skips the rest — O(log n + k).</div>
    <div><b class="rt">R-tree</b> walks only the bounding-box envelopes that intersect
      the query; envelopes overlap, so it may enter a few branches — O(log n + k).</div>
    <div style="margin-top:5px">All three return the same answer; the tree just avoids
      looking where the answer cannot be. Toggle <b>index:</b> below and select a dot
      to see the cells each one touches.</div></div>`;
}

// ── Course concepts demonstrated (item 13): five core structures -> concepts ──
function conceptsPanel() {
  const rows = [
    ['Brute force', 'baseline · complexity analysis (O(n))'],
    ['Quadtree', 'trees · recursion · spatial indexing'],
    ['R-tree', 'balanced-by-construction · bulk loading · indexing'],
    ['Interval tree', 'balanced (AVL) trees · augmented BST'],
    ['Persistent quadtree', 'persistence · structural sharing'],
  ];
  document.getElementById('concepts').innerHTML =
    rows.map(r => `<div class="concept"><b>${r[0]}</b><span>${r[1]}</span></div>`).join('') +
    `<div class="note">Full mapping: docs/COURSE_MAPPING.md.</div>`;
}

// ── GAP 3 made visible: path copying across consecutive versions ─────────────
// Each version is a mini-map of the persistent quadtree's node cells. Cells the
// version SHARES with the one before it (a refcount, no copy) are drawn dim; the
// cells it freshly allocated -- the single root-to-leaf path a mutation copies --
// are highlighted. So a geometry change lights up one branch and shares the rest,
// and a validity-only change lights up nothing (it shares the whole tree).
function versionsPanel() {
  const el = document.getElementById('versions');
  const vs = D.versions_viz || [];
  if (!vs.length) { el.innerHTML = '<div class="m" style="color:var(--dim)">no version history</div>'; return; }
  const w = 150, h = 116, span = 18;   // span: drop the world-root box
  const mx = v => 4 + (v - B.w) / (B.e - B.w) * (w - 8);
  const my = v => h - 4 - (v - B.s) / (B.n - B.s) * (h - 8);
  const fmt = ms => { const s = Math.floor(ms/1000);
    return String(Math.floor(s/3600)).padStart(2,'0')+':'+String(Math.floor(s/60)%60).padStart(2,'0'); };
  // Item 12: demonstrate the persistent index as "SAME query, different historical
  // version" -- pick an authored zone that switches on mid-run and show the same
  // question returning a different answer at two times, because each is answered by
  // the version in force THEN, not by today's rules.
  let example = '';
  const midZone = (D.zones || []).filter(z => !z.syn && z.from > 0).sort((a,b)=>a.from-b.from)[0];
  if (midZone) {
    const before = Math.max(0, midZone.from - 600000);   // 10 min before it switches on
    example = `<div class="note" style="margin-bottom:6px;border-left:2px solid var(--b);padding-left:6px">
      <b style="color:var(--fg)">Same query, two versions.</b> "Is <b>${midZone.name}</b>
      a zone in force here?" — asked at <b>${fmt(before)}</b> → <span style="color:var(--g)">no</span>;
      asked at <b>${fmt(midZone.from)}</b> → <span style="color:var(--r)">yes</span>.
      Identical query; the persistent index answers each from the version that was
      current <i>then</i> (transaction time = valid time), not from today's rules.</div>`;
  }
  let html = example +
    `<div class="m" style="color:var(--dim);margin-bottom:6px">` +
    `${D.versions.toLocaleString()} versions · ${D.sharing.toFixed(1)}× structural sharing. ` +
    `Highlighted = freshly copied this version; dim = shared from the previous one.</div>` +
    `<div style="display:flex;gap:6px;flex-wrap:wrap">`;
  for (const v of vs) {
    let cells = '';
    for (const nd of v.nodes) {
      const r = nd.r; if (!r || (r[2] - r[0]) > span) continue;   // skip world root
      const x = mx(r[1]), y = my(r[2]), ww = (r[3]-r[1])/(B.e-B.w)*(w-8), hh = (r[2]-r[0])/(B.n-B.s)*(h-8);
      const col = nd.sh ? 'rgba(139,148,158,.30)' : 'var(--b)';
      const sw = nd.sh ? 0.5 : 1.1;
      cells += `<rect x="${x.toFixed(1)}" y="${y.toFixed(1)}" width="${Math.max(ww,0.5).toFixed(1)}" height="${Math.max(hh,0.5).toFixed(1)}" fill="none" stroke="${col}" stroke-width="${sw}"/>`;
    }
    const note = v.new === 0 ? '<span style="color:var(--g)">+0 — whole tree shared</span>'
                             : `<span style="color:var(--b)">+${v.new} new</span> / ${v.shared} shared`;
    html += `<div style="background:var(--panel);border:1px solid var(--line);border-radius:5px;padding:4px">
      <svg width="${w}" height="${h}" style="display:block">
        <rect x="0" y="0" width="${w}" height="${h}" fill="#0e1116"/>${cells}</svg>
      <div class="m" style="font-size:10px;margin-top:3px">v${v.version} @ ${fmt(v.at)}<br>${note}</div>
    </div>`;
  }
  html += '</div>';
  el.innerHTML = html;
}

function render() { draw(); panel(); trackPanel(); scrub.value = frame; }
document.getElementById('play').onclick = e => {
  playing = !playing; e.target.textContent = playing ? 'pause' : 'play';
};
document.getElementById('qt').onclick = e => {
  idxMode = (idxMode + 1) % 4;                     // off -> brute -> quadtree -> r-tree
  e.target.textContent = 'index: ' + IDX_NAMES[idxMode];
  e.target.classList.toggle('on', idxMode !== 0);
  render();
};
document.getElementById('acc').onclick = e => { showAcc = !showAcc; e.target.classList.toggle('on'); render(); };
document.getElementById('disp').onclick = e => { showDisp = !showDisp; e.target.classList.toggle('on'); render(); };
scrub.oninput = () => { frame = +scrub.value; playing = false;
  document.getElementById('play').textContent = 'play'; render(); };
addEventListener('resize', () => { resize(); render(); });
resize(); render();
incidentFeed(); evidencePanel(); versionsPanel();   // end-of-run summaries; render once
experimentPanel(); whyPanel(); conceptsPanel();     // the main experiment + concept map
setInterval(() => { if (playing) { frame = (frame + 1) % D.frames.length; render(); } }, 90);
</script>
)HTML";

static void put_f(std::string& s, double v, int prec = 6) {
  char b[40]; snprintf(b, sizeof b, "%.*f", prec, v); s += b;
}

bool TraceRecorder::write_html(const sim::Simulator& s, const std::string& path) const {
  std::string d = "{\"zones\":[";
  bool first = true;
  for (index::ZoneId id : s.zones().all_ids()) {
    const auto* z = s.zones().get(id);
    if (!z) continue;
    if (!first) d += ",";
    first = false;
    const char* k = z->kind == fence::ZoneKind::Restricted ? "restricted"
                  : z->kind == fence::ZoneKind::Caution ? "caution"
                  : z->kind == fence::ZoneKind::Safe ? "safe" : "advisory";
    d += "{\"name\":\"";
    for (char c : z->name) { if (c != '"' && c != '\\') d += c; }
    d += "\",\"kind\":\""; d += k;
    d += z->synthetic ? "\",\"syn\":1" : "\",\"syn\":0";
    d += ",\"sev\":" + std::to_string(int(z->severity));
    d += ",\"from\":" + std::to_string(z->validity.from);
    d += ",\"to\":" + std::to_string(z->validity.to == kForever ? -1 : z->validity.to);
    d += ",\"ring\":[";
    const auto& r = z->shape.outer();
    for (size_t i = 0; i < r.size(); ++i) {
      if (i) d += ",";
      d += "["; put_f(d, r[i].lat); d += ","; put_f(d, r[i].lon); d += "]";
    }
    d += "]}";
  }

  d += "],\"index_boxes\":[";
  {
    std::vector<geo::Bbox> boxes;
    if (auto* qt = dynamic_cast<const index::Quadtree*>(&s.index()))
      qt->collect_node_boxes(boxes);
    for (size_t i = 0; i < boxes.size(); ++i) {
      if (i) d += ",";
      d += "["; put_f(d, boxes[i].min_lat); d += ","; put_f(d, boxes[i].min_lon);
      d += ","; put_f(d, boxes[i].max_lat); d += ","; put_f(d, boxes[i].max_lon); d += "]";
    }
  }

  // R-tree node envelopes over the SAME zone set, so the overlay can switch
  // between the two indexes and show the structural contrast: the quadtree's
  // disjoint grid vs the R-tree's tight, overlapping item envelopes.
  d += "],\"rtree_boxes\":[";
  {
    std::vector<std::pair<index::ZoneId, geo::Bbox>> items;
    for (index::ZoneId id : s.zones().all_ids()) {
      const auto* z = s.zones().get(id);
      if (z) items.emplace_back(id, z->shape.bbox());
    }
    index::RTree rt;
    rt.build(items);
    std::vector<geo::Bbox> boxes;
    rt.collect_node_boxes(boxes);
    for (size_t i = 0; i < boxes.size(); ++i) {
      if (i) d += ",";
      d += "["; put_f(d, boxes[i].min_lat); d += ","; put_f(d, boxes[i].min_lon);
      d += ","; put_f(d, boxes[i].max_lat); d += ","; put_f(d, boxes[i].max_lon); d += "]";
    }
  }

  d += "],\"frames\":[";
  for (size_t fi = 0; fi < frames_.size(); ++fi) {
    const Frame& f = frames_[fi];
    if (fi) d += ",";
    d += "{\"t_ms\":" + std::to_string(f.t_ms) + ",\"lat\":[";
    for (size_t i = 0; i < f.lat.size(); ++i) { if (i) d += ","; put_f(d, f.lat[i], 5); }
    d += "],\"lon\":[";
    for (size_t i = 0; i < f.lon.size(); ++i) { if (i) d += ","; put_f(d, f.lon[i], 5); }
    d += "],\"acc\":[";
    for (size_t i = 0; i < f.acc.size(); ++i) { if (i) d += ","; put_f(d, f.acc[i], 1); }
    d += "],\"state\":[";
    for (size_t i = 0; i < f.state.size(); ++i)
      { if (i) d += ","; d += std::to_string(int(f.state[i])); }
    d += "]}";
  }

  d += "],\"events\":[";
  size_t n = 0;
  for (const auto& e : s.events()) {
    if (n >= opt_.max_events) break;
    if (n) d += ",";
    ++n;
    d += "{\"t\":" + std::to_string(e.t_ms) +
         ",\"k\":" + std::to_string(int(e.kind)) +
         ",\"p\":" + std::to_string(e.tourist) +
         ",\"z\":" + std::to_string(e.zone) + ",\"d\":";
    put_f(d, e.depth_m, 1);
    d += ",\"a\":"; put_f(d, e.accuracy_m, 1);
    d += ",\"eta\":"; put_f(d, e.eta_s, 1);
    d += "}";
  }

  const auto ist = s.index().stats();
  const auto sum = s.summary();
  const auto c = s.counters();
  d += "],\"zone_changes\":[";
  {
    // Straight from VersionedIndex -- the same changelog an incident investigation
    // would query, not a re-derivation.
    const auto changes = s.versioned().changes_between(0, INT64_MAX);
    bool f2 = true;
    for (const auto& ch : changes) {
      const auto* z = s.zones().get(ch.zone);
      if (!z || z->synthetic) continue;
      if (!f2) d += ",";
      f2 = false;
      d += "{\"v\":" + std::to_string(ch.version) + ",\"at\":" + std::to_string(ch.at) +
           ",\"z\":" + std::to_string(ch.zone) + ",\"k\":" + std::to_string(int(ch.kind)) + "}";
    }
  }
  d += "],\"versions\":" + std::to_string(s.versioned().version_count());
  d += ",\"sharing\":";
  put_f(d, s.versioned().share_stats().sharing_ratio(), 2);

  // Persistent-index visualization: a small window of consecutive versions, each
  // node flagged shared (reused from the previous version by refcount) or new
  // (freshly allocated on this version's copied path). The viewer draws the
  // shared nodes dimmed and the new ones highlighted -- path copying made visible.
  d += ",\"versions_viz\":[";
  {
    const auto vv = s.versioned().viz_versions(4);
    for (size_t vi = 0; vi < vv.size(); ++vi) {
      if (vi) d += ",";
      d += "{\"version\":" + std::to_string(vv[vi].version);
      d += ",\"at\":" + std::to_string(vv[vi].at);
      d += ",\"new\":" + std::to_string(vv[vi].new_nodes);
      d += ",\"shared\":" + std::to_string(vv[vi].shared_nodes);
      d += ",\"nodes\":[";
      for (size_t ni = 0; ni < vv[vi].nodes.size(); ++ni) {
        const auto& nd = vv[vi].nodes[ni];
        if (ni) d += ",";
        d += "{\"d\":" + std::to_string(int(nd.depth)) +
             ",\"lf\":" + std::to_string(nd.leaf ? 1 : 0) +
             ",\"sh\":" + std::to_string(nd.shared ? 1 : 0) +
             ",\"it\":" + std::to_string(nd.items) + ",\"r\":[";
        put_f(d, nd.region.min_lat); d += ","; put_f(d, nd.region.min_lon); d += ",";
        put_f(d, nd.region.max_lat); d += ","; put_f(d, nd.region.max_lon);
        d += "]}";
      }
      d += "]}";
    }
  }
  d += "]";

  // ── MAIN EXPERIMENT (item for the professor): brute force vs quadtree vs R-tree
  // Latency/speedup/candidates come straight from bench/results/index_scaling.csv
  // -- the SAME file docs/RESULTS.md is generated from, so the dashboard cannot
  // drift from the reported numbers. If the CSV is absent the panel says to run
  // `make bench`. Nothing here is typed by hand.
  d += ",\"experiment\":[";
  {
    std::ifstream csv("bench/results/index_scaling.csv");
    std::string line;
    bool header = true, firstrow = true;
    while (std::getline(csv, line)) {
      if (header) { header = false; continue; }        // skip column names
      if (line.empty()) continue;
      // zones,brute_us,quad_us,rtree_us,quad_speedup,rtree_speedup,candidates,...
      std::vector<std::string> col;
      size_t p = 0;
      while (p <= line.size()) {
        size_t c = line.find(',', p);
        if (c == std::string::npos) c = line.size();
        col.push_back(line.substr(p, c - p));
        p = c + 1;
      }
      if (col.size() < 7) continue;
      if (!firstrow) d += ",";
      firstrow = false;
      d += "{\"zones\":" + col[0] + ",\"brute\":" + col[1] + ",\"quad\":" + col[2] +
           ",\"rtree\":" + col[3] + ",\"qgain\":" + col[4] + ",\"rgain\":" + col[5] +
           ",\"cand\":" + col[6] + "}";
    }
  }
  d += "]";

  // Correctness, measured NOW over this run's own zones: build all three indexes
  // and compare their results on random 450 m queries. mismatches must be 0 --
  // this is the equivalence gate, run live so the panel's number is real, not a
  // quoted constant.
  {
    std::vector<std::pair<index::ZoneId, geo::Bbox>> items;
    geo::Bbox bounds = geo::Bbox::empty();
    for (index::ZoneId id : s.zones().all_ids()) {
      const auto* z = s.zones().get(id);
      if (!z) continue;
      items.emplace_back(id, z->shape.bbox());
      bounds.expand(z->shape.bbox());
    }
    index::BruteForceIndex bf; bf.build(items);
    index::Quadtree qt;        qt.build(items);
    index::RTree rt;           rt.build(items);
    uint64_t rng = 0x9e3779b97f4a7c15ULL;
    auto nextf = [&]() {                              // deterministic xorshift in [0,1)
      rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
      return double(rng >> 11) / double(1ULL << 53);
    };
    const int Q = 3000;
    long long mism = 0, cand = 0;
    std::vector<index::ZoneId> a, b, c;
    for (int i = 0; i < Q; ++i) {
      geo::LatLon ctr{bounds.min_lat + nextf() * (bounds.max_lat - bounds.min_lat),
                      bounds.min_lon + nextf() * (bounds.max_lon - bounds.min_lon)};
      const geo::Bbox q = geo::Bbox::around(ctr, 450.0);
      a.clear(); b.clear(); c.clear();
      bf.query(q, a); qt.query(q, b); rt.query(q, c);
      std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end()); std::sort(c.begin(), c.end());
      if (a != b) ++mism;
      if (a != c) ++mism;
      cand += (long long)a.size();
    }
    d += ",\"equivalence\":{\"queries\":" + std::to_string(Q) +
         ",\"mismatches\":" + std::to_string(mism) +
         ",\"avg_candidates\":"; put_f(d, Q ? double(cand) / Q : 0.0, 2);
    d += ",\"zones\":" + std::to_string(items.size()) + "}";
  }
  d += ",\"stats\":{\"zones\":" + std::to_string(s.zones().size()) +
       ",\"index\":\"" + s.index().name() + "\"" +
       ",\"avg_candidates\":"; put_f(d, ist.avg_candidates(), 2);
  d += ",\"pruning\":";
  put_f(d, ist.avg_candidates() > 0 ? double(s.zones().size()) / ist.avg_candidates() : 0.0, 2);
  d += ",\"flaps\":" + std::to_string(c.flaps_suppressed) +
       ",\"alerts\":" + std::to_string(sum.alerts) +
       ",\"anomalies\":" + std::to_string(sum.anomalies) +
       ",\"dispatched\":" + std::to_string(sum.dispatched) +
       ",\"greedy_m\":"; put_f(d, sum.greedy_response_m, 0);
  d += ",\"optimal_m\":"; put_f(d, sum.optimal_response_m, 0);
  d += ",\"incidents\":" + std::to_string(s.correlator().stats().incidents_opened) + "}";

  // The GAP 5 headline made visible: the largest correlated incidents, each ONE
  // card standing in for many alerts. Labelled by the nearest authored hazard.
  d += ",\"incidents_top\":[";
  {
    auto inc = s.correlator().open_incidents();
    std::sort(inc.begin(), inc.end(),
              [](const alert::Incident* a, const alert::Incident* b) {
                if (a->people() != b->people()) return a->people() > b->people();
                return a->max_severity > b->max_severity;
              });
    size_t shown = 0;
    for (const auto* i : inc) {
      if (i->people() < 2) continue;          // a lone alert is not an "incident card"
      if (shown >= 8) break;
      if (shown) d += ",";
      ++shown;
      const char* zname = "incident";
      double best = 1e18;
      for (index::ZoneId zid : s.zones().all_ids()) {
        const auto* z = s.zones().get(zid);
        if (!z || z->synthetic) continue;
        const double dd = geo::distance_m(i->centroid, z->shape.centroid());
        if (dd < best) { best = dd; zname = z->name.c_str(); }
      }
      d += "{\"people\":" + std::to_string(i->people()) +
           ",\"sev\":" + std::to_string(int(i->max_severity)) +
           ",\"alerts\":" + std::to_string(i->alerts.size());
      d += ",\"lat\":"; put_f(d, i->centroid.lat);
      d += ",\"lon\":"; put_f(d, i->centroid.lon);
      d += ",\"zone\":\"";
      for (char ch : std::string(zname)) if (ch != '"' && ch != '\\') d += ch;
      d += "\"}";
    }
  }

  d += "],\"responders\":[";
  { const auto& rp = s.responders();
    for (size_t i = 0; i < rp.size(); ++i) {
      if (i) d += ",";
      d += "["; put_f(d, rp[i].pos.lat); d += ","; put_f(d, rp[i].pos.lon); d += "]";
    } }
  d += "],\"dispatch\":[";
  { const auto& dl = s.dispatch_lines();
    for (size_t i = 0; i < dl.size(); ++i) {
      if (i) d += ",";
      d += "["; put_f(d, dl[i][0]); d += ","; put_f(d, dl[i][1]);
      d += ","; put_f(d, dl[i][2]); d += ","; put_f(d, dl[i][3]); d += "]";
    } }
  d += "],\"tourists\":[";
  { const auto& ts = s.tourists();
    for (size_t i = 0; i < ts.size(); ++i) {
      if (i) d += ",";
      d += "{\"did\":\"";
      for (char ch : ts[i].digital_id) if (ch != '"' && ch != '\\') d += ch;
      d += "\",\"grp\":" + std::to_string(ts[i].group) + "}";
    } }
  d += "]";

  // GAP 9, made verifiable in the browser. Commit the event stream to a Merkle
  // log and export ONE inclusion proof: the leaf's record, the sibling path, the
  // index/size, and the published root. The viewer recomputes the root from just
  // the leaf and the path (SHA-256 + RFC 6962, in JS) and shows it matches — the
  // tamper-evidence property Ethereum was used for, with no chain and no network.
  {
    evidence::MerkleLog log;
    for (const auto& e : s.events()) {
      char rec[128];
      snprintf(rec, sizeof rec, "%lld|%d|T%u|Z%u",
               (long long)e.t_ms, int(e.kind), unsigned(e.tourist), unsigned(e.zone));
      log.append(std::string(rec));
    }
    d += ",\"merkle\":{\"size\":" + std::to_string(log.size());
    d += ",\"root\":\"" + (log.size() ? evidence::to_hex(log.root()) : std::string()) + "\"";
    if (log.size() > 0) {
      const uint64_t idx = log.size() / 2;
      auto proof = log.prove(idx);
      std::vector<uint8_t> entry; log.get(idx, entry);
      d += ",\"index\":" + std::to_string(idx);
      d += ",\"entry\":\"";
      for (uint8_t ch : entry) if (ch != '"' && ch != '\\') d += char(ch);
      d += "\",\"path\":[";
      for (size_t i = 0; i < proof.path.size(); ++i) {
        if (i) d += ",";
        d += "\"" + evidence::to_hex(proof.path[i]) + "\"";
      }
      d += "]";
    }
    d += "}";
  }
  d += "}";

  std::string html = kShell;
  const size_t dp = html.find("__DATA__");
  if (dp != std::string::npos) html.replace(dp, 8, d);
  const size_t tp = html.find("__TITLE__");
  if (tp != std::string::npos) html.replace(tp, 9, opt_.title);

  std::ofstream f(path);
  if (!f) return false;
  f << html;
  return true;
}

}  // namespace safetrail::viz
