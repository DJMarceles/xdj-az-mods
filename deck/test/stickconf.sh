#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# shellcheck disable=SC2034  # captured values are read inside chk's single-quoted eval strings
# MODS/azmods.cfg, the settings file on the stick (az-usbload + deck/lib/az-conf.sh), on a
# real FAT image with the stock read-only mount: migration from the deck copy (and the old
# community switch files), the stick winning over the deck copy, no stick using the deck
# copy, a malformed file refused without touching either copy, the kill switches read before
# anything is staged, and the watcher's debounced write-back, which never leaves a partial
# file. azmod's side (taking the hand-over) is the core's unit test (cfg_test.c); here the
# hand-over file is checked and azmod's save is played by hand. Run by test/run.sh.
set -u
. /stub/devflag
apk add -q dosfstools >/dev/null 2>&1 || { echo NO_DOSFSTOOLS; exit 1; }
export PATH=/stub:$PATH AZ_ACTIVE_WAIT=2 AZ_MODUPDATE_AUTOCHECK=100000 AZ_CFG_DEBOUNCE=1 AZ_CFG_RETRY=1 AZ_DECKS_FRESH=100000
. /stub/fake-ep147
for f in az-usbload az-usbpick; do cp /p/usr/sbin/$f /usr/sbin/$f; done; chmod +x /usr/sbin/az-usbload
mkdir -p /home/root/scripts; cp /p/home/root/scripts/usb-renotify.sh /home/root/scripts/
P7=/home/root/settings/az-usbload; ENVF=$P7/az-preload.env; D=/home/root/settings/azmod.conf
IM=/run/azmods-cfg.import; mkdir -p /m /media/usb/sda1 /run
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }
rm -f /img; dd if=/dev/zero of=/img bs=1M count=16 2>/dev/null; mkfs.vfat /img >/dev/null
lo=$(losetup -f); losetup "$lo" /img; trap 'umount /media/usb/sda1 /m 2>/dev/null; losetup -d $lo 2>/dev/null' EXIT
IFS=: read -r _maj _min < /sys/block/"${lo#/dev/}"/dev; mknod /dev/sda1 b "$_maj" "$_min"; echo "sda 1-1" > /tmp/ports
st() { umount /media/usb/sda1 2>/dev/null; mount /dev/sda1 /m; ( cd /m || exit 1; eval "$1" ); umount /m; mount -o ro /dev/sda1 /media/usb/sda1; }
unplug() { umount /media/usb/sda1 2>/dev/null; mv /dev/sda1 /dev.sda1.off; }
replug() { mv /dev.sda1.off /dev/sda1; mount -o ro /dev/sda1 /media/usb/sda1; }
boot() { rm -f /tmp/systemctl.calls /run/az-reload-pending /tmp/az-usbload.log; devflag; AZ_NOWATCH=1 /usr/sbin/az-usbload >/dev/null 2>&1
         grep -h "azmods.cfg\|desired\|community" /tmp/az-usbload.log | sed 's/^/    /'; }
lg() { grep -q "$1" /tmp/az-usbload.log; }
SC=/media/usb/sda1/MODS/azmods.cfg
key() { tr -d '\r' < "$1" 2>/dev/null | sed -n "s/^$2 = //p" | tail -1; }
sum() { md5sum "$1" 2>/dev/null | cut -d' ' -f1; }
CORE="LD_PRELOAD=$P7/azmod-core.so:$P7/azstem.so"
mkdir -p /home/root/settings
printf '# azmod.conf: AZ-MODS settings\nversion = 1\nstems = off\n' > $D
st 'mkdir -p MODS/deck/lib MODS/community/x-mod MODS/community/y-mod; echo c > MODS/azmod-core.so; echo s > MODS/azstem.so
    cp /d/lib/az-conf.sh MODS/deck/lib/; echo x > MODS/community/x-mod/x-mod.so; echo y > MODS/community/y-mod/y-mod.so
    printf "x-mod\ny-mod\n" > MODS/community/enabled; : > MODS/community/y-mod/y-mod.off
    mkdir -p MODS/community-data/x-mod MODS/community-data/.x-mod@0123456789abcdef MODS/community-data/gone-mod
    echo mine > MODS/community-data/x-mod/save.txt; echo local > MODS/community-data/.x-mod.owner
    echo other > MODS/community-data/.x-mod@0123456789abcdef/save.txt; echo orphan > MODS/community-data/gone-mod/save.txt'

echo "== m1 migration: no azmods.cfg -> made from the deck copy, the old community files folded in"
boot
chk "m1 the stick's file carries the deck's settings" '[ "$(key $SC stems)" = off ] && [ "$(key $SC version)" = 1 ]'
chk "m1 enabled + .off folded into community-mods.<id> keys" '[ "$(key $SC community-mods.x-mod)" = on ] && [ "$(key $SC community-mods.y-mod)" = off ]'
chk "m1 the old switch files are gone from the stick" '[ ! -e /media/usb/sda1/MODS/community-mods/enabled ] && [ ! -e /media/usb/sda1/MODS/community-mods/y-mod/y-mod.off ]'
U=/media/usb/sda1/MODS
chk "m1 the old layout moved: mods in community-mods/, each one's files in <id>/data/" '[ "$(cat $U/community-mods/x-mod/x-mod.so)" = x ] && [ "$(cat $U/community-mods/x-mod/data/save.txt)" = mine ] && [ "$(cat $U/community-mods/x-mod/.data.owner)" = local ] && [ "$(cat $U/community-mods/x-mod/.data@0123456789abcdef/save.txt)" = other ]'
chk "m1 the old folders are removed (a data folder without its mod with them)" '[ ! -e $U/community ] && [ ! -e $U/community-data ] && [ ! -e $U/community-mods/gone-mod ]'
chk "m1 CONTROL: the moved mods are what the stage mirrors" '[ "$(cat $P7/community/x-mod/x-mod.so)" = x ] && [ ! -e $P7/community/x-mod/data ]'
chk "m1 the stage's switch files come from the keys" '[ "$(cat $P7/community/enabled)" = x-mod ] && [ -e $P7/community/y-mod/y-mod.off ]'
chk "m1 folded keys are handed to azmod; the deck copy is never written by the loader" '[ "$(key $IM community-mods.x-mod)" = on ] && [ "$(key $D community-mods.x-mod)" = "" ]'
chk "m1 the stick is mounted read-only again" 'grep " /media/usb/sda1 " /proc/mounts | grep -q "[ ,]ro[ ,]"'
cp $IM $D; rm -f $IM   # azmod takes it: the deck copy now equals the stick's file

echo "== s1 the stick wins: the same file next boot hands over nothing (CONTROL), an edited one is handed over"
i0=$(sum /img); boot
chk "s1 CONTROL: unchanged stick -> no hand-over, stick untouched" '[ ! -e $IM ] && [ "$i0" = "$(sum /img)" ]'
st 'sed -i "s/^stems = off/stems = on/" MODS/azmods.cfg'; d0=$(sum $D); boot
chk "s1 the edited stick file is handed to azmod" '[ "$(key $IM stems)" = on ] && lg "handed to azmod (the stick wins)"'
chk "s1 the deck copy is left to azmod" '[ "$d0" = "$(sum $D)" ]'
cp $IM $D; rm -f $IM

echo "== n1 no stick: the deck copy is used, nothing handed over"
unplug; d0=$(sum $D); boot
chk "n1 no hand-over, deck copy untouched, no stick read" '[ ! -e $IM ] && [ "$d0" = "$(sum $D)" ] && ! lg "azmods.cfg"'
replug

echo "== b1 a malformed stick file is refused: neither copy changes (CONTROL: s1 imported a valid one)"
st 'cp MODS/azmods.cfg /keep.cfg; printf "stems = off\nwhat\n" > MODS/azmods.cfg'; i0=$(sum /img); d0=$(sum $D); boot
chk "b1 refused, logged" 'lg "not a settings file" && [ ! -e $IM ]'
chk "b1 the stick file and the deck copy are untouched" '[ "$i0" = "$(sum /img)" ] && [ "$d0" = "$(sum $D)" ]'
st 'cp /keep.cfg MODS/azmods.cfg'

echo "== k1 kill switches before EP147: mods = off in a file of its own -> stock (CONTROL: without it, the set)"
boot; chk "k1 CONTROL: the set is wanted" '[ "$(cat $ENVF)" = "$CORE" ]'
st 'cp MODS/azmods.cfg /keep.cfg; echo "mods = off" > MODS/azmods.cfg'; boot
chk "k1 mods = off -> env stock, restarted to stock" '[ -z "$(cat $ENVF 2>/dev/null)" ] && grep -q "restart EP147" /tmp/systemctl.calls'
st 'cp /keep.cfg MODS/azmods.cfg; echo "community-mods = off" >> MODS/azmods.cfg'; boot
chk "k1 community-mods = off -> the stage has no community mods" '[ ! -e $P7/community ] && [ -e $P7/community-mods.off ]'
st 'cp /keep.cfg MODS/azmods.cfg'; boot >/dev/null
chk "k1 back to the deck copy's settings: the hand-over azmod never took is dropped" '[ ! -e $IM ]'

echo "== w1 write-back: a Mod Menu change reaches the stick once every deck is empty, whole"
printf 'azdecks 1\npid %s\nup %s\ndecks ELEE\n' "$(cat /tmp/stub.ep147pid)" "$(cut -d' ' -f1 /proc/uptime)" > /tmp/az-decks
st 'printf "junk from a pulled stick" > MODS/.azmods.cfg.new'
devflag; rm -f /tmp/az-usbload.log; AZ_NOWATCH='' /usr/sbin/az-usbload >/dev/null 2>&1 & W=$!
waitfor() { i=0; while [ $i -lt "${2:-150}" ]; do eval "$1" && return 0; sleep 0.2; i=$((i+1)); done; return 1; }
waitfor 'grep -q "watching" /tmp/az-usbload.log 2>/dev/null' 450
chk "w1 CONTROL: a leftover .new is never read (the stick's file was taken whole)" '! lg "not a settings file" && [ ! -e $IM ]'
sed 's/^stems = on/stems = off/' $D > $D.new && mv $D.new $D
sleep 3; chk "w1 a track loaded -> not written yet" '[ "$(key $SC stems)" = on ] && lg "once every deck is empty"'
st 'rm -f MODS/.azmods.cfg.new; dd if=/dev/zero of=MODS/fill bs=1k count=99999 2>/dev/null; true'   # the stick is full: the write fails
printf 'azdecks 1\npid %s\nup %s\ndecks EEEE\n' "$(cat /tmp/stub.ep147pid)" "$(cut -d' ' -f1 /proc/uptime)" > /tmp/az-decks
waitfor 'lg "could not write the stick"' 100
chk "w1 a failed write keeps the old file whole, no .new left" '[ "$(key $SC stems)" = on ] && [ "$(key $SC version)" = 1 ] && [ ! -e /media/usb/sda1/MODS/.azmods.cfg.new ]'
st 'rm -f MODS/fill'
waitfor '[ "$(key $SC stems)" = off ]' 100
chk "w1 retried once there is room: the stick holds the deck copy byte for byte" 'cmp -s $D $SC && lg "written to the stick"'
chk "w1 the stick is read-only again" 'grep " /media/usb/sda1 " /proc/mounts | grep -q "[ ,]ro[ ,]"'
kill $W 2>/dev/null; sleep 0.3
