import { get, patch } from '../api.js';
import { h, mount, clock, panel, field, toast, table } from '../util.js';

export function render(root, ctx) {
  const setBox = h('div');
  const auditBox = h('div');
  const pwBox = h('div');
  mount(root, h('div', { class: 'page' },
    h('div', { class: 'page-head' }, h('div', null, h('h1', null, 'Settings'),
      h('p', null, 'Retention, alerting thresholds, the fixed engine parameters, your password, and — for administrators — the location-access audit log.'))),
    h('div', { class: 'grid cols-2' }, h('div', { class: 'stack' }, setBox, pwBox), auditBox),
    panel('Privacy commitments', h('ul', { class: 'small', style: { margin: 0, paddingLeft: '18px' } },
      h('li', null, 'Tracking is opt-in: a tourist gives consent and starts a session; nobody else can do either for them.'),
      h('li', null, 'A tourist can stop sharing at any time; zone state is discarded, not turned into a false exit.'),
      h('li', null, 'Movement history is kept for the retention period and can be erased by the tourist or an administrator; positions on open alerts are kept until the alert closes.'),
      h('li', null, 'Every read of a person\'s location by someone else is recorded in an append-only, tamper-evident audit log.'),
      h('li', null, 'The evidence log stores ids and zone ids, never coordinates, so erasure never conflicts with evidence integrity.'),
      h('li', null, 'Demonstration people are simulated and marked SIM; demonstration responders are marked DEMO.')))));

  async function loadSettings() {
    if (!ctx.staff) { mount(setBox, panel('Platform settings', h('p', { class: 'muted' }, 'Visible to operators and administrators.'))); return; }
    try {
      const s = await get('/api/settings');
      const e = s.editable;
      const ret = h('input', { type: 'number', min: 1, value: Math.round(e.trail_retention_ms / 60000), disabled: !ctx.admin });
      const inc = h('select', { disabled: !ctx.admin }, [1, 2, 3, 4, 5].map(v => h('option', { value: v, selected: v === e.incident_min_breach_severity }, String(v))));
      const warn = h('select', { disabled: !ctx.admin }, [1, 2, 3, 4, 5].map(v => h('option', { value: v, selected: v === e.warning_min_severity }, String(v))));
      const eta = h('input', { type: 'number', min: 10, max: 3600, value: e.approach_alert_eta_s, disabled: !ctx.admin });
      const jump = h('input', { type: 'number', min: 5, max: 1000, value: e.jump_speed_mps, disabled: !ctx.admin });
      const auto = h('input', { type: 'checkbox', checked: e.auto_dispatch, disabled: !ctx.admin });
      mount(setBox, panel('Platform settings (' + s.mode + ' mode)', h('div', { class: 'stack' },
        h('div', { class: 'form-grid' }, field('Trail retention (min)', ret), field('Breach → incident at severity ≥', inc),
          field('Warnings alert at severity ≥', warn), field('Approach alert if ETA ≤ (s)', eta), field('GPS jump above (m/s)', jump),
          h('label', { class: 'row small' }, auto, 'Auto-assign the recommended responder')),
        ctx.admin ? h('button', { class: 'primary', onclick: async () => {
          try {
            await patch('/api/settings', { trail_retention_ms: Number(ret.value) * 60000, incident_min_breach_severity: Number(inc.value), warning_min_severity: Number(warn.value),
              approach_alert_eta_s: Number(eta.value), jump_speed_mps: Number(jump.value), auto_dispatch: auto.checked });
            toast('Settings saved', 'ok');
          } catch (ex) { toast(ex.message, 'error'); }
        } }, 'Save') : h('p', { class: 'small muted' }, 'Only administrators change settings.'),
        h('h3', null, 'Fixed at start-up'),
        table(['Parameter', 'Value'], Object.entries(s.fixed).map(([k, v]) => h('tr', null, h('td', null, k.replace(/_/g, ' ')), h('td', { class: 'num' }, String(v))))))));
    } catch (e) { mount(setBox, h('div', { class: 'error-box' }, e.message)); }
  }

  async function loadAudit() {
    if (!ctx.admin) { mount(auditBox, panel('Access audit log', h('p', { class: 'muted' }, 'Visible to administrators.'))); return; }
    try {
      const a = await get('/api/audit');
      mount(auditBox, panel('Location-access audit log', h('div', { class: 'stack' },
        h('p', { class: 'small muted' }, a.size + ' records. Root of its Merkle log: ', h('span', { class: 'mono' }, a.root ? a.root.slice(0, 24) + '…' : '—')),
        table(['Time', 'Who', 'Whose data', 'What', 'Result'], a.records.map(r => h('tr', null, h('td', { class: 'mono' }, clock(r.t_ms)),
          h('td', null, r.actor), h('td', null, r.subject), h('td', null, r.what), h('td', null, r.allowed ? 'allowed' : 'DENIED'))), { empty: 'No access recorded yet.', maxHeight: '60vh' }))));
    } catch (e) { mount(auditBox, h('div', { class: 'error-box' }, e.message)); }
  }

  const p1 = h('input', { type: 'password', autocomplete: 'new-password' });
  const p2 = h('input', { type: 'password', autocomplete: 'new-password' });
  mount(pwBox, panel('Change my password', h('form', { class: 'stack', onsubmit: async (e) => {
    e.preventDefault();
    if (p1.value !== p2.value) { toast('Passwords do not match', 'error'); return; }
    try { await patch('/api/users/' + ctx.me.id, { password: p1.value }); toast('Password changed', 'ok'); p1.value = p2.value = ''; } catch (ex) { toast(ex.message, 'error'); }
  } }, h('div', { class: 'form-grid' }, field('New password', p1), field('Repeat', p2)), h('button', { type: 'submit' }, 'Change password'),
  h('p', { class: 'small faint' }, 'Stored as PBKDF2-HMAC-SHA256 with a random salt. In demo mode the server reads the starting passwords from the environment or var/demo-credentials.txt on each start.'))));

  loadSettings(); loadAudit();
  return {};
}
