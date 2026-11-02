import { get, post, del } from '../api.js';
import { h, mount, clock, ago, metres, sev, stateText, panel, toast, statusChip, fixed } from '../util.js';
import { MapView } from '../map.js';

// The tourist's own view: consent, the tracking switch, their location, their
// alerts, SOS, and erasure. In the demo there is no phone, so "send a fix"
// stands in for one: click the map to report a position.
export function render(root, ctx) {
  const head = h('div');
  const mapHost = h('div', { style: { height: '440px' } });
  const info = h('div', { class: 'stack' });
  mount(root, h('div', { class: 'page' },
    h('div', { class: 'page-head' }, h('div', null, h('h1', null, 'My Safety'),
      h('p', null, 'You decide whether SafeTrail knows where you are. Nothing is recorded until you give consent and start sharing; you can stop at any time.'))),
    head,
    h('div', { class: 'grid cols-main-side', style: { marginTop: '12px' } }, panel('Where SafeTrail thinks you are', mapHost), info)));
  const map = new MapView(mapHost);
  get('/api/zones').then(z => { map.zones = z.zones; map.fitData(); });
  let me = null;
  let fitted = false;
  let accuracy = 8;
  map.onClickMap = (lat, lon) => {
    if (!me || !me.sharing) { toast('Start sharing first', 'error'); return true; }
    post('/api/users/' + me.id + '/fix', { lat, lon, accuracy_m: accuracy })
      .then(r => { toast('Fix ' + r.status + (r.events ? ', ' + r.events + ' transition(s)' : '') + (r.reason ? ': ' + r.reason : ''), r.status === 'accepted' ? 'ok' : 'info'); load(); })
      .catch(e => toast(e.message, 'error'));
    return true;
  };

  async function load() {
    try {
      me = (await get('/api/users/' + ctx.me.id)).user;
      const alerts = (await get('/api/alerts')).alerts;
      draw(alerts);
    } catch (e) { mount(info, h('div', { class: 'error-box' }, e.message)); }
  }
  const act = async (fn, msg) => { try { await fn(); if (msg) toast(msg, 'ok'); load(); ctx.refresh(); } catch (e) { toast(e.message, 'error'); } };

  function draw(alerts) {
    const accSel = h('select', { onchange: (e) => { accuracy = Number(e.target.value); } }, [5, 8, 15, 30, 60].map(a => h('option', { value: a, selected: a === accuracy }, '±' + a + ' m')));
    mount(head, h('div', { class: 'panel' }, h('div', { class: 'body row', style: { justifyContent: 'space-between' } },
      h('div', { class: 'row' },
        me.sharing ? h('span', { class: 'tracking-on', style: { fontSize: '14px', padding: '6px 12px' } }, h('span', { class: 'dot' }), 'Location sharing is ON')
          : h('span', { class: 'tracking-off', style: { fontSize: '14px', padding: '6px 12px' } }, 'Location sharing is OFF'),
        statusChip(me.status)),
      h('div', { class: 'row' },
        h('label', { class: 'row small' }, h('input', { type: 'checkbox', checked: me.consent, onchange: (e) => act(() => post('/api/users/' + me.id + '/consent', { consent: e.target.checked }), e.target.checked ? 'Consent given' : 'Consent withdrawn; sharing stopped') }), 'I consent to location tracking for my safety'),
        me.sharing ? h('button', { onclick: () => act(() => post('/api/users/' + me.id + '/sharing', { active: false }), 'Sharing stopped') }, 'Stop sharing')
          : h('button', { class: 'primary', disabled: !me.consent, onclick: () => act(() => post('/api/users/' + me.id + '/sharing', { active: true }), 'Sharing started') }, 'Start sharing'),
        h('button', { class: 'sos', disabled: !me.sharing || !me.location, title: 'Raise a severity-5 SOS alert at your last position', onclick: () => { if (confirm('Send an SOS to the operations centre?')) act(() => post('/api/users/' + me.id + '/sos'), 'SOS sent'); } }, 'SOS')))));
    map.users = me.location ? [me] : [];
    if (me.location && !fitted) { map.fitPoints([[me.location.lat, me.location.lon]], 40, 0.03); fitted = true; }
    map.render();
    const mine = alerts.filter(a => a.user === me.id);
    mount(info,
      panel('Simulated phone', h('div', { class: 'stack' }, h('p', { class: 'small muted' }, 'This demo has no real phone. While sharing, click the map to report a GPS fix at that point with the accuracy below. It goes through the same endpoint a phone app would use.'),
        h('div', { class: 'row' }, 'Accuracy', accSel))),
      panel('My status', h('dl', { class: 'kv' },
        h('dt', null, 'Position'), h('dd', { class: 'mono' }, me.location ? me.location.lat.toFixed(5) + ', ' + me.location.lon.toFixed(5) : '—'),
        h('dt', null, 'Last fix'), h('dd', null, me.location ? clock(me.location.t_ms) + ' (' + ago(me.location.age_ms) + ')' : '—'),
        h('dt', null, 'Nearest zones'), h('dd', null, (me.zones || []).map(z => h('div', null, z.name + ' — ' + (z.distance_m < 0 ? 'inside' : metres(z.distance_m))))),
        h('dt', null, 'Battery'), h('dd', null, me.power ? fixed(me.power.pct_per_hour, 2) + ' %/h (model)' : '—'),
        h('dt', null, 'Battery saving'), h('dd', null, h('label', { class: 'row small' }, h('input', { type: 'checkbox', checked: me.battery_aware, onchange: (e) => act(() => post('/api/users/' + me.id + '/battery', { aware: e.target.checked })) }), 'sample less often far from hazards')))),
      panel('My alerts', mine.length ? h('ul', { class: 'feed' }, mine.slice(0, 20).map(a => h('li', null, h('span', { class: 't' }, clock(a.raised_ms).slice(3)),
        h('span', null, sev(a.severity), ' ', a.kind.toLowerCase(), ' ', a.zone_name, ' — ', stateText(a.state))))) : h('div', { class: 'faint' }, 'No alerts.')),
      panel('My data', h('div', { class: 'stack' }, h('p', { class: 'small muted' }, 'Erase your movement history. Positions attached to alerts that are still open are kept until those alerts close, so a responder on the way can still find you.'),
        h('button', { class: 'danger', onclick: () => { if (confirm('Erase your movement history?')) act(() => del('/api/users/' + me.id + '/history'), 'History erased'); } }, 'Erase my history'))));
  }
  load();
  let n = 0;
  const off = ctx.onLive(() => { if (++n % 3 === 0) load(); });
  return { destroy() { off(); map.destroy(); } };
}
