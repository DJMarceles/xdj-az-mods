#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Runs the release payload tests (az-usbload + its MODS/deck hooks: sshgate,
# bootlogo, uninstall; az-usbpick; usb-renotify) in a privileged alpine container: real FAT loop images,
# stock-style ro mounts at /media/usb/sdX1, stubbed udevadm (bus id from
# /tmp/ports) and systemctl, and (bootlogo) a synthetic RSCE p3 image. Exits
# non-zero on any FAIL. Run by hand on a docker host: `test/run.sh` for every
# suite, `test/run.sh <suite>...` for some.
#
# The repo must live under a docker-shared path (on macOS, a path outside the shared
# folders, such as /private/tmp, is NOT shared: the bind mount silently shows up as an
# empty dir and nothing runs).
#
# The tests run against the STRIPPED payload (strip_payload.py -- exactly what the
# release build ships), after `sh -n` passes on every stripped shell script. That is
# the behavior gate for the build-time comment/blank strip.
cd "$(dirname "$0")" || exit 1
# PER-RUN staging dir, removed on exit. Two run.sh invocations sharing one staging dir
# would let the second rm -rf it under the first one's running containers.
# ONE RUN AT A TIME ON THIS HOST. Loop devices are host-global and ports.sh's
# cleanup detaches ALL of them (`losetup -d` over `losetup -a`), so a second
# suite yanks the first one's stick out from under it: "no USB stick after 10s"
# in a healthy case.
#
# A host-wide lock, held for the WHOLE run (staging and every suite): a `docker ps`
# check alone is a race, since two runs can both look before either has a container
# up, and between two suites no container runs at all. mkdir is atomic everywhere
# (flock is not on macOS). The lock lives at a fixed path, never under TMPDIR, which
# can differ between two shells on the same host. The owner's pid is inside; a lock
# whose owner is gone is taken over. A second run waits for the first (up to
# AZ_TEST_LOCK_WAIT seconds, default 3600; 0 = refuse at once).
LOCK=${AZ_TEST_LOCK:-/tmp/az-release-payload-tests.lock}
WAIT=${AZ_TEST_LOCK_WAIT:-3600}
R=; rc=0
# shellcheck disable=SC2317 # run by the EXIT trap
cleanup() {
    [ -n "$R" ] && rm -rf "$R"
    [ "$(cat "$LOCK/pid" 2>/dev/null)" = "$$" ] && rm -rf "$LOCK"
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
waited=0; said=
while ! mkdir "$LOCK" 2>/dev/null; do
    op=$(cat "$LOCK/pid" 2>/dev/null)
    if [ -n "$op" ] && ! kill -0 "$op" 2>/dev/null; then
        # owner gone: move the dir aside (atomic), and drop it only if it is still that
        # owner's; a lock someone took in between goes back
        if mv "$LOCK" "$LOCK.stale.$$" 2>/dev/null; then
            if [ "$(cat "$LOCK.stale.$$/pid" 2>/dev/null)" = "$op" ]; then
                echo "lock: $LOCK was held by pid $op, which is gone: taken over"
                rm -rf "$LOCK.stale.$$"
            else
                mv "$LOCK.stale.$$" "$LOCK" 2>/dev/null || rm -rf "$LOCK.stale.$$"
            fi
        fi
        continue
    fi
    # an empty pid file for over a minute: an owner killed between mkdir and the write
    if [ -z "$op" ] && [ -n "$(find "$LOCK" -maxdepth 0 -mmin +1 2>/dev/null)" ]; then
        rm -rf "$LOCK"; continue
    fi
    if [ "$waited" -ge "$WAIT" ]; then
        echo "REFUSING: another payload test run holds $LOCK (pid ${op:-?}) after ${waited}s; wait for it or kill it"
        exit 2
    fi
    [ -n "$said" ] || { echo "waiting: another payload test run holds $LOCK (pid ${op:-?})"; said=1; }
    sleep 5; waited=$((waited + 5))
done
echo "$$" > "$LOCK/pid"
# Also refuse when a suite container is already running (a run that did not take the lock).
if docker ps --format '{{.Image}} {{.Command}}' 2>/dev/null | grep -q 'alpine:3.20 "sh /t/'; then
    echo "REFUSING: a payload suite container is already running on this host (docker ps); wait for it or kill it"; exit 2
fi
H=$(pwd); P=$(cd ../payload && pwd); R=$(mktemp -d "$H/../.stage.XXXXXX")
S="$R/payload"; D="$R/deck"
# Python 3: python3 on macOS/Linux; Windows installs give python and py instead (and
# may have a python3 that only opens the Microsoft Store), so test that each one runs.
PY=
for cand in python3 python "py -3"; do
    if $cand -c 'import sys; sys.exit(sys.version_info < (3, 9))' >/dev/null 2>&1; then
        PY=$cand; break
    fi
done
[ -n "$PY" ] || { echo "Python 3.9 or newer not found (tried python3, python and py -3)"; exit 1; }
# the handoff guard is built with the mods; no suite needs it. The sources it stages
# (stage_stick.py prints where its sources came from) head the output and close it.
ST=$($PY ../stage_stick.py "$D" --version test --no-guard 2>&1) || { echo "$ST"; exit 1; }
EMUSRC=$(echo "$ST" | grep '^\[stick\] emu sources: ')
[ -n "$EMUSRC" ] || { echo "$ST"; echo "FAIL stage_stick.py did not name its emu sources"; exit 1; }
echo "$EMUSRC"
$PY ../strip_payload.py "$P" "$S" || exit 1
echo "$ST"
for f in "$S"/usr/sbin/az-* "$S"/home/root/scripts/*.sh; do
    sh -n "$f" || { echo "FAIL sh -n $f"; exit 1; }
done
echo "stripped payload staged + sh -n OK: $S; MODS/deck staged: $D"
# a single suite: test/run.sh hooks
# AZ_BOOT_WINDOW: a container's /proc/uptime is the docker VM's, so without it every
# loader run would count as "long after boot" and the restart policy would hold back the
# restarts the older suites assert. restart.sh sets its own window per case.
[ $# -gt 0 ] && SUITES="$*" || SUITES="lint clguard ports mirror hooks poststart gate restart bootlogo uninstall renotify watch trust modupdate community remotestems logcap sshprio boothooks savelogs stickconf deckcmds"
# Git Bash on Windows rewrites /paths in arguments ("/t/x.sh" -> "T:/x.sh"); Docker
# Desktop takes the /g/... host paths as they are. Only docker gets the switch: python
# above still needs its arguments converted. A no-op elsewhere.
# community, remotestems, sshprio and part of trust run under (and deckcmds checks against) the DECK'S OWN
# busybox/curl/bash: the extracted 2.01 rootfs at AZ_DECK_ROOTFS, mounted read-only (each
# suite overlays it). Without one, those suites FAIL rather than pass on a userland that
# is not the deck's.
ROOTFS=${AZ_DECK_ROOTFS:-}
RFS=""; [ -x "$ROOTFS/bin/busybox" ] && RFS="$ROOTFS:/lower:ro"
for t in $SUITES; do
    # lint and clguard run on the host (lint starts its own pinned shellcheck container;
    # clguard tests the stripped loader's guard functions directly); the rest run inside
    # the alpine container
    if [ "$t" = lint ]; then out=$(sh "$H/lint.sh" 2>&1)
    elif [ "$t" = clguard ]; then out=$(sh "$H/clguard.sh" "$S/usr/sbin/az-usbload" 2>&1)
    else out=$(MSYS_NO_PATHCONV=1 docker run --rm --privileged -e AZ_NOWATCH=1 -e AZ_BOOT_WINDOW=1000000000 -v "$H:/t:ro" -v "$H/stub:/stub:ro" -v "$S:/p:ro" -v "$D:/d:ro" -v "$H/..:/mk:ro" ${RFS:+-v "$RFS"} alpine:3.20 sh "/t/$t.sh" 2>&1); fi
    echo "$out" | grep -E '^(==|PASS|FAIL|NO_DOSFSTOOLS)'
    echo "$out" | grep -q '^PASS' || { echo "NO RESULTS from $t (instrument broken)"; rc=1; }
    echo "$out" | grep -qE '^(FAIL|NO_DOSFSTOOLS)' && rc=1
done
echo "$EMUSRC"
exit $rc
