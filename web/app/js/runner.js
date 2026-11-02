// Scenario controls, shared by the top bar and the Live Monitoring panel.
import { post } from './api.js';
import { h, toast } from './util.js';

export function runnerControls(r, compact, after) {
  const act = async (a, body) => {
    try { await post('/api/scenario/' + a, body || {}); if (after) await after(); }
    catch (e) { toast(e.message, 'error'); }
  };
  const speed = h('select', { title: 'Simulation speed', onchange: (e) => act('speed', { speed: Number(e.target.value) }) },
    [0.5, 1, 2, 5, 10, 20, 60].map(s => h('option', { value: s, selected: Number(r.speed) === s }, s + '×')));
  const running = r.state === 'running';
  return h('div', { class: 'row', style: { gap: '6px', flexWrap: 'nowrap' } },
    compact ? h('span', { class: 'state', title: 'Scenario: ' + r.scenario }, r.state) : null,
    running ? h('button', { onclick: () => act('pause') }, 'Pause')
      : h('button', { class: 'primary', disabled: r.state === 'finished', onclick: () => act('start') }, 'Run'),
    h('button', { disabled: running || r.state === 'finished', title: 'Advance 10 simulated seconds', onclick: () => act('step', { ticks: 10 }) }, 'Step 10 s'),
    speed,
    h('button', { class: 'ghost', title: 'Reload the scenario from the start', onclick: () => { if (confirm('Reset the scenario? All simulated history is discarded.')) act('reset'); } }, 'Reset'));
}

// Scenario picker + narration + the simulated phones' own view.
export function scenarioPanel(sc, after) {
  const pick = h('select', null, sc.catalogue.map(c => h('option', { value: c.id, selected: c.id === sc.id }, c.title)));
  const info = sc.catalogue.find(c => c.id === sc.id);
  const load = async () => {
    if (!confirm('Load this scenario? The current simulated world is replaced.')) return;
    try { await post('/api/scenario/load', { id: pick.value }); toast('Scenario loaded, paused at T+00:00:00', 'ok'); if (after) await after(); }
    catch (e) { toast(e.message, 'error'); }
  };
  let nextMarked = false;
  const narr = h('ol', { class: 'narr' }, sc.narration.map(n => {
    let cls = n.done ? 'done' : '';
    if (!n.done && !nextMarked) { cls = 'next'; nextMarked = true; }
    return h('li', { class: cls }, h('span', { class: 'mono faint' }, fmt(n.at_ms) + '  '), n.text);
  }));
  const devs = sc.devices.filter(d => d.offline || d.battery_aware);
  return h('div', { class: 'stack' },
    h('div', { class: 'row' }, pick, h('button', { onclick: load }, 'Load')),
    info ? h('p', { class: 'muted small' }, info.summary) : null,
    h('div', { class: 'note sim' }, 'Everyone in a scenario is simulated (marked SIM). Fixes go through the same ingestion endpoint a phone would use; nothing bypasses the engine.'),
    h('h3', null, 'Narration'), narr,
    devs.length ? h('h3', null, 'Device-side view (simulated phones)') : null,
    devs.length ? h('div', null, devs.map(d => h('div', { class: 'note', style: { marginBottom: '6px' } },
      h('strong', null, d.name), ' — ', d.offline ? 'OFFLINE, ' + d.queued + ' events queued on the phone, phone clock ' + (d.clock_skew_ms / 60000).toFixed(0) + ' min off' : 'online',
      d.battery_aware ? ' · battery-aware: ' + d.fixes_sent + ' fixes sent, ' + d.fixes_skipped + ' skipped' : ''))) : null,
    sc.sync_reports.length ? h('h3', null, 'Reconciliation reports') : null,
    sc.sync_reports.map(s => h('div', { class: 'note' }, s)));
}

function fmt(ms) {
  const s = Math.floor(ms / 1000);
  return String(Math.floor(s / 60)).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0');
}
