#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Stage MODS/deck -- the deck-side scripts that ride the USB stick (firmware >= 1.32).
For release builds and for trying a change by hand: the output is what the AZ-MODS app
writes to <stick>/MODS/deck/.

    python3 stage_stick.py <out_dir> [--version <deck-set-version>] [--manifest <json>] [--no-guard]

<out_dir> becomes exactly that folder:

    deck/version            the deck-set version (logged by the loader, echoed to .deck-info)
    deck/az-usbload         the loader itself: the firmware copy chain-loads this one
    deck/lib/az-usbpick     the loader's helpers, preferred over the firmware copies
    deck/lib/usb-renotify.sh
    deck/lib/azrs.sh        the Remote Stems helper azmod starts (deck/azrs in this repository)
    deck/lib/az-handoff-guard   the way back to the deck during a mod's handoff (the SDK's
                            handoff): built with the mods (`make CC=gcc azhandoff` in the
                            azmod-build:18.04 container) and taken from their out/. A release
                            always carries it; --no-guard (the loader's own tests) leaves it out
    deck/lib/az-modupdate   the on-deck mods updater (stick/deck/lib; no firmware twin)
    deck/lib/az-community   the Community Mods fetcher (stick/deck/lib)
    deck/lib/az-savelogs    System Info's Save Logs to USB (stick/deck/lib)
    deck/lib/az-conf.sh     MODS/azmods.cfg read/write rules (az-usbload, az-community)
    deck/lib/gadget-renotify.sh, deck/lib/az-logcap.sh   the other stick/deck/lib helpers
    deck/lib/dropbear[.LICENSE]   the SSH server for firmware without one, if present
    deck/boot.d/NN-*.sh     hooks the loader runs in name order (stick/deck/boot.d)

One source for each file: the loader and its helpers come from payload/ (the same
bytes the firmware carries; a deck-side fix is a change to payload/ that then
ships over USB), the hooks and the other helpers from stick/deck. Shell scripts are
comment-stripped and `sh -n` gated like the firmware payload (strip_payload), so the
tests in test/ exercise what ships. The result is not yet trusted by a deck: the
loader runs only files pinned by a signed deck/trust.pins, which make_pins.py writes.

--manifest writes a JSON fragment: the file list with sha256s in the shape of an
AZ-MODS manifest.json "files" array (dir-relative to MODS/), for the bundle builder.

The mods' deck-side sources (FROM_EMU) are part of this repository (deck/azrs/), and the
handoff guard is this repository's build output (src/out/az-handoff-guard, from
`make CC=gcc azhandoff`):

    AZ_HANDOFF_GUARD  the built az-handoff-guard to stage instead of src/out/az-handoff-guard

The first line printed names the source: the rev and its full sha, or the worktree,
its branch and sha.
"""
import hashlib, json, os, shutil, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import strip_payload

HERE    = os.path.dirname(os.path.abspath(__file__))
PAYLOAD = os.path.join(HERE, "payload")
STICK   = os.path.join(HERE, "stick", "deck")
# the mods' own deck-side helpers live beside the mods' sources
EMU     = os.path.dirname(HERE)   # this repository
EMU_REV_DEFAULT = "HEAD"

# (published path under deck/, source path)
FROM_PAYLOAD = [
    ("az-usbload",          "usr/sbin/az-usbload"),
    ("lib/az-usbpick",      "usr/sbin/az-usbpick"),
    ("lib/usb-renotify.sh", "home/root/scripts/usb-renotify.sh"),
]
# (published path under deck/, path under EMU). azmod runs lib/azrs.sh
# from the stage (settings/az-usbload/deck) for MENU > MAKE STEMS.
FROM_EMU = [
    ("lib/azrs.sh",         "deck/azrs/azrs.sh"),
]
# built with the mods against the release libraries (static aarch64 ELF), copied verbatim
FROM_EMU_BUILT = [
    ("lib/az-handoff-guard", "src/out/az-handoff-guard"),
]

def _git(*args):
    r = subprocess.run(["git", "-C", EMU] + list(args), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0:
        raise SystemExit(f"REFUSING: git -C {EMU} {' '.join(args)} failed: "
                         f"{r.stderr.decode(errors='replace').strip()}")
    return r.stdout

def emu_source():
    """(label, read, tree): where FROM_EMU files come from. read(src) -> bytes or None.
    tree is True when EMU's files on disk are the source (its build output then counts too)."""
    def from_disk(src):
        path = os.path.join(EMU, src)
        return open(path, "rb").read() if os.path.isfile(path) else None
    if os.path.realpath(EMU) == os.path.realpath(os.path.dirname(HERE)):
        return f"tree {EMU}", from_disk, True        # the sources are part of this tree
    rev = os.environ.get("AZ_EMU_REV") or EMU_REV_DEFAULT
    if os.environ.get("AZ_EMU_WORKTREE") == "1":
        branch = _git("rev-parse", "--abbrev-ref", "HEAD").decode().strip()
        want = rev[len("origin/"):] if rev.startswith("origin/") else rev
        if branch != want:
            raise SystemExit(f"REFUSING: AZ_EMU_WORKTREE=1 but {EMU} is on branch '{branch}', "
                             f"not '{want}' (AZ_EMU_REV={rev}). Check out {want} there, or unset "
                             f"AZ_EMU_WORKTREE to read {rev} from git")
        dirty = _git("status", "--porcelain").decode().strip()
        if dirty:
            raise SystemExit(f"REFUSING: AZ_EMU_WORKTREE=1 but {EMU} has uncommitted changes:\n"
                             + "\n".join("  " + ln for ln in dirty.splitlines()[:20]))
        sha = _git("rev-parse", "HEAD").decode().strip()
        return f"worktree {EMU} @ {branch} {sha}", from_disk, True
    if rev.startswith("origin/"):
        r = subprocess.run(["git", "-C", EMU, "fetch", "-q", "origin", rev[len("origin/"):]],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if r.returncode != 0:
            raise SystemExit(f"REFUSING: could not fetch {rev} into {EMU}: "
                             f"{r.stderr.decode(errors='replace').strip()}")
    sha = _git("rev-parse", "--verify", "--end-of-options", rev + "^{commit}").decode().strip()
    def from_rev(src):
        r = subprocess.run(["git", "-C", EMU, "show", f"{sha}:{src}"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return r.stdout if r.returncode == 0 else None
    return f"rev {rev} {sha}", from_rev, False

def stage(out_dir, version, guard=True):
    label, read_emu, tree = emu_source()
    print(f"[stick] emu sources: {label}")
    guard_path = os.environ.get("AZ_HANDOFF_GUARD") or (os.path.join(EMU, FROM_EMU_BUILT[0][1]) if tree else None)
    if guard and guard_path is None:
        raise SystemExit(f"REFUSING: {label} has no build output: set AZ_HANDOFF_GUARD to the "
                         f"az-handoff-guard built from that rev (or AZ_EMU_WORKTREE=1)")
    if os.path.exists(out_dir):
        shutil.rmtree(out_dir)
    files = []
    def put(rel, data, is_sh, strip=True):
        dest = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        if is_sh and strip:
            data = strip_payload.strip_shell(data, rel)
        open(dest, "wb").write(data)
        if is_sh:
            strip_payload.sh_n(dest)
        files.append(rel)
        print(f"      {rel} {len(data)} B")
    for rel, src in FROM_PAYLOAD:
        put(rel, open(os.path.join(PAYLOAD, src), "rb").read(), True)
    for rel, src in FROM_EMU:
        path = os.path.join(EMU, src)
        data = read_emu(src)
        if data is None:
            raise SystemExit(f"REFUSING: no {path} (deck/azrs/azrs.sh is part of this repository)")
        # verbatim: it has a heredoc, which strip_shell refuses; still `sh -n` gated
        put(rel, data, True, strip=False)
    for rel, _ in (FROM_EMU_BUILT if guard else []):
        path = guard_path
        data = open(path, "rb").read() if os.path.isfile(path) else b""
        if data[:4] != b"\x7fELF":
            raise SystemExit(f"REFUSING: no built {path} (build it first: make CC=gcc azhandoff "
                             f"in the azmod-build:18.04 container)")
        put(rel, data, False)
    # stick-only helpers (no firmware twin): deck/lib/az-modupdate and friends
    for f in sorted(os.listdir(os.path.join(STICK, "lib"))):
        if f.startswith(".") or f.endswith(".md"):
            continue
        data = open(os.path.join(STICK, "lib", f), "rb").read()
        # ELF binaries (lib/dropbear, for firmware 2.00 which has none) and license texts
        # ride verbatim: they are not shell, so no strip and no `sh -n`
        put(f"lib/{f}", data, not (data[:4] == b"\x7fELF" or f.endswith(".LICENSE")))
    hooks = sorted(f for f in os.listdir(os.path.join(STICK, "boot.d"))
                   if f.endswith(".sh") and not f.startswith("."))
    if not hooks:
        raise SystemExit("REFUSING: stick/deck/boot.d has no hooks")
    for f in hooks:
        put(f"boot.d/{f}", open(os.path.join(STICK, "boot.d", f), "rb").read(), True)
    put("version", (version + "\n").encode(), False)
    return files

def manifest_files(out_dir, files):
    out = []
    for rel in files:
        d, name = os.path.split(rel)
        h = hashlib.sha256(open(os.path.join(out_dir, rel), "rb").read()).hexdigest()
        out.append({"name": name, "sha256": h, "dir": "deck/" + d if d else "deck"})
    return out

if __name__ == "__main__":
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    out = args[0]; version = "0.0.0"; mf = None; guard = True
    i = 1
    while i < len(args):
        if args[i] == "--version": version = args[i + 1]; i += 2
        elif args[i] == "--manifest": mf = args[i + 1]; i += 2
        elif args[i] == "--no-guard": guard = False; i += 1
        else: raise SystemExit(f"unknown arg {args[i]}")
    print(f"[stick] staging MODS/deck {version} -> {out}")
    files = stage(out, version, guard)
    if mf:
        json.dump(manifest_files(out, files), open(mf, "w"), indent=2)
        print(f"[stick] manifest fragment -> {mf} ({len(files)} files)")
