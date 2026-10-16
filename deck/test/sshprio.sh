#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# deck/lib/az-sshprio under real scheduling (the container is privileged, so SCHED_RR works):
# during EP147's start window the dropbear listener is real-time and every session is put back
# to SCHED_OTHER within a sweep; after the window, after `normal` and after the window is
# stopped, nothing dropbear runs is real-time. Run by test/run.sh (`test/run.sh sshprio`).
#
# A fake listener and fake sessions stand in for dropbear: a session is started at RR 50, as it
# would inherit it from the boosted listener. The cgroup's task list is a file (AZ_SSHPRIO_TASKS).
# CONTROLS, same run: copies of az-sshprio with the per-sweep demotion and the end-of-window
# demotion removed must each fail a check.
#
# It runs under the deck's own userland: the extracted 2.01 rootfs (busybox chrt, which cannot
# set SCHED_RESET_ON_FORK, and bash as /bin/sh, as systemd runs `/bin/sh /run/az-sshprio`),
# mounted read-only at /lower by run.sh and overlaid writable, then chroot'ed into.
set -u
if [ "${SSHPRIO_INNER:-}" != 1 ]; then
    [ -x /lower/bin/busybox ] || { echo "FAIL no deck rootfs mounted at /lower (set AZ_DECK_ROOTFS for run.sh)"; exit 1; }
    [ -f /d/lib/az-sshprio ] || { echo "FAIL no staged deck/lib/az-sshprio"; exit 1; }
    mkdir -p /ov /deck && mount -t tmpfs ov /ov && mkdir -p /ov/u /ov/w
    mount -t overlay overlay -o lowerdir=/lower,upperdir=/ov/u,workdir=/ov/w /deck || { echo "FAIL overlay mount"; exit 1; }
    mount --bind /dev /deck/dev; mount -t proc proc /deck/proc; mount -t tmpfs tmp /deck/tmp
    mkdir -p /deck/d/lib; cp /d/lib/az-sshprio /deck/d/lib/; cp "$0" /deck/tmp/sshprio-inner.sh
    exec chroot /deck /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin SSHPRIO_INNER=1 /bin/sh /tmp/sshprio-inner.sh
fi
command -v chrt >/dev/null 2>&1 || { echo "FAIL no chrt in the deck rootfs (instrument broken)"; exit 1; }
echo "== userland: $(readlink -f "$(command -v chrt)"), /bin/sh = $(readlink -f /bin/sh)"
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }
# policy of task $1, from /proc/PID/stat field 41 (read past the comm); 0 OTHER, 1 FIFO, 2 RR
pol() { awk '{ sub(/.*\) /, ""); print $39 % 1073741824 }' "/proc/$1/stat" 2>/dev/null; }
spin() { sh -c 'while :; do sleep 1; done' >/dev/null 2>&1 & echo $!; }
TF=/tmp/sshprio.tasks
cp /d/lib/az-sshprio /tmp/sp.real
sed 's/chrt -o -p 0 "\$t" >\/dev\/null 2>&1 \&\& log/false \&\& log/' /tmp/sp.real > /tmp/sp.nosweep
sed -e 's/^\( *\)chrt -o -p 0 "\$mp" >\/dev\/null 2>&1$/\1:/' -e 's/^\( *\)demote_all_but 0$/\1:/' /tmp/sp.real > /tmp/sp.noend
cmp -s /tmp/sp.real /tmp/sp.nosweep && { echo "FAIL control nosweep did not apply"; exit 1; }
cmp -s /tmp/sp.real /tmp/sp.noend && { echo "FAIL control noend did not apply"; exit 1; }

# round <label> <script>: listener L, a session S started mid-window at RR 50, a 3 s window
round() {
    L=$(spin); echo "$L" > $TF
    chk "$1 CONTROL: the listener starts at SCHED_OTHER" '[ "$(pol $L)" = 0 ]'
    AZ_SSHPRIO_MAIN=$L AZ_SSHPRIO_TASKS=$TF AZ_SSHPRIO_TICK=0.2 sh "$2" window 3 & W=$!
    sleep 0.5
    chk "$1 window: the listener is RR" '[ "$(pol $L)" = 2 ]'
    S=$(chrt -r 50 sh -c 'while :; do sleep 1; done' >/dev/null 2>&1 & echo $!)
    echo "$S" >> $TF
    sleep 0.1
    chk "$1 CONTROL: the session starts real-time (as inherited)" '[ "$(pol $S)" = 2 ]'
    sleep 1
    chk "$1 window: the session is back to SCHED_OTHER within a sweep" '[ "$(pol $S)" = 0 ]'
    chrt -r -p 50 "$S" >/dev/null 2>&1   # a session that slipped back in before the window ended
    wait $W
    chk "$1 after the window: the listener is SCHED_OTHER" '[ "$(pol $L)" = 0 ]'
    chk "$1 after the window: no session is real-time" '[ "$(pol $S)" = 0 ]'
    kill ${L:+"$L"} ${S:+"$S"} 2>/dev/null
}
echo "== window (shipped az-sshprio)"; round "real" /tmp/sp.real
echo "== CONTROLS (their own check lines are hidden; each must have failed one)"
o1=$(round "ctl1" /tmp/sp.nosweep 2>&1); o2=$(round "ctl2" /tmp/sp.noend 2>&1)
printf '%s\n%s\n' "$o1" "$o2" | grep -E "^(PASS|FAIL)" | sed 's/^/    /'
chk "CONTROL no per-sweep demotion: a session stays real-time (a check fails)" 'echo "$o1" | grep -q "^FAIL ctl1 window: the session is back"'
chk "CONTROL no end-of-window demotion: the listener stays real-time (a check fails)" 'echo "$o2" | grep -q "^FAIL ctl2 after the window: the listener"'

echo "== normal: everything back to SCHED_OTHER"
L=$(spin); S=$(spin); printf '%s\n%s\n' "$L" "$S" > $TF
chrt -r -p 50 "$L" >/dev/null; chrt -r -p 50 "$S" >/dev/null
chk "normal CONTROL: both real-time before" '[ "$(pol $L)" = 2 ] && [ "$(pol $S)" = 2 ]'
AZ_SSHPRIO_MAIN=$L AZ_SSHPRIO_TASKS=$TF sh /tmp/sp.real normal
chk "normal: listener and session SCHED_OTHER" '[ "$(pol $L)" = 0 ] && [ "$(pol $S)" = 0 ]'
kill ${L:+"$L"} ${S:+"$S"} 2>/dev/null

echo "== window stopped early (systemctl stop sends TERM): nothing left real-time"
L=$(spin); echo "$L" > $TF
AZ_SSHPRIO_MAIN=$L AZ_SSHPRIO_TASKS=$TF AZ_SSHPRIO_TICK=0.2 sh /tmp/sp.real window 30 & W=$!
sleep 0.5
chk "stop CONTROL: listener RR inside the window" '[ "$(pol $L)" = 2 ]'
kill -TERM $W; wait $W 2>/dev/null
chk "stop: listener SCHED_OTHER after TERM" '[ "$(pol $L)" = 0 ]'
kill ${L:+"$L"} 2>/dev/null

echo "== no dropbear running: the window does nothing and exits 0"
chk "no listener: exit 0" 'AZ_SSHPRIO_MAIN=0 AZ_SSHPRIO_TASKS=$TF sh /tmp/sp.real window 1'
