import { get, post, patch } from '../api.js';
import { h, mount, clock, metres, panel, field, toast, sourceBadge, statusChip, table } from '../util.js';
import { MapView } from '../map.js';

export function render(root, ctx) {
  const listBox = h('div', { class: 'stack' });
  const mapHost = h('div', { style: { height: '520px' } });
  const name = h('input', { placeholder: 'e.g. Laitlum Day Trek' });
  mount(root, h('div', { class: 'page' },
    h('div', { class: 'page-head' }, h('div', null, h('h1', null, 'Groups'),
      h('p', null, 'A tour party is a declared group. Every 10 s each group\'s tracked members are joined into connected components by union-find over the proximity graph (members within the proximity distance are linked). More than one component means the party has split; a member outside the largest component and beyond the straggler distance, for longer than the confirmation time, raises a Straggler alert.'))),
    h('form', { class: 'row', style: { marginBottom: '12px' }, onsubmit: async (e) => {
      e.preventDefault();
      try { await post('/api/groups', { name: name.value.trim() }); name.value = ''; toast('Group created', 'ok'); load(); } catch (ex) { toast(ex.message, 'error'); }
    } }, name, h('button', { class: 'primary', type: 'submit' }, 'Create group')),
    h('div', { class: 'grid cols-main-side' }, listBox, panel('Group map', mapHost))));
  const map = new MapView(mapHost, { legend: false });
  get('/api/zones').then(z => { map.zones = z.zones; map.render(); });
  let groups = null, users = [], focus = null;

  async function load() {
    try {
      const [g, u] = await Promise.all([get('/api/groups'), get('/api/users')]);
      groups = g.groups; users = u.users.filter(x => x.role === 'tourist');
      draw();
    } catch (e) { mount(listBox, h('div', { class: 'error-box' }, e.message)); }
  }
  function draw() {
    if (!groups) return;
    if (!groups.length) { mount(listBox, h('div', { class: 'empty panel' }, 'No groups yet. Create one above, or load a scenario.')); return; }
    mount(listBox, groups.map(g => groupCard(g)));
    const live = ctx.live;
    if (live) {
      const shown = focus === null ? live.users : live.users.filter(u => u.group === focus);
      map.users = shown; map.groups = focus === null ? live.groups : live.groups.filter(x => x.id === focus);
      map.render();
    }
  }
  function groupCard(g) {
    const byId = new Map(users.map(u => [u.id, u]));
    const add = h('select', null, [h('option', { value: '' }, 'Add a tourist…')].concat(users.filter(u => u.group !== g.id).map(u => h('option', { value: u.id }, u.display_name))));
    const prox = h('input', { type: 'number', min: 10, max: 5000, value: g.proximity_m, style: { width: '90px' } });
    const strag = h('input', { type: 'number', min: 10, max: 20000, value: g.straggler_m, style: { width: '90px' } });
    const conf = h('input', { type: 'number', min: 0, max: 3600, value: g.confirm_ms / 1000, style: { width: '80px' } });
    return panel(h('div', { class: 'row' }, h('h2', null, g.name), sourceBadge(g.source),
      g.fragmented ? h('span', { class: 'chip', style: { color: 'var(--sev-4)' } }, 'split into ' + g.components) : h('span', { class: 'chip' }, g.tracked >= 2 ? 'together' : 'too few tracked')),
    h('div', { class: 'stack' },
      h('div', { class: 'row small muted' }, g.members.length + ' members · ' + g.tracked + ' tracked now · centre ' + (g.centre ? g.centre[0].toFixed(4) + ', ' + g.centre[1].toFixed(4) : '—'),
        h('button', { class: 'small', onclick: () => { focus = focus === g.id ? null : g.id; draw(); if (g.centre) map.fitPoints([g.centre].concat(ctx.live.users.filter(u => u.group === g.id && u.location).map(u => [u.location.lat, u.location.lon])), 40, 0.006); } }, focus === g.id ? 'Show all' : 'Show on map')),
      g.stragglers.length ? h('div', { class: 'note', style: { borderLeftColor: 'var(--sev-4)' } }, g.stragglers.map(s => h('div', null,
        (byId.get(s.user) || { display_name: '#' + s.user }).display_name + ' is ' + metres(s.separation_m) + ' from the group, separated since ' + clock(s.since_ms) + (s.alerted ? ' — alert raised' : ' — confirming')))) : null,
      table(['Member', 'Status', 'Tracking', ''], g.members.map(id => { const u = byId.get(id); return h('tr', null,
        h('td', null, u ? u.display_name : '#' + id, ' ', u ? sourceBadge(u.source) : null), h('td', null, u ? statusChip(u.status) : '—'),
        h('td', null, u && u.sharing ? 'sharing' : 'off'),
        h('td', null, h('button', { class: 'small ghost', onclick: async () => { try { await post('/api/groups/' + g.id + '/members', { user: id, action: 'remove' }); load(); } catch (e) { toast(e.message, 'error'); } } }, 'Remove'))); }), { empty: 'No members.' }),
      h('div', { class: 'row' }, add, h('button', { onclick: async () => { if (!add.value) return; try { await post('/api/groups/' + g.id + '/members', { user: Number(add.value), action: 'add' }); load(); } catch (e) { toast(e.message, 'error'); } } }, 'Add')),
      h('div', { class: 'row' }, field('Proximity m', prox), field('Straggler m', strag), field('Confirm s', conf),
        h('button', { onclick: async () => { try { await patch('/api/groups/' + g.id, { proximity_m: Number(prox.value), straggler_m: Number(strag.value), confirm_s: Number(conf.value) }); toast('Thresholds saved; monitoring restarts for this group', 'ok'); load(); } catch (e) { toast(e.message, 'error'); } } }, 'Save thresholds'))));
  }
  load();
  let n = 0;
  const off = ctx.onLive(() => { if (++n % 3 === 0) load(); else draw(); });
  return { destroy() { off(); map.destroy(); } };
}
