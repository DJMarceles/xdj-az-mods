#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# shellcheck disable=SC2034  # captured values are read inside chk's single-quoted eval strings
# Test suite for az-usbload's restart gate: boot transaction finished and EP147
# initialized (WiFi up, or settled). Run by test/run.sh in its alpine container
# (`test/run.sh gate`).
set -u
. /stub/devflag
apk add -q dosfstools >/dev/null 2>&1 || { echo NO_DOSFSTOOLS; exit 1; }
export PATH=/stub:$PATH AZ_ACTIVE_WAIT=1
for f in az-usbload az-usbpick; do cp /p/usr/sbin/$f /usr/sbin/$f; done; chmod +x /usr/sbin/az-usbload
P7=/home/root/settings/az-usbload; mkdir -p /m /media/usb/sda1 /var/run/wpa_supplicant /tmp/bin
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }
rm -f /img; dd if=/dev/zero of=/img bs=1M count=16 2>/dev/null; mkfs.vfat /img >/dev/null
mount /img /m; mkdir /m/MODS; echo c > /m/MODS/azmod-core.so; umount /m
lo=$(losetup -f); losetup "$lo" /img; IFS=: read -r _maj _min < /sys/block/"${lo#/dev/}"/dev; mknod /dev/sda1 b "$_maj" "$_min"; echo "sda 1-1" > /tmp/ports
mount -o ro /dev/sda1 /media/usb/sda1
printf '#!/bin/sh\nwhile :; do sleep 1; done\n' > /tmp/bin/wpa_supplicant; chmod +x /tmp/bin/wpa_supplicant
ep147() { sh -c "echo -n $1 > /proc/self/comm; while :; do sleep 1; done" & echo $! > /tmp/stub.ep147pid; EP=$!; }
wpa_on() { /tmp/bin/wpa_supplicant -i mlan0 -D nl80211 -B -c /x & WPA=$!; sleep 0.3; }
reset() { kill ${EP:+"$EP"} ${WPA:+"$WPA"} 2>/dev/null; rm -rf $P7 /tmp/systemctl.calls /tmp/stub.* /var/net_mlan0_state.dat; EP=; WPA=; }
boot() { devflag; /usr/sbin/az-usbload; grep -hE "gate|wifi\[pre-gate\] procs|failed state" /tmp/az-usbload.log | sed 's/^/    /'; }
restarted() { grep -q "restart EP147" /tmp/systemctl.calls 2>/dev/null; }
gate_s() { sed -n 's/.*restart gate satisfied after \([0-9.]*\)s.*/\1/p' /tmp/az-usbload.log; }
between() { awk -v g="$(gate_s)" -v a="$1" -v b="$2" 'BEGIN{exit !(g!="" && g>=a && g<b)}'; }

echo "== g1 boot transaction never finishes -> no restart"; reset; ep147 HuiProcessor; echo -n up > /var/net_mlan0_state.dat; echo starting > /tmp/stub.sysstate
AZ_GATE_MAX=2 boot; chk "g1 no restart" '! restarted'; chk "g1 logged" 'grep -q "gate NOT satisfied .*system=starting" /tmp/az-usbload.log'

echo "== g2 running + HuiProcessor + net_mlan0_state=up -> immediate"; reset; ep147 HuiProcessor; echo -n up > /var/net_mlan0_state.dat
AZ_EP147_SETTLE=99 boot; chk "g2 restarted" restarted; chk "g2 via state file, < 1s" 'grep -q "satisfied .*net_mlan0_state=up" /tmp/az-usbload.log && between 0 1'

echo "== g3 wpa_supplicant running, COMPLETED after ~2s"; reset; ep147 HuiProcessor; wpa_on; echo SCANNING > /tmp/stub.wpa_state
( sleep 2; echo COMPLETED > /tmp/stub.wpa_state ) & AZ_EP147_SETTLE=99 boot
chk "g3 via wpa_cli in 1.5..4s" 'grep -q "satisfied .*wpa_state=COMPLETED" /tmp/az-usbload.log && between 1.5 4'
chk "g3 pre-gate capture shows wpa_supplicant" 'grep -q "wifi\[pre-gate\] procs=\[.*wpa_supplicant -i mlan0" /tmp/az-usbload.log'
chk "g3 capture after restart" 'grep -q "wifi\[+20s-after-active\]" /tmp/az-usbload.log'
chk "g3 back = new pid (not 0s/empty)" 'grep -qE "EP147 back after [0-9.]+s \(pid [0-9]+ -> [0-9]+, HuiProcessor up\)" /tmp/az-usbload.log'

echo "== g4 COMPLETED but no wpa_supplicant process -> falls to settle"; reset; ep147 HuiProcessor; echo COMPLETED > /tmp/stub.wpa_state
AZ_EP147_SETTLE=2 boot; chk "g4 via settle, not wpa_cli" 'grep -q "satisfied .*WiFi not associated" /tmp/az-usbload.log'

echo "== g5 no WiFi at all -> settle fallback"; reset; ep147 HuiProcessor; sleep 1
AZ_EP147_SETTLE=2 boot; chk "g5 restarted via settle" 'restarted && grep -q "WiFi not associated" /tmp/az-usbload.log'

echo "== g6 degraded counts as finished"; reset; ep147 HuiProcessor; echo -n up > /var/net_mlan0_state.dat; echo degraded > /tmp/stub.sysstate
boot; chk "g6 restarted" restarted

echo "== g7 WiFi up but EP147 not running -> no restart"; reset; echo -n up > /var/net_mlan0_state.dat
AZ_GATE_MAX=2 boot; chk "g7 no restart" '! restarted'

echo "== g8 EP147 running but no HuiProcessor thread -> no restart"; reset; ep147 EP147; echo -n up > /var/net_mlan0_state.dat
AZ_GATE_MAX=2 boot; chk "g8 no restart" '! restarted'; chk "g8 logged hui=no" 'grep -q "hui=no" /tmp/az-usbload.log'

echo "== g10 passPhrase: length only, value NEVER, in any log the loader writes"; reset; ep147 HuiProcessor; echo -n up > /var/net_mlan0_state.dat
mkdir -p /home/root/settings
# nasty fixture: spaces, a backslash, a hash, other keys, on one line and again pretty-printed;
# escaped quotes are left out
# deliberately: sed stops at the first quote, which can only under-count, never leak.
SECRET='Sup3r-tricky pass\phrase #1'
EXPECT=$(printf '%s' "$SECRET" | wc -c | tr -cd '0-9')
printf '{"ssid":"HomeNet","passPhrase":"%s","auto":true}\n' "$SECRET" > /home/root/settings/wifi.json
boot >/dev/null
chk "g10 length is digits only" 'grep -qE "passPhrase length=\[[0-9]+\] file=\[present\]" /tmp/az-usbload.log'
chk "g10 length is right ($EXPECT)" 'grep -q "passPhrase length=\[$EXPECT\]" /tmp/az-usbload.log'
LOGS="/tmp/az-usbload.log $P7/usbload.log"
chk "g10 value never appears in any log" '! grep -rqF "tricky" $LOGS 2>/dev/null && ! grep -rqF "Sup3r" $LOGS 2>/dev/null'
printf '{\n  "ssid": "HomeNet",\n  "passPhrase" :  "%s" ,\n  "auto": true\n}\n' "$SECRET" > /home/root/settings/wifi.json
rm -f $P7/*.so; boot >/dev/null
chk "g10 pretty-printed json: same length" 'grep -q "passPhrase length=\[$EXPECT\]" /tmp/az-usbload.log'
chk "g10 pretty-printed: value still absent" '! grep -rqF "tricky" $LOGS 2>/dev/null'
chk "g10 ssid presence only, never the name" '! grep -rqF "HomeNet" $LOGS 2>/dev/null && grep -q "ssid:" /tmp/az-usbload.log'
rm -f /home/root/settings/wifi.json; rm -f $P7/*.so; boot >/dev/null
chk "g10 absent file reported" 'grep -q "file=\[absent\]" /tmp/az-usbload.log'

echo "== g9 boot finishes after ~2s -> gate time reflects it"; reset; ep147 HuiProcessor; echo -n up > /var/net_mlan0_state.dat; echo starting > /tmp/stub.sysstate
( sleep 2; echo running > /tmp/stub.sysstate ) & boot
chk "g9 satisfied in 1.5..4s" 'between 1.5 4'; reset
