#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# busthread-lint.sh [bus_interpose.c]: the mixer-bus read() interposer makes no call into
# EP147's UI, the banner, the drag's message-thread steps, or code patching.
#
# read() runs on EP147's real-time bus read thread: a call there that blocks or enters
# JUCE/EP147 UI code can stall every control. The drag's end is recorded there
# (dh_post, guest/azmod/drag_handoff.h) and performed by the message tick. Comments are
# stripped first, so a comment naming a function is not a hit. Exit 1 on any hit.
set -euo pipefail
f=${1:-"$(dirname "$0")/../../guest/azmod/bus_interpose.c"}
# The read path: read() and every helper it calls, from the section marker to the end of the file.
body=$(awk '/^\/\* ---- read\(\): the panel input buses/ {p=1} p {print}' "$f" |
       perl -0pe 's{/\*.*?\*/}{}gs; s{//[^\n]*}{}g')
[ -n "$body" ] || { echo "busthread-lint: the read() section not found in $f"; exit 2; }
deny='\b(drag_commit|drag_cancel|drag_end_local|drag_handoff_drain|drag_update_content|drag_pin|drag_try_grab|drag_scroll_to|fl_start|fl_tick|knob_cursor_to|az_banner_[a-z_]+|g_orig_[a-z_]+|LISTBOX_[A-Z_]+|VIEWPORT_[A-Z_]+|EP_[0-9a-f]+|azm_patch_vtable|azm_install_hook|mprotect|azprot_[a-z_]+|pthread_mutex_lock|usleep|nanosleep)\s*\('
hits=$(printf '%s\n' "$body" | grep -nE "$deny" || true)
if [ -n "$hits" ]; then
    echo "FAIL  busthread-lint: read() calls message-thread / UI / patching code:"
    printf '%s\n' "$hits" | sed 's/^/      /'
    exit 1
fi
echo "PASS  busthread-lint: read() makes no UI, banner, drag-step or patching call ($(printf '%s\n' "$body" | wc -l | tr -d ' ') lines checked)"
