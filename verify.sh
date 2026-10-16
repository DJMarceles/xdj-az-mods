#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# verify.sh MANIFEST: compare src/out/*.so with an official release's manifest.json.
# Exit 0 = both files match the release, 1 = a file differs, 2 = usage / missing file,
# 3 = the manifest is not a valid release manifest.
set -eu
cd "$(dirname "$0")"
[ $# -eq 1 ] && [ -f "$1" ] || { echo "usage: ./verify.sh path/to/manifest.json" >&2; exit 2; }
exec python3 - "$1" <<'PY'
import hashlib, json, sys
try:
    m = json.load(open(sys.argv[1], "rb"))
    want = {f["name"]: f["sha256"] for e in m["entries"] for f in e["files"] if "dir" not in f}
    for so in ("azmod.so", "azstem.so"):
        if not isinstance(want.get(so), str):
            raise ValueError("no sha256 for %s" % so)
    version, commit = m.get("version"), (m.get("source") or {}).get("commit")
except OSError as e:
    print("cannot read %s: %s" % (sys.argv[1], e.strerror)); sys.exit(2)
except KeyError as e:
    print("malformed manifest %s: no %s field" % (sys.argv[1], e)); sys.exit(3)
except (ValueError, TypeError, AttributeError) as e:
    print("malformed manifest %s: %s" % (sys.argv[1], e)); sys.exit(3)
print("release %s, built from %s" % (version, commit))
bad = 0
for so in ("azmod.so", "azstem.so"):
    try:
        got = hashlib.sha256(open("src/out/" + so, "rb").read()).hexdigest()
    except OSError:
        print("missing src/out/%s: build first" % so); sys.exit(2)
    ok = got == want.get(so)
    bad |= not ok
    print("%-10s %s  %s" % (so, "MATCH" if ok else "DIFFERENT", got))
    if not ok:
        print("%-10s release   %s" % ("", want.get(so)))
sys.exit(1 if bad else 0)
PY
