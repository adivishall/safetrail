// DOM and formatting helpers. Every text node is created with textContent,
// never innerHTML: names, notes and zone labels come from users and imports,
// and must not be able to inject markup.

export function h(tag, props, ...kids) {
  const el = document.createElement(tag);
  if (props) {
    for (const [k, v] of Object.entries(props)) {
      if (v === undefined || v === null || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
      else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
      else if (k === 'dataset') Object.assign(el.dataset, v);
      else if (v === true) el.setAttribute(k, '');
      else el.setAttribute(k, String(v));
    }
  }
  append(el, kids);
  return el;
}

function append(el, kids) {
  for (const k of kids) {
    if (k === null || k === undefined || k === false) continue;
    if (Array.isArray(k)) append(el, k);
    else if (k instanceof Node) el.appendChild(k);
    else el.appendChild(document.createTextNode(String(k)));
  }
}

export function clear(el) { while (el.firstChild) el.removeChild(el.firstChild); return el; }
export function mount(el, ...kids) { clear(el); append(el, kids); return el; }

// The platform clock is milliseconds since the platform's own epoch.
export function clock(ms) {
  if (ms === null || ms === undefined) return '—';
  const neg = ms < 0;
  let s = Math.floor(Math.abs(ms) / 1000);
  const hh = Math.floor(s / 3600); s -= hh * 3600;
  const mm = Math.floor(s / 60); s -= mm * 60;
  return (neg ? '−' : '') + 'T+' + String(hh).padStart(2, '0') + ':' + String(mm).padStart(2, '0') + ':' + String(s).padStart(2, '0');
}

export function ago(ms) {
  if (ms === null || ms === undefined) return '—';
  const s = Math.round(ms / 1000);
  if (s < 0) return 'in ' + (-s) + ' s';
  if (s < 60) return s + ' s ago';
  if (s < 3600) return Math.floor(s / 60) + ' min ' + (s % 60) + ' s ago';
  return Math.floor(s / 3600) + ' h ' + Math.floor((s % 3600) / 60) + ' min ago';
}

export function metres(m) {
  if (m === null || m === undefined || !isFinite(m)) return '—';
  if (Math.abs(m) >= 1000) return (m / 1000).toFixed(m >= 10000 ? 1 : 2) + ' km';
  return Math.round(m) + ' m';
}

export function fixed(x, d = 1) { return x === null || x === undefined || !isFinite(x) ? '—' : Number(x).toFixed(d); }
export function int(x) { return x === null || x === undefined ? '—' : Math.round(x).toLocaleString('en-US'); }

export function sev(n) { return h('span', { class: 'sev sev-' + Math.max(1, Math.min(5, n || 1)), title: 'severity ' + n }, String(n || 0)); }

export function sourceBadge(source) {
  if (source === 'scenario') return h('span', { class: 'badge-sim', title: 'Simulated by the scenario runner — not a real person or record' }, 'SIM');
  if (source === 'fixture') return h('span', { class: 'badge-fixture', title: 'Demonstration fixture' }, 'DEMO');
  return null;
}

export const STATUS_COLOUR = {
  'clear': '#3fb6a8', 'approaching': '#d6a440', 'uncertain': '#d6a440', 'separated': '#e07b39',
  'stationary': '#e07b39', 'inside advisory': '#8e7cd8', 'inside caution': '#e07b39',
  'inside restricted': '#e5484d', 'inside safe': '#46a758', 'no contact': '#6f7b89',
  'awaiting fix': '#6f7b89', 'not sharing': '#4a5563', 'no consent': '#4a5563',
};
export function statusColour(s) { return STATUS_COLOUR[s] || '#a7b1bd'; }

export const ZONE_COLOUR = { restricted: '#e5484d', caution: '#e0a33a', advisory: '#8e7cd8', safe: '#46a758' };
export const SEV_COLOUR = ['#7d8a99', '#7d8a99', '#6c9bd2', '#d6a440', '#e07b39', '#e5484d'];

export function statusChip(s) {
  return h('span', { class: 'chip' }, h('span', { class: 'dot', style: { background: statusColour(s) } }), s);
}

export function stateText(s) { return h('span', { class: 'state-' + String(s).toLowerCase() }, String(s).toLowerCase()); }

let toastBox = null;
export function toast(msg, kind = 'info') {
  if (!toastBox) {
    toastBox = h('div', { style: { position: 'fixed', right: '14px', bottom: '14px', zIndex: 50, display: 'grid', gap: '6px' } });
    document.body.appendChild(toastBox);
  }
  const el = h('div', {
    class: 'panel', style: {
      padding: '8px 12px', maxWidth: '380px', fontSize: '12.5px',
      borderColor: kind === 'error' ? 'rgba(229,72,77,.6)' : kind === 'ok' ? 'rgba(63,182,168,.6)' : 'var(--line-strong)',
    },
  }, msg);
  toastBox.appendChild(el);
  setTimeout(() => el.remove(), kind === 'error' ? 6000 : 3200);
}

export function emptyRow(cols, text) {
  return h('tr', null, h('td', { colspan: cols, class: 'empty' }, text));
}

export function table(headers, rows, opts = {}) {
  const thead = h('thead', null, h('tr', null, headers.map(x =>
    typeof x === 'string' ? h('th', null, x) : h('th', { class: x.cls || '' }, x.label))));
  const tbody = h('tbody', null, rows.length ? rows : emptyRow(headers.length, opts.empty || 'Nothing to show.'));
  return h('div', { class: 'table-wrap', style: opts.maxHeight ? { maxHeight: opts.maxHeight } : null },
    h('table', { class: 'data' }, thead, tbody));
}

export function kpi(label, value, sub) {
  return h('div', { class: 'kpi' }, h('div', { class: 'label' }, label), h('div', { class: 'value' }, value), sub ? h('div', { class: 'sub' }, sub) : null);
}

export function panel(title, body, actions) {
  return h('section', { class: 'panel' },
    h('header', null, typeof title === 'string' ? h('h3', null, title) : title, actions || null),
    h('div', { class: 'body' }, body));
}

export function field(label, input) { return h('label', { class: 'field' }, label, input); }

export function debounce(fn, ms) {
  let t = 0;
  return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); };
}
