import { get, post, patch } from '../api.js';
import { h, mount, clock, sourceBadge, table, panel, field, toast } from '../util.js';
import { MapView } from '../map.js';

export function render(root, ctx) {
  const box = h('div');
  const mapHost = h('div', { style: { height: '520px' } });
  const form = h('div');
  mount(root, h('div', { class: 'page' },
    h('div', { class: 'page-head' }, h('div', null, h('h1', null, 'Responders'),
      h('p', null, 'Units that can be sent to incidents. Each is snapped to its nearest road junction with the k-d tree (O(log n) instead of a scan of 144,090 junctions). Availability changes when a unit is assigned or freed — it is never invented.'))),
    h('div', { class: 'grid cols-main-side' },
      h('div', { class: 'stack' }, panel('Units', box), panel('Map', mapHost)),
      h('div', { class: 'stack' }, form))));
  const map = new MapView(mapHost);
  get('/api/zones').then(z => { map.zones = z.zones; map.render(); });
  let picking = null;
  map.onClickMap = (lat, lon) => { if (!picking) return false; picking(lat, lon); return true; };
  let fitted = false;

  function draw(list) {
    mount(box, table(['ID', 'Name', 'Type', 'Status', 'Incident', 'Position', 'Junction', ''], list.map(r => h('tr', null,
      h('td', { class: 'num' }, String(r.id)), h('td', null, r.name, ' ', sourceBadge(r.source)), h('td', null, r.type),
      h('td', null, r.available ? 'available' : r.assigned !== null ? 'assigned' : 'unavailable'),
      h('td', null, r.assigned === null ? '—' : h('a', { href: '#/incidents/' + r.assigned }, '#' + r.assigned)),
      h('td', { class: 'mono small' }, r.position[0].toFixed(4) + ', ' + r.position[1].toFixed(4)),
      h('td', { class: 'mono small' }, String(r.node)),
      h('td', null, r.assigned === null ? h('button', { class: 'small', onclick: async () => {
        try { await patch('/api/responders/' + r.id, { available: !r.available }); toast(r.name + (r.available ? ' marked unavailable' : ' available'), 'ok'); ctx.refresh(); }
        catch (e) { toast(e.message, 'error'); } } }, r.available ? 'Set unavailable' : 'Set available') : null))), { empty: 'No responders registered.' }));
    map.responders = list;
    if (!fitted && list.length) { map.fitPoints(list.map(r => r.position), 40); fitted = true; }
    map.render();
  }

  if (ctx.admin) {
    const name = h('input', { placeholder: 'Unit name' });
    const type = h('select', null, ['medical', 'rescue', 'police', 'fire'].map(t => h('option', { value: t }, t)));
    const lat = h('input', { type: 'number', step: 'any', placeholder: 'latitude' });
    const lon = h('input', { type: 'number', step: 'any', placeholder: 'longitude' });
    mount(form, panel('Register a responder', h('div', { class: 'stack' },
      h('div', { class: 'form-grid' }, field('Name', name), field('Type', type), field('Latitude', lat), field('Longitude', lon)),
      h('div', { class: 'row' }, h('button', { onclick: () => { picking = (a, b) => { lat.value = a.toFixed(6); lon.value = b.toFixed(6); picking = null; map.points = [{ lat: a, lon: b, colour: '#3fb6a8', r: 6 }]; map.render(); }; toast('Click the map to place the unit'); } }, 'Pick on map'),
        h('button', { class: 'primary', onclick: async () => {
          try { await post('/api/responders', { name: name.value.trim(), type: type.value, lat: Number(lat.value), lon: Number(lon.value) }); toast('Responder registered and snapped to the road network', 'ok'); name.value = ''; map.points = []; ctx.refresh(); }
          catch (e) { toast(e.message, 'error'); } } }, 'Register')))));
  } else {
    mount(form, panel('Register a responder', h('p', { class: 'muted' }, 'Only administrators register responders.')));
  }

  const off = ctx.onLive((l) => { draw(l.responders); map.incidents = l.incidents.filter(i => i.state !== 'resolved'); map.routes = l.incidents.filter(i => i.route && i.state !== 'resolved').map(i => ({ points: i.route.points, colour: '#6c9bd2', width: 2.5 })); map.render(); });
  if (ctx.live) draw(ctx.live.responders);
  return { destroy() { off(); map.destroy(); } };
}
