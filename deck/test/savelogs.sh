#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# shellcheck disable=SC2034  # captured values are read inside chk's single-quoted eval strings
set -u
# deck/lib/az-savelogs (System Info > Save Logs to USB), run by the DECK'S OWN userland: the
# 2.01 rootfs mounted read-only at /lower and overlaid writable at /deck, every case under the
# deck's `busybox ash` and under its /bin/sh (bash), the shell the watcher uses.
#   sticks: real FAT images, mounted read-only at /media/usb/sda1 as stock does (the script
#           must remount rw and put it back): 64 MB (room), 6 MB (under the 8 MB it needs),
#           2 MB with the free-space floor lowered (the copy runs out of room part way)
#   p7:     a tmpfs at /home/root/settings with a staged azmod.so
# The script is the STRIPPED copy from the staged MODS/deck (/d), i.e. what ships.
#
# CONTROLS, same run, each a copy of the script with one guard removed, must produce the bad
# outcome its case rules out: without the free-space check it writes on the 6 MB stick; without the process check it writes during
# an update; without the cleanup a failed copy leaves its .tmp; without the caps the copy is
# bigger than the limits.
[ -x /lower/bin/busybox ] || { echo "FAIL no deck rootfs mounted at /lower (set AZ_DECK_ROOTFS for run.sh)"; exit 1; }
apk add -q dosfstools >/dev/null 2>&1 || { echo NO_DOSFSTOOLS; exit 1; }
[ -f /d/lib/az-savelogs ] || { echo "FAIL no staged deck/lib/az-savelogs"; exit 1; }

PASS=0; FAILN=0; SHN=-
chk() { if eval "$2"; then echo "PASS [$SHN] $1"; PASS=$((PASS+1)); else echo "FAIL [$SHN] $1"; FAILN=$((FAILN+1)); fi; }

# ---- the deck userland ----------------------------------------------------------
mkdir -p /ov /deck && mount -t tmpfs ov /ov && mkdir -p /ov/u /ov/w
mount -t overlay overlay -o lowerdir=/lower,upperdir=/ov/u,workdir=/ov/w /deck || { echo "FAIL overlay mount"; exit 1; }
mount --bind /dev /deck/dev; mount -t proc proc /deck/proc
mount -t tmpfs tmp /deck/tmp; mount -t tmpfs run /deck/run
mkdir -p /deck/home/root/settings && mount -t tmpfs -o size=61m p7 /deck/home/root/settings
mkdir -p /deck/home/root/settings/az-usbload /deck/tmp/az-deck/lib /deck/media/usb/sda1 /m
echo "staged core" > /deck/home/root/settings/az-usbload/azmod.so
SO_MD5=$(md5sum /deck/home/root/settings/az-usbload/azmod.so | cut -d' ' -f1)
cp /d/lib/az-savelogs /d/lib/az-logcap.sh /deck/tmp/az-deck/lib/

L=/deck/tmp/az-deck/lib

# ---- the sticks -------------------------------------------------------------------
mkimg() { dd if=/dev/zero of="$1" bs=1M count="$2" 2>/dev/null; mkfs.vfat "$1" >/dev/null
          lo=$(losetup -f); losetup "$lo" "$1"; mount "$lo" /m && mkdir -p /m/MODS/deck && echo core > /m/MODS/azmod.so && umount /m; echo "$lo"; }
BIG=$(mkimg /big.img 64); SMALL=$(mkimg /small.img 6); TINY=$(mkimg /tiny.img 2)
ST=/deck/media/usb/sda1; LOGS=$ST/MODS/logs; CUR=""
use() { umount $ST 2>/dev/null; CUR=$1; mount -o ro "$1" $ST; }
rwst() { umount $ST; mount "$CUR" /m; ( cd /m/MODS && eval "$1" ); umount /m; mount -o ro "$CUR" $ST; }
cleanup() { umount $ST 2>/dev/null; for l in $BIG $SMALL $TINY; do losetup -d "$l" 2>/dev/null; done; }
trap cleanup EXIT
stick_ro() { grep " /deck/media/usb/sda1 " /proc/mounts | grep -q "[ ,]ro[ ,]"; }
tree() { (cd $ST && find . | sort | md5sum); }
S=/deck/tmp/az-savelogs/status
sv() { sed -n "s/^$1=//p" $S; }
# run <script> <decks state> <request> [VAR=value...]
run() { scr=$1 dk=$2 rq=$3; shift 3
    # shellcheck disable=SC2086 # the shell is a command and its argument ("/bin/busybox ash")
    chroot /deck /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin AZ_MNT=/media/usb/sda1 AZ_MODS=/media/usb/sda1/MODS \
        AZ_DECKS_STATE="$dk" "$@" $SHC "/tmp/az-deck/lib/$scr" "$rq"; }
saved() { sv msg | sed -n 's|^Saved to MODS/logs/||p'; }   # the folder the last save names
ntmp() { find $LOGS -maxdepth 1 -name "*.tmp" 2>/dev/null | wc -l; }
logs() { rm -rf /deck/tmp/az* /deck/tmp/stub; mkdir -p /deck/tmp/az-deck/lib /deck/tmp/azmod-community
         cp /d/lib/az-savelogs /d/lib/az-logcap.sh $L/ 2>/dev/null; }

for SHC in "/bin/busybox ash" "/bin/sh"; do
SHN=${SHC##*/}; SHN=${SHN%% *}; [ "$SHN" = busybox ] && SHN=ash
# the controls, one guard removed each, rebuilt beside the script after logs() clears /tmp
regen() { logs
    sed 's/^\[ "\$free_kb" -ge "\$MIN_KB" \] || refuse .*$/:/' $L/az-savelogs > $L/ctl-nospace
    sed 's/^ *refuse "An update or a community change is running.*$/            : ;;/' $L/az-savelogs > $L/ctl-noproc
    sed 's/ rm -rf "\$T" 2>\/dev\/null$/ :/' $L/az-savelogs > $L/ctl-noclean
    sed -e 's/^FILE_MAX=1048576$/FILE_MAX=99999999/' -e 's/^TOTAL_MAX=4194304$/TOTAL_MAX=99999999/' $L/az-savelogs > $L/ctl-nocap
    for c in nospace noproc noclean nocap; do
        cmp -s $L/az-savelogs $L/ctl-$c && { echo "FAIL control $c did not apply (did the script change?)"; exit 1; }
        sh -n $L/ctl-$c || { echo "FAIL control $c does not parse"; exit 1; }
    done; }
regen

echo "== [$SHN] s1 a save: the final folder, summary.txt, the logs, no .tmp, the stick ro again"
use "$BIG"; rwst 'rm -rf logs; mkdir -p logs/20260101-000000.tmp; echo half > logs/20260101-000000.tmp/x'
echo "1.2.3" > /deck/tmp/az-deck/version
printf '=== azmod crash: signal 11\n=== end\n' > /deck/tmp/azmod-crash.log
echo "loader line" > /deck/tmp/az-usbload.log; echo "boot line" > /deck/home/root/settings/az-usbload/usbload.log
printf 'fw=ep147-0123456789abcdef\nsuspended=0\n' > /deck/tmp/azmod-community/loaded
OUT0=$(cd $ST && find . | grep -v '^\./MODS/logs' | sort | md5sum)
run az-savelogs empty "save 42"
N=$(saved)
chk "s1 status saved, answers request 42, msg last" '[ "$(sv state)" = saved ] && [ "$(sv req)" = 42 ] && tail -1 $S | grep -q "^msg=Saved to MODS/logs/"'
chk "s1 the folder is named by the deck clock (yyyymmdd-HHMMSS) and exists" 'echo "$N" | grep -qxE "[0-9]{8}-[0-9]{6}(-[0-9]+)?" && [ -d "$LOGS/$N" ]'
chk "s1 no .tmp left, and the one a power cut left is gone" '[ "$(ntmp)" = 0 ]'
chk "s1 summary.txt: mods set, firmware, EP147 tag, md5 of the staged core" 'f=$LOGS/$N/summary.txt; grep -q "^mods set:  1.2.3$" $f && grep -q "^firmware:  2.01$" $f && grep -q "^EP147:     ep147-0123456789abcdef$" $f && grep -q "^$SO_MD5  /home/root/settings/az-usbload/azmod.so$" $f'
chk "s1 summary.txt: uptime, decks, and the journal section" 'f=$LOGS/$N/summary.txt; grep -qE "^uptime:    [0-9.]+ s$" $f && grep -q "^decks:     empty$" $f && grep -q "^==== journalctl -u EP147 -n 200$" $f'
chk "s1 the logs, byte for byte, under files/<deck path>" 'cmp -s /deck/tmp/azmod-crash.log $LOGS/$N/files/tmp/azmod-crash.log && cmp -s /deck/home/root/settings/az-usbload/usbload.log $LOGS/$N/files/home/root/settings/az-usbload/usbload.log && cmp -s /deck/tmp/azmod-community/loaded $LOGS/$N/files/tmp/azmod-community/loaded'
chk "s1 the stick is read-only again" 'stick_ro'
chk "s1 nothing changed on the stick outside MODS/logs" '[ "$(cd $ST && find . | grep -v "^\./MODS/logs" | sort | md5sum)" = "$OUT0" ]'
run az-savelogs empty "save 43"
chk "s1b a second save in the same second gets its own folder" '[ "$(sv state)" = saved ] && [ "$(saved)" != "$N" ] && [ -d "$LOGS/$(saved)" ] && [ -d "$LOGS/$N" ]'

echo "== [$SHN] s2 size caps: each file to its newest 1 MB, the copy to 4 MB"
regen; head -c 3000000 /dev/urandom > /deck/tmp/az-usbload.log
for x in a b c d e f; do head -c 1048576 /dev/urandom > /deck/tmp/azmod-$x.log; done
run az-savelogs empty "save 50"; N=$(saved)
chk "s2 saved" '[ "$(sv state)" = saved ] && [ -d "$LOGS/$N" ]'
chk "s2 a 3 MB log is cut to its NEWEST 1048576 bytes" '[ "$(wc -c < $LOGS/$N/files/tmp/az-usbload.log)" = 1048576 ] && [ "$(tail -c 1048576 /deck/tmp/az-usbload.log | md5sum)" = "$(md5sum < $LOGS/$N/files/tmp/az-usbload.log)" ]'
chk "s2 the files copied add up to at most 4194304 bytes" '[ "$(find $LOGS/$N/files -type f -exec cat {} + | wc -c)" -le 4194304 ]'
chk "s2 summary.txt names the cut file and the skipped ones" 'grep -q "^cut to its newest 1048576 bytes: /tmp/az-usbload.log (3000000 bytes)$" $LOGS/$N/summary.txt && grep -q "^skipped (total limit 4194304 bytes reached): /tmp/azmod-" $LOGS/$N/summary.txt'
run ctl-nocap empty "save 51"; N=$(saved)
chk "s2c CONTROL: without the caps the copy is over 4 MB and the 3 MB log whole" '[ "$(sv state)" = saved ] && [ "$(wc -c < $LOGS/$N/files/tmp/az-usbload.log)" = 3000000 ] && [ "$(find $LOGS/$N/files -type f -exec cat {} + | wc -c)" -gt 4194304 ]'

echo "== [$SHN] s3 a track loaded or the decks unknown: it still saves (the user asked)"
regen; echo x > /deck/tmp/azmod-crash.log; use "$BIG"; rwst 'rm -rf logs'
run az-savelogs loaded "save 60"; N=$(saved)
chk "s3 loaded: saved, and summary.txt records the decks as loaded" '[ "$(sv state)" = saved ] && [ "$(sv req)" = 60 ] && grep -q "^decks:     loaded$" $LOGS/$N/summary.txt'
chk "s3 loaded: the stick read-only again" 'stick_ro'
run az-savelogs unknown "save 61"
chk "s3 unknown: saved" '[ "$(sv state)" = saved ] && [ "$(sv req)" = 61 ]'

echo "== [$SHN] s4 a stick with under 8 MB free: refused, nothing written"
use "$SMALL"; T0=$(tree)
run az-savelogs empty "save 70"
chk "s4 refused, the reason names the space" '[ "$(sv state)" = refused ] && sv msg | grep -q "Not enough space on the USB (8 MB needed)"'
chk "s4 the stick untouched and still ro" '[ "$(tree)" = "$T0" ] && stick_ro'
run ctl-nospace empty "save 71"
chk "s4c CONTROL: without the free-space check it writes on that stick" '[ "$(sv state)" = saved ] && [ "$(tree)" != "$T0" ]'

echo "== [$SHN] s5 a mods update running: refused, nothing written"
use "$BIG"; rwst 'rm -rf logs'; T0=$(tree)
printf 'sleep 30\n' > $L/az-modupdate
chroot /deck /bin/sh /tmp/az-deck/lib/az-modupdate update & UP=$!
sleep 0.5
run az-savelogs empty "save 80"
chk "s5 refused while az-modupdate runs" '[ "$(sv state)" = refused ] && sv msg | grep -q "update or a community change is running" && [ "$(tree)" = "$T0" ]'
run ctl-noproc empty "save 81"
chk "s5c CONTROL: without the process check it writes during the update" '[ "$(sv state)" = saved ] && [ "$(tree)" != "$T0" ]'
kill $UP 2>/dev/null; wait $UP 2>/dev/null; rm -f $L/az-modupdate
run az-savelogs empty "save 82"
chk "s5 after the update it saves (CONTROL for the refusal)" '[ "$(sv state)" = saved ]'

echo "== [$SHN] s6 the stick runs out of room part way: error, no .tmp, no folder, ro again"
regen; for x in a b c; do head -c 1048576 /dev/urandom > /deck/tmp/azmod-$x.log; done
use "$TINY"; rwst 'rm -rf logs'; T0=$(tree)
run az-savelogs empty "save 90" AZ_SAVELOGS_MIN_KB=1
chk "s6 error, says the USB could not be written" '[ "$(sv state)" = error ] && [ "$(sv req)" = 90 ] && sv msg | grep -q "Could not write the USB"'
chk "s6 no .tmp and no saved folder left" '[ "$(ntmp)" = 0 ] && [ -z "$(ls $LOGS 2>/dev/null)" ]'
chk "s6 the stick is read-only again" 'stick_ro'
run ctl-noclean empty "save 91" AZ_SAVELOGS_MIN_KB=1
chk "s6c CONTROL: without the cleanup the failed copy leaves its .tmp" '[ "$(sv state)" = error ] && [ "$(ntmp)" = 1 ]'

echo "== [$SHN] s7 bad requests and no stick"
use "$BIG"; T0=$(tree)
run az-savelogs empty "save 1;x"
chk "s7 a request id that is not digits: error, nothing written" '[ "$(sv state)" = error ] && sv msg | grep -q "unknown request" && [ "$(tree)" = "$T0" ]'
run az-savelogs empty "frobnicate"
chk "s7 an unknown request: error" '[ "$(sv state)" = error ] && [ "$(tree)" = "$T0" ]'
# shellcheck disable=SC2086 # the shell is a command and its argument ("/bin/busybox ash")
chroot /deck /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin AZ_DECKS_STATE=empty $SHC /tmp/az-deck/lib/az-savelogs "save 99"
chk "s7 no stick: refused" '[ "$(sv state)" = refused ] && [ "$(sv msg)" = "No mods USB in the deck." ] && [ "$(sv req)" = 99 ]'
chk "s7 p7 holds only the test's own files (nothing written there)" '[ "$(cd /deck/home/root/settings && find . -type f | sort | tr "\n" " ")" = "./az-usbload/azmod.so ./az-usbload/usbload.log " ]'
done

echo "== totals: $PASS passed, $FAILN failed"
