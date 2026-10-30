import { get, post } from '../api.js';
import { h, mount, clock, sev, sourceBadge, stateText, table, panel, toast, fixed } from '../util.js';

export function render(root, ctx) {
  const box = h('div');
  const f = { state: 'open', minSev: 1, kind: 'all' };
  mount(root, h('div', { class: 'page' },
    h('div', { class: 'page-head' }, h('div', null, h('h1', null, 'Alerts'),
      h('p', null, 'Open alerts are listed in triage order: they are pushed into a binary max-heap keyed on severity + 0.5 per minute open + 0.1 per km to the nearest free responder (SOS +100), then popped. An alert left unacknowledged for 2 minutes escalates one severity level — its deadline sits in a hashed timing wheel, so checking costs one slot per tick, not one look at every alert.'))),
    panel(h('div', { class: 'row' }, h('h3', null, 'Alerts'),
      h('select', { onchange: (e) => { f.state = e.target.value; load(); } }, [['open', 'Open (triage order)'], ['all', 'All, including closed']].map(([v, t]) => h('option', { value: v }, t))),
      h('select', { onchange: (e) => { f.minSev = Number(e.target.value); draw(); } }, [1, 2, 3, 4, 5].map(s => h('option', { value: s }, 'severity ≥ ' + s))),
      h('select', { onchange: (e) => { f.kind = e.target.value; draw(); } }, ['all', 'ZONE BREACH', 'APPROACHING', 'UNCERTAIN', 'DWELL EXCEEDED', 'STRAGGLER', 'GROUP SPLIT', 'STATIONARY', 'SIGNAL LOST', 'SOS'].map(k => h('option', { value: k }, k === 'all' ? 'All kinds' : k.toLowerCase())))), box)));
  let data = null, err = null;
  async function load() {
    try { data = (await get('/api/alerts' + (f.state === 'open' ? '?state=open' : ''))).alerts; err = null; } catch (e) { err = e.message; }
    draw();
  }
  const act = async (id, what) => {
    try { await post('/api/alerts/' + id + '/' + what); toast('Alert ' + id + ' ' + (what === 'ack' ? 'acknowledged' : what + 'd'), 'ok'); load(); }
    catch (e) { toast(e.message, 'error'); }
  };
  function draw() {
    if (err) return mount(box, h('div', { class: 'error-box' }, err));
    if (!data) return mount(box, h('div', { class: 'empty' }, 'Loading…'));
    const rows = data.filter(a => a.severity >= f.minSev && (f.kind === 'all' || a.kind === f.kind));
    mount(box, table([{ label: 'Rank', cls: 'num' }, 'Sev', 'Kind', 'User', 'Zone', 'Raised', 'State', { label: 'Priority', cls: 'num' }, 'Incident', ''],
      rows.map((a, i) => h('tr', null,
        h('td', { class: 'num' }, ['open', 'ack', 'dispatched'].includes(a.state) ? String(i + 1) : '—'),
        h('td', null, sev(a.severity), a.escalated ? h('span', { class: 'faint small', title: 'escalated from ' + a.original_severity }, ' ↑') : null),
        h('td', null, a.kind.toLowerCase(), ' ', a.late ? h('span', { class: 'badge-late', title: 'raised from an event synced after an offline period' }, 'LATE') : null),
        h('td', null, a.user_name || '—', ' ', sourceBadge(a.source)),
        h('td', null, a.zone_name || h('span', { class: 'faint' }, a.note || '—')),
        h('td', { class: 'mono' }, clock(a.raised_ms)),
        h('td', null, stateText(a.state)),
        h('td', { class: 'num' }, a.priority ? fixed(a.priority, 2) : '—'),
        h('td', null, a.incident === null ? '—' : h('a', { href: '#/incidents/' + a.incident }, '#' + a.incident)),
        h('td', null, a.state === 'open' ? h('button', { class: 'small', onclick: () => act(a.id, 'ack') }, 'Acknowledge') : null, ' ',
          ['open', 'ack', 'dispatched'].includes(a.state) ? [h('button', { class: 'small', onclick: () => act(a.id, 'resolve') }, 'Resolve'), ' ',
            h('button', { class: 'small ghost', onclick: () => act(a.id, 'dismiss') }, 'Dismiss')] : null))),
      { empty: f.state === 'open' ? 'No open alerts.' : 'No alerts yet.', maxHeight: '72vh' }));
  }
  load();
  let n = 0;
  const off = ctx.onLive(() => { if (++n % 2 === 0) load(); });
  return { destroy() { off(); } };
}
