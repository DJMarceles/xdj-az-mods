#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# hookorder-lint.sh [GUEST_DIR]: azmod's hooks go in from its constructor, before EP147's
# main, never from a thread racing EP147's start :
#   1. azmod_init calls install_static_hooks(), then azm_hooks_go_late(), then starts the
#      overlay thread, in that order (after azm_hooks_go_late a 16-byte patch is refused).
#   2. The overlay thread installs nothing: overlay_thread and its ov_* stages make no
#      install_*() call and no azm_install_hook / azm_patch_vtable / azm_replace_fn call.
#      (community_init is late by design: its hooks are island-only, enforced by
#      azm_install_hook.)
#   3. install_static_hooks really holds the installers (a renamed or emptied one would pass 2).
# Control (tools/unit-tests/run.sh): copies with an installer moved back onto the overlay
# thread, and with azm_hooks_go_late before install_static_hooks, must fail.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
G=$(cd "${1:-$here/../../guest}" && pwd)
body() { RE="$2" awk '$0 ~ ENVIRON["RE"] {p=1} p {print} p && /^}$/ {exit}' "$1"; }
bad=0
fail() { echo "FAIL  hook order: $*"; bad=1; }

init=$(body "$G/azmod/azmod.c" '^__attribute__\(\(constructor\)\) static void azmod_init\(')
[ -n "$init" ] || fail "azmod_init not found in azmod.c"
line_of() { printf '%s\n' "$init" | grep -n -E "$1" | head -1 | cut -d: -f1; }
a=$(line_of '^[[:space:]]*install_static_hooks\(\);')
b=$(line_of '^[[:space:]]*azm_hooks_go_late\(\);')
c=$(line_of 'az_thread_create\([^)]*overlay_thread')
if [ -z "$a" ] || [ -z "$b" ] || [ -z "$c" ]; then
    fail "azmod_init must call install_static_hooks(), azm_hooks_go_late() and start overlay_thread (found: ${a:-none} ${b:-none} ${c:-none})"
elif ! [ "$a" -lt "$b" ] || ! [ "$b" -lt "$c" ]; then
    fail "azmod_init order is install_static_hooks@$a azm_hooks_go_late@$b overlay_thread@$c (want increasing)"
fi

ov=$(body "$G/azmod/mod_menu_overlay.c" '^static void \*overlay_thread\(void \*arg\) \{')
[ -n "$ov" ] || fail "overlay_thread not found in mod_menu_overlay.c"
# the thread's stages: every ov_* function in the file, each body whole
stages=$(awk '/^static [^(]*[ *]ov_[a-z0-9_]+\(.*\{$/ {p=1} p {print} p && /^}$/ {p=0}' \
    "$G/azmod/mod_menu_overlay.c")
[ -n "$stages" ] || fail "no ov_* stage functions found in mod_menu_overlay.c"
ov=$(printf '%s\n%s\n' "$ov" "$stages")
# comments stripped, so a mention in prose does not count
calls=$(printf '%s\n' "$ov" | sed -e 's://.*$::' -e 's:/\*.*\*/::g' |
    grep -n -E '\b(install_[a-z0-9_]+|azm_install_hook|azm_patch_vtable|azm_replace_fn)[[:space:]]*\(' || true)
if [ -n "$calls" ]; then
    printf '%s\n' "$calls" | sed 's/^/      overlay thread:/'
    fail "the overlay thread installs hooks (move them into install_static_hooks)"
fi

st=$(body "$G/azmod/mod_menu_overlay.c" '^static void install_static_hooks\(void\) \{')
for need in install_wavestems_hook install_bpmstate_hook install_srcrow_hook install_reorder install_rowcache; do
    printf '%s\n' "$st" | grep -q -E "^[[:space:]]*(if \(.*\)[[:space:]]*)?$need\(\);" ||
        fail "install_static_hooks does not call $need()"
done

if [ "$bad" = 0 ]; then
    echo "PASS  hook order: every installer runs from azmod's constructor, before azm_hooks_go_late"
    exit 0
fi
exit 1
