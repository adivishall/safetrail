#include "safetrail/viz/html_export.hpp"
#include "safetrail/index/quadtree.hpp"
#include <cstdio>
#include <climits>
#include <fstream>

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
    <h2>counters</h2><table id="stats"></table>
    <h2>rules in force <span id="asof" style="color:var(--b)"></span></h2>
    <div id="rules"></div>
    <h2>zone change log [GAP 3]</h2><div id="changes"></div>
    <h2>event stream</h2><div id="events"></div>
  </aside>
</div>
<footer>
  <button id="play">pause</button>
  <input type="range" id="scrub" min="0" value="0">
  <button id="qt">index overlay</button>
  <button id="acc">accuracy discs</button>
  <button id="disp">dispatch</button>
  <span style="color:var(--dim);font-size:10px">discs indicative, not to scale</span>
</footer>
<script>
const D = __DATA__;
const cv = document.getElementById('c'), cx = cv.getContext('2d');
let frame = 0, playing = true, showQT = false, showAcc = false, showDisp = false;
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

  if (showQT) {                       // real index node boxes, not an approximation
    cx.strokeStyle = 'rgba(88,166,255,.20)'; cx.lineWidth = 0.6;
    let drawn = 0;
    for (const b of D.index_boxes) {
      if (b[2] - b[0] > 20) continue;      // skip the root world box
      cx.strokeRect(X(b[1]), Y(b[2]), (b[3]-b[1])*sc, (b[2]-b[0])*sc);
      ++drawn;
    }
    if (!drawn) {                          // be explicit rather than silently blank
      cx.fillStyle = 'rgba(139,148,158,.9)'; cx.font = '12px ui-monospace';
      cx.fillText('index has a single node at this zone count - run with --synthetic to see subdivision', 14, 22);
    }
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
  const authored = D.zones.filter(z => !z.syn);
  document.getElementById('rules').innerHTML = authored.map(z => {
    const on = t >= z.from && (z.to < 0 || t < z.to);
    const win = z.from === 0 && z.to < 0 ? 'always'
              : fmt(z.from) + '-' + (z.to < 0 ? 'end' : fmt(z.to));
    return `<div class="rule ${on?'':'off'}"><b>${z.name}</b>
      <span>${on?'IN FORCE':'not in force'} · ${win}</span></div>`;
  }).join('');

  const CK = {0:'added', 1:'removed', 2:'validity changed'};
  document.getElementById('changes').innerHTML = (D.zone_changes||[]).map(c => {
    const z = D.zones[c.z];
    return `<div class="chg ${c.at > t ? 'future' : ''}">v${c.v} @ ${fmt(c.at)} —
      ${z ? z.name : '?'} ${CK[c.k]||''}</div>`;
  }).join('') || '<div class="chg">no changes</div>';

  const KL = {0:['ENTER','enter'],1:['EXIT','exit'],2:['UNCERTAIN','unc'],
              3:['APPROACHING','appr'],4:['DWELL','dwell']};
  const vis = D.events.filter(e => e.t <= t).slice(-40).reverse();
  document.getElementById('events').innerHTML = vis.map(e => {
    const [lbl, cls] = KL[e.k] || ['?',''];
    const z = D.zones[e.z] ? D.zones[e.z].name : '?';
    const d = e.k === 3 ? `ETA ${e.eta|0}s` : `${Math.abs(e.d)|0}m, ±${e.a|0}m`;
    return `<div class="ev ${cls}"><div class="k">${lbl}</div>
      <div class="m">TID-${String(e.p).padStart(5,'0')} · ${z}</div>
      <div class="m">${d}</div></div>`;
  }).join('') || '<div class="m" style="color:var(--dim)">no events yet</div>';
}

function render() { draw(); panel(); scrub.value = frame; }
document.getElementById('play').onclick = e => {
  playing = !playing; e.target.textContent = playing ? 'pause' : 'play';
};
document.getElementById('qt').onclick = e => { showQT = !showQT; e.target.classList.toggle('on'); render(); };
document.getElementById('acc').onclick = e => { showAcc = !showAcc; e.target.classList.toggle('on'); render(); };
document.getElementById('disp').onclick = e => { showDisp = !showDisp; e.target.classList.toggle('on'); render(); };
scrub.oninput = () => { frame = +scrub.value; playing = false;
  document.getElementById('play').textContent = 'play'; render(); };
addEventListener('resize', () => { resize(); render(); });
resize(); render();
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
    d += z->name.rfind("synthetic", 0) == 0 ? "\",\"syn\":1" : "\",\"syn\":0";
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
    for (const auto& c : changes) {
      const auto* z = s.zones().get(c.zone);
      if (!z || z->name.rfind("synthetic", 0) == 0) continue;
      if (!f2) d += ",";
      f2 = false;
      d += "{\"v\":" + std::to_string(c.version) + ",\"at\":" + std::to_string(c.at) +
           ",\"z\":" + std::to_string(c.zone) + ",\"k\":" + std::to_string(int(c.kind)) + "}";
    }
  }
  d += "],\"versions\":" + std::to_string(s.versioned().version_count());
  d += ",\"sharing\":";
  put_f(d, s.versioned().share_stats().sharing_ratio(), 2);
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
  d += ",\"responders\":[";
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
  d += "]}";

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
