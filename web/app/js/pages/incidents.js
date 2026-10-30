import { get, post } from '../api.js';
import { h, mount, clock, sev, sourceBadge, stateText, table, panel, toast, metres } from '../util.js';
import { MapView } from '../map.js';

export function render(root, ctx) {
  const listBox = h('div');
  const detailBox = h('div', { class: 'stack' });
  const planBox = h('div');
  mount(root, h('div', { class: 'page' },
    h('div', { class: 'page-head' }, h('div', null, h('h1', null, 'Incidents'),
      h('p', null, 'Alerts that need a response are correlated into incidents: alerts within 200 m and 10 minutes of each other are joined by union-find into one card. Each incident gets the nearest available responder by road distance (one Dijkstra from the incident reaches every responder); assigning it routes the responder with A*.'))),
    h('div', { class: 'grid cols-main-side' },
      h('div', { class: 'stack' }, panel('Incidents', listBox), panel('Batch dispatch: greedy vs optimal', planBox)),
      detailBox)));
  let selected = Number(location.hash.split('/')[2]);
  if (isNaN(selected)) selected = null;
  let list = null;
  let map = null;

  async function loadList() {
    try { list = (await get('/api/incidents')).incidents; drawList(); } catch (e) { mount(listBox, h('div', { class: 'error-box' }, e.message)); }
  }
  function drawList() {
    mount(listBox, table(['#', 'Sev', 'People', 'State', 'Recommended', 'Assigned', 'Opened'], list.map(i =>
      h('tr', { class: 'clickable' + (i.id === selected ? ' selected' : ''), onclick: () => { selected = i.id; history.replaceState(null, '', '#/incidents/' + i.id); drawList(); loadDetail(); } },
        h('td', { class: 'num' }, '#' + i.id), h('td', null, sev(i.severity)),
        h('td', null, i.affected_names.join(', '), ' ', i.late ? h('span', { class: 'badge-late' }, 'LATE') : null),
        h('td', null, stateText(i.state)),
        h('td', null, i.state === 'resolved' ? '—' : i.recommended_name ? i.recommended_name + ' · ' + metres(i.recommended_m) : h('span', { class: 'faint' }, 'none free')),
        h('td', null, i.responder_name || '—'), h('td', { class: 'mono' }, clock(i.opened_ms)))), { empty: 'No incidents yet.', maxHeight: '44vh' }));
  }

  async function loadDetail() {
    if (selected === null) { mount(detailBox, panel('Incident', h('p', { class: 'muted' }, 'Select an incident.'))); return; }
    try {
      const [d, r, z] = await Promise.all([get('/api/incidents/' + selected), get('/api/responders'), get('/api/zones')]);
      drawDetail(d, r.responders, z.zones);
    } catch (e) { mount(detailBox, h('div', { class: 'error-box' }, e.message)); }
  }

  function drawDetail(d, responders, zones) {
    const i = d.incident;
    const act = async (path, body) => {
      try { await post('/api/incidents/' + i.id + '/' + path, body || {}); toast('Incident #' + i.id + ' updated', 'ok'); await loadList(); await loadDetail(); }
      catch (e) { toast(e.message, 'error'); }
    };
    const choose = h('select', null, responders.filter(r => r.available || r.id === i.responder).map(r =>
      h('option', { value: r.id, selected: r.id === (i.responder ?? i.recommended) }, r.name + (r.id === i.recommended ? ' (recommended)' : ''))));
    const note = h('input', { placeholder: 'resolution note', style: { flex: 1 } });
    const mapHost = h('div', { style: { height: '300px' } });
    const steps = ['open', 'acknowledged', 'dispatched', 'resolved'];
    const reached = new Set(i.history.map(x => x[1]));
    mount(detailBox,
      panel(h('div', { class: 'row' }, h('h2', null, 'Incident #' + i.id), sev(i.severity), stateText(i.state), i.late ? h('span', { class: 'badge-late' }, 'LATE') : null), h('div', { class: 'stack' },
        h('div', { class: 'row small' }, steps.map((s, k) => h('span', { class: 'chip', style: reached.has(s) ? { borderColor: 'var(--accent)', color: 'var(--text)' } : null }, (k + 1) + '. ' + s))),
        h('dl', { class: 'kv' },
          h('dt', null, 'People affected'), h('dd', null, i.affected_names.join(', ')),
          h('dt', null, 'Opened'), h('dd', null, clock(i.opened_ms)), h('dt', null, 'Last alert'), h('dd', null, clock(i.updated_ms)),
          h('dt', null, 'Area'), h('dd', null, 'radius ' + metres(i.radius_m)),
          h('dt', null, 'Recommended'), h('dd', null, i.recommended_name ? i.recommended_name + ' — ' + metres(i.recommended_m) + ' by road' : 'no responder available'),
          h('dt', null, 'Assigned'), h('dd', null, i.responder_name || '—'),
          h('dt', null, 'Route'), h('dd', null, i.route ? (i.route.found ? metres(i.route.distance_m) + ' along ' + i.route.points.length + ' junctions' : 'no road route') : '—'),
          i.route ? [h('dt', null, 'Search effort'), h('dd', null, 'A* settled ' + i.route.astar_expanded + ' junctions; Dijkstra needs ' + i.route.dijkstra_expanded + ' for the same pair')] : null,
          h('dt', null, 'Travel time'), h('dd', { class: 'faint' }, 'not estimated: the road file has lengths, not speeds'),
          i.resolution ? [h('dt', null, 'Resolution'), h('dd', null, i.resolution)] : null),
        i.state !== 'resolved' ? h('div', { class: 'stack' },
          h('div', { class: 'row' }, i.acknowledged_ms === null ? h('button', { onclick: () => act('ack') }, 'Acknowledge') : h('span', { class: 'chip' }, 'acknowledged ' + clock(i.acknowledged_ms)),
            choose, h('button', { class: 'primary', disabled: !choose.options.length, onclick: () => act('assign', { responder: Number(choose.value) }) }, i.responder === null ? 'Assign' : 'Reassign')),
          h('div', { class: 'row' }, note, h('button', { onclick: () => act('resolve', { note: note.value }) }, 'Resolve'))) : null,
        mapHost)),
      panel('Alerts in this incident', table(['Alert', 'Kind', 'Sev', 'User', 'Raised', 'State'], d.alerts.map(a => h('tr', null,
        h('td', { class: 'num' }, '#' + a.id), h('td', null, a.kind.toLowerCase(), ' ', a.late ? h('span', { class: 'badge-late' }, 'LATE') : null), h('td', null, sev(a.severity)),
        h('td', null, a.user_name, ' ', sourceBadge(a.source)), h('td', { class: 'mono' }, clock(a.raised_ms)), h('td', null, stateText(a.state)))))),
      panel('Timeline (evidence log)', h('ul', { class: 'feed' }, d.timeline.map(e => h('li', null, h('span', { class: 't' }, clock(e.t_ms).slice(3)),
        h('span', null, h('strong', null, e.kind.replace(/_/g, ' ')), ' ', e.late ? h('span', { class: 'badge-late' }, 'LATE') : null,
          h('div', { class: 'faint small' }, e.detail, ' · evidence #' + e.evidence_index)))))));
    if (map) map.destroy();
    map = new MapView(mapHost, { legend: false });
    map.zones = zones;
    map.incidents = [i];
    map.responders = responders;
    if (i.route) map.routes = [{ points: i.route.points, colour: '#e3e8ee' }];
    const users = (ctx.live ? ctx.live.users : []).filter(u => i.affected.includes(u.id));
    map.users = users;
    const pts = [i.centroid].concat(i.route ? i.route.points : []);
    const rec = responders.find(r => r.id === i.recommended);
    if (rec) pts.push(rec.position);
    map.fitPoints(pts, 30, 0.01);
  }

  async function drawPlan(result) {
    const run = async (apply) => {
      try { const r = await post('/api/dispatch/plan', { apply }); if (apply) { toast('Optimal plan applied', 'ok'); await loadList(); } drawPlan(r); }
      catch (e) { toast(e.message, 'error'); }
    };
    const body = [h('p', { class: 'small muted' }, 'For every open, unassigned incident and every free responder: build the road-distance cost matrix (Dijkstra), then compare greedy cheapest-pair-first against the Hungarian algorithm\'s minimum total.'),
      h('div', { class: 'row' }, h('button', { onclick: () => run(false) }, 'Compare'), h('button', { class: 'primary', onclick: () => { if (confirm('Assign every pair in the optimal plan?')) run(true); } }, 'Apply optimal plan'))];
    if (result) {
      if (!result.incidents.length || !result.responders.length) body.push(h('div', { class: 'faint' }, 'Nothing to plan: ' + result.incidents.length + ' unassigned incidents, ' + result.responders.length + ' free responders.'));
      else body.push(h('dl', { class: 'kv' },
        h('dt', null, 'Greedy total'), h('dd', null, metres(result.greedy.total_m) + ' (longest ' + metres(result.greedy.makespan_m) + ')'),
        h('dt', null, 'Hungarian total'), h('dd', null, metres(result.optimal.total_m) + ' (longest ' + metres(result.optimal.makespan_m) + ')'),
        h('dt', null, 'Saved'), h('dd', null, metres(result.greedy.total_m - result.optimal.total_m)),
        h('dt', null, 'Uncovered'), h('dd', null, String(result.optimal.unassigned))));
    }
    mount(planBox, h('div', { class: 'stack' }, body));
  }

  loadList(); loadDetail(); drawPlan(null);
  let n = 0;
  const off = ctx.onLive(() => { if (++n % 3 === 0) loadList(); });
  return { destroy() { off(); if (map) map.destroy(); } };
}
