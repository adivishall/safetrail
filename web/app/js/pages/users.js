import { get, post, patch, del } from '../api.js';
import { h, mount, clock, ago, sev, statusChip, sourceBadge, table, panel, field, toast, metres, fixed } from '../util.js';
import { MapView } from '../map.js';

export function render(root, ctx) {
  const tableBox = h('div');
  const createBox = h('div');
  let drawer = null;
  const filt = { text: '', role: 'all', status: 'all' };
  mount(root, h('div', { class: 'page' },
    h('div', { class: 'page-head' }, h('div', null, h('h1', null, 'Users'),
      h('p', null, 'Every registered account. Tourists are tracked only while they have consented and opened a session. Opening a tourist\'s detail is recorded in the access audit log.'))),
    ctx.admin ? createBox : null,
    panel(h('div', { class: 'row' }, h('h3', null, 'Accounts'),
      h('input', { placeholder: 'search name', oninput: (e) => { filt.text = e.target.value.toLowerCase(); draw(); } }),
      h('select', { onchange: (e) => { filt.role = e.target.value; draw(); } }, ['all', 'tourist', 'operator', 'responder', 'admin'].map(r => h('option', { value: r }, r === 'all' ? 'All roles' : r))),
      h('select', { onchange: (e) => { filt.status = e.target.value; draw(); } }, [['all', 'Any status'], ['sharing', 'Sharing'], ['alerts', 'With open alerts'], ['nocontact', 'No contact'], ['off', 'Not sharing']].map(([v, t]) => h('option', { value: v }, t)))),
    tableBox)));

  let users = null, err = null;
  async function load() {
    try { users = (await get('/api/users')).users; err = null; } catch (e) { err = e.message; }
    draw();
  }
  function draw() {
    if (err) return mount(tableBox, h('div', { class: 'error-box' }, err));
    if (!users) return mount(tableBox, h('div', { class: 'empty' }, 'Loading…'));
    const rows = users.filter(u => (filt.role === 'all' || u.role === filt.role) &&
      (!filt.text || u.display_name.toLowerCase().includes(filt.text) || u.username.includes(filt.text)) &&
      (filt.status === 'all' || (filt.status === 'sharing' && u.sharing) || (filt.status === 'off' && u.role === 'tourist' && !u.sharing) ||
       (filt.status === 'alerts' && u.open_alerts > 0) || (filt.status === 'nocontact' && u.status === 'no contact')));
    mount(tableBox, table(['ID', 'Name', 'Role', 'Tracking', 'Status', 'Last update', 'Accuracy', 'Group', 'Alerts', { label: 'Active' }],
      rows.map(u => h('tr', { class: 'clickable', onclick: () => open(u.id) },
        h('td', { class: 'num' }, String(u.id)),
        h('td', null, u.display_name, ' ', sourceBadge(u.source), h('div', { class: 'faint small' }, u.username)),
        h('td', null, u.role),
        h('td', null, u.role !== 'tourist' ? '—' : u.sharing ? h('span', { class: 'tracking-on' }, h('span', { class: 'dot' }), 'active') : h('span', { class: 'tracking-off' }, u.consent ? 'consented, off' : 'no consent')),
        h('td', null, u.role === 'tourist' ? statusChip(u.status) : '—'),
        h('td', null, u.location ? ago(u.location.age_ms) : '—'),
        h('td', { class: 'num' }, u.location ? '±' + Math.round(u.location.accuracy_m) + ' m' : '—'),
        h('td', null, u.group_name || '—'),
        h('td', { class: 'num' }, u.open_alerts ? sev(u.worst_severity) : '—'),
        h('td', null, u.active ? 'yes' : 'no'))), { empty: 'No matching users.', maxHeight: '64vh' }));
  }

  async function open(id) {
    if (drawer) drawer.close();
    drawer = userDrawer(id, ctx, () => { drawer = null; load(); });
  }

  if (ctx.admin) {
    const f = { username: h('input', { placeholder: 'e.g. asha.k' }), display: h('input', { placeholder: 'Display name' }),
      role: h('select', null, ['tourist', 'operator', 'responder', 'admin'].map(r => h('option', { value: r }, r))),
      password: h('input', { type: 'password', placeholder: 'at least 10 characters', autocomplete: 'new-password' }) };
    mount(createBox, panel('Register a user', h('form', { class: 'form-grid', onsubmit: async (e) => {
      e.preventDefault();
      try {
        await post('/api/users', { username: f.username.value.trim(), display_name: f.display.value.trim(), role: f.role.value, password: f.password.value });
        toast('User registered', 'ok'); f.username.value = f.display.value = f.password.value = ''; load();
      } catch (ex) { toast(ex.message, 'error'); }
    } }, field('Username', f.username), field('Display name', f.display), field('Role', f.role), field('Password', f.password),
      h('button', { class: 'primary', type: 'submit' }, 'Register'))));
    createBox.style.marginBottom = '12px';
  }
  load();
  const off = ctx.onLive(() => { if (!drawer) load(); });
  return { destroy() { off(); if (drawer) drawer.close(); } };
}

export function userDrawer(id, ctx, onClose) {
  const scroll = h('div', { class: 'scroll' });
  const el = h('aside', { class: 'drawer', role: 'dialog', 'aria-label': 'User detail' },
    h('header', null, h('h2', null, 'User #' + id), h('button', { class: 'ghost', onclick: () => close() }, 'Close')), scroll);
  document.body.appendChild(el);
  let map = null;
  function close() { if (map) map.destroy(); el.remove(); if (onClose) onClose(); }
  const keys = (e) => { if (e.key === 'Escape') close(); };
  document.addEventListener('keydown', keys, { once: true });

  async function load() {
    try {
      const [{ user: u }, ev] = await Promise.all([get('/api/users/' + id), get('/api/events?limit=60&user=' + id)]);
      const trail = u.role === 'tourist' && u.location_visible ? await get('/api/users/' + id + '/trail') : null;
      const actions = h('div', { class: 'row' });
      if (ctx.admin && u.username !== ctx.me.username)
        actions.append(h('button', { onclick: async () => { try { await patch('/api/users/' + id, { active: !u.active }); toast(u.active ? 'Deactivated' : 'Activated', 'ok'); load(); } catch (e) { toast(e.message, 'error'); } } }, u.active ? 'Deactivate' : 'Activate'));
      if (ctx.admin && u.role === 'tourist')
        actions.append(h('button', { class: 'danger', onclick: async () => { if (!confirm('Erase this user\'s movement history? Open alerts keep their positions.')) return; try { await del('/api/users/' + id + '/history'); toast('History erased', 'ok'); load(); } catch (e) { toast(e.message, 'error'); } } }, 'Erase movement history'));
      const mapHost = h('div', { style: { height: '260px' } });
      mount(scroll, h('div', { class: 'stack' },
        h('div', { class: 'row' }, h('h2', null, u.display_name), sourceBadge(u.source), h('span', { class: 'chip' }, u.role), u.active ? null : h('span', { class: 'chip' }, 'deactivated')),
        u.simulated ? h('div', { class: 'note sim' }, 'This is a simulated person created by the scenario runner.') : null,
        actions,
        u.role === 'tourist' ? h('dl', { class: 'kv' },
          h('dt', null, 'Consent'), h('dd', null, u.consent ? 'given' : 'not given'),
          h('dt', null, 'Tracking'), h('dd', null, u.sharing ? h('span', { class: 'tracking-on' }, h('span', { class: 'dot' }), 'active since ' + clock(u.session_started_ms)) : 'off'),
          h('dt', null, 'Sessions'), h('dd', null, String(u.sessions)),
          h('dt', null, 'Status'), h('dd', null, statusChip(u.status)),
          h('dt', null, 'Location'), h('dd', { class: 'mono' }, u.location ? u.location.lat.toFixed(5) + ', ' + u.location.lon.toFixed(5) + ' ±' + Math.round(u.location.accuracy_m) + ' m' : u.location_visible ? '—' : 'not visible to you'),
          h('dt', null, 'Last update'), h('dd', null, u.location ? clock(u.location.t_ms) + ' (' + ago(u.location.age_ms) + ')' : '—'),
          h('dt', null, 'Group'), h('dd', null, u.group_name || '—'),
          h('dt', null, 'Open alerts'), h('dd', null, String(u.open_alerts)),
          h('dt', null, 'Incidents'), h('dd', null, u.incidents.length ? u.incidents.map(i => h('a', { href: '#/incidents/' + i, style: { marginRight: '6px' } }, '#' + i)) : '—'),
          u.ingest ? [h('dt', null, 'Fixes'), h('dd', null, `${u.ingest.accepted} accepted, ${u.ingest.duplicates} duplicates, ${u.ingest.out_of_order} out of order, ${u.ingest.invalid} invalid, ${u.ingest.jumps} jumps`)] : null,
          u.power ? [h('dt', null, 'Battery model'), h('dd', null, `${fixed(u.power.pct_per_hour, 2)} %/h (${u.power.fixes_sent} fixes; fixed 1 Hz would send ${Math.round(u.power.fixes_at_1hz)})`)] : null)
          : h('dl', { class: 'kv' }, h('dt', null, 'Username'), h('dd', null, u.username), h('dt', null, 'Created'), h('dd', null, clock(u.created_ms))),
        u.zones && u.zones.length ? [h('h3', null, 'Zones near this user'), table(['Zone', 'Sev', 'State', 'Distance'], u.zones.map(z => h('tr', null, h('td', null, z.name), h('td', null, sev(z.severity)), h('td', null, z.state), h('td', { class: 'num' }, z.distance_m < 0 ? Math.round(-z.distance_m) + ' m inside' : metres(z.distance_m)))))] : null,
        trail ? [h('h3', null, 'Movement history (' + trail.points.length + ' points)'), mapHost] : null,
        h('h3', null, 'History'),
        ev.events.length ? h('ul', { class: 'feed' }, ev.events.map(e => h('li', null, h('span', { class: 't' }, clock(e.t_ms).slice(3)),
          h('span', null, h('strong', null, e.kind.replace(/_/g, ' ')), e.zone_name ? ' · ' + e.zone_name : '', ' ', e.late ? h('span', { class: 'badge-late' }, 'LATE') : null,
            h('div', { class: 'faint small' }, e.detail))))) : h('div', { class: 'faint' }, 'No events.')));
      if (trail) {
        if (map) map.destroy();
        map = new MapView(mapHost, { legend: false });
        const zones = await get('/api/zones');
        map.zones = zones.zones;
        map.trails = [{ points: trail.points, colour: '#3fb6a8' }];
        if (u.location) map.users = [u];
        if (trail.points.length) map.fitPoints(trail.points.map(p => [p[1], p[2]]), 30);
        map.render();
      }
    } catch (e) { mount(scroll, h('div', { class: 'error-box' }, e.message)); }
  }
  load();
  return { close };
}
