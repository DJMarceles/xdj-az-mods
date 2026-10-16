#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A Remote Stems server for test/remotestems.sh: the REMOTE-STEMS.md protocol (wire
version 2: the deck proves the pairing token, never sends it), with faults the suite
switches on through a control file.

    remotestems_fixture.py serve <dir> <port>     run the server (state lives in <dir>)
    remotestems_fixture.py ctl <dir> k=v ...      merge keys into <dir>/ctl.json
    remotestems_fixture.py count <dir> <name>     print a request counter (0 if none)
    remotestems_fixture.py existing <dir>         the last POST's "existing" list: one
                                                  "<name>|<its source file, or ->" per entry

ctl.json keys (read on every request):
    epoch    a change clears every job and counter (one test case = one epoch)
    hello    ok | 401 | old | impostor | replay       GET /v2/hello
             401: an app that does not know this USB; old: an app from before version 2
             (it answers every request 401 without "v"); impostor: a device that advertises
             the id but does not hold the token (a made-up proof), and that answers the
             version-1 /v1/health as the real app would, to collect a bearer token; replay:
             the last genuine answer, recorded and sent again
    post     ok | close | 503 | 401 | 400  POST /v1/jobs (close: no response at all,
                                          what a timeout looks like to curl: code 000)
    put      ok | lost                    lost: take the whole body, queue the job, then
                                          close without a response (the response is lost)
    seed     <file>:<state>               the next POST for <file> finds an existing job of
                                          this token in <state> and returns it (200), as the
                                          server's own dedupe does
Counters: posts, puts, put409, gets, hello, health, deletes, files, badauth.
The last POST /v1/jobs body is kept as <dir>/post.last; one that is not JSON is
answered 400, as the app answers it.
Every request's line, headers and body go to <dir>/wire.log (what a listener on the
network sees); `wire <dir> <text>` prints how many times <text> appears in it.
"""
import hashlib, hmac, json, os, re, secrets, sys, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ID = "3fa2c41b9e07"
TOKEN = "6c0d1f2e3a4b5c6d7e8f901a2b3c4d5e"
FILES = [("drums.ogg", 3000), ("bass.ogg", 2000), ("other.ogg", 1500), ("vocals.ogg", 1200),
         ("source.json", 40)]
LOCK = threading.Lock()
AUTH_RE = re.compile(r"ns=([0-9a-f]{32}) ctr=([0-9]{1,15}) sig=([0-9a-f]{64})")


def mac(key, msg):
    return hmac.new(key, msg.encode(), hashlib.sha256).hexdigest()


def load(d, name, default):
    try:
        return json.load(open(os.path.join(d, name)))
    except Exception:
        return default


def save(d, name, obj):
    tmp = os.path.join(d, name + ".tmp")
    json.dump(obj, open(tmp, "w"))
    os.replace(tmp, os.path.join(d, name))


class State:
    def __init__(self, d):
        self.d, self.epoch, self.jobs, self.counts, self.sessions = d, None, {}, {}, {}

    def ctl(self):
        c = load(self.d, "ctl.json", {})
        if c.get("epoch") != self.epoch:
            self.epoch, self.jobs, self.counts = c.get("epoch"), {}, {}
            save(self.d, "counts.json", self.counts)
        return c

    def count(self, k):
        self.counts[k] = self.counts.get(k, 0) + 1
        save(self.d, "counts.json", self.counts)

    def public(self, j):
        done = j["state"] == "done"
        return {"id": j["id"], "state": j["state"], "pct": 100 if done else j.get("pct", 0),
                "position": 0 if j["state"] == "queued" else None, "folder": j["folder"],
                "file": j["file"],
                "files": [{"name": n, "size": s} for n, s in FILES] if done else None,
                "error": j.get("error")}


def handler(st):
    class H(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_a):
            pass

        def send(self, code, obj):
            b = (json.dumps(obj, ensure_ascii=False) + "\n").encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)

        def drop(self):
            # no response at all: curl reports "Empty reply from server", code 000
            self.close_connection = True

        def body(self):
            n = int(self.headers.get("Content-Length") or 0)
            b = self.rfile.read(n) if n else b""
            with open(os.path.join(st.d, "wire.log"), "ab") as w:
                w.write(b + b"\n")
            return b

        def handle_any(self, method):
            with LOCK:
                c = st.ctl()
                with open(os.path.join(st.d, "wire.log"), "ab") as w:
                    w.write(f"{self.requestline}\n{self.headers}".encode())
                path = self.path.split("?")[0].rstrip("/")
                mode = c.get("hello", "ok")
                if method == "GET" and path == "/v2/hello":
                    st.count("hello")
                    q = dict(p.split("=", 1) for p in self.path.partition("?")[2].split("&") if "=" in p)
                    nd, kid = q.get("nd", ""), q.get("kid", "")
                    if mode == "old":
                        return self.send(401, {"error": "missing or unknown token -- pair this stick in AZ-MODS"})
                    if mode == "401" or kid != hashlib.sha256(TOKEN.encode()).hexdigest()[:16]:
                        return self.send(401, {"error": "this USB is not paired with this computer", "v": 2})
                    last = os.path.join(st.d, "last_hello.json")
                    if mode == "replay" and os.path.exists(last):
                        return self.send(200, json.load(open(last)))
                    ns = secrets.token_hex(16)
                    if mode == "impostor":     # holds no token: the best it can do is guess
                        return self.send(200, {"id": ID, "v": 2, "name": "Fixture PC", "ns": ns,
                                               "proof": mac(b"not-the-token", f"azrs-srv\n{ID}\n{nd}\n{ns}")})
                    key = bytes.fromhex(mac(TOKEN.encode(), f"azrs-ses\n{nd}\n{ns}"))
                    st.sessions[ns] = {"key": key, "ctr": 0}
                    out = {"id": ID, "v": 2, "name": "Fixture PC", "ns": ns,
                           "proof": mac(TOKEN.encode(), f"azrs-srv\n{ID}\n{nd}\n{ns}")}
                    json.dump(out, open(last, "w"))
                    return self.send(200, out)
                if method == "GET" and path == "/v1/health":
                    st.count("health")
                    if mode == "impostor":     # what a version-1 deck sent its bearer token to
                        return self.send(200, {"id": ID, "v": 1, "name": "Fixture PC"})
                    return self.send(401, {"error": "update the mods on the deck", "v": 2})
                if mode in ("old", "impostor"):
                    self.body()
                    return self.send(401, {"error": "missing or unknown token"})
                m = AUTH_RE.fullmatch(self.headers.get("X-AZ-Auth", "").strip())
                sess = st.sessions.get(m.group(1)) if m else None
                ok = False
                if sess:
                    ctr = int(m.group(2))
                    want = mac(sess["key"], f"{method}\n{self.path}\n{ctr}\n{self.headers.get('Content-Length') or ''}")
                    ok = hmac.compare_digest(want, m.group(3)) and ctr > sess["ctr"]
                    if ok:
                        sess["ctr"] = ctr
                if not ok:
                    st.count("badauth")
                    self.body()
                    self.close_connection = True
                    return self.send(401, {"error": "bad or missing signature", "v": 2})
                if method == "POST" and path == "/v1/jobs":
                    st.count("posts")
                    raw = self.body()
                    with open(os.path.join(st.d, "post.last"), "wb") as w:
                        w.write(raw)
                    try:   # as the app answers a body that is not JSON
                        req = json.loads(raw.decode("utf-8"), strict=False)
                    except (ValueError, UnicodeDecodeError):
                        return self.send(400, {"error": "the body is not JSON"})
                    mode = c.get("post", "ok")
                    if mode == "close":
                        return self.drop()
                    if mode in ("503", "401", "400"):
                        return self.send(int(mode), {"error": f"fixture says {mode}"})
                    f = req.get("file")
                    seed = c.get("seed", "")
                    if seed and seed.rsplit(":", 1)[0] == f and not c.get("_seeded_" + str(st.epoch)):
                        jid = secrets.token_hex(8)
                        st.jobs[jid] = {"id": jid, "file": f, "folder": "Artist - Song",
                                        "state": seed.rsplit(":", 1)[1], "pct": 0}
                        c["_seeded_" + str(st.epoch)] = 1
                        save(st.d, "ctl.json", c)
                    for j in st.jobs.values():
                        if j["file"] == f and j["state"] != "error":
                            return self.send(200, {"id": j["id"], "folder": j["folder"]})
                    jid = secrets.token_hex(8)
                    st.jobs[jid] = {"id": jid, "file": f, "folder": "Artist - Song",
                                    "state": "waiting_upload", "pct": 0}
                    return self.send(201, {"id": jid, "folder": "Artist - Song"})
                parts = path.split("/")       # '', v1, jobs, <id>, [audio | files, <name>]
                j = st.jobs.get(parts[3]) if len(parts) > 3 and parts[2] == "jobs" else None
                if method == "PUT" and len(parts) == 5 and parts[4] == "audio":
                    st.count("puts")
                    if j is None:
                        self.body()
                        return self.send(404, {"error": "no such job"})
                    if j["state"] != "waiting_upload":
                        st.count("put409")
                        self.close_connection = True
                        return self.send(409, {"error": f"this job is not waiting for its audio (state {j['state']})"})
                    self.body()
                    j["state"] = "queued"
                    if c.get("put") == "lost":
                        return self.drop()
                    return self.send(200, st.public(j))
                if j is None:
                    self.body()
                    return self.send(404, {"error": "no such job"})
                if method == "GET" and len(parts) == 4:
                    st.count("gets")
                    out = st.public(j)
                    # every poll moves a job on: queued -> running -> done
                    if j["state"] == "queued":
                        j["state"] = "running"; j["pct"] = 50
                    elif j["state"] == "running":
                        j["state"] = "done"
                    return self.send(200, out)
                if method == "GET" and len(parts) == 6 and parts[4] == "files":
                    st.count("files")
                    size = dict(FILES).get(parts[5])
                    if size is None or j["state"] != "done":
                        return self.send(404, {"error": "no such file"})
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Content-Length", str(size))
                    self.end_headers()
                    self.wfile.write(b"x" * size)
                    return
                if method == "DELETE" and len(parts) == 4:
                    st.count("deletes")
                    del st.jobs[j["id"]]
                    return self.send(200, {"ok": True})
                return self.send(405, {"error": "method not allowed"})

        def do_GET(self):
            self.handle_any("GET")

        def do_POST(self):
            self.handle_any("POST")

        def do_PUT(self):
            self.handle_any("PUT")

        def do_DELETE(self):
            self.handle_any("DELETE")
    return H


if __name__ == "__main__":
    cmd, d = sys.argv[1], sys.argv[2]
    os.makedirs(d, exist_ok=True)
    if cmd == "serve":
        ThreadingHTTPServer(("127.0.0.1", int(sys.argv[3])), handler(State(d))).serve_forever()
    elif cmd == "ctl":
        c = load(d, "ctl.json", {})
        for kv in sys.argv[3:]:
            k, v = kv.split("=", 1)
            c[k] = v
        save(d, "ctl.json", c)
    elif cmd == "count":
        print(load(d, "counts.json", {}).get(sys.argv[3], 0))
    elif cmd == "existing":
        try:
            body = json.loads(open(os.path.join(d, "post.last"), "rb").read().decode("utf-8"), strict=False)
        except (OSError, ValueError, UnicodeDecodeError):
            raise SystemExit("no POST body, or not JSON")
        for e in body.get("existing") or []:
            src = e.get("source")
            print(e.get("name"), "|", src.get("file") if isinstance(src, dict) else "-", sep="")
    elif cmd == "wire":
        try:
            print(open(os.path.join(d, "wire.log"), "rb").read().count(sys.argv[3].encode()))
        except OSError:
            print(0)
    else:
        raise SystemExit(__doc__)
