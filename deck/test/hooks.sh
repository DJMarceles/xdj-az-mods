#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# shellcheck disable=SC2034  # captured values are read inside chk's single-quoted eval strings
# Test suite for MODS/deck: the stick-side deck scripts (firmware >= 1.32).
# az-usbload stages deck/ to /tmp/az-deck + the p7 cache, chain-loads
# deck/az-usbload, and runs deck/boot.d hooks in order with the pick exported and
# a watchdog each. Run by test/run.sh in its alpine container (`test/run.sh hooks`).
#
# Same harness as mirror.sh. Every case has its control in the same round: a hook that RAN leaves a
# marker file naming its environment, so "did not run" and "ran with the wrong
# env" are both visible, and the firmware/chained loader is told apart by its log.
set -u
. /stub/devflag
apk add -q dosfstools >/dev/null 2>&1 || { echo NO_DOSFSTOOLS; exit 1; }
export PATH=/stub:$PATH AZ_ACTIVE_WAIT=2
. /stub/fake-ep147
for f in az-usbload az-usbpick; do cp /p/usr/sbin/$f /usr/sbin/$f; done; chmod +x /usr/sbin/az-usbload
mkdir -p /home/root/scripts; cp /p/home/root/scripts/usb-renotify.sh /home/root/scripts/; chmod +x /home/root/scripts/usb-renotify.sh
P7=/home/root/settings/az-usbload; ENVF=$P7/az-preload.env; mkdir -p /m /media/usb/sda1
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }
rm -f /img; dd if=/dev/zero of=/img bs=1M count=16 2>/dev/null; mkfs.vfat /img >/dev/null
lo=$(losetup -f); losetup "$lo" /img; IFS=: read -r _maj _min < /sys/block/"${lo#/dev/}"/dev; mknod /dev/sda1 b "$_maj" "$_min"; echo "sda 1-1" > /tmp/ports
st() { umount /media/usb/sda1 2>/dev/null; mount /dev/sda1 /m; ( cd /m || exit 1; eval "$1" ); umount /m; mount -o ro /dev/sda1 /media/usb/sda1; }
unplug() { umount /media/usb/sda1 2>/dev/null; mv /dev/sda1 /dev.sda1.off; }
replug() { mv /dev.sda1.off /dev/sda1; mount -o ro /dev/sda1 /media/usb/sda1; }
boot() { rm -rf /tmp/systemctl.calls /tmp/ran /tmp/az-usbload.log.hooks; mkdir -p /tmp/ran; devflag; /usr/sbin/az-usbload
         grep -hE "start|deck:|chain|hook|desired|same as|env written|restart EP147|uninstall boot|deck-info" /tmp/az-usbload.log | sed 's/^/    /'; }
restarted() { grep -q "restart EP147" /tmp/systemctl.calls 2>/dev/null; }
env_() { cat $ENVF 2>/dev/null; }
lg() { grep -q "$1" /tmp/az-usbload.log; }
# a hook that records where it ran from and what it was told
mkhook() { cat > "$1" <<'H'
#!/bin/sh
n=${0##*/}; d=$(date +%s)
printf 'self=%s mnt=%s port=%s mods=%s off=%s stage=%s deck=%s\n' "$0" "${AZ_MNT-UNSET}" "${AZ_PORT-UNSET}" "${AZ_MODS-UNSET}" "${AZ_MODS_OFF-UNSET}" "${AZ_STAGE-UNSET}" "${AZ_DECK-UNSET}" > /tmp/ran/$n
echo "hello from $n"
H
}
ran() { cat /tmp/ran/"$1" 2>/dev/null; }
echo c > /tmp/core.so
mkhook /tmp/h10.sh; mkhook /tmp/h20.sh

echo "== h1 stick with deck/boot.d: both hooks run, in order, from /tmp, with the pick"
st 'mkdir -p MODS/deck/boot.d; cp /tmp/core.so MODS/azmod-core.so; cp /tmp/h10.sh MODS/deck/boot.d/10-a.sh; cp /tmp/h20.sh MODS/deck/boot.d/20-b.sh; echo 9.9.9 > MODS/deck/version'; boot
chk "h1 10-a ran" '[ -n "$(ran 10-a.sh)" ]'; chk "h1 20-b ran" '[ -n "$(ran 20-b.sh)" ]'
chk "h1 ran from /tmp/az-deck, never the stick" 'ran 10-a.sh | grep -q "self=/tmp/az-deck/boot.d/10-a.sh"'
chk "h1 env: mnt/port/mods" 'ran 10-a.sh | grep -q "mnt=/media/usb/sda1 port=1 mods=/media/usb/sda1/MODS off=0 stage=$P7 deck=/tmp/az-deck"'
chk "h1 order 10 before 20" 'awk "/hook 10-a.sh/{a=NR} /hook 20-b.sh/{b=NR} END{exit !(a&&b&&a<b)}" /tmp/az-usbload.log'
chk "h1 hook stdout captured" 'grep -q "hello from 20-b.sh" /tmp/az-usbload.log.hooks'
chk "h1 exit logged with time" 'grep -qE "hook 10-a.sh: exit 0 after [0-9.]+s" /tmp/az-usbload.log'
chk "h1 deck set version logged" 'lg "using the stick.s MODS/deck (set 9.9.9)"'
chk "h1 cached to p7" '[ -f $P7/deck/boot.d/10-a.sh ] && [ "$(cat $P7/deck/version)" = 9.9.9 ]'
chk "h1 mods still staged + restarted" '[ "$(env_)" = "LD_PRELOAD=$P7/azmod-core.so" ] && restarted'
chk "h1 hooks ran BEFORE the restart" 'awk "/hook 20-b.sh: exit/{h=NR} /restart EP147/{r=NR} END{exit !(h&&r&&h<r)}" /tmp/az-usbload.log'
chk "h1 .deck-info carries the deck set" 'grep -q "^deck=9.9.9$" /media/usb/sda1/MODS/.deck-info && grep -q "^firmware=" /media/usb/sda1/MODS/.deck-info'

echo "== h2 same stick next boot: cache kept, no restart, hooks still run"; boot
chk "h2 keep cache" 'lg "deck: keep deck/ (cache up to date)"'; chk "h2 no restart" '! restarted'
chk "h2 hooks ran on a no-change boot" '[ -n "$(ran 10-a.sh)" ] && [ -n "$(ran 20-b.sh)" ]'
i0=$(md5sum /img | cut -d' ' -f1); boot >/dev/null; chk "h2 stick untouched on the boot after" '[ "$i0" = "$(md5sum /img | cut -d" " -f1)" ]'

echo "== h3 a hook that hangs is killed by the watchdog and the next one still runs"
printf '#!/bin/sh\nsleep 60\n' > /tmp/hang.sh
st 'cp /tmp/hang.sh MODS/deck/boot.d/15-hang.sh'; t0=$(date +%s); AZ_HOOK_MAX=2 boot; t1=$(date +%s)
chk "h3 timed out logged" 'grep -qE "hook 15-hang.sh: TIMED OUT after [0-9.]+s" /tmp/az-usbload.log'
chk "h3 20-b still ran" '[ -n "$(ran 20-b.sh)" ]'
chk "h3 loader did not wait 60s" '[ $((t1 - t0)) -lt 30 ]'
st 'rm MODS/deck/boot.d/15-hang.sh'

echo "== h4 a hook that does not parse is skipped, others run"
printf '#!/bin/sh\nif [ x; then\n' > /tmp/bad.sh
st 'cp /tmp/bad.sh MODS/deck/boot.d/15-bad.sh'; boot
chk "h4 skipped + logged" 'lg "hook 15-bad.sh: does not parse -> skipped"'
chk "h4 20-b still ran" '[ -n "$(ran 20-b.sh)" ]'
st 'rm MODS/deck/boot.d/15-bad.sh'

echo "== h5 AppleDouble sidecar in boot.d is not a hook"
st 'printf "\\0\\5\\26\\7" > "MODS/deck/boot.d/._10-a.sh"'; boot
chk "h5 sidecar not run" '! lg "hook ._10-a.sh"'; chk "h5 real hook ran" '[ -n "$(ran 10-a.sh)" ]'
chk "h5 sidecar not cached" '[ ! -e "$P7/deck/boot.d/._10-a.sh" ]'
st 'rm -f "MODS/deck/boot.d/._10-a.sh"'

echo "== h6 MODS/deck.off: hooks skipped (CONTROL: the boot before ran them)"
st 'touch MODS/deck.off'; boot
chk "h6 skipped logged" 'lg "hooks: deck = off -> skipped"'; chk "h6 nothing ran" '[ -z "$(ran 10-a.sh)" ]'
st 'rm MODS/deck.off'

echo "== h7 chain: deck/az-usbload takes the boot; the firmware copy stops"
printf '#!/bin/sh\necho "CHAINED-MARK chained=${AZ_CHAINED-UNSET}" > /tmp/chained; exit 0\n' > /tmp/chain-ok.sh
st 'cp /tmp/chain-ok.sh MODS/deck/az-usbload'; boot
chk "h7 handed over" 'lg "chain: handing over to deck/az-usbload"'
chk "h7 chained copy ran with AZ_CHAINED=1" 'grep -q "CHAINED-MARK chained=1" /tmp/chained'
chk "h7 firmware copy did NOT go on (no hooks from it)" '! lg "hook 10-a.sh"'
rm -f /tmp/chained

echo "== h8 chain exits non-zero -> firmware copy carries on"
printf '#!/bin/sh\necho x > /tmp/chained; exit 7\n' > /tmp/chain-bad.sh
st 'cp /tmp/chain-bad.sh MODS/deck/az-usbload'; boot
chk "h8 fallback logged" 'lg "chain: deck/az-usbload exited 7 -> firmware loader carries on"'
chk "h8 hooks ran from the firmware copy" '[ -n "$(ran 10-a.sh)" ]'

echo "== h9 chain that does not parse -> firmware copy keeps the boot"
st 'cp /tmp/bad.sh MODS/deck/az-usbload'; boot
chk "h9 logged" 'lg "chain: deck/az-usbload does not parse -> firmware loader keeps this boot"'
chk "h9 hooks ran" '[ -n "$(ran 20-b.sh)" ]'

echo "== h10 mods.off with a chain present: chain skipped, boot goes stock, hooks see off=1"
rm -f /tmp/chained; st 'cp /tmp/chain-ok.sh MODS/deck/az-usbload; touch MODS/mods.off'; boot
chk "h10 chain skipped" 'lg "chain: mods = off -> firmware loader keeps this boot"'
chk "h10 chained copy did not run" '[ ! -e /tmp/chained ]'
chk "h10 env stock + restarted" '[ -z "$(env_)" ] && restarted'
chk "h10 hooks ran with AZ_MODS_OFF=1" 'ran 10-a.sh | grep -q " off=1 "'
st 'rm MODS/mods.off MODS/deck/az-usbload'; boot >/dev/null

echo "== h11 no stick: the CACHED deck/ hooks run with an empty pick"
unplug; boot
chk "h11 cached copy used" 'lg "deck: no stick -> using the cached deck/ (set 9.9.9)"'
chk "h11 hooks ran with mnt empty" 'ran 10-a.sh | grep -q "self=/tmp/az-deck/boot.d/10-a.sh mnt= port= mods= off=0"'
replug

echo "== h12 stick without deck/: nothing runs, the cache is dropped"
st 'rm -rf MODS/deck'; boot
chk "h12 logged" 'lg "deck: stick has no MODS/deck -> no deck scripts this boot"'
chk "h12 cache removed" '[ ! -e $P7/deck ]'; chk "h12 nothing ran" '[ -z "$(ran 10-a.sh)" ]'
chk "h12 hooks: none" 'lg "hooks: none"'

echo "== h13 THE REAL CHAIN: the staged MODS/deck (what ships) runs the whole boot"
# lib/az-usbpick marked so we can see the chained loader used the STICK's helper,
# lib/usb-renotify.sh replaced by a marker so we can see the stick's copy was used
mkdir -p /tmp/deckreal; cp -r /d/. /tmp/deckreal/
sed -i 's/^pick_stick() {/pick_stick() { log "USBPICK-FROM-DECK"/' /tmp/deckreal/lib/az-usbpick
printf '#!/bin/sh\necho RENOTIFY-FROM-DECK > /tmp/renotified\n' > /tmp/deckreal/lib/usb-renotify.sh
rm -rf /tmp/deckreal/boot.d/*; cp /tmp/h10.sh /tmp/deckreal/boot.d/10-a.sh
rm -rf $P7; rm -f /tmp/renotified
st 'rm -rf MODS/deck; mkdir -p MODS/deck; cp -r /tmp/deckreal/. MODS/deck/; echo r1 > MODS/azmod-core.so'; boot
chk "h13 chained start logged" 'lg "start: CHAINED loader from MODS/deck (set test)"'
chk "h13 stick usbpick used" 'lg "USBPICK-FROM-DECK"'
chk "h13 mods staged by the chained loader + restarted" '[ "$(env_)" = "LD_PRELOAD=$P7/azmod-core.so" ] && restarted'
chk "h13 hook ran under the chained loader" 'ran 10-a.sh | grep -q "mnt=/media/usb/sda1 port=1"'
chk "h13 stick renotify used after the restart" 'grep -q RENOTIFY-FROM-DECK /tmp/renotified'
chk "h13 firmware copy did not also run hooks (one hook line)" '[ "$(grep -c "hook 10-a.sh: exit" /tmp/az-usbload.log)" = 1 ]'
chk "h13 one restart only" '[ "$(grep -c "restart EP147" /tmp/systemctl.calls)" = 1 ]'
echo "== h13b same stick again: chained, nothing changes, no restart"; boot
chk "h13b chained" 'lg "start: CHAINED"'; chk "h13b no restart" '! restarted'; chk "h13b same as loaded" 'lg "same as loaded"'

echo "== h14 uninstall boot through the REAL hooks: cache purged and NOT recreated by the env write"
mkdir -p /home/root/settings; echo cfg > /home/root/settings/azmod.cfg
cp /d/boot.d/30-uninstall.sh /tmp/deckreal/boot.d/
st 'rm -rf MODS/deck; mkdir -p MODS/deck; cp -r /tmp/deckreal/. MODS/deck/; touch MODS/uninstall'; boot
chk "h14 desired none" 'lg "MODS/uninstall present -> desired: none"'
chk "h14 env not written on an uninstall boot" 'lg "uninstall boot: not writing the env"'
chk "h14 cache gone after the hooks" '[ ! -e $P7 ]'
chk "h14 azmod.cfg gone" '[ ! -e /home/root/settings/azmod.cfg ]'
chk "h14 restarted to stock" 'restarted'
chk "h14 CLEAN reported to the stick" 'grep -q "^result=CLEAN$" /media/usb/sda1/MODS/.uninstall-result'
chk "h14 p7 cache still absent after the restart path" '[ ! -e $P7/az-preload.env ]'

echo "== h14b uninstall with a LIVE azstem: files re-created AFTER the purge, yet CLEAN in ONE boot"
# The hooks run while EP147 is still up with the cached preload, so azstem keeps writing
# azstem.avail*/curtrack to p7: the moment after 30-uninstall deletes them they are back,
# and the first pass reports INCOMPLETE. The loader must re-run 30-uninstall after it has
# restarted EP147 to stock (azstem gone). Reproduce the race deterministically: a sim refills
# the files until the stock restart, and a PATH-override systemctl stops it the instant the
# restart is issued, exactly as a real restart kills the azstem that lived inside EP147.
P7ROOT=/home/root/settings; rm -rf "$P7ROOT"; mkdir -p "$P7/azmod-pngpatch"
echo core > "$P7/azmod-core.so"; echo stem > "$P7/azstem.so"
printf 'LD_PRELOAD=%s/azmod-core.so:%s/azstem.so\n' "$P7" "$P7" > "$ENVF"   # HAVE != "" -> the stock restart fires
echo cfg > "$P7ROOT/azmod.cfg"
for f in azstem.avail azstem.avail2 azstem.curtrack; do echo seed > "$P7ROOT/$f"; done
cp /d/boot.d/30-uninstall.sh /tmp/deckreal/boot.d/
st 'rm -rf MODS/deck; mkdir -p MODS/deck; cp -r /tmp/deckreal/. MODS/deck/; touch MODS/uninstall'
rm -f /tmp/systemctl.calls /tmp/azstem.recreated /tmp/sim.pid
# systemctl override: a real `restart EP147.service` kills EP147 (and the azstem in it) before
# the new one starts; model that by stopping the sim synchronously, then fall through to the stub.
mkdir -p /tmp/ov
cat > /tmp/ov/systemctl <<'SC'
#!/bin/sh
[ "$1 $2" = "restart EP147.service" ] && [ -f /tmp/sim.pid ] && { kill "$(cat /tmp/sim.pid)" 2>/dev/null; rm -f /tmp/sim.pid; }
exec /stub/systemctl "$@"
SC
chmod +x /tmp/ov/systemctl
# the live azstem: refill its files; count every time it refills one the purge had deleted
( while :; do
    for f in azstem.avail azstem.avail2 azstem.curtrack; do
      [ -e "$P7ROOT/$f" ] || echo x >> /tmp/azstem.recreated
      echo live > "$P7ROOT/$f" 2>/dev/null
    done; sleep 0.02
  done ) & SIM=$!
echo $SIM > /tmp/sim.pid   # the SUBSHELL's pid ($! in the parent), not $$ (which is the parent shell)
OLDPATH=$PATH; export PATH=/tmp/ov:$PATH
boot
export PATH=$OLDPATH; kill $SIM 2>/dev/null; wait $SIM 2>/dev/null; rm -rf /tmp/ov /tmp/sim.pid
chk "h14b the race is REAL: azstem refilled a file the purge had deleted" '[ -s /tmp/azstem.recreated ]'
chk "h14b restarted to stock" 'restarted'
chk "h14b loader re-ran 30-uninstall after the stock restart" 'lg "re-running 30-uninstall"'
chk "h14b azstem runtime files are GONE from p7" '[ ! -e $P7ROOT/azstem.avail ] && [ ! -e $P7ROOT/azstem.avail2 ] && [ ! -e $P7ROOT/azstem.curtrack ]'
chk "h14b cache + cfg gone" '[ ! -e $P7 ] && [ ! -e $P7ROOT/azmod.cfg ]'
chk "h14b nothing az* left on p7" '[ -z "$(ls -d $P7ROOT/az* 2>/dev/null)" ]'
chk "h14b verdict CLEAN in ONE boot" 'grep -q "^result=CLEAN$" /media/usb/sda1/MODS/.uninstall-result'

echo "== h15 the mods-update WATCHER: the loader stays up, runs requests, re-runs itself after an install"
# a stub updater that records what it was asked and fakes a successful install on "update"
cat > /tmp/stub-modupdate <<'S'
#!/bin/sh
mkdir -p /tmp/az-modupdate; echo "$1 mnt=${AZ_MNT-UNSET} port=${AZ_PORT-UNSET}" >> /tmp/modupdate.calls
# "update" writes a new core to the stick, as a real install does (the stock mount is ro)
if [ "$1" = update ]; then
    mount -o remount,rw "$AZ_MNT"; echo "new core $(date +%s%N)" > "$AZ_MODS/azmod-core.so"; sync; mount -o remount,ro "$AZ_MNT"
    printf 'state=installed\nmsg=fake\n' > /tmp/az-modupdate/status
else printf 'state=uptodate\nmsg=fake\n' > /tmp/az-modupdate/status; fi
S
rm -rf $P7/deck /tmp/modupdate.calls /run/az-modupdate.req
st 'rm -f MODS/uninstall; rm -rf MODS/deck; mkdir -p MODS/deck/lib MODS/deck/boot.d; cp /tmp/stub-modupdate MODS/deck/lib/az-modupdate; cp /tmp/h10.sh MODS/deck/boot.d/10-a.sh; echo 7.7.7 > MODS/deck/version; echo w1 > MODS/azmod-core.so'
# stop a backgrounded loader and its chained-watcher child: h15's re-run chains to
# sh $DECK/az-usbload, whose cmdline is not /usr/sbin/az-usbload, so killing only that
# path would leave the watcher alive: it would pollute the next test's log and hold the
# stick mount (the next loader would see "no mods stick"). Kill the whole az-usbload tree.
stoploader() { pkill -f az-usbload 2>/dev/null; pkill -f usb-renotify 2>/dev/null; [ -n "${LP:-}" ] && { kill "$LP" 2>/dev/null; wait "$LP" 2>/dev/null; }; k=0; while pgrep -f az-usbload >/dev/null 2>&1 && [ $k -lt 25 ]; do pkill -9 -f az-usbload 2>/dev/null; sleep 0.2; k=$((k+1)); done; }
: > /tmp/az-usbload.log
rm -f /tmp/systemctl.calls; devflag; ( unset AZ_NOWATCH; AZ_MODUPDATE_AUTOCHECK=2 /usr/sbin/az-usbload ) & LP=$!
i=0; while [ $i -lt 140 ] && ! grep -q "modupdate: watching" /tmp/az-usbload.log 2>/dev/null; do sleep 0.5; i=$((i+1)); done   # the first run stages + restarts (~22 s of sleeps) before it watches
chk "h15 loader entered the watcher (did not exit)" 'grep -q "modupdate: watching /run/az-modupdate.req (deck set 7.7.7)" /tmp/az-usbload.log && kill -0 $LP 2>/dev/null'
i=0; while [ $i -lt 20 ] && ! grep -q "^check" /tmp/modupdate.calls 2>/dev/null; do sleep 0.5; i=$((i+1)); done
chk "h15 boot auto-check ran with the pick" 'grep -q "^check mnt=/media/usb/sda1 port=1" /tmp/modupdate.calls'
echo check > /run/az-modupdate.req; i=0; while [ $i -lt 20 ] && [ "$(grep -c "^check" /tmp/modupdate.calls 2>/dev/null)" != 2 ]; do sleep 0.5; i=$((i+1)); done
chk "h15 a check request is served and consumed" '[ "$(grep -c "^check" /tmp/modupdate.calls)" = 2 ] && [ ! -e /run/az-modupdate.req ]'
echo update > /run/az-modupdate.req; i=0; while [ $i -lt 40 ] && ! grep -q "re-running the loader" /tmp/az-usbload.log 2>/dev/null; do sleep 0.5; i=$((i+1)); done
chk "h15 update request ran the updater" 'grep -q "^update mnt=/media/usb/sda1" /tmp/modupdate.calls'
chk "h15 after an install the loader re-ran itself" 'grep -q "re-running the loader" /tmp/az-usbload.log'
i=0; while [ $i -lt 140 ] && [ "$(grep -c "modupdate: watching" /tmp/az-usbload.log 2>/dev/null)" != 2 ] ; do sleep 0.5; i=$((i+1)); done
sleep 2; chk "h15 the re-run is the firmware loader, log appended" 'grep -q "start (firmware loader, RE-RUN after a mods update" /tmp/az-usbload.log && grep -q "modupdate: request .update." /tmp/az-usbload.log'
chk "h15 the re-run staged the new set" 'grep -q "update azmod-core.so" /tmp/az-usbload.log && grep -q "mods update staged" /tmp/az-usbload.log && grep -q "new core" $P7/azmod-core.so'
chk "h15 ... and did NOT restart EP147 (one restart: the first boot's)" '[ "$(grep -c "restart EP147.service" /tmp/systemctl.calls)" = 1 ]'
chk "h15 the screen's status was settled by the updater's staged step" 'grep -q "^staged" /tmp/modupdate.calls'
chk "h15 the re-run watches again" '[ "$(grep -c "modupdate: watching" /tmp/az-usbload.log)" = 2 ] && kill -0 $LP 2>/dev/null'
stoploader
st 'rm -rf MODS/deck'
echo "== h15b the re-run after an install goes through the STICK's loader (a 1.32 firmware loader has no watcher)"
# the real staged loader on the stick + the stub updater: after "update" the re-run must be
# the stick copy (non-chained: it refreshes deck/ and chains to itself), and it must not use env -u
chk "h15b loader never uses env -u (the deck's env has none)" '! grep -q "env -u" /usr/sbin/az-usbload'
rm -rf /tmp/modupdate.calls /run/az-modupdate.req /tmp/az-modupdate /run/az-reload-pending
st 'rm -f MODS/uninstall; mkdir -p MODS/deck; cp -r /tmp/deckreal/. MODS/deck/; rm -f MODS/deck/boot.d/*; cp /tmp/h10.sh MODS/deck/boot.d/10-a.sh; cp /tmp/stub-modupdate MODS/deck/lib/az-modupdate; echo 7.7.8 > MODS/deck/version; echo w2 > MODS/azmod-core.so'
: > /tmp/az-usbload.log
rm -f /tmp/systemctl.calls; devflag; ( unset AZ_NOWATCH; AZ_MODUPDATE_AUTOCHECK=999 /usr/sbin/az-usbload ) & LP=$!
i=0; while [ $i -lt 140 ] && ! grep -q "modupdate: watching" /tmp/az-usbload.log 2>/dev/null; do sleep 0.5; i=$((i+1)); done
chk "h15b chained loader is watching" 'grep -q "start: CHAINED" /tmp/az-usbload.log && grep -q "modupdate: watching" /tmp/az-usbload.log'
R0=$(grep -c "restart EP147.service" /tmp/systemctl.calls 2>/dev/null)   # the first boot's (a deck running mods: none)
echo update > /run/az-modupdate.req
i=0; while [ $i -lt 140 ] && [ "$(grep -c "modupdate: watching" /tmp/az-usbload.log 2>/dev/null)" != 2 ]; do sleep 0.5; i=$((i+1)); done; sleep 1
chk "h15b re-run happened" 'grep -q "re-running the loader" /tmp/az-usbload.log'
chk "h15b re-run is the stick copy, non-chained, then chains to the fresh copy" 'grep -q "start (firmware loader, RE-RUN" /tmp/az-usbload.log && [ "$(grep -c "chain: handing over" /tmp/az-usbload.log)" = 2 ] && [ "$(grep -c "start: CHAINED" /tmp/az-usbload.log)" = 2 ]'
chk "h15b post-rerun status settled (staged: the set waits for a Reload)" 'grep -q "^staged" /tmp/modupdate.calls && [ "$(head -n 1 /run/az-reload-pending)" = update ]'
chk "h15b no restart after the update" '[ "$(grep -c "restart EP147.service" /tmp/systemctl.calls 2>/dev/null)" = "$R0" ]'
chk "h15b watching again, process alive" '[ "$(grep -c "modupdate: watching" /tmp/az-usbload.log)" = 2 ] && kill -0 $LP 2>/dev/null'
stoploader
st 'rm -rf MODS/deck'
echo "== h16 no deck/lib/az-modupdate -> the loader exits as before (CONTROL for h15)"
devflag; ( unset AZ_NOWATCH; timeout 20 /usr/sbin/az-usbload ); rc=$?
chk "h16 exited on its own" '[ $rc = 0 ] && ! grep -q "modupdate: watching" /tmp/az-usbload.log'
