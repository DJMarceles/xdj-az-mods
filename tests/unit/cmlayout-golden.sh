#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# cmlayout-golden.sh [GUEST_DIR] [OUT_DIR]: lay out the Community Mods screens over the catalog
# and installed-mod test fixtures (cmlayout_digest.h) and compare the MD5 of every layout's digest
# with cmlayout.md5. Exit 0 = the screens are laid out and drawn as recorded.
# After an intended change to these screens, record the new value with --update.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
update=0
if [ "${1:-}" = "--update" ]; then
    update=1
    shift
fi
G=$(cd "${1:-$here/../../guest}" && pwd)
O=${2:-$(mktemp -d)}
mkdir -p "$O"
: > "$O/digest.txt"
for t in cmcat cmmod; do
    sed 's/g_nctl = cm_layout(&g_s, &g_fb, g_ctl, 1);/g_nctl = cm_layout(\&g_s, \&g_fb, g_ctl, 1); CMLAYOUT_DIGEST();/' \
        "$here/${t}_test.c" > "$O/${t}_test.c"
    cat "$here/cmlayout_digest.c" >> "$O/${t}_test.c"
    grep -q "CMLAYOUT_DIGEST();" "$O/${t}_test.c" || { echo "cmlayout-golden: no cm_layout call found in ${t}_test.c"; exit 2; }
    extra=()
    [ "$t" = cmmod ] && extra=(-Dazcm_count=lanetest_azcm_count -Dazcm_info=lanetest_azcm_info)
    ${CC:-cc} -w -I"$G/azmod" -Dmain=lanetest_main -Dazmod_ttf_menu=lanetest_ttf_menu \
        -Dazmod_ttf_menu2=lanetest_ttf_menu2 "${extra[@]}" -c -o "$O/stubs-$t.o" "$G/azmod/lanetest.c"
    ${CC:-cc} -std=gnu11 -O1 -w -I"$G/azmod" -I"$here" -include "$here/cmlayout_digest.h" \
        -DMENU_C="\"$G/azmod/menu.c\"" -DCM_CATALOG="\"$O/catalog-$t\"" -o "$O/$t" "$O/${t}_test.c" "$O/stubs-$t.o"
    [ "${AZ_COMPILE_ONLY:-0}" != 1 ] || continue # built, not run (run.sh's compile-only pass)
    CMLAYOUT_OUT="$O/digest.txt" "$O/$t" > "$O/$t.log" 2>&1 || { echo "cmlayout-golden: ${t}_test failed"; tail -5 "$O/$t.log"; exit 2; }
done
[ "${AZ_COMPILE_ONLY:-0}" != 1 ] || exit 0
layouts=$(wc -l < "$O/digest.txt" | tr -d ' ')
got=$({ md5sum 2>/dev/null || md5 -r; } < "$O/digest.txt" | cut -c1-32)
if [ "$update" = 1 ]; then
    echo "$got" > "$here/cmlayout.md5"
    echo "cmlayout-golden: recorded $got ($layouts layouts)"
    exit 0
fi
want=$(cut -c1-32 "$here/cmlayout.md5")
if [ "$got" = "$want" ]; then
    echo "PASS  cmlayout-golden: $layouts Community Mods layouts drawn as recorded ($got)"
else
    echo "FAIL  cmlayout-golden: digest $got, recorded $want ($layouts layouts: a change in how the Community Mods screens draw)"
    exit 1
fi
