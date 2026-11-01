#!/usr/bin/env python3
"""End-to-end check of safetrail_server over real HTTP.

Starts the server on a free port with throwaway credentials in a temporary
directory, then drives the workflow an examiner would: sign in, load and step
the walkthrough scenario, read the live map, acknowledge and assign an incident,
verify an evidence proof, run a lab experiment, and check the security edges
(no session, no CSRF header, path traversal). Standard library only.

Usage: python3 tools/api_e2e.py        (make e2e)
"""
import hashlib
import http.cookiejar
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER = os.path.join(ROOT, "build", "safetrail_server")
checks = failures = 0


def ok(cond, what):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("  FAIL", what)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Client:
    def __init__(self, base):
        self.base = base
        self.jar = http.cookiejar.CookieJar()
        self.op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar))

    def call(self, method, path, body=None, csrf=True):
        data = None if body is None else (body if isinstance(body, bytes) else json.dumps(body).encode())
        req = urllib.request.Request(self.base + path, data=data, method=method)
        if csrf and method != "GET":
            req.add_header("X-SafeTrail-Request", "1")
        if data is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with self.op.open(req, timeout=30) as r:
                raw = r.read()
                return r.status, raw, dict(r.headers)
        except urllib.error.HTTPError as e:
            return e.code, e.read(), dict(e.headers)

    def json(self, method, path, body=None):
        st, raw, _ = self.call(method, path, body)
        try:
            return st, json.loads(raw or b"null")
        except ValueError:
            return st, None


def rfc9162_verify(entry, index, size, path, root_hex):
    h = lambda b: hashlib.sha256(b).digest()
    r = h(b"\x00" + entry.encode())
    fn, sn = index, size - 1
    for p in (bytes.fromhex(x) for x in path):
        if sn == 0:
            return False
        if fn & 1 or fn == sn:
            r = h(b"\x01" + p + r)
            while not fn & 1 and fn != 0:
                fn >>= 1
                sn >>= 1
        else:
            r = h(b"\x01" + r + p)
        fn >>= 1
        sn >>= 1
    return sn == 0 and r.hex() == root_hex


def slow_client(port, result):
    """Trickle a request one byte a second, never finishing it. The idle
    timeout never fires (the client is never idle), so only the server's
    request deadline can close it; record how long that took."""
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(b"GET /api/health HTTP/1.1\r\nX-Pad: ")
    t0 = time.monotonic()
    try:
        while time.monotonic() - t0 < 45:
            time.sleep(1)
            s.sendall(b"a")
            s.settimeout(0.01)
            try:
                if s.recv(1) == b"":
                    break
            except socket.timeout:
                pass
    except OSError:
        pass                                   # reset by the server: also closed
    result["closed_after_s"] = time.monotonic() - t0
    s.close()


def main():
    if not os.path.exists(SERVER):
        print("build the server first: make build/safetrail_server")
        return 2
    port = free_port()
    var = tempfile.mkdtemp(prefix="safetrail-e2e-")
    env = dict(os.environ, SAFETRAIL_ADMIN_PASSWORD="e2e-admin-password",
               SAFETRAIL_OPERATOR_PASSWORD="e2e-operator-password",
               SAFETRAIL_RESPONDER_PASSWORD="e2e-responder-password",
               SAFETRAIL_TOURIST_PASSWORD="e2e-tourist-password")
    proc = subprocess.Popen([SERVER, "--port", str(port), "--var", var], cwd=ROOT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    base = "http://127.0.0.1:%d" % port
    try:
        for _ in range(100):
            try:
                urllib.request.urlopen(base + "/api/health", timeout=1)
                break
            except OSError:
                time.sleep(0.1)
        slow = {}
        slow_thread = threading.Thread(target=slow_client, args=(port, slow), daemon=True)
        slow_thread.start()
        anon = Client(base)
        st, h = anon.json("GET", "/api/health")
        ok(st == 200 and h["mode"] == "demo", "health in demo mode")
        ok(anon.call("GET", "/api/live")[0] == 401, "no session: 401")
        ok(anon.call("POST", "/api/login", {"username": "operator", "password": "x"}, csrf=False)[0] == 403,
           "no CSRF header: 403")
        ok(anon.call("GET", "/../var/demo-credentials.txt")[0] == 404, "traversal: 404")
        ok(anon.call("GET", "/%2e%2e/Makefile")[0] == 404, "encoded traversal: 404")
        st, _, hdr = anon.call("GET", "/")
        ok(st == 200 and "frame-ancestors 'none'" in hdr.get("Content-Security-Policy", ""), "app served with CSP")

        op = Client(base)
        st, me = op.json("POST", "/api/login", {"username": "operator", "password": "e2e-operator-password"})
        ok(st == 200 and me["user"]["role"] == "operator", "operator signs in")
        st, sc = op.json("GET", "/api/scenario")
        ok(st == 200 and sc["id"] == "walkthrough" and sc["state"] == "paused", "walkthrough preloaded, paused")
        st, sc = op.json("POST", "/api/scenario/step", {"ticks": 600})
        ok(st == 200 and sc["now_ms"] == 600000, "stepped ten simulated minutes")
        st, live = op.json("GET", "/api/live")
        users = live["users"]
        ok(len([u for u in users if u["sharing"]]) == 16, "sixteen simulated tourists sharing")
        ok(all(u["simulated"] for u in users if u["sharing"]), "every scenario tourist is labelled simulated")
        ok(len(live["incidents"]) >= 3, "incidents formed")
        inc = live["incidents"][0]
        ok(inc["recommended"] is not None, "incident has a recommended responder")
        ok(op.json("POST", "/api/incidents/%d/ack" % inc["id"], {})[0] == 200, "acknowledge")
        st, _ = op.json("POST", "/api/incidents/%d/assign" % inc["id"], {})
        ok(st == 200, "assign recommended")
        st, d = op.json("GET", "/api/incidents/%d" % inc["id"])
        r = d["incident"]["route"]
        ok(r and r["found"] and len(r["points"]) > 2, "A* route along roads")
        ok(r["astar_expanded"] <= r["dijkstra_expanded"], "A* settles no more than Dijkstra")
        st, al = op.json("GET", "/api/alerts?state=open")
        pr = [a["priority"] for a in al["alerts"]]
        ok(pr == sorted(pr, reverse=True), "open alerts come back in triage (heap) order")
        st, ev = op.json("GET", "/api/evidence")
        n = ev["size"]
        good = True
        for i in sorted({0, 1, n // 3, n // 2, n - 2, n - 1}):
            st, p = op.json("GET", "/api/evidence/%d" % i)
            good = good and rfc9162_verify(p["entry"], p["index"], p["size"], p["path"], p["root"])
        ok(good, "Merkle inclusion proofs verify independently (Python hashlib)")
        st, hist = op.json("GET", "/api/history?t=300000")
        ok(st == 200 and len(hist["zones_in_force"]) > 30, "history query at t=300 s")
        st, lab = op.json("POST", "/api/lab/spatial", {"n": 500})
        ok(st == 200 and all(lab[k]["mismatches_vs_brute"] == 0 for k in ("quadtree", "rtree_str", "rtree_insert", "geohash")),
           "lab: every index agrees with brute force")
        ok(op.call("POST", "/api/zones", {"name": "x", "kind": "caution", "severity": 2,
                                          "ring": [[25.5, 91.8], [25.5, 91.81], [25.51, 91.81]]})[0] == 403,
           "operator cannot create zones")
        st, sc = op.json("POST", "/api/scenario/step", {"ticks": 300})
        st, sc = op.json("GET", "/api/scenario")
        ok(any("admitted" in s for s in sc["sync_reports"]), "offline phone reconciled after reconnect")
        st, stats = op.json("GET", "/api/stats")
        ok(stats["sync"]["duplicates_rejected"] == stats["sync"]["merged"], "retried batch rejected event for event")

        tour = Client(base)
        tour.json("POST", "/api/login", {"username": "tourist", "password": "e2e-tourist-password"})
        st, me = tour.json("GET", "/api/session")
        tid = me["user"]["id"]
        ok(tour.json("POST", "/api/users/%d/fix" % tid, {"lat": 25.58, "lon": 91.88})[0] == 403,
           "no fix before consent and a session")
        tour.json("POST", "/api/users/%d/consent" % tid, {"consent": True})
        tour.json("POST", "/api/users/%d/sharing" % tid, {"active": True})
        st, fr = tour.json("POST", "/api/users/%d/fix" % tid, {"lat": 25.58, "lon": 91.88, "accuracy_m": 8})
        ok(st == 200 and fr["status"] == "accepted", "fix accepted after opt-in")
        st, lv = tour.json("GET", "/api/live")
        ok(len(lv["users"]) == 1 and lv["users"][0]["id"] == tid, "a tourist sees only themself on the map")
        ok(tour.call("GET", "/api/audit")[0] == 403, "tourist cannot read the audit log")
        adm = Client(base)
        adm.json("POST", "/api/login", {"username": "admin", "password": "e2e-admin-password"})
        st, au = adm.json("GET", "/api/audit")
        ok(st == 200 and au["size"] > 0, "operator reads were audited")
        slow_thread.join(timeout=60)
        closed = slow.get("closed_after_s", 99)
        ok(15 <= closed <= 30, "a request trickled in byte by byte is cut off at the 20 s deadline (%.1f s)" % closed)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    print("%s api/e2e %d checks, %d failed" % ("[ ok ]" if not failures else "[FAIL]", checks, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
