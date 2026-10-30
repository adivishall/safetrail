// Application shell: sign-in, navigation by role, hash routing, the global
// scenario controls, and the one-second live poll every page reads from.

import { api, get, post, setUnauthorisedHandler } from './api.js';
import { h, mount, clock } from './util.js';
import { runnerControls } from './runner.js';
import * as overview from './pages/overview.js';
import * as live from './pages/live.js';
import * as users from './pages/users.js';
import * as groups from './pages/groups.js';
import * as zones from './pages/zones.js';
import * as alerts from './pages/alerts.js';
import * as incidents from './pages/incidents.js';
import * as responders from './pages/responders.js';
import * as history from './pages/history.js';
import * as analytics from './pages/analytics.js';
import * as lab from './pages/lab.js';
import * as settings from './pages/settings.js';
import * as mine from './pages/mine.js';
import * as assignments from './pages/assignments.js';

const STAFF = ['admin', 'operator'];
const ROUTES = [
  { id: 'mine', label: 'My Safety', page: mine, roles: ['tourist'], section: 'Me' },
  { id: 'assignments', label: 'My Assignments', page: assignments, roles: ['responder'], section: 'Me' },
  { id: 'overview', label: 'Overview', page: overview, roles: STAFF, section: 'Operations' },
  { id: 'live', label: 'Live Monitoring', page: live, roles: STAFF },
  { id: 'users', label: 'Users', page: users, roles: STAFF },
  { id: 'groups', label: 'Groups', page: groups, roles: STAFF },
  { id: 'zones', label: 'Hazard Zones', page: zones, roles: STAFF },
  { id: 'alerts', label: 'Alerts', page: alerts, roles: STAFF, count: 'alerts' },
  { id: 'incidents', label: 'Incidents', page: incidents, roles: STAFF, count: 'incidents' },
  { id: 'responders', label: 'Responders', page: responders, roles: STAFF },
  { id: 'history', label: 'Investigation', page: history, roles: STAFF, section: 'Analysis' },
  { id: 'analytics', label: 'Analytics', page: analytics, roles: STAFF },
  { id: 'lab', label: 'Data Structures Lab', page: lab, roles: ['admin', 'operator', 'responder', 'tourist'], section: 'Learn' },
  { id: 'settings', label: 'Settings', page: settings, roles: ['admin', 'operator', 'responder', 'tourist'], section: 'System' },
];

const ctx = {
  me: null, mode: 'demo', live: null, listeners: new Set(),
  get staff() { return this.me && STAFF.includes(this.me.role); },
  get admin() { return this.me && this.me.role === 'admin'; },
  onLive(fn) { this.listeners.add(fn); return () => this.listeners.delete(fn); },
  go(route) { location.hash = '#/' + route; },
  async refresh() { await poll(true); },
};

const root = document.getElementById('root');
let current = null;      // { route, instance }
let timer = 0;
let shell = null;

setUnauthorisedHandler(() => { if (ctx.me) { ctx.me = null; showLogin('Your session ended. Please sign in again.'); } });

async function boot() {
  try {
    const s = await get('/api/session');
    ctx.mode = s.mode;
    if (s.user) { ctx.me = s.user; startApp(); }
    else showLogin();
  } catch (e) {
    mount(root, h('div', { class: 'login' }, h('div', { class: 'error-box' }, e.message)));
  }
}

function showLogin(message) {
  clearInterval(timer);
  if (current && current.instance && current.instance.destroy) current.instance.destroy();
  current = null; shell = null;
  const user = h('input', { name: 'username', autocomplete: 'username', required: true, autofocus: true });
  const pass = h('input', { name: 'password', type: 'password', autocomplete: 'current-password', required: true });
  const err = h('div', { class: 'error-box', style: { display: message ? 'block' : 'none' } }, message || '');
  const form = h('form', {
    onsubmit: async (e) => {
      e.preventDefault();
      err.style.display = 'none';
      try {
        const r = await post('/api/login', { username: user.value.trim(), password: pass.value });
        ctx.me = r.user;
        pass.value = '';
        startApp();
      } catch (ex) { err.textContent = ex.message; err.style.display = 'block'; }
    },
  },
    h('div', { class: 'brand' }, h('span', { class: 'brand-mark' }), h('span', null, 'SafeTrail')),
    h('p', { class: 'muted' }, 'Geofencing and incident response for tourist safety.'),
    h('label', { class: 'field' }, 'Username', user),
    h('label', { class: 'field' }, 'Password', pass),
    err,
    h('button', { class: 'primary', type: 'submit' }, 'Sign in'),
    h('p', { class: 'hint' }, 'Accounts and passwords are printed by safetrail_server when it starts and kept in var/demo-credentials.txt (never in the repository). Location sharing is opt-in: tourists are tracked only after they consent and start a session.'));
  mount(root, h('div', { class: 'login' }, form));
}

function startApp() {
  const routes = ROUTES.filter(r => r.roles.includes(ctx.me.role));
  const nav = h('nav', { class: 'nav', 'aria-label': 'Main' });
  let lastSection = null;
  for (const r of routes) {
    if (r.section && r.section !== lastSection) { nav.append(h('div', { class: 'group-label' }, r.section)); lastSection = r.section; }
    nav.append(h('a', { href: '#/' + r.id, dataset: { route: r.id } }, h('span', null, r.label),
      r.count ? h('span', { class: 'count', dataset: { count: r.count } }, '0') : null));
  }
  const clockEl = h('span', { class: 'clock', title: 'Platform clock' }, 'T+00:00:00');
  const runbar = h('div', { class: 'runbar' });
  const who = h('div', { class: 'who' },
    h('span', null, ctx.me.display_name + ' · ' + ctx.me.role),
    h('button', { class: 'ghost small', onclick: logout }, 'Sign out'));
  const top = h('header', { class: 'topbar' },
    h('div', { class: 'brand' }, h('span', { class: 'brand-mark' }), h('span', null, 'SafeTrail'), h('small', null, 'operations')),
    h('span', { class: 'mode-tag ' + ctx.mode, title: ctx.mode === 'demo' ? 'Demonstration mode: records marked SIM are simulated people; DEMO marks fixtures.' : 'Production mode' },
      ctx.mode === 'demo' ? 'demo data' : 'production'),
    clockEl, runbar, h('div', { class: 'spacer' }), who);
  const main = h('main', { class: 'main' });
  mount(root, h('div', { class: 'shell' }, top, nav, main));
  shell = { nav, main, clockEl, runbar, routes };
  window.onhashchange = route;
  route();
  clearInterval(timer);
  timer = setInterval(() => poll(false), 1000);
  poll(true);
}

async function logout() {
  try { await post('/api/logout'); } catch (e) { /* the session may already be gone */ }
  ctx.me = null;
  showLogin();
}

function route() {
  if (!shell) return;
  const id = (location.hash.replace(/^#\/?/, '').split('/')[0]) || shell.routes[0].id;
  const r = shell.routes.find(x => x.id === id) || shell.routes[0];
  for (const a of shell.nav.querySelectorAll('a')) a.classList.toggle('active', a.dataset.route === r.id);
  if (current && current.instance && current.instance.destroy) current.instance.destroy();
  ctx.listeners.clear();
  const host = h('div');
  mount(shell.main, host);
  try {
    current = { route: r.id, instance: r.page.render(host, ctx) || {} };
  } catch (e) {
    console.error(e);
    mount(host, h('div', { class: 'page' }, h('div', { class: 'error-box' }, 'This page failed to render: ' + e.message)));
    current = { route: r.id, instance: {} };
  }
  document.title = r.label + ' — SafeTrail';
}

let polling = false;
async function poll(force) {
  if (!ctx.me || (polling && !force)) return;
  polling = true;
  try {
    const l = await get('/api/live');
    ctx.live = l;
    renderTopbar(l);
    for (const fn of [...ctx.listeners]) { try { fn(l); } catch (e) { console.error(e); } }
  } catch (e) {
    if (shell) shell.clockEl.textContent = 'offline';
  } finally {
    polling = false;
  }
}

let runState = '';
function renderTopbar(l) {
  shell.clockEl.textContent = clock(l.now_ms);
  const open = l.open_alerts || 0;
  const incOpen = (l.incidents || []).filter(i => i.state !== 'resolved').length;
  for (const c of shell.nav.querySelectorAll('[data-count]')) {
    const n = c.dataset.count === 'alerts' ? open : incOpen;
    c.textContent = String(n);
    c.classList.toggle('hot', n > 0);
  }
  if (!ctx.staff || ctx.mode !== 'demo' || !l.runner || !l.runner.scenario) { mount(shell.runbar); return; }
  const key = l.runner.state + '|' + l.runner.speed + '|' + l.runner.scenario;
  if (key === runState) return;
  runState = key;
  mount(shell.runbar, runnerControls(l.runner, true, () => { runState = ''; return poll(true); }));
}

boot();
