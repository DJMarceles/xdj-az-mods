#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# ep147-thread-lint.sh [GUEST_DIR]: code that runs on one of EP147's own threads must not
# block. Three rules, each over the text of named function bodies (callees are not followed:
# a blocking call moved into a helper passes this lint, so keep these bodies flat):
#
#   audio   azstem's hooks on EP147's audio and prefetch threads (hook_pbi, pbread_hook,
#           fread_hook, operate_hook, kf_hook, fgpr_op_hook, ringfill_hook) call no
#           sleep, file, lock or allocation function. logln() is allowed: it returns at
#           once unless settings/azmod-logs.on existed at start.
#   vinyl   the Vinyl Speed Adjust touch handler (vs_mouse, EP147's message thread) posts
#           the value to the overlay thread; it never calls vs_save()/settings_write(),
#           which fsync p7 twice.
#   smlock  every g_sm_mu lock goes through sm_lock()/sm_unlock(), which take the
#           SCHED_IDLE stems-mark worker out of the idle class while it holds the lock
#           EP147's row painters wait on.
#
# Control: the same scan over a copy with one planted violation per rule must report
# exactly those three.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
G=$(cd "${1:-$here/../../guest}" && pwd)

AUDIO_HOOKS="hook_pbi pbread_hook fread_hook operate_hook kf_hook fgpr_op_hook ringfill_hook"
BLOCKING='access|open|open64|openat|fopen|stat|lstat|read|write|pread|pwrite|usleep|sleep|nanosleep|fsync|fdatasync|sync|poll|select|pthread_mutex_lock|pthread_cond_wait|pthread_cond_timedwait|malloc|calloc|realloc|free|fprintf|printf|puts|system|popen|opendir|readdir|mprotect|mmap'

body() { # file name -> the function's body (from its definition line to the first "}" line)
    NAME="$2" perl -ne '
        if (!$p && /^static\b[^;]*\b\Q$ENV{NAME}\E\s*\(/ && !/;\s*$/) { $p = 1 }
        if ($p) { print; exit if /^}\s*$/ }' "$1"
}
strip() { # drop comments and string literals
    perl -0pe 's{/\*.*?\*/}{}gs; s{//[^\n]*}{}g; s/"(?:[^"\\]|\\.)*"/""/g'
}

scan() { # guest dir -> prints violations, exit 1 if any
    local d=$1 bad=0 n f b
    for n in $AUDIO_HOOKS; do
        f=$(grep -l -E "^static\b[^;]*\b$n\s*\(" "$d"/azstem/*.c 2>/dev/null | head -1)
        if [ -z "$f" ]; then echo "audio: $n not found in azstem/*.c"; bad=1; continue; fi
        b=$(body "$f" "$n" | strip)
        [ -n "$b" ] || { echo "audio: could not extract $n"; bad=1; continue; }
        hits=$(printf '%s\n' "$b" | grep -oE "\b($BLOCKING)\s*\(" | sort -u | tr -d ' (' | tr '\n' ' ')
        if [ -n "$hits" ]; then echo "audio: $n (${f#$d/}) calls: $hits"; bad=1; fi
    done
    b=$(body "$d/azmod/vinyl_slider.c" vs_mouse | strip)
    if [ -z "$b" ]; then echo "vinyl: could not extract vs_mouse"; bad=1
    elif printf '%s\n' "$b" | grep -qE '\b(vs_save|settings_write)\s*\('; then
        echo "vinyl: vs_mouse saves on EP147's message thread (vs_save/settings_write)"; bad=1
    fi
    hits=$(grep -nE 'pthread_mutex_(lock|unlock)\s*\(\s*&g_sm_mu\s*\)' "$d"/azmod/*.c "$d"/azmod/*.h 2>/dev/null |
           grep -v 'azmod_internal.h' || true)
    if [ -n "$hits" ]; then
        printf '%s\n' "$hits" | sed "s#^$d/#smlock: raw g_sm_mu use at #"; bad=1
    fi
    grep -q 'static inline void sm_lock' "$d/azmod/azmod_internal.h" ||
        { echo "smlock: sm_lock() missing from azmod_internal.h"; bad=1; }
    return $bad
}

echo "--- real tree"
scan "$G"; real=$?
[ $real -eq 0 ] && echo "PASS  ep147-thread: no blocking call in the listed EP147-thread bodies"

echo "--- control (one planted violation per rule)"
C=$(mktemp -d "${TMPDIR:-/tmp}/az-ep147lint.XXXXXX")
trap 'rm -rf "$C"' EXIT
mkdir -p "$C/azmod" "$C/azstem"
cp "$G"/azmod/*.c "$G"/azmod/*.h "$C/azmod/"; cp "$G"/azstem/*.c "$G"/azstem/*.h "$C/azstem/"
perl -0pi -e 's/(static Pos32 operate_hook\([^\n]*\{\n)/$1    access("\/tmp\/x", 0);\n/' "$C/azstem/key_shift.c"
perl -0pi -e 's/vs_save_post\(__atomic_load_n/vs_save(__atomic_load_n/' "$C/azmod/vinyl_slider.c"
perl -0pi -e 's/sm_lock\(\);/pthread_mutex_lock(&g_sm_mu);/' "$C/azmod/stem_marks.c"
out=$(scan "$C"); crc=$?
printf '%s\n' "$out" | sed 's/^/      /'
got=$(printf '%s\n' "$out" | grep -cE '^(audio: operate_hook|vinyl: vs_mouse|smlock: raw)')
if [ $crc -ne 0 ] && [ "$got" -eq 3 ]; then
    echo "CONTROL FAILED AS REQUIRED  ep147-thread: all three planted violations reported"
    ctl=0
else
    echo "BROKEN TEST: control reported $got of 3 planted violations (exit $crc)"
    ctl=1
fi
[ $real -eq 0 ] && [ $ctl -eq 0 ]
