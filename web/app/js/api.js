// The only way the web app talks to the server. Every state-changing request
// carries X-SafeTrail-Request (the server's CSRF check); the session cookie is
// HttpOnly, so this code never sees the token.

let onUnauthorised = () => {};
export function setUnauthorisedHandler(fn) { onUnauthorised = fn; }

export class ApiError extends Error {
  constructor(status, message) { super(message); this.status = status; }
}

export async function api(method, path, body, raw = false) {
  const opts = { method, credentials: 'same-origin', headers: {} };
  if (method !== 'GET') opts.headers['X-SafeTrail-Request'] = '1';
  if (body !== undefined) {
    if (typeof body === 'string') { opts.body = body; opts.headers['Content-Type'] = 'application/geo+json'; }
    else { opts.body = JSON.stringify(body); opts.headers['Content-Type'] = 'application/json'; }
  }
  let res;
  try {
    res = await fetch(path, opts);
  } catch (e) {
    throw new ApiError(0, 'The server is not reachable. Is safetrail_server running?');
  }
  if (raw) {
    if (!res.ok) throw new ApiError(res.status, res.status + ' ' + res.statusText);
    return res.text();
  }
  let data = null;
  try { data = await res.json(); } catch (e) { data = null; }
  if (res.status === 401 && path !== '/api/login') onUnauthorised();
  if (!res.ok) throw new ApiError(res.status, (data && data.error) || (res.status + ' ' + res.statusText));
  return data;
}

export const get = (p) => api('GET', p);
export const post = (p, b = {}) => api('POST', p, b);
export const patch = (p, b) => api('PATCH', p, b);
export const del = (p) => api('DELETE', p);
