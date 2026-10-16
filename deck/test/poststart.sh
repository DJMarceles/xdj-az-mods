#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# Test suite for az-usbload --post-start: the ExecStartPost re-announce after an
# EP147 restart the loader did not make (mod-menu RELOAD, crash restart); without
# it the USB does not come back after such a restart. Run by test/run.sh
# in its alpine container (`test/run.sh poststart`).
#
# Control in every round: the stub renotify leaves a marker naming which copy ran, so
# "not called" and "wrong copy" are both visible. Fast: the two announce delays are 0 here.
set -u
. /stub/devflag
export PATH=/stub:$PATH AZ_ACTIVE_WAIT=2 AZ_RENOTIFY_DELAY1=0 AZ_RENOTIFY_DELAY2=0
. /stub/fake-ep147
cp /p/usr/sbin/az-usbload /usr/sbin/; chmod +x /usr/sbin/az-usbload
mkdir -p /run /home/root/scripts /home/root/settings/az-usbload /tmp/az-deck/lib
STAGE=/home/root/settings/az-usbload; LOG=/tmp/az-usbload-poststart.log
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }
mkren() { printf '#!/bin/sh\necho %s >> /tmp/renotified\n' "$2" > "$1"; }
mkren /home/root/scripts/usb-renotify.sh firmware
mkren /tmp/az-deck/lib/usb-renotify.sh tmpcopy
mkdir -p $STAGE/deck/lib; mkren $STAGE/deck/lib/usb-renotify.sh cache
ps_() { rm -f /tmp/renotified; devflag; /usr/sbin/az-usbload --post-start; grep -h "" $LOG | sed 's/^/    /'; }
calls() { tr '\n' ' ' < /tmp/renotified 2>/dev/null; }
lg() { grep -q "$1" $LOG; }

echo "== p1 first start of the boot (no booted marker) -> nothing"; rm -f /run/az-usbload.booted; ps_
chk "p1 logged first start" 'lg "first start of this boot"'; chk "p1 renotify NOT called" '[ -z "$(calls)" ]'

echo "== p2 booted, EP147 up -> re-announce twice via this boot's /tmp copy"; touch /run/az-usbload.booted; ps_
chk "p2 announced twice" '[ "$(calls)" = "tmpcopy tmpcopy " ]'; chk "p2 logged" 'lg "re-announcing USB via /tmp/az-deck/lib/usb-renotify.sh"'

echo "== p3 the loader's own restart in flight -> skipped (CONTROL: p2 ran)"; touch /run/az-usbload.restarting; ps_
chk "p3 skipped" 'lg "the loader.s own restart"'; chk "p3 not called" '[ -z "$(calls)" ]'
echo "== p3b a STALE restarting marker (loader died mid-restart) does not block forever"
touch -d '2020-01-01' /run/az-usbload.restarting 2>/dev/null || touch -t 202001010000 /run/az-usbload.restarting; ps_
chk "p3b announced despite the stale marker" '[ "$(calls)" = "tmpcopy tmpcopy " ]'; rm -f /run/az-usbload.restarting

echo "== p4 no /tmp copy (a reload long after boot) -> the p7 cache's copy"; rm -rf /tmp/az-deck; ps_
chk "p4 cache copy used" '[ "$(calls)" = "cache cache " ]'
echo "== p4b no cache either -> the firmware copy"; mv $STAGE/deck /tmp/deck.keep; ps_
chk "p4b firmware copy used" '[ "$(calls)" = "firmware firmware " ]'; mv /tmp/deck.keep $STAGE/deck

echo "== p5 a cached deck/az-usbload takes --post-start too (chained, one announce pair)"
cp /usr/sbin/az-usbload $STAGE/deck/az-usbload; ps_
chk "p5 chained" 'lg "chain: handing over to the cached deck/az-usbload"'; chk "p5 announced once each" '[ "$(calls)" = "cache cache " ]'
echo "== p5b a cached copy that does not parse is ignored"; printf 'if [ x; then\n' > $STAGE/deck/az-usbload; ps_
chk "p5b not chained, still announced" '! lg "chain:" && [ "$(calls)" = "cache cache " ]'; rm -f $STAGE/deck/az-usbload

echo "== p6 EP147 never comes up (no HuiProcessor) -> not announcing"; kill "$(cat /tmp/stub.ep147pid)" 2>/dev/null; rm -f /tmp/stub.ep147pid; ps_
chk "p6 gave up" 'lg "not up within"'; chk "p6 not called" '[ -z "$(calls)" ]'

echo "== p7 the shipped drop-in wires it in, backgrounded, and keeps the EnvironmentFile"
chk "p7 ExecStartPost present + backgrounded" 'grep -q "^ExecStartPost=-/bin/sh -c .*az-usbload --post-start.* &" /p/etc/systemd/system/EP147.service.d/10-azmod.conf'
chk "p7 EnvironmentFile kept" 'grep -q "^EnvironmentFile=-/home/root/settings/az-usbload/az-preload.env" /p/etc/systemd/system/EP147.service.d/10-azmod.conf'
chk "p7 no static LD_PRELOAD" '! grep -q "Environment=LD_PRELOAD" /p/etc/systemd/system/EP147.service.d/10-azmod.conf'
