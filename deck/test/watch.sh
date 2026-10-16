# SPDX-License-Identifier: GPL-3.0-only
set -u
# The loader's request WATCHER serving /run/az-community.req and /run/az-savelogs.req
# beside /run/az-modupdate.req. Stub deck/lib scripts record exactly what they were given,
# so "not run", "run with split/globbed arguments" and "run twice" are all visible.
# The watcher never exits: each round starts it in the background and kills it.
apk add -q dosfstools >/dev/null 2>&1 || { echo NO_DOSFSTOOLS; exit 1; }
export PATH=/stub:$PATH AZ_ACTIVE_WAIT=2 AZ_MODUPDATE_AUTOCHECK=100000
. /stub/fake-ep147
. /stub/devflag
for f in az-usbload az-usbpick; do cp /p/usr/sbin/$f /usr/sbin/$f; done; chmod +x /usr/sbin/az-usbload
mkdir -p /home/root/scripts; cp /p/home/root/scripts/usb-renotify.sh /home/root/scripts/
mkdir -p /m /media/usb/sda1 /run
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }
rm -f /img; dd if=/dev/zero of=/img bs=1M count=16 2>/dev/null; mkfs.vfat /img >/dev/null
lo=$(losetup -f); losetup "$lo" /img; IFS=: read -r _maj _min < /sys/block/"${lo#/dev/}"/dev; mknod /dev/sda1 b "$_maj" "$_min"; echo "sda 1-1" > /tmp/ports
st() { umount /media/usb/sda1 2>/dev/null; mount /dev/sda1 /m; ( cd /m || exit; eval "$1" ); umount /m; mount -o ro /dev/sda1 /media/usb/sda1; }
# stubs: one line per call = "$# [$1]"; apply/update end in reloading, settle/check in ok
printf '%s\n' 'echo "$# [$*]" >> /tmp/community.calls; echo "${AZ_MODS:-none}" >> /tmp/community.mods; mkdir -p /tmp/az-community' \
  'case "$1" in apply) echo state=reloading > /tmp/az-community/status ;; *) echo state=ok > /tmp/az-community/status ;; esac' > /tmp/stub-community
printf '%s\n' 'echo "$# [$*]" >> /tmp/modupdate.calls; mkdir -p /tmp/az-modupdate; echo state=uptodate > /tmp/az-modupdate/status' > /tmp/stub-modupdate
printf '%s\n' 'echo "$# [$*] decks=${AZ_DECKS_STATE:-none} mods=${AZ_MODS:-none}" >> /tmp/savelogs.calls' > /tmp/stub-savelogs
st 'mkdir -p MODS/deck/lib; echo c > MODS/azmod-core.so; cp /tmp/stub-community MODS/deck/lib/az-community; cp /tmp/stub-modupdate MODS/deck/lib/az-modupdate; cp /tmp/stub-savelogs MODS/deck/lib/az-savelogs'
waitfor() { i=0; while [ $i -lt "${2:-150}" ]; do eval "$1" && return 0; sleep 0.2; i=$((i+1)); done; return 1; }
# the first boot applies the set (restart + the renotify sleeps, ~20 s) before it watches
start() { rm -f /tmp/community.calls /tmp/modupdate.calls /tmp/az-usbload.log; devflag; AZ_NOWATCH='' /usr/sbin/az-usbload >/dev/null 2>&1 & W=$!
          waitfor 'grep -q "watching" /tmp/az-usbload.log 2>/dev/null' 450; }
stop() { kill $W 2>/dev/null; pkill -f az-usbload 2>/dev/null; sleep 0.3; }
calls() { cat /tmp/community.calls 2>/dev/null; }

echo "== x1 a request left from before the watcher started is dropped, never run"
mkdir -p /run; echo "remove stale-mod" > /run/az-community.req; start
chk "x1 watcher up for both files" 'grep -q "modupdate: watching /run/az-modupdate.req" /tmp/az-usbload.log && grep -q "community: watching /run/az-community.req" /tmp/az-usbload.log'
sleep 1.5; chk "x1 stale request not run" '! grep -q stale-mod /tmp/community.calls 2>/dev/null'
echo "== x2 a request line reaches az-community as ONE argument, unglobbed"
echo "install https://example.com/azmods-repo.json good-mod" > /run/az-community.req
waitfor '[ -s /tmp/community.calls ]'
chk "x2 one call, one argument, the whole line" '[ "$(calls)" = "1 [install https://example.com/azmods-repo.json good-mod]" ]'
chk "x2 request file consumed" '[ ! -e /run/az-community.req ]'
printf 'install * x\r\n' > /run/az-community.req; waitfor '[ "$(calls | wc -l)" -ge 2 ]'
chk "x2b a glob stays literal, CR stripped" '[ "$(calls | sed -n 2p)" = "1 [install * x]" ]'
echo "== x3 CONTROL in the same round: a modupdate request is still served"
echo check > /run/az-modupdate.req; waitfor '[ -s /tmp/modupdate.calls ]'
chk "x3 modupdate check ran once" '[ "$(cat /tmp/modupdate.calls)" = "1 [check]" ]'
echo "== x4 apply -> the loader re-runs (stage + gated reload), then settle"
echo apply > /run/az-community.req
waitfor 'grep -q "RE-RUN" /tmp/az-usbload.log' 450
chk "x4 re-run logged" 'grep -q "community: apply -> re-running the loader" /tmp/az-usbload.log && grep -q "RE-RUN" /tmp/az-usbload.log'
waitfor 'grep -q "settle" /tmp/community.calls' 450
chk "x4 settle after the re-run" '[ "$(calls | tail -1)" = "1 [settle]" ]'
chk "x4 the re-run's watcher is up again" 'waitfor "[ \$(grep -c watching /tmp/az-usbload.log) -ge 2 ]"'
echo "== x5 after the re-run, requests are still served"
echo "enable good-mod" > /run/az-community.req; waitfor 'grep -q "enable good-mod" /tmp/community.calls'
chk "x5 served" 'grep -qx "1 \[enable good-mod\]" /tmp/community.calls'
echo "== x11 Save Logs to USB: one sanitized argument, the stick, and the loader's decks_state"
rm -f /tmp/savelogs.calls /tmp/az-decks; echo "save 123" > /run/az-savelogs.req; waitfor '[ -s /tmp/savelogs.calls ]'
chk "x11a CONTROL: no azdecks publication -> decks unknown" '[ "$(cat /tmp/savelogs.calls)" = "1 [save 123] decks=unknown mods=/media/usb/sda1/MODS" ]'
chk "x11a request file consumed" '[ ! -e /run/az-savelogs.req ]'
printf 'azdecks 1\npid %s\nup %s\ndecks EEEE\n' "$(cat /tmp/stub.ep147pid)" "$(cut -d' ' -f1 /proc/uptime)" > /tmp/az-decks
printf 'save 456; rm -rf /*\r\n' > /run/az-savelogs.req; waitfor '[ "$(wc -l < /tmp/savelogs.calls)" -ge 2 ]'
chk "x11b every deck proven empty -> decks=empty; the line cut to [a-z0-9 ]" '[ "$(sed -n 2p /tmp/savelogs.calls)" = "1 [save 456 rm rf ] decks=empty mods=/media/usb/sda1/MODS" ]'
printf 'azdecks 1\npid %s\nup %s\ndecks ELEE\n' "$(cat /tmp/stub.ep147pid)" "$(cut -d' ' -f1 /proc/uptime)" > /tmp/az-decks
echo "save 789" > /run/az-savelogs.req; waitfor '[ "$(wc -l < /tmp/savelogs.calls)" -ge 3 ]'
chk "x11c a deck with a track -> decks=loaded" '[ "$(sed -n 3p /tmp/savelogs.calls)" = "1 [save 789] decks=loaded mods=/media/usb/sda1/MODS" ]'
rm -f /tmp/az-decks
stop
echo "== x6 a stick with ONLY az-community (no az-modupdate): the watcher still runs"
st 'rm -f MODS/deck/lib/az-modupdate'; start
chk "x6 watcher up" 'grep -q watching /tmp/az-usbload.log'
echo refresh > /run/az-community.req; waitfor '[ -s /tmp/community.calls ]'
chk "x6 served" '[ "$(calls)" = "1 [refresh]" ]'
chk "x6 CONTROL: the request got the boot's stick" '[ "$(tail -1 /tmp/community.mods)" = /media/usb/sda1/MODS ]'
echo "== x10 .deck-info fwtag= follows core's /tmp/azmod-community/loaded (the watcher's 5 s tick)"
# shellcheck disable=SC2034 # read by the eval'd assertions below
DI=/media/usb/sda1/MODS/.deck-info; LD=/tmp/azmod-community/loaded
chk "x10 CONTROL: no loaded file -> .deck-info written, no fwtag line" '[ -f $DI ] && ! grep -q "^fwtag=" $DI'
mkdir -p ${LD%/*}; printf 'fw=\nsuspended=0\n' > $LD; sleep 7
chk "x10b an empty fw= (tag not derivable) -> still no fwtag line" '! grep -q "^fwtag=" $DI'
printf 'fw=ep147-0123456789abcdef\nsuspended=0\n' > $LD; waitfor 'grep -qx fwtag=ep147-0123456789abcdef $DI 2>/dev/null' 60
chk "x10c fwtag line written, firmware= kept, stick back to ro" 'grep -qx fwtag=ep147-0123456789abcdef $DI && grep -q "^firmware=" $DI && grep " /media/usb/sda1 " /proc/mounts | grep -q "[ ,]ro[ ,]"'
# shellcheck disable=SC2034 # read by the eval'd assertions below
nw=$(grep -c "deck-info: writing" /tmp/az-usbload.log); sleep 7
chk "x10d unchanged -> not rewritten" '[ "$(grep -c "deck-info: writing" /tmp/az-usbload.log)" = "$nw" ]'
rm -f $LD
echo "== x8 the stick swapped while the watcher runs (new sdX name): the request acts on the stick now in the port"
umount /media/usb/sda1; mv /dev/sda1 /dev/sdb1; echo "sdb 1-1" > /tmp/ports; mkdir -p /media/usb/sdb1; mount -o ro /dev/sdb1 /media/usb/sdb1
rm -f /tmp/community.mods; echo refresh > /run/az-community.req; waitfor '[ -s /tmp/community.mods ]'
chk "x8 AZ_MODS is the new mount" '[ "$(cat /tmp/community.mods)" = /media/usb/sdb1/MODS ]'
echo "== x9 the stick pulled: the request gets no stick (az-community refuses: no mods stick)"
umount /media/usb/sdb1; mv /dev/sdb1 /dev.sdb1.off
rm -f /tmp/community.mods; echo "remove good-mod" > /run/az-community.req; waitfor '[ -s /tmp/community.mods ]'
chk "x9 AZ_MODS empty, logged" '[ "$(cat /tmp/community.mods)" = none ] && grep -q "watcher: the pick changed \[/media/usb/sdb1\] -> \[none\]" /tmp/az-usbload.log'
mv /dev.sdb1.off /dev/sda1; echo "sda 1-1" > /tmp/ports; mount -o ro /dev/sda1 /media/usb/sda1
stop
echo "== x7 CONTROL: none of the scripts -> no watcher (the loader exits as before)"
st 'rm -f MODS/deck/lib/az-community MODS/deck/lib/az-savelogs'; rm -f /tmp/az-usbload.log; devflag; AZ_NOWATCH='' timeout 60 /usr/sbin/az-usbload >/dev/null 2>&1
# shellcheck disable=SC2034 # read by the eval'd assertion below
rc=$?
chk "x7 loader exited on its own" '[ "$rc" = 0 ] && ! grep -q watching /tmp/az-usbload.log'
losetup -d "$lo" 2>/dev/null
