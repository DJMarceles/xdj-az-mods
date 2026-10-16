#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Stage the release payload: strip comments/blank lines, drop release-excluded
files, and refuse anything `sh -n` rejects.

Stripping happens at build time; the repo keeps fully commented sources. The firmware
image has a fixed byte budget for the payload, and comments and blank lines are bytes
that change no behavior, so the release carries none.

What it does to <in> to produce <out> (both directories):
  - shell scripts (usr/sbin/az-* and *.sh): whole-line comments, trailing
    comments, and blank lines removed. The scanner is stateful per POSIX rules:
    it never touches anything inside single/double quotes (quotes carry across
    lines, so multi-line awk programs pass verbatim), never inside ${...}, and
    only cuts '#' at a word start (after whitespace or ; & | ( ), never after
    a normal character, so `${src##*/}` and `foo#bar` are untouched). The
    shebang line is kept.
  - systemd units (*.service, *.conf): whole-line '#' or ';' comments and blank
    lines removed (trailing comments are not cut: systemd does not strip
    inline comments itself).
  - everything else copied byte-exact.
  - EXCLUDE: dropped from the staged tree (kept in the repo, out of the release).
    az-bootlog is a diagnostic.

Every stripped shell script must pass `sh -n`, and test/run.sh runs the same staging
first and exercises the stripped scripts in an alpine container.
"""
import os, shutil, subprocess, sys

# Files that stay in the repo but are not in the release build.
EXCLUDE = {
    "usr/sbin/az-bootlog",                      # diagnostic; not release payload
    "etc/systemd/system/az-bootlog.service",
}

WORD_START = (None, b" ", b"\t", b";", b"|", b"&", b"(", b")", b"\n")

def _scan_line(line, quote, depth):
    """Cut index of a trailing comment in `line`, else None.

    `quote` (b"'" / b'"' / None) and `depth` (${ nesting) carry across lines.
    Returns (cut, quote, depth) with the state at end-of-line.
    """
    prev = None
    i = 0
    n = len(line)
    while i < n:
        c = line[i:i+1]
        if quote == b"'":
            if c == b"'": quote = None
            prev = c; i += 1; continue
        if quote == b'"':
            if c == b"\\":
                prev = c; i += 2; continue          # backslash keeps the next byte literal
            if c == b'"': quote = None
            prev = c; i += 1; continue
        if c == b"\\":
            prev = c; i += 2; continue
        if c == b"'" or c == b'"':
            quote = c; prev = c; i += 1; continue
        if c == b"$" and i + 1 < n and line[i+1:i+2] == b"{":
            depth += 1; prev = b"{"; i += 2; continue
        if c == b"}" and depth > 0:
            depth -= 1; prev = c; i += 1; continue
        if c == b"#" and depth == 0 and prev in WORD_START:
            return i, quote, depth
        prev = c; i += 1
    return None, quote, depth

def strip_shell(src, name):
    if b"<<" in src:
        raise SystemExit(f"REFUSING to strip {name}: heredocs not handled "
                         "(heredoc bodies must stay verbatim)")
    out = []
    quote = None
    depth = 0
    for idx, line in enumerate(src.split(b"\n")):
        if idx == 0 and line.startswith(b"#!"):
            out.append(line)
            continue
        cut, quote, depth = _scan_line(line, quote, depth)
        kept = line if cut is None else line[:cut].rstrip()
        if kept:
            out.append(kept)
    return b"\n".join(out)

def strip_unit(src):
    out = []
    for line in src.split(b"\n"):
        t = line.lstrip()
        if not t or t.startswith(b"#") or t.startswith(b";"):
            continue
        out.append(line)
    return b"\n".join(out)

def sh_n(path):
    r = subprocess.run(["sh", "-n", path], capture_output=True)
    if r.returncode != 0:
        raise SystemExit(f"REFUSING: `sh -n` failed on stripped {path}:\n"
                         f"{r.stderr.decode(errors='replace')}")

def stage(in_dir, out_dir):
    if os.path.exists(out_dir):
        shutil.rmtree(out_dir)
    raw = stripped = 0
    for root, _, files in os.walk(in_dir):
        for f in sorted(files):
            if f == ".DS_Store":      # Finder litter in a working copy is not payload
                continue
            p = os.path.join(root, f)
            # "/"-separated on every host: EXCLUDE and the usr/sbin test below are
            # written that way, and os.walk gives "\" on Windows.
            rel = os.path.relpath(p, in_dir).replace(os.sep, "/")
            if rel in EXCLUDE:
                print(f"      drop (release-excluded): {rel}")
                continue
            data = open(p, "rb").read()
            raw += len(data)
            dest = os.path.join(out_dir, rel)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            kind = None
            if f.startswith("az-") and root.replace(os.sep, "/").endswith("usr/sbin") or f.endswith(".sh"):
                data2 = strip_shell(data, rel); kind = "sh"
            elif f.endswith(".service") or f.endswith(".conf"):
                data2 = strip_unit(data); kind = "unit"
            else:
                data2 = data
            open(dest, "wb").write(data2)
            stripped += len(data2)
            if kind:
                print(f"      {kind}: {rel} {len(data)} -> {len(data2)} B")
                if kind == "sh":
                    sh_n(dest)
    print(f"    staged payload: {raw} -> {stripped} B raw "
          f"(-{raw - stripped} B, {len(EXCLUDE)} file(s) excluded)")
    return raw, stripped

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    stage(sys.argv[1], sys.argv[2])
