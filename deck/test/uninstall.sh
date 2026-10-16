#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# Test suite for az-uninstall (boot.d/30-uninstall.sh): the MODS/uninstall purge.
# Run by test/run.sh in its alpine container (`test/run.sh uninstall`). Same
# harness as bootlogo.sh: a FAT loop stick with MODS/ mounted stock-style at
# /media/usb/sda1, stubbed udevadm, and a real /home/root/settings tree standing
# in for p7.
#
# Every case asserts on the p7 TREE, not only on the log: a log line saying
# "removed" never proves a file is gone, and a CLEAN verdict never proves the
# deck's own files survived. The control (no marker) runs in the same round.
set -u
apk add -q dosfstools >/dev/null 2>&1 || { echo NO_DOSFSTOOLS; exit 1; }
export PATH=/stub:$PATH
cp /p/usr/sbin/az-usbpick /usr/sbin/
# The hook is run the way az-usbload runs it: picked stick in AZ_MNT/AZ_PORT/AZ_MODS,
# via `sh` from a /tmp copy. This wrapper IS that contract (see run_hooks in az-usbload).
hookwrap(){ mkdir -p /tmp/az-hookwrap; cp "$2" /tmp/az-hookwrap/; cat > "/usr/sbin/$1" <<W
#!/bin/sh
log(){ :; }
. /usr/sbin/az-usbpick
MNT=""; PORT=""; pick_stick
export AZ_MNT="\$MNT" AZ_PORT="\$PORT" AZ_MODS="\${MNT:+\$MNT/MODS}" AZ_MODS_OFF=0 AZ_STAGE=/home/root/settings/az-usbload AZ_DECK=/tmp/az-deck AZ_LOG=/tmp/az-usbload.log
exec sh /tmp/az-hookwrap/${2##*/} "\$@"
W
chmod +x "/usr/sbin/$1"; }
hookwrap az-uninstall /d/boot.d/30-uninstall.sh
P7S=/home/root/settings; mkdir -p "$P7S" /m /media/usb/sda1
chk(){ if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }

# the stick
rm -f /img; dd if=/dev/zero of=/img bs=1M count=16 2>/dev/null; mkfs.vfat /img >/dev/null
lo=$(losetup -f); losetup "$lo" /img; IFS=: read -r _maj _min < /sys/block/"${lo#/dev/}"/dev; mknod /dev/sda1 b "$_maj" "$_min"; echo "sda 1-1" > /tmp/ports
# a mods stick: az-usbpick needs a core or a switch in MODS/, not the folder alone
mount /dev/sda1 /m; mkdir /m/MODS; touch /m/MODS/mods.on; echo so > /m/MODS/azmod.so; umount /m; mount -o ro /dev/sda1 /media/usb/sda1
st(){ umount /media/usb/sda1 2>/dev/null; mount /dev/sda1 /m; ( cd /m || exit 1; eval "$1" ); umount /m; mount -o ro /dev/sda1 /media/usb/sda1; }
result(){ cat /media/usb/sda1/MODS/.uninstall-result 2>/dev/null; }

# a p7 that looks like a real modded deck: our files plus the deck's own.
seed_p7(){
    rm -rf "$P7S"; mkdir -p "$P7S"
    # --- the deck's own, which must never be touched
    echo SETTINGS      > "$P7S/SETTINGS.DAT"
    echo WIFI          > "$P7S/wifi.json"
    echo BT            > "$P7S/bluetooth_snk.json"
    echo CABINET       > "$P7S/cabinet.img"
    mkdir -p "$P7S/cabinet/encryption"; echo PKI > "$P7S/cabinet/encryption/key.dat"
    # --- ours
    mkdir -p "$P7S/az-usbload/azmod-pngpatch"
    echo so  > "$P7S/az-usbload/azmod.so"
    echo so  > "$P7S/az-usbload/azstem.so"
    echo env > "$P7S/az-usbload/az-preload.env"
    echo art > "$P7S/az-usbload/azmod-pngpatch/0.bin"
    echo cfg > "$P7S/azmod.cfg"
    echo st  > "$P7S/azstem.enable"; echo st > "$P7S/azstem.stems"; echo st > "$P7S/azstem.avail"
    echo st  > "$P7S/azstem.curtrack2"
    echo fl  > "$P7S/azmod-example.on"; echo fl > "$P7S/azmod-stems4.off"
    echo lg  > "$P7S/azmod-srcrow.log"
    mkdir -p "$P7S/az-bootlogo/orig"; echo orig > "$P7S/az-bootlogo/orig/logo.bmp"
    echo bl  > "$P7S/az-bootlogo.log"
    # leftovers the explicit list does not name (diagnostics, dated backups): the globs must catch them
    for n in az-restart-diag.log az-restart-diag.run1.log az-restart-diag.sh \
             az-restart-repro.sh az-restart-runs.log azmod-pngpatch.old-20260918 \
             azmod.cfg.bak-orch-20260915; do echo x > "$P7S/$n"; done
}
deck_intact(){ [ "$(cat $P7S/SETTINGS.DAT)" = SETTINGS ] && [ "$(cat $P7S/wifi.json)" = WIFI ] \
    && [ "$(cat $P7S/bluetooth_snk.json)" = BT ] && [ "$(cat $P7S/cabinet.img)" = CABINET ] \
    && [ "$(cat $P7S/cabinet/encryption/key.dat)" = PKI ]; }

echo "== 1 CONTROL: no marker -> nothing is touched"
seed_p7; st 'rm -f MODS/uninstall'; rm -f /tmp/az-bootlogo.log
/usr/sbin/az-uninstall
chk "1 logged no marker" 'grep -q "no MODS/uninstall marker" /tmp/az-uninstall.log'
chk "1 our files still there" '[ -f $P7S/az-usbload/azmod.so ] && [ -f $P7S/azmod.cfg ]'
chk "1 deck files intact" 'deck_intact'
chk "1 no breadcrumb written" '[ ! -e /media/usb/sda1/MODS/.uninstall-result ]'

echo "== 2 marker + logo restored -> everything of ours goes, deck untouched"
seed_p7; st 'touch MODS/uninstall'
echo "[bootlogo] RESTORED: boot logo -> stock original" > /tmp/az-bootlogo.log
/usr/sbin/az-uninstall
chk "2 az-usbload cache gone" '[ ! -e $P7S/az-usbload ]'
chk "2 azmod.cfg gone" '[ ! -e $P7S/azmod.cfg ]'
chk "2 azstem state gone" '[ ! -e $P7S/azstem.enable ] && [ ! -e $P7S/azstem.stems ] && [ ! -e $P7S/azstem.avail ] && [ ! -e $P7S/azstem.curtrack2 ]'
chk "2 azmod flags gone" '[ ! -e $P7S/azmod-example.on ] && [ ! -e $P7S/azmod-stems4.off ]'
chk "2 azmod logs gone" '[ ! -e $P7S/azmod-srcrow.log ]'
chk "2 bootlogo backup gone (p3 is stock)" '[ ! -e $P7S/az-bootlogo ] && [ ! -e $P7S/az-bootlogo.log ]'
chk "2 NOTHING az* left on p7" '[ -z "$(ls -d $P7S/az* 2>/dev/null)" ]'
chk "2 THE DECK OWN FILES ARE INTACT (the one that matters)" 'deck_intact'
chk "2 verdict CLEAN on the stick" 'result | grep -q "^result=CLEAN$"'
chk "2 no p7 log left behind on a clean run" '[ ! -e $P7S/az-uninstall.log ]'
chk "2 breadcrumb has all five keys in order" '[ "$(result | cut -d= -f1 | tr "\n" ",")" = "result,removed,remaining,failed,message," ]'
chk "2 remaining and failed are empty when CLEAN" 'result | grep -q "^remaining=$" && result | grep -q "^failed=$"'
chk "2 removed names the cache" 'result | grep "^removed=" | grep -q az-usbload'

echo "== 3 idempotent: a second run on an already-clean deck"
/usr/sbin/az-uninstall
chk "3 still CLEAN" 'result | grep -q "^result=CLEAN$"'
chk "3 removed= is empty when nothing was removed (case 2 names files)" 'result | grep -qx "removed="'
chk "3 deck files still intact" 'deck_intact'

echo "== 4 logo NOT restored -> backup is KEPT and the verdict says INCOMPLETE"
seed_p7; st 'touch MODS/uninstall'
echo "[bootlogo] SKIPPED: boot logo already the AZ MODS logo (sha deadbeef)" > /tmp/az-bootlogo.log
/usr/sbin/az-uninstall
chk "4 backup KEPT" '[ -f $P7S/az-bootlogo/orig/logo.bmp ]'
chk "4 logged the reason" 'grep -q "KEEPING az-bootlogo" /tmp/az-uninstall.log'
chk "4 everything else still went" '[ ! -e $P7S/az-usbload ] && [ ! -e $P7S/azmod.cfg ]'
chk "4 verdict INCOMPLETE" 'result | grep -q "^result=INCOMPLETE$"'
chk "4 breadcrumb names the logo" 'result | grep -qi "boot logo has NOT been restored"'
chk "4 breadcrumb has all five keys in order" '[ "$(result | cut -d= -f1 | tr "\n" ",")" = "result,removed,remaining,failed,message," ]'
chk "4 remaining names az-bootlogo" 'result | grep "^remaining=" | grep -q az-bootlogo'
chk "4 p7 log kept for an incomplete run" '[ -f $P7S/az-uninstall.log ]'
chk "4 deck files intact" 'deck_intact'

echo "== 5 no bootlogo log at all (never patched) -> still refuses to guess"
seed_p7; st 'touch MODS/uninstall'; rm -f /tmp/az-bootlogo.log
/usr/sbin/az-uninstall
chk "5 backup KEPT with no evidence either way" '[ -f $P7S/az-bootlogo/orig/logo.bmp ]'
chk "5 deck files intact" 'deck_intact'

echo "== 6 a deck that never had the logo patched -> CLEAN, nothing kept"
seed_p7; rm -rf "$P7S/az-bootlogo" "$P7S/az-bootlogo.log"; st 'touch MODS/uninstall'; rm -f /tmp/az-bootlogo.log
/usr/sbin/az-uninstall
chk "6 CLEAN" 'result | grep -q "^result=CLEAN$"'
chk "6 nothing az* left" '[ -z "$(ls -d $P7S/az* 2>/dev/null)" ]'
chk "6 deck files intact" 'deck_intact'

echo "== 7 no stick -> nothing to do, p7 untouched"
seed_p7; umount /media/usb/sda1 2>/dev/null; mv /dev/sda1 /dev.sda1.off
/usr/sbin/az-uninstall
chk "7 our files still there" '[ -f $P7S/az-usbload/azmod.so ]'
chk "7 deck files intact" 'deck_intact'
mv /dev.sda1.off /dev/sda1; mount -o ro /dev/sda1 /media/usb/sda1

echo "== 8 THE REAL RUN's leftovers: dev-era az* names are ours and must go"
seed_p7; st 'touch MODS/uninstall'
echo "[bootlogo] RESTORED: boot logo -> stock original" > /tmp/az-bootlogo.log
/usr/sbin/az-uninstall
chk "8 az-restart-* gone" '[ -z "$(ls -d $P7S/az-restart* 2>/dev/null)" ]'
chk "8 azmod-pngpatch.old-* gone" '[ ! -e $P7S/azmod-pngpatch.old-20260918 ]'
chk "8 azmod.cfg.bak-* gone" '[ ! -e $P7S/azmod.cfg.bak-orch-20260915 ]'
chk "8 CLEAN now" 'result | grep -q "^result=CLEAN$"'
chk "8 DECK FILES STILL INTACT" 'deck_intact'

echo "== 9 THE RACE: az-bootlogo has not written its verdict yet when we start"
seed_p7; st 'touch MODS/uninstall'
rm -f /tmp/az-bootlogo.log
# the bootlogo hook may still be copying p3 when this runs and writes RESTORED a few
# seconds in, so a single sample can read an empty file and wrongly report the logo as
# not restored.
( sleep 5; echo "[bootlogo] RESTORED: boot logo -> stock original" > /tmp/az-bootlogo.log ) &
/usr/sbin/az-uninstall
chk "9 waited for the verdict" 'grep -q "bootlogo verdict: stock" /tmp/az-uninstall.log'
chk "9 backup removed once p3 is stock" '[ ! -e $P7S/az-bootlogo ]'
chk "9 CLEAN, not a false INCOMPLETE" 'result | grep -q "^result=CLEAN$"'
chk "9 deck files intact" 'deck_intact'
wait

echo "== 10 CONTROL for 9: a verdict that never says stock still KEEPS the backup"
seed_p7; st 'touch MODS/uninstall'
echo "[bootlogo] REFUSED: bundled logo missing" > /tmp/az-bootlogo.log
/usr/sbin/az-uninstall
chk "10 backup KEPT" '[ -f $P7S/az-bootlogo/orig/logo.bmp ]'
chk "10 INCOMPLETE" 'result | grep -q "^result=INCOMPLETE$"'
chk "10 deck files intact" 'deck_intact'
