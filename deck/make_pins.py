#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Write MODS/deck/trust.pins for a mods set, and (optionally) sign it.

    python3 make_pins.py <mods_dir> [--sign <minisign secret key>]

<mods_dir> is the set as it will land in <stick>/MODS/ (the AZ-MODS bundle dir: the
.so files at the top, deck/ beside them). trust.pins names, by sha256, every file the
deck may execute from the stick: every file under deck/ (but trust.pins and its
.minisig) and every top-level *.so. az-usbload runs or stages nothing else (its TRUST
block), so a file added to deck/ after this ran will not run on a deck. A developer
can bypass this on one deck with the p7 flag settings/azmods-unsigned.on.

Order, at release time:
  1. stage deck/ (stage_stick.py) and put the release .so files beside it;
  2. make_pins.py <mods_dir> --sign <key>   -> deck/trust.pins + deck/trust.pins.minisig;
  3. add both files to manifest.json like any other deck/ file (stage_stick.py's
     manifest fragment shape: {"name", "sha256", "dir": "deck"});
  4. sign manifest.json as before (publish-mods-release.sh).
The manifest then pins the signed pins, so every installer that copies a set (the app,
the deck's az-modupdate) delivers them without knowing what they are.

--sign runs `minisign -S -s <key> -m trust.pins -x trust.pins.minisig` (minisign's
default prehashed signature, the only kind az-usbload accepts). Without --sign only
trust.pins is written, and the set will not run on a deck until it is signed.
"""
import hashlib, os, subprocess, sys

HEADER = "azmods-pins 1"
SKIP = {"trust.pins", "trust.pins.minisig"}

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def pins(mods_dir):
    out = []
    deck = os.path.join(mods_dir, "deck")
    if not os.path.isdir(deck):
        raise SystemExit(f"REFUSING: {deck} does not exist")
    for root, dirs, files in os.walk(deck):
        dirs[:] = sorted(d for d in dirs if not d.startswith("._"))
        for f in sorted(files):
            if f in SKIP or f.startswith("._"):
                continue
            rel = os.path.relpath(os.path.join(root, f), mods_dir).replace(os.sep, "/")
            out.append((rel, sha256(os.path.join(root, f))))
    for f in sorted(os.listdir(mods_dir)):
        p = os.path.join(mods_dir, f)
        if f.endswith(".so") and not f.startswith(".") and os.path.isfile(p):
            out.append((f, sha256(p)))
    bad = [r for r, _ in out if not all(c.isalnum() or c in "._+-/" for c in r) or "/." in "/" + r]
    if bad:
        raise SystemExit(f"REFUSING: names the loader will not accept: {bad}")
    if not any(r.endswith(".so") for r, _ in out):
        raise SystemExit("REFUSING: no .so at the top of the set (a set without its core pins nothing EP147 loads)")
    return out

def main(argv):
    if not argv:
        raise SystemExit(__doc__)
    mods_dir, key = argv[0], None
    if argv[1:2] == ["--sign"]:
        key = argv[2]
    elif argv[1:]:
        raise SystemExit(f"unknown args {argv[1:]}")
    lines = pins(mods_dir)
    path = os.path.join(mods_dir, "deck", "trust.pins")
    with open(path, "w", newline="\n") as f:
        f.write(HEADER + "\n")
        for rel, h in lines:
            f.write(f"{rel} {h}\n")
    print(f"[pins] {path}: {len(lines)} files")
    if key:
        subprocess.run(["minisign", "-S", "-s", key, "-m", path, "-x", path + ".minisig"],
                       stdin=subprocess.DEVNULL, check=True)
        print(f"[pins] signed -> {path}.minisig")

if __name__ == "__main__":
    main(sys.argv[1:])
