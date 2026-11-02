import { get } from '../api.js';
import { h, mount, kpi, panel, clock, sev, int, fixed, sourceBadge, stateText, toast } from '../util.js';
import { MapView } from '../map.js';
import { scenarioPanel } from '../runner.js';

export function render(root, ctx) {
  const kpis = h('div', { class: 'kpis' });
  const mapHost = h('div', { style: { height: '420px' } });
  const incBox = h('div');
  const feedBox = h('div');
  const scenBox = h('div');
  mount(root, h('div', { class: 'page' },
    h('div', { class: 'page-head' },
      h('div', null, h('h1', null, 'Overview'),
        h('p', null, 'Every figure on this page is read from the running platform; nothing is a placeholder.'))),
    kpis,
    h('div', { class: 'grid cols-main-side' },
      h('div', { class: 'stack' },
        panel('Situation map', mapHost, h('a', { href: '#/live' }, 'Open live monitoring →')),
        panel('Open incidents', incBox, h('a', { href: '#/incidents' }, 'All incidents →'))),
      h('div', { class: 'stack' },
        ctx.mode === 'demo' ? panel('Scenario', scenBox) : null,
        panel('Latest events', feedBox, h('a', { href: '#/history' }, 'Investigate →'))))));

  const map = new MapView(mapHost, { legend: false });
  let fitted = false;
  let stats = null;
  get('/api/zones').then(z => { map.zones = z.zones; if (!fitted) { map.fitData(); } map.render(); }).catch(e => toast(e.message, 'error'));

  const loadStats = () => get('/api/stats').then(s => { stats = s; }).catch(() => {});
  const loadFeed = () => get('/api/events?limit=14').then(d => {
    mount(feedBox, d.events.length ? h('ul', { class: 'feed' }, d.events.map(e => h('li', null,
      h('span', { class: 't' }, clock(e.t_ms).slice(3)),
      h('span', null, h('strong', null, e.kind.replace(/_/g, ' ')), ' ', e.user_name || '', e.zone_name ? ' · ' + e.zone_name : '',
        e.late ? [' ', h('span', { class: 'badge-late' }, 'LATE')] : null,
        h('div', { class: 'faint small' }, e.detail))))) : h('div', { class: 'empty' }, 'No events yet. Start the scenario, or have a tourist share their location.'));
  }).catch(() => {});
  const loadScen = () => ctx.mode === 'demo' && get('/api/scenario').then(sc => mount(scenBox, scenarioPanel(sc, ctx.refresh))).catch(e => mount(scenBox, h('div', { class: 'muted' }, e.message)));
  loadStats(); loadFeed(); loadScen();
  let n = 0;

  const off = ctx.onLive((l) => {
    const users = l.users;
    const sharing = users.filter(u => u.sharing).length;
    const noContact = users.filter(u => u.status === 'no contact').length;
    const openInc = l.incidents.filter(i => i.state !== 'resolved');
    const avail = l.responders.filter(r => r.available).length;
    mount(kpis,
      kpi('Tracked now', int(sharing), int(users.length) + ' tourists registered'),
      kpi('No contact', int(noContact), 'sharing, but silent > 2 min'),
      kpi('Open alerts', int(l.open_alerts), 'unacknowledged'),
      kpi('Open incidents', int(openInc.length), int(l.incidents.length) + ' in total'),
      kpi('Responders free', int(avail) + ' / ' + l.responders.length, 'demo fixtures in demo mode'),
      kpi('Eval cost', stats ? fixed(stats.evaluator.mean_us, 1) + ' µs' : '—', 'per fix, measured live'),
      kpi('Events logged', int(l.log_size), 'Merkle-backed evidence'));
    map.users = users; map.responders = l.responders; map.incidents = openInc; map.groups = l.groups;
    if (!fitted && users.some(u => u.location)) { map.fitData(); fitted = true; }
    map.render();
    mount(incBox, openInc.length ? h('table', { class: 'data' },
      h('thead', null, h('tr', null, ['#', 'Sev', 'People', 'State', 'Responder', 'Opened'].map(x => h('th', null, x)))),
      h('tbody', null, openInc.map(i => h('tr', { class: 'clickable', onclick: () => ctx.go('incidents/' + i.id) },
        h('td', { class: 'num' }, '#' + i.id), h('td', null, sev(i.severity)),
        h('td', null, i.affected_names.join(', ').replace(/SIM · /g, ''), ' ', i.late ? h('span', { class: 'badge-late' }, 'LATE') : null),
        h('td', null, stateText(i.state)), h('td', null, i.responder_name || h('span', { class: 'faint' }, 'recommended: ' + (i.recommended_name || 'none'))),
        h('td', { class: 'num' }, clock(i.opened_ms)))))) : h('div', { class: 'empty' }, 'No open incidents.'));
    if (++n % 3 === 0) { loadFeed(); loadStats(); }
    if (n % 2 === 0) loadScen();
  });
  return { destroy() { off(); map.destroy(); } };
}

export { sourceBadge };
