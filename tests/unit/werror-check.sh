#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# werror-check.sh <dir> [make args...] : the compile lines `make` issues in <dir> for azmod.so,
# azstem.so and az-handoff-guard must turn a new -Wall warning and a new -Wextra warning into a
# build failure, and must still compile a clean file. Runs where gcc is the release compiler
# (the azmod-build:18.04 container); run.sh starts it there.
#
# A planted file with an unused static function (-Wall) and an unused parameter (-Wextra) is
# compiled with each line's own flags (the link-only flags dropped, -c added): it must fail with
# both diagnostics as errors. The same flags must compile a file without them.
set -u
dir=$1; shift
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
printf 'static int planted(int unused) { return 0; }\n' > "$t/planted.c"
printf 'int clean(int x) { return x; }\n' > "$t/clean.c"
make -s -n -B -C "$dir" CC=gcc "$@" azmod azstem azhandoff > "$t/lines" || { echo "FAIL make -n in $dir"; exit 1; }
fail=0 n=0
for out in azmod.so.full azstem.so.full az-handoff-guard.full; do
    line=$(grep "^gcc .* -o out/$out " "$t/lines")
    [ "$(printf '%s\n' "$line" | grep -c .)" = 1 ] || { echo "FAIL $out: expected one compile line"; fail=1; continue; }
    flags=()
    for w in ${line#gcc }; do
        [ "$w" = -o ] && break
        case $w in -shared | -static | -static-libgcc | -Wl,*) ;; *) flags+=("$w") ;; esac
    done
    n=$((n + 1))
    if ! (cd "$dir" && gcc "${flags[@]}" -c -o /dev/null "$t/clean.c") > "$t/log" 2>&1; then
        echo "FAIL $out: its flags do not compile a clean file"; sed 's/^/      /' "$t/log"; fail=1; continue
    fi
    if (cd "$dir" && gcc "${flags[@]}" -c -o /dev/null "$t/planted.c") > "$t/log" 2>&1; then
        echo "FAIL $out: a new warning still builds (${flags[*]})"; fail=1; continue
    fi
    for w in unused-function unused-parameter; do
        grep -q "\[-Werror=$w\]" "$t/log" || { echo "FAIL $out: -W$w is not an error"; sed 's/^/      /' "$t/log"; fail=1; }
    done
    echo "ok   $out: -Wall and -Wextra warnings are errors"
done
[ "$n" = 3 ] || fail=1
exit $fail
