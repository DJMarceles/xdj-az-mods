#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""community_statsfuzz.py BASE OUT SEED: a damaged copy of the stats.json at BASE, written to OUT.

Each seed picks one way to damage it (cut short, bytes changed, tokens spliced in, nesting,
numbers and keys of every odd shape, a body over the fetcher's cap, a body of many entries),
so the same seed always writes the same file. community.sh feeds each one to az-community
refresh and checks the catalog stays well formed and every count is digits or nothing.
"""
import json
import random
import sys

CAP = 262144  # CAP_STATS in deck/lib/az-community


def main(base, out, seed):
    r = random.Random(seed)
    raw = open(base, "rb").read()
    doc = json.loads(raw)
    mods = doc["mods"]
    repo = next(iter(mods))
    kind = seed % 12
    if kind == 0:    # cut short anywhere
        body = raw[: r.randrange(len(raw))]
    elif kind == 1:  # a few bytes replaced with any byte
        b = bytearray(raw)
        for _ in range(r.randint(1, 8)):
            b[r.randrange(len(b))] = r.randrange(256)
        body = bytes(b)
    elif kind == 2:  # JSON tokens spliced in at random places
        b = raw
        for _ in range(r.randint(1, 6)):
            i = r.randrange(len(b))
            b = b[:i] + r.choice([b"{", b"}", b"[", b"]", b",", b":", b'"', b"\\", b"null", b"1e999", b"\\u0000"]) + b[i:]
        body = b
    elif kind == 3:  # nesting deeper than the parser takes
        d = r.randint(14, 40)
        body = json.dumps({"azmods_stats": 1, "mods": {repo: json.loads("[" * d + "1" + "]" * d)}}).encode()
    elif kind == 4:  # counts of every odd shape for valid ids
        odd = [0, 7, -1, 1.5, 1e3, 10 ** 13 - 1, 10 ** 13, 10 ** 30, "12", True, None, [], {}]
        mods[repo] = {"good-mod": r.choice(odd), "any-mod": r.choice(odd), "dd-mod": r.choice(odd)}
        body = json.dumps(doc).encode()
    elif kind == 5:  # ids and repository keys of every odd shape
        ids = ["", "a", "ab", "x" * 33, "UPPER", "a.b", "a b", "a\tb", "a|b", "../x", "été", "good-mod"]
        keys = [repo, repo.upper(), repo + "/", "http://x", "https://", "https://" + "x" * 400, "a.b.c", ""]
        doc["mods"] = {r.choice(keys): {r.choice(ids): r.randint(0, 99)} for _ in range(r.randint(1, 6))}
        body = json.dumps(doc).encode()
    elif kind == 6:  # over the size cap: refused, the last counts kept
        pad = "x" * (CAP + r.randint(1, 4096))
        doc["pad"] = pad
        body = json.dumps(doc).encode()
    elif kind == 7:  # many entries, under the cap
        n = r.randint(2000, 9000)
        mods[repo].update({"m%05d" % i: i for i in range(n)})
        body = json.dumps(doc, separators=(",", ":")).encode()
    elif kind == 8:  # not an object, or the wrong format number
        body = r.choice([b"[]", b"1", b'"x"', b"null", b"", b"\xef\xbb\xbf", json.dumps(dict(doc, azmods_stats="1")).encode(),
                         json.dumps(dict(doc, azmods_stats=1.0)).encode()])
    elif kind == 9:  # mods is not an object of objects
        doc["mods"] = r.choice([[], 1, "x", None, {repo: [1, 2]}, {repo: "good-mod"}, {repo: None}])
        body = json.dumps(doc).encode()
    elif kind == 10:  # escapes and control characters in keys and values
        k = r.choice(["good\\u002dmod", "good-mod\\u0000", "\\ud800", "good\\nmod", "good-mod\\t"])
        body = raw.replace(b'"good-mod"', ('"%s"' % k).encode(), 1)
    else:            # a byte-order mark, CR LF and data after the end
        body = b"\xef\xbb\xbf" + raw.replace(b",", b",\r\n") + r.choice([b"", b" ", b"{}", b"x"])
    open(out, "wb").write(body)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]))
