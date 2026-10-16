#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# nofork-check.sh: the release azmod.so / azstem.so (guest/out, built by build-check.sh)
# import none of system, popen, fork, vfork, clone or the exec family: anything they start
# goes through posix_spawn (guest/common/azspawn.h), so EP147 is never forked.
# Control: a small library that calls system() must be reported.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd); fail=0
BAD='^(system|popen|fork|vfork|__clone|clone|execl|execle|execlp|execv|execve|execvp|execvpe|daemon)$'
for lib in azmod.so azstem.so; do
    [ -s "$root/guest/out/$lib" ] || { echo "FAIL  nofork: guest/out/$lib missing (build first)"; exit 1; }
done
out=$(docker run --rm --platform=linux/arm64 -v "$root/guest/out":/s:ro azmod-build:18.04 sh -c '
    for f in azmod.so azstem.so; do nm -D --undefined-only /s/$f | awk "{print \$2}" | sed "s/@.*//" | sed "s|^|$f |"; done
    printf "#include <stdlib.h>\nint planted(void){ return system(\"true\"); }\n" > /tmp/p.c
    gcc -shared -fPIC -o /tmp/planted.so /tmp/p.c && nm -D --undefined-only /tmp/planted.so | awk "{print \$2}" | sed "s/@.*//" | sed "s|^|planted.so |"')
hits=$(printf '%s\n' "$out" | awk -v re="$BAD" '$2 ~ re')
real=$(printf '%s\n' "$hits" | grep -v '^planted.so ' | grep . || true)
ctl=$(printf '%s\n' "$hits" | grep '^planted.so ' || true)
if [ -z "$real" ]; then echo "PASS  nofork: azmod.so and azstem.so import no system/popen/fork/exec"
else echo "FAIL  nofork: process-creating imports:"; printf '%s\n' "$real" | sed 's/^/      /'; fail=1; fi
if [ -n "$ctl" ]; then echo "CONTROL FAILED AS REQUIRED  nofork: a library calling system() is reported"
else echo "CONTROL DID NOT FAIL  nofork: planted system() not seen (instrument broken)"; fail=1; fi
[ $fail = 0 ] && echo "NOFORK: OK" || echo "NOFORK: FAILED"; exit $fail
