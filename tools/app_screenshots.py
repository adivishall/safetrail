#!/usr/bin/env python3
"""Screenshots of the running operations console, for the report and slides.

Starts safetrail_server on a free port with throwaway credentials, signs in
over HTTP, advances the walkthrough scenario through the API, then drives
headless Chrome over the DevTools protocol (a minimal WebSocket client, standard
library only) with the session cookie set, and saves PNGs to
docs/final/images/. Every screen shows the real system in its real state.

Usage: python3 tools/app_screenshots.py     (needs Google Chrome)
"""
import base64
import http.cookiejar
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "final", "images")
CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")


def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


class Api:
    def __init__(self, base):
        self.base = base
        self.jar = http.cookiejar.CookieJar()
        self.op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar))

    def call(self, method, path, body=None):
        req = urllib.request.Request(self.base + path, method=method,
                                     data=None if body is None else json.dumps(body).encode())
        if method != "GET":
            req.add_header("X-SafeTrail-Request", "1")
            req.add_header("Content-Type", "application/json")
        with self.op.open(req, timeout=60) as r:
            return json.loads(r.read() or b"null")

    def token(self):
        return next(c.value for c in self.jar if c.name == "st_session")


class WebSocket:
    """Just enough RFC 6455 for the DevTools protocol: text frames, masking,
    extended lengths, continuation frames."""
    def __init__(self, url):
        assert url.startswith("ws://")
        hostport, path = url[5:].split("/", 1)
        host, port = hostport.split(":")
        self.s = socket.create_connection((host, int(port)))
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(("GET /%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (path, hostport, key)).encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += self.s.recv(4096)
        self.rest = buf.split(b"\r\n\r\n", 1)[1]
        self.next_id = 0

    def _read(self, n):
        while len(self.rest) < n:
            chunk = self.s.recv(1 << 20)
            if not chunk:
                raise EOFError
            self.rest += chunk
        out, self.rest = self.rest[:n], self.rest[n:]
        return out

    def send(self, text):
        data = text.encode()
        head = bytearray([0x81])
        n = len(data)
        if n < 126: head.append(0x80 | n)
        elif n < 65536: head += bytes([0x80 | 126]) + struct.pack(">H", n)
        else: head += bytes([0x80 | 127]) + struct.pack(">Q", n)
        mask = os.urandom(4)
        self.s.sendall(bytes(head) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def recv(self):
        msg = b""
        while True:
            b0, b1 = self._read(2)
            n = b1 & 0x7F
            if n == 126: n = struct.unpack(">H", self._read(2))[0]
            elif n == 127: n = struct.unpack(">Q", self._read(8))[0]
            payload = self._read(n)
            msg += payload
            if b0 & 0x80:
                return msg.decode()

    def cmd(self, method, params=None, session=None):
        self.next_id += 1
        m = {"id": self.next_id, "method": method, "params": params or {}}
        if session: m["sessionId"] = session
        self.send(json.dumps(m))
        while True:
            r = json.loads(self.recv())
            if r.get("id") == self.next_id:
                if "error" in r:
                    raise RuntimeError("%s: %s" % (method, r["error"]))
                return r.get("result", {})


def main():
    server = os.path.join(ROOT, "build", "safetrail_server")
    if not os.path.exists(server) or not os.path.exists(CHROME):
        print("needs build/safetrail_server and Google Chrome"); return 2
    port, dport = free_port(), free_port()
    var, profile = tempfile.mkdtemp(), tempfile.mkdtemp()
    env = dict(os.environ, SAFETRAIL_ADMIN_PASSWORD="shots-admin-password", SAFETRAIL_OPERATOR_PASSWORD="shots-operator-pw",
               SAFETRAIL_RESPONDER_PASSWORD="shots-responder-pw", SAFETRAIL_TOURIST_PASSWORD="shots-tourist-pw")
    srv = subprocess.Popen([server, "--port", str(port), "--var", var], cwd=ROOT, env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    chrome = None
    base = "http://127.0.0.1:%d" % port
    try:
        for _ in range(100):
            try: urllib.request.urlopen(base + "/api/health", timeout=1); break
            except OSError: time.sleep(0.1)
        admin = Api(base)
        admin.call("POST", "/api/login", {"username": "admin", "password": "shots-admin-password"})
        # The tourist account opts in and reports a position near the cliff.
        tour = Api(base)
        tour.call("POST", "/api/login", {"username": "tourist", "password": "shots-tourist-pw"})
        tid = tour.call("GET", "/api/session")["user"]["id"]
        admin.call("POST", "/api/scenario/step", {"ticks": 400})
        tour.call("POST", "/api/users/%d/consent" % tid, {"consent": True})
        tour.call("POST", "/api/users/%d/sharing" % tid, {"active": True})
        for k in range(4):
            admin.call("POST", "/api/scenario/step", {"ticks": 1})
            tour.call("POST", "/api/users/%d/fix" % tid, {"lat": 25.5822, "lon": 91.8745 + k * 0.00005, "accuracy_m": 8})
        admin.call("POST", "/api/scenario/step", {"ticks": 350})
        live = admin.call("GET", "/api/live")
        inc = next((i for i in live["incidents"] if any("Ravi" in n for n in i["affected_names"])), live["incidents"][0])
        admin.call("POST", "/api/incidents/%d/ack" % inc["id"], {})
        admin.call("POST", "/api/incidents/%d/assign" % inc["id"], {})

        chrome = subprocess.Popen([CHROME, "--headless=new", "--remote-debugging-port=%d" % dport, "--user-data-dir=" + profile,
                                   "--hide-scrollbars", "--no-first-run", "--no-default-browser-check", "about:blank"],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            try:
                ver = json.loads(urllib.request.urlopen("http://127.0.0.1:%d/json/version" % dport, timeout=1).read())
                break
            except OSError: time.sleep(0.1)
        ws = WebSocket(ver["webSocketDebuggerUrl"])
        target = ws.cmd("Target.createTarget", {"url": "about:blank"})["targetId"]
        sess = ws.cmd("Target.attachToTarget", {"targetId": target, "flatten": True})["sessionId"]
        ws.cmd("Page.enable", session=sess)
        ws.cmd("Network.enable", session=sess)

        def as_user(api):
            ws.cmd("Network.clearBrowserCookies", session=sess)
            ws.cmd("Network.setCookie", {"name": "st_session", "value": api.token(), "domain": "127.0.0.1",
                                         "path": "/", "httpOnly": True}, session=sess)

        def shot(route, name, w=1440, h=900, wait=3.0, script=None, scale=1.25):
            ws.cmd("Emulation.setDeviceMetricsOverride", {"width": w, "height": h, "deviceScaleFactor": scale,
                                                          "mobile": w < 700}, session=sess)
            ws.cmd("Page.navigate", {"url": "about:blank"}, session=sess)   # force a full load, not a hash change
            time.sleep(0.3)
            ws.cmd("Page.navigate", {"url": base + "/#/" + route}, session=sess)
            time.sleep(wait)
            if script:
                ws.cmd("Runtime.evaluate", {"expression": script, "awaitPromise": True}, session=sess)
                time.sleep(1.5)
            data = ws.cmd("Page.captureScreenshot", {"format": "png"}, session=sess)["data"]
            with open(os.path.join(OUT, name + ".png"), "wb") as f:
                f.write(base64.b64decode(data))
            print("  " + name)

        os.makedirs(OUT, exist_ok=True)
        as_user(admin)
        shot("overview", "shot-overview")
        shot("live", "shot-live", wait=4)
        shot("incidents/%d" % inc["id"], "shot-incident", wait=4)
        shot("alerts", "shot-alerts")
        shot("groups", "shot-groups", wait=4)
        shot("zones", "shot-zones", wait=4)
        shot("history", "shot-history", wait=4)
        shot("analytics", "shot-analytics", wait=4)
        shot("lab/spatial", "shot-lab-spatial", wait=4, script="window.scrollTo(0, 360)")
        shot("lab/avl", "shot-lab-avl", wait=4, script="window.scrollTo(0, 360)")
        shot("lab/graph", "shot-lab-graph", wait=4, script="window.scrollTo(0, 360)")
        shot("lab/hash", "shot-lab-hash", wait=4, script="window.scrollTo(0, 360)")
        shot("lab/merkle", "shot-lab-merkle", wait=4, script="window.scrollTo(0, 360)")
        shot("live", "shot-mobile-live", w=390, h=844, wait=4, scale=2)
        as_user(tour)
        shot("mine", "shot-tourist", wait=4)
        ws.cmd("Network.deleteCookies", {"name": "st_session", "domain": "127.0.0.1"}, session=sess)
        shot("", "shot-login", wait=2)
    finally:
        for p in (chrome, srv):
            if p:
                p.terminate()
                try: p.wait(timeout=5)
                except subprocess.TimeoutExpired: p.kill()
        shutil.rmtree(var, ignore_errors=True)
        shutil.rmtree(profile, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
