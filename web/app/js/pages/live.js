import { get } from '../api.js';
import { h, mount, clock, ago, metres, fixed, sev, statusChip, sourceBadge, stateText, toast } from '../util.js';
import { MapView } from '../map.js';
import { scenarioPanel } from '../runner.js';

// Live Monitoring: the map is the page. The side panel shows what is
// selected, the filters, the scenario, and the event feed.
export function render(root, ctx) {
  const mapHost = h('div', { style: { height: '100%', minHeight: '360px' } });
  const tabs = ['Selected', 'Filters', 'Scenario', 'Events'];
  let tab = 'Selected';
  const tabBar = h('div', { class: 'tabs', role: 'tablist' });
  const body = h('div', { class: 'scroll' });
  mount(root, h('div', { class: 'live' }, mapHost, h('aside', { class: 'side' }, tabBar, body)));

  const map = new MapView(mapHost);
  const f = { kinds: { restricted: true, caution: true, advisory: true, safe: true }, minSev: 1, group: 'all',
              tracking: 'sharing', incident: 'open', text: '', trails: true, responders: true };
  map.filter = (kind, o) => {
    if (kind === 'zone') return f.kinds[o.kind] && o.severity >= f.minSev;
    if (kind === 'user') {
      if (f.group !== 'all' && String(o.group) !== f.group) return false;
      if (f.text && !o.display_name.toLowerCase().includes(f.text)) return false;
      if (f.tracking === 'alerts') return o.open_alerts > 0;
      if (f.tracking === 'nocontact') return o.status === 'no contact';
      return o.sharing;
    }
    if (kind === 'incident') return f.incident === 'all' || (f.incident === 'open' ? o.state !== 'resolved' : o.state === f.incident);
    if (kind === 'responder') return f.responders;
    if (kind === 'group') return f.group === 'all' || String(o.id) === f.group;
    return true;
  };
  let selected = null;          // {kind, id}
  let detail = null;            // fetched detail for a selected user
  let fitted = false;
  let scen = null;

  get('/api/zones').then(z => { map.zones = z.zones; map.render(); }).catch(e => toast(e.message, 'error'));

  map.onPick = async (p) => {
    if (!p) { selected = null; detail = null; map.trails = []; map.routes = []; draw(); return; }
    selected = { kind: p.kind, id: p.obj.id };
    tab = 'Selected';
    await loadDetail();
    draw();
  };

  async function loadDetail() {
    detail = null; map.trails = []; map.routes = [];
    if (!selected) return;
    try {
      if (selected.kind === 'user') {
        const [u, t, ev] = await Promise.all([get('/api/users/' + selected.id), get('/api/users/' + selected.id + '/trail'),
          get('/api/events?limit=40&user=' + selected.id)]);
        detail = { user: u.user, trail: t, events: ev.events };
        if (f.trails) map.trails = [{ points: t.points, colour: '#3fb6a8' }];
      } else if (selected.kind === 'incident') {
        const d = await get('/api/incidents/' + selected.id);
        detail = d;
        if (d.incident.route) map.routes = [{ points: d.incident.route.points, colour: '#e3e8ee' }];
      } else if (selected.kind === 'zone') {
        detail = await get('/api/zones/' + selected.id);
      }
    } catch (e) { detail = { error: e.message }; }
  }

  function drawTabs() {
    mount(tabBar, tabs.map(t => h('button', { class: t === tab ? 'on' : '', role: 'tab', onclick: () => { tab = t; draw(); } }, t)));
  }

  function draw() {
    drawTabs();
    const l = ctx.live;
    if (tab === 'Selected') mount(body, selectedView(l));
    else if (tab === 'Filters') mount(body, filtersView(l));
    else if (tab === 'Scenario') mount(body, scen ? scenarioPanel(scen, async () => { await refreshScen(); await ctx.refresh(); map.fitData(); }) : h('div', { class: 'muted' }, ctx.mode === 'demo' ? 'Loading…' : 'Scenarios are disabled in production mode.'));
    else if (tab === 'Events') eventsView();
    map.render();
  }

  function selectedView(l) {
    if (!selected) {
      const users = l ? l.users.filter(u => u.sharing) : [];
      return h('div', { class: 'stack' },
        h('p', { class: 'muted' }, 'Click a tourist, responder, incident or zone on the map.'),
        h('h3', null, 'Tracked users (' + users.length + ')'),
        users.length ? h('table', { class: 'data' }, h('tbody', null, users.map(u => h('tr', { class: 'clickable', onclick: () => map.onPick({ kind: 'user', obj: u }) },
          h('td', null, u.display_name.replace(/^SIM · /, ''), ' ', sourceBadge(u.source)), h('td', null, statusChip(u.status)),
          h('td', { class: 'num' }, u.location ? ago(u.location.age_ms) : '—'))))) : h('div', { class: 'empty' }, 'Nobody is sharing a location right now.'));
    }
    if (!detail) return h('div', { class: 'muted' }, 'Loading…');
    if (detail.error) return h('div', { class: 'error-box' }, detail.error);
    if (selected.kind === 'user') return userView(detail);
    if (selected.kind === 'incident') return incidentView(detail);
    if (selected.kind === 'zone') return zoneView(detail);
    if (selected.kind === 'responder') {
      const r = l && l.responders.find(x => x.id === selected.id);
      if (!r) return h('div', { class: 'muted' }, 'Gone.');
      return h('div', { class: 'stack' }, h('h2', null, r.name, ' ', sourceBadge(r.source)),
        h('dl', { class: 'kv' }, h('dt', null, 'Type'), h('dd', null, r.type), h('dt', null, 'Status'), h('dd', null, r.available ? 'available' : 'assigned'),
          h('dt', null, 'Incident'), h('dd', null, r.assigned === null ? '—' : h('a', { href: '#/incidents/' + r.assigned }, '#' + r.assigned)),
          h('dt', null, 'Position'), h('dd', { class: 'mono' }, r.position[0].toFixed(5) + ', ' + r.position[1].toFixed(5)),
          h('dt', null, 'Road junction'), h('dd', { class: 'mono' }, String(r.node))),
        h('p', { class: 'faint small' }, 'Demo responders are fixtures at fixed points; their availability changes only through assignments and operator actions.'));
    }
    return null;
  }

  function userView(d) {
    const u = d.user;
    const loc = u.location;
    return h('div', { class: 'stack' },
      h('div', { class: 'row' }, h('h2', null, u.display_name), sourceBadge(u.source)),
      h('div', { class: 'row' }, u.sharing ? h('span', { class: 'tracking-on' }, h('span', { class: 'dot' }), 'Tracking active') : h('span', { class: 'tracking-off' }, 'Not sharing'),
        statusChip(u.status), u.group_name ? h('span', { class: 'chip' }, u.group_name) : null),
      h('dl', { class: 'kv' },
        h('dt', null, 'User ID'), h('dd', { class: 'mono' }, String(u.id)),
        h('dt', null, 'Coordinates'), h('dd', { class: 'mono' }, loc ? loc.lat.toFixed(5) + ', ' + loc.lon.toFixed(5) : '—'),
        h('dt', null, 'Last update'), h('dd', null, loc ? clock(loc.t_ms) + ' (' + ago(loc.age_ms) + ')' : '—'),
        h('dt', null, 'GPS accuracy'), h('dd', null, loc ? '±' + Math.round(loc.accuracy_m) + ' m' : '—'),
        h('dt', null, 'Speed'), h('dd', null, loc ? fixed(loc.speed_mps, 1) + ' m/s' : '—'),
        h('dt', null, 'Session'), h('dd', null, u.sharing ? 'since ' + clock(u.session_started_ms) + ' (#' + u.sessions + ')' : 'none'),
        h('dt', null, 'Connection'), h('dd', null, u.status === 'no contact' ? 'no contact — the server only knows the last fix' : loc ? 'last contact ' + ago(loc.age_ms) : '—'),
        h('dt', null, 'Open alerts'), h('dd', null, String(u.open_alerts)),
        h('dt', null, 'Incidents'), h('dd', null, u.incidents.length ? u.incidents.map(i => h('a', { href: '#/incidents/' + i, style: { marginRight: '6px' } }, '#' + i)) : '—'),
        h('dt', null, 'Fixes'), h('dd', { class: 'small' }, u.ingest.accepted + ' accepted · ' + u.ingest.duplicates + ' duplicate · ' + u.ingest.out_of_order + ' late · ' + u.ingest.jumps + ' jumps')),
      h('h3', null, 'Nearest hazard zones'),
      u.zones.length ? h('table', { class: 'data' }, h('tbody', null, u.zones.map(z => h('tr', null,
        h('td', null, z.name), h('td', null, sev(z.severity)), h('td', null, z.state), h('td', { class: 'num' }, z.distance_m < 0 ? Math.round(-z.distance_m) + ' m inside' : metres(z.distance_m)))))) : h('div', { class: 'faint' }, 'No zone within 3 km.'),
      h('h3', null, 'Battery model'),
      h('dl', { class: 'kv' },
        h('dt', null, 'Mode'), h('dd', null, u.power.battery_aware ? 'adaptive sampling' : 'every fix (1 Hz)'),
        h('dt', null, 'Next interval'), h('dd', null, u.power.interval_ms ? (u.power.interval_ms / 1000) + ' s' : '—'),
        h('dt', null, 'Fixes sent'), h('dd', null, u.power.fixes_sent + ' (fixed 1 Hz: ' + Math.round(u.power.fixes_at_1hz) + ')'),
        h('dt', null, 'Est. drain'), h('dd', null, fixed(u.power.pct_per_hour, 2) + ' %/h vs ' + u.power.pct_per_hour_fixed + ' %/h fixed')),
      h('p', { class: 'faint small' }, 'Model: ' + u.power.model + '.'),
      h('h3', null, 'Geofence transitions'),
      transitions(d.events),
      h('p', { class: 'faint small' }, 'Trail: ' + d.trail.points.length + ' points retained (retention ' + Math.round(d.trail.retention_ms / 60000) + ' min). Amber squares are late fixes, red are jumps, violet were synced from an offline phone. This view is recorded in the access audit log.'));
  }

  function transitions(evs) {
    const z = evs.filter(e => /^zone_|dwell|alert_raised|session|device|sync/.test(e.kind));
    if (!z.length) return h('div', { class: 'faint' }, 'None yet.');
    return h('ul', { class: 'feed' }, z.slice(0, 25).map(e => h('li', null, h('span', { class: 't' }, clock(e.t_ms).slice(3)),
      h('span', null, h('strong', null, e.kind.replace(/_/g, ' ')), e.zone_name ? ' · ' + e.zone_name : '', ' ',
        e.late ? h('span', { class: 'badge-late' }, 'LATE') : null, h('div', { class: 'faint small' }, e.detail)))));
  }

  function incidentView(d) {
    const i = d.incident;
    return h('div', { class: 'stack' },
      h('div', { class: 'row' }, h('h2', null, 'Incident #' + i.id), sev(i.severity), stateText(i.state), i.late ? h('span', { class: 'badge-late' }, 'LATE') : null),
      h('dl', { class: 'kv' },
        h('dt', null, 'People'), h('dd', null, i.affected_names.join(', ')),
        h('dt', null, 'Opened'), h('dd', null, clock(i.opened_ms)),
        h('dt', null, 'Alerts'), h('dd', null, String(i.alerts.length)),
        h('dt', null, 'Recommended'), h('dd', null, i.recommended_name ? i.recommended_name + ' · ' + metres(i.recommended_m) + ' by road' : 'none available'),
        h('dt', null, 'Assigned'), h('dd', null, i.responder_name || '—'),
        h('dt', null, 'Route'), h('dd', null, i.route ? metres(i.route.distance_m) + ' · A* settled ' + i.route.astar_expanded + ' junctions, Dijkstra ' + i.route.dijkstra_expanded : '—')),
      h('a', { href: '#/incidents/' + i.id }, 'Open the incident to acknowledge, assign or resolve →'));
  }

  function zoneView(d) {
    const z = d.zone;
    return h('div', { class: 'stack' },
      h('div', { class: 'row' }, h('h2', null, z.name), sev(z.severity), sourceBadge(z.source)),
      h('dl', { class: 'kv' },
        h('dt', null, 'Category'), h('dd', null, z.kind),
        h('dt', null, 'In force'), h('dd', null, z.active_now ? 'yes' : 'no'),
        h('dt', null, 'Validity'), h('dd', null, clock(z.active_from_ms) + ' → ' + (z.active_to_ms === null ? 'open-ended' : clock(z.active_to_ms))),
        h('dt', null, 'Dwell limit'), h('dd', null, z.max_dwell_s ? z.max_dwell_s + ' s' : 'none'),
        h('dt', null, 'Users inside'), h('dd', null, String(z.users_inside)),
        h('dt', null, 'Vertices'), h('dd', null, String(z.vertices)),
        h('dt', null, 'Incidents'), h('dd', null, d.incidents.length ? d.incidents.map(i => h('a', { href: '#/incidents/' + i.id, style: { marginRight: '6px' } }, '#' + i.id)) : '—')),
      h('a', { href: '#/zones/' + z.id }, 'Manage this zone →'));
  }

  function filtersView(l) {
    const cb = (label, checked, on) => h('label', { class: 'row', style: { gap: '6px' } }, h('input', { type: 'checkbox', checked, onchange: (e) => { on(e.target.checked); map.render(); } }), label);
    const groupsOpts = [h('option', { value: 'all' }, 'All groups')].concat((l ? l.groups : []).map(g => h('option', { value: String(g.id), selected: f.group === String(g.id) }, g.name)));
    return h('div', { class: 'stack' },
      h('h3', null, 'Hazard category'),
      h('div', { class: 'row' }, Object.keys(f.kinds).map(k => cb(k, f.kinds[k], v => { f.kinds[k] = v; }))),
      h('label', { class: 'field' }, 'Minimum zone severity',
        h('select', { onchange: (e) => { f.minSev = Number(e.target.value); map.render(); } }, [1, 2, 3, 4, 5].map(s => h('option', { value: s, selected: f.minSev === s }, String(s))))),
      h('label', { class: 'field' }, 'Group', h('select', { onchange: (e) => { f.group = e.target.value; map.render(); } }, groupsOpts)),
      h('label', { class: 'field' }, 'Tracking status', h('select', { onchange: (e) => { f.tracking = e.target.value; map.render(); } },
        [['sharing', 'Everyone sharing'], ['alerts', 'With open alerts'], ['nocontact', 'No contact']].map(([v, t]) => h('option', { value: v, selected: f.tracking === v }, t)))),
      h('label', { class: 'field' }, 'User name', h('input', { value: f.text, placeholder: 'filter by name', oninput: (e) => { f.text = e.target.value.toLowerCase(); map.render(); } })),
      h('label', { class: 'field' }, 'Incident status', h('select', { onchange: (e) => { f.incident = e.target.value; map.render(); } },
        [['open', 'Open (not resolved)'], ['all', 'All'], ['dispatched', 'Dispatched'], ['resolved', 'Resolved']].map(([v, t]) => h('option', { value: v, selected: f.incident === v }, t)))),
      cb('Show responders', f.responders, v => { f.responders = v; }),
      cb('Show selected user\'s trail', f.trails, v => { f.trails = v; if (!v) map.trails = []; else loadDetail().then(draw); }));
  }

  async function eventsView() {
    mount(body, h('div', { class: 'muted' }, 'Loading…'));
    try {
      const d = await get('/api/events?limit=60');
      if (tab !== 'Events') return;
      mount(body, h('div', { class: 'stack' }, h('p', { class: 'faint small' }, 'Newest first. L = Lamport stamp (counter@device; device 0 is the server).'),
        h('ul', { class: 'feed' }, d.events.map(e => h('li', null, h('span', { class: 't' }, clock(e.t_ms).slice(3), h('div', null, 'L' + e.lamport + '@' + e.device)),
          h('span', null, h('strong', null, e.kind.replace(/_/g, ' ')), ' ', e.user_name || '', e.zone_name ? ' · ' + e.zone_name : '', ' ',
            e.late ? h('span', { class: 'badge-late' }, 'LATE') : null, h('div', { class: 'faint small' }, e.detail)))))));
    } catch (e) { mount(body, h('div', { class: 'error-box' }, e.message)); }
  }

  async function refreshScen() {
    if (ctx.mode !== 'demo') return;
    try { scen = await get('/api/scenario'); } catch (e) { scen = null; }
  }

  let n = 0;
  const off = ctx.onLive(async (l) => {
    map.users = l.users; map.responders = l.responders; map.incidents = l.incidents; map.groups = l.groups;
    if (!fitted && l.users.some(u => u.location)) { map.fitData(); fitted = true; }
    if (selected && ++n % 2 === 0 && tab === 'Selected') { await loadDetail(); }
    if (tab === 'Scenario' && n % 2 === 0) await refreshScen();
    if (tab === 'Selected' || tab === 'Scenario') draw(); else map.render();
  });
  refreshScen().then(draw);
  draw();
  if (ctx.live) { map.users = ctx.live.users; map.responders = ctx.live.responders; map.incidents = ctx.live.incidents; map.groups = ctx.live.groups; }
  setTimeout(() => { if (!fitted) map.fitData(); }, 300);
  return { destroy() { off(); map.destroy(); } };
}
