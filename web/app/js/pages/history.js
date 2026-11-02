import { get } from '../api.js';
import { h, mount, clock, sev, table, panel, toast, sourceBadge } from '../util.js';
import { MapView } from '../map.js';

// Historical investigation: pick a past time and see what the system KNEW
// then -- positions it had received, zones in force (persistent quadtree),
// alert and incident states -- and verify any log entry against the Merkle
// root in this browser, independently of the server's own check.
export function render(root, ctx) {
  const now = ctx.live ? ctx.live.now_ms : 0;
  const slider = h('input', { type: 'range', min: 0, max: Math.max(1000, now), step: 1000, value: now, style: { flex: 1 } });
  const exact = h('input', { type: 'number', min: 0, value: Math.round(now / 1000), style: { width: '110px' } });
  const label = h('span', { class: 'clock' }, clock(now));
  const mapHost = h('div', { style: { height: '440px' } });
  const out = h('div', { class: 'stack' });
  const evBox = h('div');
  const order = h('select', null, h('option', { value: 'arrival' }, 'arrival order'), h('option', { value: 'lamport' }, 'Lamport order'));
  const kind = h('select', null, ['', 'zone_enter', 'zone_exit', 'zone_uncertain', 'dwell_exceeded', 'alert_raised', 'alert_escalated', 'incident_opened',
    'incident_assigned', 'incident_resolved', 'session_started', 'session_stopped', 'sync_merged', 'zone_created', 'zone_updated', 'zone_validity']
    .map(k => h('option', { value: k }, k ? k.replace(/_/g, ' ') : 'all kinds')));
  mount(root, h('div', { class: 'page' },
    h('div', { class: 'page-head' }, h('div', null, h('h1', null, 'Historical Investigation'),
      h('p', null, 'Choose a moment. The answer is what the system knew at that time: fixes it had received (each trail is binary-searched), zones in force in the persistent-quadtree version current then, and each alert\'s and incident\'s state as of then. Events learned later from an offline phone are excluded until the time they arrived.'))),
    panel('Time', h('div', { class: 'row' }, slider, exact, label, h('button', { class: 'primary', onclick: () => query(Number(slider.value)) }, 'Query'),
      h('button', { onclick: () => { const n = ctx.live ? ctx.live.now_ms : 0; slider.max = n; slider.value = n; query(n); } }, 'Now'))),
    h('div', { class: 'grid cols-main-side', style: { marginTop: '12px' } }, panel('What the system knew', mapHost), out),
    h('div', { style: { marginTop: '12px' } }, panel(h('div', { class: 'row' }, h('h3', null, 'Event log and evidence'), kind, order,
      h('button', { onclick: () => loadEvents() }, 'Refresh')), evBox))));
  const map = new MapView(mapHost);
  let zonesAll = [];
  get('/api/zones').then(z => { zonesAll = z.zones; map.zones = zonesAll; map.fitData(); });
  slider.oninput = () => { label.textContent = clock(Number(slider.value)); exact.value = Math.round(slider.value / 1000); };
  slider.onchange = () => query(Number(slider.value));
  exact.onchange = () => { slider.value = Number(exact.value) * 1000; query(Number(exact.value) * 1000); };

  async function query(t) {
    label.textContent = clock(t);
    try {
      const d = await get('/api/history?t=' + t);
      const inForce = new Set(d.zones_in_force);
      map.zones = zonesAll.map(z => Object.assign({}, z, { active_now: inForce.has(z.id) }));
      map.users = d.users.map(u => ({ id: u.id, display_name: u.name, status: u.flags & 4 ? 'uncertain' : 'clear', location: { lat: u.lat, lon: u.lon, accuracy_m: u.accuracy_m } }));
      map.render();
      const alertsOpen = d.alerts.filter(a => ['open', 'ack', 'dispatched'].includes(a.state_at_t));
      mount(out,
        panel('At ' + clock(t), h('dl', { class: 'kv' },
          h('dt', null, 'Users known'), h('dd', null, String(d.users.length)),
          h('dt', null, 'Zones in force'), h('dd', null, d.zones_in_force.length + ' (index version v' + d.version_at_t + ')'),
          h('dt', null, 'Alerts open then'), h('dd', null, String(alertsOpen.length)),
          h('dt', null, 'Incidents then'), h('dd', null, d.incidents.map(i => i.state_at_t).join(', ') || 'none'))),
        panel('Alerts as of then', table(['#', 'Kind', 'Sev', 'Then', 'Now'], d.alerts.slice(-40).reverse().map(a => h('tr', null,
          h('td', { class: 'num' }, '#' + a.id), h('td', null, a.kind.toLowerCase(), ' ', a.late ? h('span', { class: 'badge-late' }, 'LATE') : null),
          h('td', null, sev(a.severity)), h('td', null, a.state_at_t), h('td', { class: 'faint' }, a.state_now))), { empty: 'No alerts existed yet.', maxHeight: '260px' })),
        panel('Events in the 5 minutes before', h('ul', { class: 'feed' }, d.recent.slice(-30).reverse().map(e => h('li', null, h('span', { class: 't' }, clock(e.t_ms).slice(3)),
          h('span', null, e.kind.replace(/_/g, ' '), ' ', e.late ? h('span', { class: 'badge-late' }, 'LATE') : null, h('div', { class: 'faint small' }, e.detail)))))));
    } catch (e) { mount(out, h('div', { class: 'error-box' }, e.message)); }
  }

  async function loadEvents() {
    try {
      const d = await get('/api/events?limit=300&order=' + order.value + (kind.value ? '&kind=' + kind.value : ''));
      mount(evBox, h('div', { class: 'stack' },
        h('p', { class: 'small muted' }, d.total + ' entries. Every entry is a leaf of a Merkle tree (RFC 6962). "Verify" fetches the inclusion proof and recomputes the root in this browser with WebCrypto SHA-256 — no trust in the server needed beyond the published root.'),
        table(['Seq', 'Lamport', 'Time', 'Received', 'Kind', 'Who', 'Detail', ''], d.events.map(e => h('tr', null,
          h('td', { class: 'num' }, String(e.seq)), h('td', { class: 'mono' }, e.lamport + '@' + e.device), h('td', { class: 'mono' }, clock(e.t_ms)),
          h('td', { class: 'mono' }, e.recorded_ms !== e.t_ms ? clock(e.recorded_ms) : ''),
          h('td', null, e.kind.replace(/_/g, ' '), ' ', e.late ? h('span', { class: 'badge-late' }, 'LATE') : null),
          h('td', null, e.user_name || '', e.zone_name ? ' · ' + e.zone_name : ''), h('td', { class: 'small faint' }, e.detail),
          h('td', null, h('button', { class: 'small', onclick: (ev) => verify(e.evidence_index, ev.target) }, 'Verify')))), { maxHeight: '420px' })));
    } catch (e) { mount(evBox, h('div', { class: 'error-box' }, e.message)); }
  }

  async function verify(index, btn) {
    try {
      const p = await get('/api/evidence/' + index);
      const ok = await verifyInclusion(p.entry, p.index, p.size, p.path, p.root);
      btn.textContent = ok ? '✓ verified' : '✗ FAILED';
      btn.style.color = ok ? 'var(--ok)' : 'var(--sev-5)';
      toast('Entry ' + index + ': ' + (ok ? 'browser recomputed the root from ' + p.path.length + ' sibling hashes — it matches' : 'proof does not match the root'), ok ? 'ok' : 'error');
    } catch (e) { toast(e.message, 'error'); }
  }

  query(now);
  loadEvents();
  return { destroy() { map.destroy(); } };
}

// RFC 9162 §2.1.3.2 inclusion verification, with leaf = SHA-256(0x00 || entry)
// and node = SHA-256(0x01 || left || right) -- the same domain separation the
// server's evidence/merkle_log.cpp uses.
async function sha(bytes) { return new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)); }
const fromHex = (s) => new Uint8Array(s.match(/../g).map(b => parseInt(b, 16)));
const toHex = (b) => [...b].map(x => x.toString(16).padStart(2, '0')).join('');
async function node(l, r) { const b = new Uint8Array(65); b[0] = 1; b.set(l, 1); b.set(r, 33); return sha(b); }

export async function verifyInclusion(entry, index, size, path, rootHex) {
  const enc = new TextEncoder().encode(entry);
  const lb = new Uint8Array(enc.length + 1); lb[0] = 0; lb.set(enc, 1);
  let r = await sha(lb);
  let fn = index, sn = size - 1;
  for (const ph of path) {
    const p = fromHex(ph);
    if (sn === 0) return false;
    if ((fn & 1) || fn === sn) {
      r = await node(p, r);
      if (!(fn & 1)) { while (!(fn & 1) && fn !== 0) { fn >>= 1; sn >>= 1; } }
    } else {
      r = await node(r, p);
    }
    fn >>= 1; sn >>= 1;
  }
  return sn === 0 && toHex(r) === rootHex;
}

export { sourceBadge };
