import { get, post, patch } from '../api.js';
import { h, mount, clock, sev, stateText, panel, toast, metres } from '../util.js';
import { MapView } from '../map.js';

// A responder's own view: the incidents assigned to their unit, the route,
// the people involved (visible only while assigned), and their availability.
export function render(root, ctx) {
  const listBox = h('div', { class: 'stack' });
  const mapHost = h('div', { style: { height: '480px' } });
  mount(root, h('div', { class: 'page' },
    h('div', { class: 'page-head' }, h('div', null, h('h1', null, 'My Assignments'),
      h('p', null, 'Incidents assigned to your unit. You can see the location of the people in an incident only while it is assigned to you; every such view is audited.'))),
    h('div', { class: 'grid cols-main-side' }, panel('Route', mapHost), listBox)));
  const map = new MapView(mapHost);
  get('/api/zones').then(z => { map.zones = z.zones; map.render(); });
  let fitted = false;

  const off = ctx.onLive((l) => {
    const unit = ctx.me.unit;
    const mineR = l.responders.find(r => r.id === unit);
    const inc = l.incidents.filter(i => i.responder === unit && i.state !== 'resolved');
    map.responders = mineR ? [mineR] : [];
    map.incidents = inc;
    map.users = l.users.filter(u => u.location);
    map.routes = inc.filter(i => i.route).map(i => ({ points: i.route.points, colour: '#e3e8ee' }));
    if (!fitted && mineR) { map.fitPoints([mineR.position].concat(inc.map(i => i.centroid)), 40, 0.01); fitted = true; }
    map.render();
    const act = async (id, what, body) => { try { await post('/api/incidents/' + id + '/' + what, body || {}); toast('Incident #' + id + ' updated', 'ok'); ctx.refresh(); } catch (e) { toast(e.message, 'error'); } };
    mount(listBox,
      panel('My unit', mineR ? h('div', { class: 'stack' }, h('div', null, h('strong', null, mineR.name), ' — ', mineR.available ? 'available' : 'assigned'),
        mineR.assigned === null ? h('button', { onclick: async () => { try { await patch('/api/responders/' + unit, { available: !mineR.available }); ctx.refresh(); } catch (e) { toast(e.message, 'error'); } } }, mineR.available ? 'Go off duty' : 'Go on duty') : null)
        : h('p', { class: 'muted' }, 'Your account is not linked to a responder unit.')),
      inc.length ? inc.map(i => panel(h('div', { class: 'row' }, h('h2', null, 'Incident #' + i.id), sev(i.severity), stateText(i.state)), h('div', { class: 'stack' },
        h('dl', { class: 'kv' }, h('dt', null, 'People'), h('dd', null, i.affected_names.join(', ')), h('dt', null, 'Opened'), h('dd', null, clock(i.opened_ms)),
          h('dt', null, 'Route'), h('dd', null, i.route && i.route.found ? metres(i.route.distance_m) + ' by road' : '—')),
        h('div', { class: 'row' }, i.acknowledged_ms === null ? h('button', { onclick: () => act(i.id, 'ack') }, 'Acknowledge') : h('span', { class: 'chip' }, 'acknowledged'),
          h('button', { class: 'primary', onclick: () => { const note = prompt('Resolution note', 'on scene, person safe'); if (note !== null) act(i.id, 'resolve', { note }); } }, 'Resolve')))))
        : panel('Assignments', h('div', { class: 'empty' }, 'No incident is assigned to your unit.')));
  });
  return { destroy() { off(); map.destroy(); } };
}
