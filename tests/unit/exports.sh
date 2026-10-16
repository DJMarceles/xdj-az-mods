#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# exports.sh: the release .so files export exactly the symbols in exports-azmod.txt /
# exports-azstem.txt. Both libraries are preloaded into EP147, so any extra exported
# symbol can interpose on one of EP147's own; a missing one breaks an interposer or an
# interface the other library resolves with dlsym. Builds fresh (build-check.sh) first.
# Control: the same comparison against a list with one name removed must fail.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd); fail=0
"$root/tools/refactor/build-check.sh" "$root/guest" >/dev/null || { echo "FAIL  exports: release build failed"; exit 1; }
dyn() { docker run --rm --platform=linux/arm64 -v "$root/guest/out":/s:ro azmod-build:18.04 \
          sh -c "nm -D --defined-only /s/$1 | awk '{print \$3}' | sort"; }
check() { # lib list label -> 0 if identical
    if diff <(dyn "$1") "$2" >/tmp/exports.diff.$$; then return 0; fi
    sed 's/^/        /' /tmp/exports.diff.$$ | head -20; return 1; }
for lib in azmod azstem; do
    if check "$lib.so" "$here/exports-$lib.txt"; then echo "PASS  exports: $lib.so matches exports-$lib.txt"
    else echo "FAIL  exports: $lib.so differs from exports-$lib.txt (> = listed but not exported, < = exported but not listed)"; fail=1; fi
    sed 1d "$here/exports-$lib.txt" > /tmp/exports.ctl.$$
    if check "$lib.so" /tmp/exports.ctl.$$ >/dev/null; then echo "CONTROL DID NOT FAIL  exports: $lib (instrument broken)"; fail=1
    else echo "CONTROL FAILED AS REQUIRED  exports: $lib with one name removed"; fi
done
rm -f /tmp/exports.diff.$$ /tmp/exports.ctl.$$
[ $fail = 0 ] && echo "EXPORTS: OK" || echo "EXPORTS: FAILED"; exit $fail
