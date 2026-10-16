#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""compdb.py: compile_commands.json for the static check, built from the real lines of guest/Makefile.

Reads `make -n -B azmod azstem azhandoff CC=gcc` (run in guest/) on stdin and writes one entry
per C translation unit to stdout. Each entry keeps the line's preprocessor and code-generation
flags (-I, -D, -O, -f, -std=) and drops what only the link or the warning policy needs (-W, -w, -o,
-shared, -static*, -l, -Wl): the analyzers report their own findings, and a compiler warning is
already an error in the real build. Vendored code (stb_vorbis.c) has no entry.

  compdb.py <guest-dir> < make-n-output > compile_commands.json
"""
import json
import shlex
import sys

SKIP_SOURCES = {"azstem/stb_vorbis.c"}  # vendored, kept as upstream wrote it


def main():
    guest = sys.argv[1]
    entries = []
    for line in sys.stdin:
        toks = shlex.split(line)
        if not toks or toks[0] != "gcc":
            continue
        flags, srcs, skip_next = [], [], False
        for t in toks[1:]:
            if skip_next:
                skip_next = False
                continue
            if t == "-o":
                skip_next = True
            elif t.endswith(".c"):
                srcs.append(t)
            elif t.startswith(("-I", "-D", "-O", "-f", "-std=")):
                flags.append(t)
        for s in srcs:
            if s in SKIP_SOURCES:
                continue
            entries.append({"directory": guest, "file": s, "arguments": ["gcc"] + flags + ["-c", s]})
    if not entries:
        sys.exit("compdb.py: no gcc compile lines in the input")
    json.dump(entries, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
