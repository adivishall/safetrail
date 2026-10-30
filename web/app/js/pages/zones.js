import { get, post, patch, del, api } from '../api.js';
import { h, mount, clock, sev, sourceBadge, table, panel, field, toast, int } from '../util.js';
import { MapView } from '../map.js';

export function render(root, ctx) {
  const mapHost = h('div', { style: { height: '560px' } });
  const listBox = h('div');
  const side = h('div', { class: 'stack' });
  const filt = { kind: 'all', text: '', active: 'all' };
  mount(root, h('div', { class: 'page' },
    h('div', { class: 'page-head' }, h('div', null, h('h1', null, 'Hazard Zones'),
      h('p', null, 'Polygons from OpenStreetMap plus any added here. Every change is a new version of the persistent quadtree; the history of each zone stays queryable. Validity windows switch a zone on and off without deleting it.'))),
    h('div', { class: 'grid cols-main-side' },
      h('div', { class: 'stack' }, panel('Map', mapHost), panel(h('div', { class: 'row' }, h('h3', null, 'Zones'),
        h('input', { placeholder: 'search', oninput: (e) => { filt.text = e.target.value.toLowerCase(); drawList(); } }),
        h('select', { onchange: (e) => { filt.kind = e.target.value; drawList(); map.render(); } }, ['all', 'restricted', 'caution', 'advisory', 'safe'].map(k => h('option', { value: k }, k === 'all' ? 'All categories' : k))),
        h('select', { onchange: (e) => { filt.active = e.target.value; drawList(); } }, [['all', 'Any validity'], ['on', 'In force now'], ['off', 'Not in force']].map(([v, t]) => h('option', { value: v }, t)))), listBox)),
      side)));
  const map = new MapView(mapHost);
  map.filter = (kind, z) => kind !== 'zone' || filt.kind === 'all' || z.kind === filt.kind;
  let zones = null, selectedId = null;
  const routeId = Number((location.hash.split('/')[2]) || NaN);
  if (!isNaN(routeId)) selectedId = routeId;

  async function load(refit) {
    try {
      zones = (await get('/api/zones')).zones;
      map.zones = zones;
      if (refit) map.fitData();
      drawList();
      if (selectedId !== null) select(selectedId); else drawSide();
    } catch (e) { mount(listBox, h('div', { class: 'error-box' }, e.message)); }
  }
  function drawList() {
    if (!zones) return;
    const rows = zones.filter(z => (filt.kind === 'all' || z.kind === filt.kind) && (!filt.text || z.name.toLowerCase().includes(filt.text)) &&
      (filt.active === 'all' || (filt.active === 'on') === z.active_now));
    mount(listBox, table(['ID', 'Name', 'Category', 'Sev', 'In force', 'Validity', 'Dwell', 'Inside', 'Vertices'], rows.map(z =>
      h('tr', { class: 'clickable' + (z.id === selectedId ? ' selected' : ''), onclick: () => select(z.id) },
        h('td', { class: 'num' }, String(z.id)), h('td', null, z.name, ' ', sourceBadge(z.source)), h('td', null, z.kind), h('td', null, sev(z.severity)),
        h('td', null, z.active_now ? 'yes' : 'no'),
        h('td', { class: 'small' }, (z.active_from_ms ? clock(z.active_from_ms) : 'start') + ' → ' + (z.active_to_ms === null ? 'open' : clock(z.active_to_ms))),
        h('td', { class: 'num' }, z.max_dwell_s ? z.max_dwell_s + ' s' : '—'),
        h('td', { class: 'num' }, String(z.users_inside)), h('td', { class: 'num' }, int(z.vertices)))), { empty: 'No zones match.', maxHeight: '40vh' }));
  }

  map.onPick = (p) => { if (p && p.kind === 'zone') select(p.obj.id); };

  async function select(id) {
    selectedId = id;
    map.highlightZone = id;
    map.render();
    drawList();
    try {
      const d = await get('/api/zones/' + id);
      drawSide(d);
    } catch (e) { mount(side, h('div', { class: 'error-box' }, e.message)); }
  }

  function drawSide(d) {
    const parts = [];
    if (ctx.admin) parts.push(createPanel(), importPanel());
    if (d) parts.unshift(detailPanel(d));
    else parts.unshift(panel('Zone', h('p', { class: 'muted' }, 'Select a zone on the map or in the table.')));
    mount(side, parts);
  }

  function detailPanel(d) {
    const z = d.zone;
    const name = h('input', { value: z.name });
    const kind = h('select', null, ['restricted', 'caution', 'advisory', 'safe'].map(k => h('option', { value: k, selected: z.kind === k }, k)));
    const severity = h('select', null, [1, 2, 3, 4, 5].map(s => h('option', { value: s, selected: z.severity === s }, String(s))));
    const dwell = h('input', { type: 'number', min: 0, value: z.max_dwell_s, style: { width: '100px' } });
    const from = h('input', { type: 'number', min: 0, value: Math.round(z.active_from_ms / 1000), style: { width: '100px' } });
    const to = h('input', { type: 'number', min: 0, value: z.active_to_ms === null ? '' : Math.round(z.active_to_ms / 1000), placeholder: 'open-ended', style: { width: '100px' } });
    const save = async () => {
      try {
        await patch('/api/zones/' + z.id, { name: name.value, kind: kind.value, severity: Number(severity.value), max_dwell_s: Number(dwell.value),
          active_from_ms: Number(from.value) * 1000, active_to_ms: to.value === '' ? null : Number(to.value) * 1000, ring: z.ring });
        toast('Zone saved as a new version', 'ok'); load();
      } catch (e) { toast(e.message, 'error'); }
    };
    return panel(h('div', { class: 'row' }, h('h2', null, z.name), sev(z.severity), sourceBadge(z.source)), h('div', { class: 'stack' },
      h('dl', { class: 'kv' },
        h('dt', null, 'Category'), h('dd', null, z.kind), h('dt', null, 'In force now'), h('dd', null, z.active_now ? 'yes' : 'no'),
        h('dt', null, 'Users inside'), h('dd', null, String(z.users_inside)), h('dt', null, 'Area'), h('dd', null, int(z.area_m2) + ' m²'),
        h('dt', null, 'Vertices'), h('dd', null, String(z.vertices))),
      ctx.admin ? h('div', { class: 'stack' },
        h('div', { class: 'form-grid' }, field('Name', name), field('Category', kind), field('Severity', severity), field('Dwell limit (s)', dwell),
          field('In force from (s)', from), field('until (s)', to)),
        h('div', { class: 'row' }, h('button', { class: 'primary', onclick: save }, 'Save'),
          h('button', { onclick: async () => { try { await post('/api/zones/' + z.id + '/validity', { active: !z.active_now }); toast(z.active_now ? 'Zone deactivated (validity closed now)' : 'Zone activated', 'ok'); load(); } catch (e) { toast(e.message, 'error'); } } }, z.active_now ? 'Deactivate now' : 'Activate now'),
          h('button', { class: 'danger', onclick: async () => { if (!confirm('Delete "' + z.name + '"? It stays in the history.')) return; try { await del('/api/zones/' + z.id); selectedId = null; toast('Zone deleted', 'ok'); load(); } catch (e) { toast(e.message, 'error'); } } }, 'Delete'))) : null,
      h('h3', null, 'Version history (persistent quadtree)'),
      table(['Version', 'Time', 'Change'], d.history.map(c => h('tr', null, h('td', { class: 'num' }, 'v' + c.version), h('td', null, clock(c.at_ms)), h('td', null, c.change))), { empty: 'No changes recorded.' }),
      d.log.length ? [h('h3', null, 'Changes by people'), h('ul', { class: 'feed' }, d.log.map(e => h('li', null, h('span', { class: 't' }, clock(e.t_ms).slice(3)), h('span', null, e.kind.replace(/_/g, ' '), h('div', { class: 'faint small' }, e.detail)))))] : null,
      h('h3', null, 'Incidents involving this zone'),
      d.incidents.length ? h('div', { class: 'row' }, d.incidents.map(i => h('a', { href: '#/incidents/' + i.id }, '#' + i.id + ' (' + i.state + ')'))) : h('div', { class: 'faint' }, 'None.')));
  }

  function createPanel() {
    const name = h('input', { placeholder: 'e.g. Landslide closure' });
    const kind = h('select', null, ['restricted', 'caution', 'advisory', 'safe'].map(k => h('option', { value: k }, k)));
    const severity = h('select', null, [1, 2, 3, 4, 5].map(s => h('option', { value: s, selected: s === 4 }, String(s))));
    const dwell = h('input', { type: 'number', min: 0, value: 0, style: { width: '90px' } });
    const status = h('div', { class: 'small muted' }, 'Click "Draw", then click the map to place vertices; double-click or press Finish.');
    const drawBtn = h('button', { onclick: () => {
      map.draft = []; map.canvas.classList.add('draw'); status.textContent = '0 vertices — click the map.';
      map.onDraw = (ev) => { status.textContent = map.draft.length + ' vertices' + (map.draft.length >= 3 ? ' — Finish when done.' : ''); if (ev === 'finish') finish(); };
    } }, 'Draw');
    const finish = async () => {
      if (!map.draft || map.draft.length < 3) { toast('Place at least 3 vertices', 'error'); return; }
      const ring = map.draft.slice();
      map.draft = null; map.canvas.classList.remove('draw'); map.render();
      try {
        const r = await post('/api/zones', { name: name.value.trim() || 'New zone', kind: kind.value, severity: Number(severity.value), max_dwell_s: Number(dwell.value), ring });
        toast('Zone created (validated: simple polygon, finite coordinates)', 'ok'); selectedId = r.id; load();
      } catch (e) { toast(e.message, 'error'); }
    };
    return panel('Create a zone', h('div', { class: 'stack' },
      h('div', { class: 'form-grid' }, field('Name', name), field('Category', kind), field('Severity', severity), field('Dwell limit (s)', dwell)),
      h('div', { class: 'row' }, drawBtn, h('button', { class: 'primary', onclick: finish }, 'Finish'), h('button', { class: 'ghost', onclick: () => { map.draft = null; map.canvas.classList.remove('draw'); map.render(); status.textContent = 'Cancelled.'; } }, 'Cancel')), status));
  }

  function importPanel() {
    const ta = h('textarea', { placeholder: '{"type":"FeatureCollection","features":[...]}  — GeoJSON coordinates are [lon, lat]' });
    const file = h('input', { type: 'file', accept: '.geojson,.json,application/geo+json,application/json', onchange: async (e) => {
      const f = e.target.files[0]; if (!f) return;
      if (f.size > 3_000_000) { toast('File too large (3 MB limit)', 'error'); return; }
      ta.value = await f.text();
    } });
    return panel('Import GeoJSON', h('div', { class: 'stack' },
      h('p', { class: 'small muted' }, 'All or nothing: every feature is validated (finite coordinates, valid ranges, no self-intersection, holes inside the outer ring) before any is added.'),
      file, ta, h('button', { class: 'primary', onclick: async () => {
        if (!ta.value.trim()) return;
        try { const r = await api('POST', '/api/zones/import', ta.value); toast('Imported ' + r.added + ' zone(s)', 'ok'); ta.value = ''; load(true); }
        catch (e) { toast(e.message, 'error'); }
      } }, 'Import')));
  }

  load(true);
  const off = ctx.onLive((l) => { map.users = l.users.filter(u => u.sharing); map.render(); });
  return { destroy() { off(); map.destroy(); } };
}
