#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fixture for test/community.sh: a set of community repositories, packages and icons,
and the HTTPS server that serves them (the fetcher refuses plain http, so the test does
not get an exception: it trusts a throwaway CA through CURL_CA_BUNDLE).

    community_fixture.py gen <dir> <base-url>   write the tree (indexes carry real sha256/size)
    community_fixture.py serve <dir> <port> <cert> <key>

Server paths:
    /<file>          ordinary, with Content-Length
    /nolen/<file>    the same bytes with NO Content-Length (chunked, as a real server
                     sends an unknown length), so a size cap has to hold while the bytes
                     arrive, not from the header
    /stall/<file>    headers, 10 bytes, then silence for 60 s (the stall timeout)
"""
import hashlib, io, json, os, ssl, struct, sys, threading, time, zipfile, zlib
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

FW = "ep147-679e1b451e5fe82b"

def png(w=4, h=4, rgb=(200, 40, 40)):
    raw = b"".join(b"\0" + bytes(rgb) * w for _ in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

def manifest(mid, ver="1.0.0", **over):
    m = {"azmod": 1, "id": mid, "name": mid.replace("-", " ").title(), "version": ver,
         "author": "fixture", "summary": "A test mod.", "description": "Line one.\nLine two.",
         "license": "MIT", "source": "https://github.com/fixture/" + mid, "api": 1,
         "firmware": [FW], "hooks": False}
    for k, v in over.items():
        if v is None: m.pop(k, None)
        else: m[k] = v
    return json.dumps(m, indent=2).encode()

def zip_bytes(members, stream=False, zip64=False):
    """members: list of (name, bytes). stream=True writes through an unseekable file,
    so zipfile emits data descriptors (sizes after the data, as Finder does): the deck's
    busybox reads those from the central directory, so they MUST install. zip64=True
    writes Zip64 entries (what `zip -` from a pipe produces): busybox 1.27 lists their
    sizes as 4294967295 and cannot read them, so they MUST be refused."""
    if not stream:
        b = io.BytesIO()
        with zipfile.ZipFile(b, "w", zipfile.ZIP_DEFLATED) as z:
            for n, d in members:
                if zip64 and not n.endswith("/"):
                    with z.open(n, "w", force_zip64=True) as f: f.write(d)
                else:
                    z.writestr(n, d)
        return b.getvalue()
    class W(io.RawIOBase):
        def __init__(s): s.buf = bytearray()
        def writable(s): return True
        def write(s, d): s.buf += d; return len(d)
        def seekable(s): return False
    w = W()
    with zipfile.ZipFile(w, "w", zipfile.ZIP_DEFLATED) as z:
        for n, d in members:
            with z.open(n, "w") as f:
                f.write(d)
    return bytes(w.buf)

def so(mid, ver):  # a stub: the fetcher never runs or parses it (core validates the ELF)
    return b"\x7fELF\x02\x01\x01" + (mid + " " + ver).encode() * 50

def pkg(mid, ver="1.0.0", man=None, extra=(), drop=(), stream=False, zip64=False):
    m = [("azmod.json", man if man is not None else manifest(mid, ver)),
         (mid + ".so", so(mid, ver)), ("icon.png", png()), ("shots/", b""), ("shots/1.png", png(8, 8))]
    m = [x for x in m if x[0] not in drop] + list(extra)
    return zip_bytes(m, stream, zip64)

def entry(base, mid, data, ver="1.0.0", **over):
    e = {"id": mid, "name": mid.replace("-", " ").title(), "version": ver, "author": "fixture",
         "summary": "Summary of " + mid, "icon": f"{base}/icons/{mid}.png",
         "package": f"{base}/pkg/{mid}-{ver}.azmod", "sha256": hashlib.sha256(data).hexdigest(),
         "size": len(data), "firmware": [FW], "hooks": False,
         "source": "https://github.com/fixture/" + mid, "updated": "2026-10-08"}
    for k, v in over.items():
        if v is None: e.pop(k, None)
        else: e[k] = v
    return e

def gen(root, base):
    def put(rel, data):
        p = os.path.join(root, rel); os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "wb").write(data if isinstance(data, bytes) else data.encode())
    def ship(mid, data, ver="1.0.0"):
        put(f"pkg/{mid}-{ver}.azmod", data); return data
    put("ping", "pong\n")
    mods = []
    # --- installable -----------------------------------------------------------
    d = ship("good-mod", pkg("good-mod")); put("icons/good-mod.png", png())
    mods.append(entry(base, "good-mod", d, category="visual"))
    d = ship("dd-mod", pkg("dd-mod", stream=True))                  # data descriptors: fine
    mods.append(entry(base, "dd-mod", d))
    d = ship("any-mod", pkg("any-mod", man=manifest("any-mod", firmware=["any"], hooks=False)))
    put("icons/any-mod.png", b"this is not a png\n")              # icon refused, mod still listed
    mods.append(entry(base, "any-mod", d, firmware=["any"]))
    d = ship("core-mod", pkg("core-mod", man=manifest("core-mod", min_core="9.0.0")))
    mods.append(entry(base, "core-mod", d, min_core="9.0.0"))     # listed; the deck says it needs a newer core
    # --- refused at install --------------------------------------------------------
    d = ship("badsha-mod", pkg("badsha-mod"))
    mods.append(entry(base, "badsha-mod", d, sha256="0" * 64))
    d = ship("small-mod", pkg("small-mod"))
    mods.append(entry(base, "small-mod", d, size=100))             # the repository file understates the size
    d = ship("datadir-mod", pkg("datadir-mod", extra=[("data/save.txt", b"shipped")]))   # data is never shipped
    mods.append(entry(base, "datadir-mod", d))
    d = ship("trav-mod", pkg("trav-mod", extra=[("../../evil.so", b"pwn")]))
    mods.append(entry(base, "trav-mod", d))
    d = ship("abs-mod", pkg("abs-mod", extra=[("/tmp/abs-evil", b"pwn")]))
    mods.append(entry(base, "abs-mod", d))
    d = ship("mid-trav-mod", pkg("mid-trav-mod", extra=[("shots/../../../mid-evil", b"pwn")]))
    mods.append(entry(base, "mid-trav-mod", d))
    d = ship("nosrc-mod", pkg("nosrc-mod", man=manifest("nosrc-mod", source=None)))
    mods.append(entry(base, "nosrc-mod", d))
    d = ship("api2-mod", pkg("api2-mod", man=manifest("api2-mod", api=2)))
    mods.append(entry(base, "api2-mod", d))
    d = ship("nofw-mod", pkg("nofw-mod", man=manifest("nofw-mod", firmware=[])))
    mods.append(entry(base, "nofw-mod", d))
    d = ship("anyhooks-mod", pkg("anyhooks-mod", man=manifest("anyhooks-mod", firmware=["any"], hooks=True)))
    mods.append(entry(base, "anyhooks-mod", d, firmware=["any"]))
    d = ship("idmis-mod", pkg("idmis-mod", man=manifest("someone-else")))
    mods.append(entry(base, "idmis-mod", d))
    d = ship("vermis-mod", pkg("vermis-mod", man=manifest("vermis-mod", ver="9.9.9")))
    mods.append(entry(base, "vermis-mod", d))
    d = ship("badjson-mod", pkg("badjson-mod", man=b'{"azmod": 1, "id": "badjson-mod",'))
    mods.append(entry(base, "badjson-mod", d))
    d = ship("noso-mod", pkg("noso-mod", drop=("noso-mod.so",)))
    mods.append(entry(base, "noso-mod", d))
    d = ship("zip64-mod", pkg("zip64-mod", zip64=True))
    mods.append(entry(base, "zip64-mod", d))
    d = ship("space-mod", pkg("space-mod", extra=[("has space.txt", b"x")]))
    mods.append(entry(base, "space-mod", d))
    d = ship("hidden-mod", pkg("hidden-mod", extra=[(".origin", b"repo=https://evil\n")]))
    mods.append(entry(base, "hidden-mod", d))
    d = ship("offsw-mod", pkg("offsw-mod", extra=[("offsw-mod.off", b"")]))
    mods.append(entry(base, "offsw-mod", d))
    d = ship("bomb-mod", pkg("bomb-mod", extra=[("blob.bin", b"\0" * (17 * 1024 * 1024))]))
    mods.append(entry(base, "bomb-mod", d))                      # 17 MB unpacked from a tiny zip
    d = ship("case-mod", pkg("case-mod", extra=[("assets/Kick.wav", b"A"), ("assets/kick.wav", b"B")]))
    mods.append(entry(base, "case-mod", d))                       # one file on FAT32: refused
    d = ship("assets-mod", pkg("assets-mod", extra=[("assets/kits/kick.wav", b"K" * 100)]))
    mods.append(entry(base, "assets-mod", d))                     # CONTROL: assets/ installs
    d = ship("mcshort-mod", pkg("mcshort-mod", man=manifest("mcshort-mod", min_core="1.2")))
    mods.append(entry(base, "mcshort-mod", d))                    # the core's cm_ver3 refuses 1.2
    d = ship("mcok-mod", pkg("mcok-mod", man=manifest("mcok-mod", min_core="1.0.0")))
    mods.append(entry(base, "mcok-mod", d))                       # CONTROL: a full min_core installs
    def padded(mid, total):   # azmod.json of exactly <total> bytes
        m = manifest(mid, pad="")
        return manifest(mid, pad="x" * (total - len(m)))
    d = ship("bigman-mod", pkg("bigman-mod", man=padded("bigman-mod", 16384)))
    mods.append(entry(base, "bigman-mod", d))                     # one byte past what the core reads
    d = ship("edgeman-mod", pkg("edgeman-mod", man=padded("edgeman-mod", 16383)))
    mods.append(entry(base, "edgeman-mod", d))                    # CONTROL: exactly what the core reads
    d = ship("notzip-mod", b"this is not a zip at all\n" * 10)
    mods.append(entry(base, "notzip-mod", d))
    # --- skipped at refresh (never listed) ---------------------------------------------
    mods.append(entry(base, "Bad_Id", d))
    mods.append(entry(base, "huge-mod", d, size=17 * 1024 * 1024))
    mods.append(entry(base, "nofwidx-mod", d, firmware=[]))
    mods.append(entry(base, "http-mod", d, package="http://127.0.0.1/pkg/x.azmod"))
    mods.append(entry(base, "anyhooksidx-mod", d, firmware=["any"], hooks=True))
    mods.append(entry(base, "badcore-mod", d, min_core="1.3"))     # min_core not MAJOR.MINOR.PATCH
    mods.append(entry(base, "badver-mod", d, ver="1.2"))           # version not MAJOR.MINOR.PATCH (azcm_ver_newer)
    mods.append(dict(mods[0]))                                   # duplicate good-mod: first wins
    rsv = ship("enabled", pkg("enabled"))                         # a reserved id (the enabled list's name)
    mods.append(entry(base, "enabled", rsv))
    SKIPPED = 9
    idx = {"azmods_repo": 1, "name": "Fixture R1 é", "maintainer": "fixture",
           "homepage": "https://github.com/fixture/r1", "mods": mods}
    put("r1/azmods-repo.json", json.dumps(idx, indent=2))
    # good-mod 1.0.1 (a real update) and 1.0.2 (a broken one: wrong sha256)
    v2 = ship("good-mod", pkg("good-mod", "1.0.1"), "1.0.1")
    put("r1/azmods-repo.v2.json", json.dumps(dict(idx, mods=[entry(base, "good-mod", v2, "1.0.1")] + mods[1:])))
    v3 = ship("good-mod", pkg("good-mod", "1.0.2"), "1.0.2")
    put("r1/azmods-repo.v3.json", json.dumps(dict(idx, mods=[entry(base, "good-mod", v3, "1.0.2", sha256="1" * 64)] + mods[1:])))
    # r2: a user repository. Its good-mod is a DIFFERENT mod with the same id.
    o = ship("other-mod", pkg("other-mod")); put("icons/other-mod.png", png(rgb=(0, 200, 0)))
    g2 = pkg("good-mod", "2.0.0"); put("pkg2/good-mod-2.0.0.azmod", g2)
    r2 = {"azmods_repo": 1, "name": "Fixture R2", "maintainer": "someone-else", "homepage": "https://example.com",
          "mods": [entry(base, "other-mod", o, category="weird",
                         name="Pipe|Name " + "\u00e9\u4e2d" * 12),            # | stripped, cap 47, UTF-8 whole
                   entry(base, "good-mod", g2, "2.0.0", package=f"{base}/pkg2/good-mod-2.0.0.azmod")]}
    put("r2/azmods-repo.json", json.dumps(r2))
    # many: more mods than the catalog holds (az-community CAT_MODS). Only the last one has a
    # real package: it is installed, and an installed mod is kept when the catalog is cut.
    last = ship("many-139", pkg("many-139"))
    put("many/azmods-repo.json", json.dumps({"azmods_repo": 1, "name": "Many", "mods":
        [entry(base, "many-%03d" % i, last) for i in range(140)]}))
    # broken repositories
    put("big/azmods-repo.json", json.dumps({"azmods_repo": 1, "pad": "x" * 300000, "mods": []}))
    put("broken/azmods-repo.json", '{"azmods_repo": 1, "mods": [ {"id": "x" ')
    put("notrepo/azmods-repo.json", json.dumps({"something": "else"}))
    # the Mod Index: good + broken + refused entries
    put("list.json", json.dumps({"azmods_repos": 1, "repos": [
        f"{base}/r1/azmods-repo.json", f"{base}/big/azmods-repo.json", f"{base}/nolen/big/azmods-repo.json",
        f"{base}/broken/azmods-repo.json", f"{base}/notrepo/azmods-repo.json",
        "not a repo", "http://127.0.0.1:8443/r1/azmods-repo.json"]}))
    put("expect.txt", f"skipped={SKIPPED}\n")

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def do_GET(self):
        p = self.path.split("?")[0]
        mode = ""
        for m in ("/nolen", "/stall"):
            if p.startswith(m + "/"): mode, p = m, p[len(m):]
        f = os.path.normpath(os.path.join(ROOT, p.lstrip("/")))
        if not f.startswith(ROOT) or not os.path.isfile(f):
            self.send_response(404); self.end_headers(); return
        data = open(f, "rb").read()
        self.send_response(200); self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Connection", "close"); self.close_connection = True
        if mode == "/nolen": self.send_header("Transfer-Encoding", "chunked")
        else: self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        try:
            if mode == "/stall":
                self.wfile.write(data[:10]); self.wfile.flush(); time.sleep(60); return
            for i in range(0, len(data), 16384):
                c = data[i:i + 16384]
                if mode == "/nolen": self.wfile.write(b"%x\r\n" % len(c) + c + b"\r\n")
                else: self.wfile.write(c)
            if mode == "/nolen": self.wfile.write(b"0\r\n\r\n")
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError, OSError):
            pass
    def handle(self):
        try: super().handle()
        except (ssl.SSLError, OSError): pass

if __name__ == "__main__":
    if sys.argv[1] == "gen":
        gen(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "serve":
        ROOT = os.path.abspath(sys.argv[2])
        s = ThreadingHTTPServer(("127.0.0.1", int(sys.argv[3])), H)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(sys.argv[4], sys.argv[5])
        s.socket = ctx.wrap_socket(s.socket, server_side=True)
        s.serve_forever()
