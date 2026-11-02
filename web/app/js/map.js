// A self-contained geographic map on <canvas>. No tile server and no map
// library: the base layer is the OpenStreetMap road network the platform
// already holds (fetched in tiles from /api/roads), so the map works offline,
// shows exactly the graph that routing runs on, and costs nothing to run.
//
// Projection: spherical Web Mercator, so shapes look the way they do on every
// other map. Layers are plain data set by the page; render() redraws.

import { get } from './api.js';
import { h, statusColour, ZONE_COLOUR, SEV_COLOUR, metres } from './util.js';

const R2D = 180 / Math.PI;
const D2R = Math.PI / 180;
const mx = (lon) => lon * D2R;
const my = (lat) => -Math.log(Math.tan(Math.PI / 4 + (lat * D2R) / 2));
const lonOf = (x) => x * R2D;
const latOf = (y) => (2 * Math.atan(Math.exp(-y)) - Math.PI / 2) * R2D;

const TILE = 0.02;            // road tiles, degrees
const roadCache = new Map();  // shared by every map on the page

export class MapView {
  constructor(container, opts = {}) {
    this.opts = Object.assign({ legend: true, roads: true, height: null }, opts);
    this.root = h('div', { class: 'map-wrap' });
    if (this.opts.height) this.root.style.height = this.opts.height;
    else this.root.style.height = '100%';
    this.canvas = h('canvas', { 'aria-label': 'map', role: 'img' });
    this.tip = h('div', { class: 'map-tip' });
    this.scaleEl = h('div', { class: 'map-scale' });
    const tools = h('div', { class: 'map-tools' },
      h('button', { title: 'Zoom in', onclick: () => this.zoomBy(1.6) }, '+'),
      h('button', { title: 'Zoom out', onclick: () => this.zoomBy(1 / 1.6) }, '−'),
      h('button', { title: 'Fit to data', onclick: () => this.fitData() }, '⤢'));
    this.root.append(this.canvas, this.tip, this.scaleEl, tools);
    if (this.opts.legend) this.root.append(this.legend());
    container.appendChild(this.root);

    this.cx = mx(91.88); this.cy = my(25.58); this.k = 200000;   // pixels per mercator radian
    this.zones = []; this.users = []; this.responders = []; this.incidents = []; this.groups = [];
    this.trails = []; this.routes = []; this.points = []; this.marks = [];
    this.selected = null; this.highlightZone = null;
    this.draft = null;               // polygon being drawn: [[lat, lon], ...]
    this.onPick = null; this.onDraw = null; this.onClickMap = null;
    this.extra = null;               // (ctx, toPx) => void, page-specific overlay
    this.filter = () => true;

    this.bind();
    this.ro = new ResizeObserver(() => this.resize());
    this.ro.observe(this.root);
    this.resize();
  }

  destroy() { this.ro.disconnect(); this.root.remove(); this.dead = true; }

  legend() {
    const item = (colour, text, square) => h('span', null,
      h('span', { class: 'dot', style: { background: colour, borderRadius: square ? '1px' : '50%' } }), text);
    return h('div', { class: 'map-legend' },
      item(ZONE_COLOUR.restricted, 'restricted zone', true), item(ZONE_COLOUR.caution, 'caution zone', true),
      item(ZONE_COLOUR.advisory, 'advisory zone', true),
      item(statusColour('clear'), 'tourist: clear'), item(statusColour('uncertain'), 'approaching / uncertain'),
      item(statusColour('inside restricted'), 'inside restricted'), item(statusColour('no contact'), 'no contact'),
      item('#e3e8ee', 'responder', true));
  }

  // ── view ────────────────────────────────────────────────────────────────
  resize() {
    const r = this.root.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    this.w = Math.max(10, r.width); this.h = Math.max(10, r.height);
    this.canvas.width = Math.round(this.w * dpr); this.canvas.height = Math.round(this.h * dpr);
    this.canvas.style.height = this.h + 'px';
    this.dpr = dpr;
    this.render();
  }
  toPx(lat, lon) { return [(mx(lon) - this.cx) * this.k + this.w / 2, (my(lat) - this.cy) * this.k + this.h / 2]; }
  toLL(x, y) { return [latOf((y - this.h / 2) / this.k + this.cy), lonOf((x - this.w / 2) / this.k + this.cx)]; }
  metresPerPx() { return 6371000 * Math.cos(latOf(this.cy) * D2R) / this.k; }

  fit(minLat, minLon, maxLat, maxLon, pad = 40) {
    if (!(maxLat >= minLat && maxLon >= minLon)) return;
    const x0 = mx(minLon), x1 = mx(maxLon), y0 = my(maxLat), y1 = my(minLat);
    this.cx = (x0 + x1) / 2; this.cy = (y0 + y1) / 2;
    const kx = (this.w - 2 * pad) / Math.max(1e-9, x1 - x0), ky = (this.h - 2 * pad) / Math.max(1e-9, y1 - y0);
    this.k = Math.max(2000, Math.min(4e7, Math.min(kx, ky)));
    this.render();
  }
  fitPoints(pts, pad = 50, minSpanDeg = 0.004) {
    if (!pts.length) return;
    let a = 90, b = 180, c = -90, d = -180;
    for (const [la, lo] of pts) { a = Math.min(a, la); c = Math.max(c, la); b = Math.min(b, lo); d = Math.max(d, lo); }
    const ml = (a + c) / 2, mo = (b + d) / 2, sl = Math.max(c - a, minSpanDeg) / 2, so = Math.max(d - b, minSpanDeg) / 2;
    this.fit(ml - sl, mo - so, ml + sl, mo + so, pad);
  }
  fitData() {
    const pts = [];
    for (const u of this.users) if (u.location) pts.push([u.location.lat, u.location.lon]);
    for (const r of this.responders) pts.push(r.position);
    if (pts.length < 2) for (const z of this.zones) if (!z.synthetic) { pts.push([z.bbox[0], z.bbox[1]]); pts.push([z.bbox[2], z.bbox[3]]); }
    this.fitPoints(pts);
  }
  zoomBy(f, px, py) {
    px = px ?? this.w / 2; py = py ?? this.h / 2;
    const [la, lo] = this.toLL(px, py);
    this.k = Math.max(2000, Math.min(4e7, this.k * f));
    const [nx, ny] = this.toPx(la, lo);
    this.cx += (nx - px) / this.k; this.cy += (ny - py) / this.k;
    this.render();
  }

  // ── interaction ─────────────────────────────────────────────────────────
  bind() {
    let drag = null;
    const c = this.canvas;
    c.addEventListener('pointerdown', (e) => {
      drag = { x: e.clientX, y: e.clientY, cx: this.cx, cy: this.cy, moved: false };
      c.setPointerCapture(e.pointerId);
    });
    c.addEventListener('pointermove', (e) => {
      const r = c.getBoundingClientRect();
      if (drag) {
        const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
        if (Math.abs(dx) + Math.abs(dy) > 3) { drag.moved = true; c.classList.add('drag'); }
        if (drag.moved) { this.cx = drag.cx - dx / this.k; this.cy = drag.cy - dy / this.k; this.render(); }
      } else {
        this.hover(e.clientX - r.left, e.clientY - r.top);
      }
    });
    c.addEventListener('pointerup', (e) => {
      const r = c.getBoundingClientRect();
      c.classList.remove('drag');
      if (drag && !drag.moved) this.click(e.clientX - r.left, e.clientY - r.top);
      drag = null;
    });
    c.addEventListener('pointerleave', () => { this.tip.style.display = 'none'; });
    c.addEventListener('wheel', (e) => {
      e.preventDefault();
      const r = c.getBoundingClientRect();
      this.zoomBy(Math.exp(-e.deltaY * 0.0015), e.clientX - r.left, e.clientY - r.top);
    }, { passive: false });
    c.addEventListener('dblclick', (e) => {
      if (this.draft) { if (this.onDraw) this.onDraw('finish'); return; }
      const r = c.getBoundingClientRect();
      this.zoomBy(2, e.clientX - r.left, e.clientY - r.top);
    });
  }

  pick(x, y) {
    let best = null, bd = 12 * 12;
    const test = (kind, obj, lat, lon) => {
      const [px, py] = this.toPx(lat, lon);
      const d = (px - x) ** 2 + (py - y) ** 2;
      if (d < bd) { bd = d; best = { kind, obj }; }
    };
    for (const u of this.users) if (u.location && this.filter('user', u)) test('user', u, u.location.lat, u.location.lon);
    for (const r of this.responders) if (this.filter('responder', r)) test('responder', r, r.position[0], r.position[1]);
    for (const i of this.incidents) if (this.filter('incident', i)) test('incident', i, i.centroid[0], i.centroid[1]);
    if (best) return best;
    const [la, lo] = this.toLL(x, y);
    for (let i = this.zones.length - 1; i >= 0; --i) {
      const z = this.zones[i];
      if (!this.filter('zone', z) || !z.ring) continue;
      if (la < z.bbox[0] || la > z.bbox[2] || lo < z.bbox[1] || lo > z.bbox[3]) continue;
      if (pointInRing(la, lo, z.ring)) return { kind: 'zone', obj: z };
    }
    return null;
  }
  hover(x, y) {
    const p = this.draft ? null : this.pick(x, y);
    if (!p) { this.tip.style.display = 'none'; this.canvas.style.cursor = this.draft ? 'crosshair' : ''; return; }
    this.canvas.style.cursor = 'pointer';
    const o = p.obj;
    let text = '';
    if (p.kind === 'user') text = o.display_name + ' — ' + o.status + (o.location ? ' · ±' + Math.round(o.location.accuracy_m) + ' m' : '');
    else if (p.kind === 'responder') text = o.name + ' — ' + (o.available ? 'available' : 'assigned');
    else if (p.kind === 'incident') text = 'Incident #' + o.id + ' — ' + o.state + ', ' + o.people + ' person(s), severity ' + o.severity;
    else if (p.kind === 'zone') text = o.name + ' — ' + o.kind + ', severity ' + o.severity + (o.active_now ? '' : ' (not in force)');
    this.tip.textContent = text;
    this.tip.style.display = 'block';
    this.tip.style.left = Math.min(this.w - 290, x + 14) + 'px';
    this.tip.style.top = (y + 12) + 'px';
  }
  click(x, y) {
    const [la, lo] = this.toLL(x, y);
    if (this.draft) { this.draft.push([la, lo]); if (this.onDraw) this.onDraw('point'); this.render(); return; }
    if (this.onClickMap && this.onClickMap(la, lo)) return;
    const p = this.pick(x, y);
    this.selected = p;
    if (this.onPick) this.onPick(p);
    this.render();
  }

  // ── roads ───────────────────────────────────────────────────────────────
  visibleTiles() {
    const [la0, lo0] = this.toLL(0, this.h), [la1, lo1] = this.toLL(this.w, 0);
    const t = [];
    for (let a = Math.floor(la0 / TILE); a <= Math.floor(la1 / TILE); ++a)
      for (let b = Math.floor(lo0 / TILE); b <= Math.floor(lo1 / TILE); ++b) t.push(a + ',' + b);
    return t;
  }
  ensureRoads() {
    if (!this.opts.roads || this.metresPerPx() > 18) return [];
    const tiles = this.visibleTiles();
    if (tiles.length > 64) return [];
    const ready = [];
    for (const key of tiles) {
      const c = roadCache.get(key);
      if (c && c.segments) { ready.push(c.segments); continue; }
      if (c) continue;   // in flight
      const [a, b] = key.split(',').map(Number);
      const entry = {};
      roadCache.set(key, entry);
      get(`/api/roads?min_lat=${a * TILE}&min_lon=${b * TILE}&max_lat=${(a + 1) * TILE}&max_lon=${(b + 1) * TILE}`)
        .then(d => { entry.segments = d.segments; if (!this.dead) this.render(); })
        .catch(() => roadCache.delete(key));
    }
    return ready;
  }

  // ── drawing ─────────────────────────────────────────────────────────────
  render() {
    if (this.pending || this.dead) return;
    this.pending = true;
    requestAnimationFrame(() => { this.pending = false; this.draw(); });
  }

  draw() {
    const ctx = this.canvas.getContext('2d');
    ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    ctx.fillStyle = '#0a0e12';
    ctx.fillRect(0, 0, this.w, this.h);
    const P = (lat, lon) => this.toPx(lat, lon);

    // roads
    const tiles = this.ensureRoads();
    if (tiles.length) {
      ctx.strokeStyle = this.metresPerPx() < 3 ? '#33404e' : '#26303b';
      ctx.lineWidth = this.metresPerPx() < 3 ? 1.4 : 0.8;
      ctx.beginPath();
      for (const segs of tiles) for (const s of segs) {
        const [x0, y0] = P(s[0], s[1]), [x1, y1] = P(s[2], s[3]);
        ctx.moveTo(x0, y0); ctx.lineTo(x1, y1);
      }
      ctx.stroke();
    }

    // zones
    for (const z of this.zones) {
      if (!this.filter('zone', z) || !z.ring) continue;
      const col = ZONE_COLOUR[z.kind] || '#a7b1bd';
      const sel = (this.selected && this.selected.kind === 'zone' && this.selected.obj.id === z.id) || this.highlightZone === z.id;
      ctx.beginPath();
      ringPath(ctx, z.ring, P);
      for (const hole of z.holes || []) ringPath(ctx, hole, P);
      ctx.globalAlpha = z.active_now ? (sel ? 0.32 : 0.16) : 0.05;
      ctx.fillStyle = col;
      ctx.fill('evenodd');
      ctx.globalAlpha = z.active_now ? 0.9 : 0.5;
      ctx.setLineDash(z.active_now ? [] : [5, 4]);
      ctx.strokeStyle = col;
      ctx.lineWidth = sel ? 2.2 : 1.1;
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.globalAlpha = 1;
      if (this.metresPerPx() < 6 && z.kind !== 'advisory') {
        const [x, y] = P((z.bbox[0] + z.bbox[2]) / 2, (z.bbox[1] + z.bbox[3]) / 2);
        ctx.fillStyle = 'rgba(227,232,238,.75)';
        ctx.font = '11px -apple-system, sans-serif';
        ctx.fillText(z.name, x + 4, y);
      }
    }

    // trails
    for (const t of this.trails) {
      if (!t.points || t.points.length < 2) continue;
      ctx.strokeStyle = t.colour || '#3fb6a8';
      ctx.lineWidth = 1.6;
      ctx.globalAlpha = 0.8;
      ctx.beginPath();
      t.points.forEach((p, i) => { const [x, y] = P(p[1], p[2]); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); });
      ctx.stroke();
      ctx.globalAlpha = 1;
      for (const p of t.points) {
        if (!p[4]) continue;   // flagged points: late (amber), jump (red), synced (violet)
        const [x, y] = P(p[1], p[2]);
        ctx.fillStyle = p[4] & 4 ? '#9b8afb' : p[4] & 2 ? '#e5484d' : p[4] & 1 ? '#d6a440' : '#6f7b89';
        ctx.fillRect(x - 2, y - 2, 4, 4);
      }
    }

    // routes
    for (const r of this.routes) {
      if (!r.points || r.points.length < 2) continue;
      ctx.strokeStyle = r.colour || '#e3e8ee';
      ctx.lineWidth = r.width || 3;
      ctx.setLineDash(r.dash || []);
      ctx.beginPath();
      r.points.forEach((p, i) => { const [x, y] = P(p[0], p[1]); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); });
      ctx.stroke();
      ctx.setLineDash([]);
    }

    // groups: centre, and a dashed line to each straggler
    for (const g of this.groups) {
      if (!g.centre || !this.filter('group', g)) continue;
      const [cx, cy] = P(g.centre[0], g.centre[1]);
      for (const s of g.stragglers || []) {
        const u = this.users.find(x => x.id === s.user);
        if (!u || !u.location) continue;
        const [x, y] = P(u.location.lat, u.location.lon);
        ctx.strokeStyle = '#e07b39'; ctx.setLineDash([4, 4]); ctx.lineWidth = 1.2;
        ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(x, y); ctx.stroke(); ctx.setLineDash([]);
        ctx.fillStyle = '#e07b39'; ctx.font = '11px ui-monospace, monospace';
        ctx.fillText(metres(s.separation_m), (cx + x) / 2 + 4, (cy + y) / 2 - 4);
      }
      ctx.strokeStyle = g.fragmented ? '#e07b39' : '#a7b1bd';
      ctx.lineWidth = 1.5;
      ctx.beginPath(); ctx.moveTo(cx - 6, cy); ctx.lineTo(cx + 6, cy); ctx.moveTo(cx, cy - 6); ctx.lineTo(cx, cy + 6); ctx.stroke();
      if (this.metresPerPx() < 12) { ctx.fillStyle = '#a7b1bd'; ctx.font = '11px -apple-system, sans-serif'; ctx.fillText(g.name, cx + 8, cy - 6); }
    }

    // incidents
    for (const i of this.incidents) {
      if (!this.filter('incident', i)) continue;
      const [x, y] = P(i.centroid[0], i.centroid[1]);
      const r = Math.max(10, (i.radius_m || 0) / this.metresPerPx());
      const col = i.state === 'resolved' ? '#6f7b89' : SEV_COLOUR[i.severity] || '#e5484d';
      ctx.strokeStyle = col; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.arc(x, y, r, 0, Math.PI * 2); ctx.stroke();
      ctx.beginPath(); ctx.arc(x, y, r + 5, 0, Math.PI * 2); ctx.globalAlpha = 0.35; ctx.stroke(); ctx.globalAlpha = 1;
      ctx.fillStyle = col; ctx.font = '600 11px ui-monospace, monospace';
      ctx.fillText('#' + i.id, x + r + 6, y - r);
    }

    // points (generic markers set by pages)
    for (const p of this.points) {
      const [x, y] = P(p.lat, p.lon);
      ctx.fillStyle = p.colour || '#e3e8ee';
      ctx.beginPath(); ctx.arc(x, y, p.r || 4, 0, Math.PI * 2); ctx.fill();
      if (p.label) { ctx.font = '11px -apple-system, sans-serif'; ctx.fillText(p.label, x + 7, y + 4); }
    }

    // responders
    for (const r of this.responders) {
      if (!this.filter('responder', r)) continue;
      const [x, y] = P(r.position[0], r.position[1]);
      const sel = this.selected && this.selected.kind === 'responder' && this.selected.obj.id === r.id;
      ctx.fillStyle = r.available ? '#e3e8ee' : '#6c9bd2';
      ctx.fillRect(x - 5, y - 5, 10, 10);
      ctx.strokeStyle = '#0a0e12'; ctx.lineWidth = 1.5; ctx.strokeRect(x - 5, y - 5, 10, 10);
      if (sel) { ctx.strokeStyle = '#3fb6a8'; ctx.lineWidth = 2; ctx.strokeRect(x - 9, y - 9, 18, 18); }
      if (this.metresPerPx() < 10) { ctx.fillStyle = '#a7b1bd'; ctx.font = '11px -apple-system, sans-serif'; ctx.fillText(r.name, x + 9, y + 4); }
    }

    // users
    for (const u of this.users) {
      if (!u.location || !this.filter('user', u)) continue;
      const [x, y] = P(u.location.lat, u.location.lon);
      const col = statusColour(u.status);
      const acc = u.location.accuracy_m / this.metresPerPx();
      if (acc > 6) {
        ctx.fillStyle = col; ctx.globalAlpha = 0.10;
        ctx.beginPath(); ctx.arc(x, y, acc, 0, Math.PI * 2); ctx.fill(); ctx.globalAlpha = 1;
      }
      const sel = this.selected && this.selected.kind === 'user' && this.selected.obj.id === u.id;
      ctx.beginPath(); ctx.arc(x, y, sel ? 6.5 : 5, 0, Math.PI * 2);
      if (u.status === 'no contact') { ctx.strokeStyle = col; ctx.lineWidth = 2; ctx.stroke(); }
      else { ctx.fillStyle = col; ctx.fill(); ctx.strokeStyle = '#0a0e12'; ctx.lineWidth = 1.5; ctx.stroke(); }
      if (sel) { ctx.strokeStyle = '#e3e8ee'; ctx.lineWidth = 1.5; ctx.beginPath(); ctx.arc(x, y, 10, 0, Math.PI * 2); ctx.stroke(); }
      if (this.metresPerPx() < 4 || sel) {
        ctx.fillStyle = '#e3e8ee'; ctx.font = '11px -apple-system, sans-serif';
        ctx.fillText(u.display_name.replace(/^SIM · /, ''), x + 8, y - 6);
      }
    }

    // polygon being drawn
    if (this.draft && this.draft.length) {
      ctx.strokeStyle = '#3fb6a8'; ctx.fillStyle = 'rgba(63,182,168,.15)'; ctx.lineWidth = 2;
      ctx.beginPath();
      this.draft.forEach((p, i) => { const [x, y] = P(p[0], p[1]); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); });
      if (this.draft.length > 2) { ctx.closePath(); ctx.fill(); }
      ctx.stroke();
      for (const p of this.draft) { const [x, y] = P(p[0], p[1]); ctx.fillStyle = '#3fb6a8'; ctx.fillRect(x - 3, y - 3, 6, 6); }
    }

    if (this.extra) this.extra(ctx, P);

    // scale
    const mpp = this.metresPerPx();
    const nice = [10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000];
    const m = nice.find(n => n / mpp > 70) || 50000;
    this.scaleEl.textContent = '▏' + '—'.repeat(Math.max(3, Math.round(m / mpp / 9))) + '▕ ' + metres(m);
  }
}

function ringPath(ctx, ring, P) {
  ring.forEach((p, i) => { const [x, y] = P(p[0], p[1]); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); });
  ctx.closePath();
}

// Ray casting, for click picking only. The server does containment.
export function pointInRing(lat, lon, ring) {
  let inside = false;
  for (let i = 0, j = ring.length - 1; i < ring.length; j = i++) {
    const [ai, bi] = ring[i], [aj, bj] = ring[j];
    if ((ai > lat) !== (aj > lat) && lon < (bj - bi) * (lat - ai) / (aj - ai) + bi) inside = !inside;
  }
  return inside;
}
