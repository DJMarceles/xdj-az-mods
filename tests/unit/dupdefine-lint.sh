#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# dupdefine-lint.sh [dir...] : fails when one header defines the same macro twice at the top
# level. A repeated #define is legal C when the bodies match, so the compiler stays silent, and
# the copies drift apart later. A name #undef'd in between (the X-macro pattern) may be
# defined again. Definitions inside #if/#ifdef/#else blocks are skipped (two
# branches of one conditional may each define the name).
# Default directories: guest/azmod guest/azstem guest/common.
set -u
dirs=("$@")
[ ${#dirs[@]} -gt 0 ] || dirs=(guest/azmod guest/azstem guest/common)
bad=0
for f in $(find "${dirs[@]}" -maxdepth 1 -name '*.h' | sort); do
    out=$(awk '
        /^[ \t]*#[ \t]*if/   { depth++ }
        /^[ \t]*#[ \t]*endif/ { if (depth > 0) depth-- }
        /^#[ \t]*undef[ \t]+/ {
            name = $0; sub(/^#[ \t]*undef[ \t]+/, "", name); sub(/[^A-Za-z0-9_].*/, "", name)
            delete seen[name]
        }
        depth <= 1 && /^#[ \t]*define[ \t]+[A-Za-z_][A-Za-z0-9_]*/ {
            name = $0; sub(/^#[ \t]*define[ \t]+/, "", name); sub(/[^A-Za-z0-9_].*/, "", name)
            if (name in seen) printf "%s:%d: %s already defined at line %d\n", FILENAME, FNR, name, seen[name]
            else seen[name] = FNR
        }' "$f")
    if [ -n "$out" ]; then
        echo "$out"
        bad=1
    fi
done
[ $bad = 0 ] && echo "DUPLICATE DEFINES: none"
exit $bad
