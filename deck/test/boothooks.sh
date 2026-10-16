#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# Test suite for two boot-time helpers no other suite runs: deck/boot.d/05-keep-testmode.sh
# (the tmpfiles exclusion that keeps /tmp/testmode, without which a warm EP147 restart
# starts nothing) and az-bootlog (the opt-in support snapshot written to MODS/az-boot.log
# only when the stick carries MODS/diag.on). az-bootlog is a development diagnostic that the
# release build leaves out (strip_payload EXCLUDE), so it is tested from the repo source at
# /mk, not from the stripped payload. Run by test/run.sh in its alpine container
# (`test/run.sh boothooks`).
#
# Each assertion is checked against a broken copy in the same round (CONTROL lines): a
# check that a broken copy also passes proves nothing.
set -u
export PATH=/stub:$PATH
chk(){ if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }
# a CONTROL passes when the assertion it names FAILS on the broken copy
ctl(){ if eval "$2"; then echo "FAIL CONTROL $1 (the broken copy passed)"; else echo "PASS CONTROL $1"; fi; }

# ---- 05-keep-testmode.sh ---------------------------------------------------------------
HOOK=/d/boot.d/05-keep-testmode.sh
[ -f "$HOOK" ] || { echo "FAIL fixture: $HOOK not staged"; exit 1; }
echo "== keep-testmode: the hook writes the tmpfiles exclusion and nothing else"
echo off > /tmp/testmode
export AZ_TESTMODE_CONF=/etc/tmpfiles.d/az-keep-testmode.conf AZ_LOG=/tmp/kt.log
rm -rf /etc/tmpfiles.d /tmp/kt.log
sh "$HOOK"; rc=$?
excl='[ "$(grep -v "^#" "$AZ_TESTMODE_CONF" 2>/dev/null)" = "x /tmp/testmode" ]'
chk "k1 the exclusion is written (x /tmp/testmode, and no other rule)" "$excl"
chk "k2 /tmp/testmode itself is untouched" '[ "$(cat /tmp/testmode)" = off ]'
chk "k3 exits 0" "[ $rc = 0 ]"
chk "k4 no .new is left behind" '[ ! -e "$AZ_TESTMODE_CONF.new" ]'
sh "$HOOK"
chk "k5 a second run leaves the same file" "$excl"
rm -f /tmp/testmode; sh "$HOOK"
chk "k6 a missing /tmp/testmode is reported in the log" 'grep -q "ALREADY MISSING" "$AZ_LOG"'
echo "== keep-testmode: an unwritable config directory"
rm -rf /etc/tmpfiles.d; : > /etc/tmpfiles.d   # a FILE where the directory should be
sh "$HOOK"; rc=$?
chk "k7 the failure is logged and the boot carries on (exit 0)" "[ $rc = 0 ] && grep -q 'FAILED writing' \"\$AZ_LOG\""
rm -f /etc/tmpfiles.d
echo "== keep-testmode: CONTROL, a copy that writes no exclusion"
sed 's|'"'"'x /tmp/testmode'"'"'|'"'"'#'"'"'|' "$HOOK" > /tmp/kt-broken.sh
cmp -s "$HOOK" /tmp/kt-broken.sh && { echo "FAIL fixture: the control mutation did not apply"; exit 1; }
rm -rf /etc/tmpfiles.d; sh /tmp/kt-broken.sh
ctl "k1 on a hook that writes no exclusion" "$excl"
unset AZ_TESTMODE_CONF AZ_LOG

# ---- az-bootlog ------------------------------------------------------------------------
# FIXTURE: az-bootlog finds the stick through /proc/mounts (a /dev/sd* device), so the
# stick is a loop device behind a /dev/sda node, mounted read-only as stock mounts it.
apk add -q dosfstools >/dev/null 2>&1 || { echo "FAIL fixture: apk add dosfstools"; exit 1; }
dd if=/dev/zero of=/img bs=1M count=8 2>/dev/null || { echo "FAIL fixture: dd"; exit 1; }
mkfs.vfat /img >/dev/null 2>&1 || { echo "FAIL fixture: mkfs.vfat"; exit 1; }
lo=$(losetup -f) || { echo "FAIL fixture: losetup -f"; exit 1; }
[ -b "$lo" ] || mknod "$lo" b 7 "${lo#/dev/loop}" || { echo "FAIL fixture: mknod $lo"; exit 1; }
losetup "$lo" /img || { echo "FAIL fixture: losetup attach $lo"; exit 1; }
trap 'umount /media/usb/sda1 /m 2>/dev/null; losetup -d "$lo" 2>/dev/null' EXIT
rm -f /dev/sda1
IFS=: read -r _maj _min < /sys/block/"${lo#/dev/}"/dev
mknod /dev/sda1 b "$_maj" "$_min" || { echo "FAIL fixture: mknod /dev/sda1"; exit 1; }
mkdir -p /m /media/usb/sda1
# st <shell>: change the stick's files rw, then mount it back read-only
st(){ umount /media/usb/sda1 2>/dev/null; mount /dev/sda1 /m || return 1; ( cd /m || exit 1; eval "$1" ); umount /m; mount -o ro /dev/sda1 /media/usb/sda1; }
st 'mkdir -p MODS' || { echo "FAIL fixture: mount the stick"; exit 1; }
grep -q '^/dev/sda1 /media/usb/sda1 ' /proc/mounts || { echo "FAIL fixture: /dev/sda1 is not in /proc/mounts"; exit 1; }
cp /mk/payload/usr/sbin/az-bootlog /tmp/az-bootlog || { echo "FAIL fixture: az-bootlog not found under /mk"; exit 1; }
export AZ_BOOTLOG_TRIES=1 AZ_BOOTLOG_SETTLE=0
ro_now(){ awk '$2=="/media/usb/sda1"{print ($4 ~ /(^|,)ro(,|$)/)?"ro":"rw"}' /proc/mounts; }

echo "== bootlog: no MODS/diag.on writes nothing"
rm -f /tmp/az-bootlog.txt
sh /tmp/az-bootlog; rc=$?
chk "b1 exits 0" "[ $rc = 0 ]"
chk "b2 nothing is written on the stick" '[ ! -e /media/usb/sda1/MODS/az-boot.log ]'
chk "b3 no snapshot is taken" '[ ! -e /tmp/az-bootlog.txt ]'

echo "== bootlog: MODS/diag.on on a read-only stick"
st 'touch MODS/diag.on'
sh /tmp/az-bootlog; rc=$?
chk "b4 the snapshot is written to MODS/az-boot.log" 'grep -q "^=== az-boot.log" /media/usb/sda1/MODS/az-boot.log'
chk "b5 the stick is read-only again afterwards" '[ "$(ro_now)" = ro ]'
chk "b6 no .new is left on the stick" '[ ! -e /media/usb/sda1/MODS/az-boot.log.new ]'
chk "b7 exits 0" "[ $rc = 0 ]"

echo "== bootlog: CONTROL, a copy that ignores MODS/diag.on"
st 'rm -f MODS/diag.on MODS/az-boot.log'
sed 's|\[ -e "\$m/MODS/diag.on" \] && ||' /tmp/az-bootlog > /tmp/az-bootlog-broken
cmp -s /tmp/az-bootlog /tmp/az-bootlog-broken && { echo "FAIL fixture: the control mutation did not apply"; exit 1; }
sh /tmp/az-bootlog-broken
ctl "b2 on a copy that ignores the opt-in" '[ ! -e /media/usb/sda1/MODS/az-boot.log ]'
