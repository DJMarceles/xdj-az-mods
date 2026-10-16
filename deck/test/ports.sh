#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# shellcheck disable=SC2034  # captured values are read inside chk's single-quoted eval strings
# Test suite for the stick pick by physical port (az-usbpick, through az-usbload)
# and the sshgate hook (boot.d/10-sshgate.sh) on the picked stick. Run by
# test/run.sh in its alpine container (`test/run.sh ports`).
set -u
. /stub/devflag
apk add -q dosfstools >/dev/null 2>&1 || { echo NO_DOSFSTOOLS; exit 1; }
export PATH=/stub:$PATH
. /stub/fake-ep147
mkdir -p /usr/sbin; for f in az-usbload az-usbpick; do cp /p/usr/sbin/$f /usr/sbin/$f; done; chmod +x /usr/sbin/az-usbload
# firmware 1.30 ships /usr/sbin/dropbear (the sshgate hook checks it exists; 2.00 has none, see 4d/4e).
# A stub: answers -V like dropbear; the unit is only ever written, never run, in this container.
printf '%s\n' '#!/bin/sh' '[ "${1:-}" = -V ] && echo "Dropbear v-stub(1.30)"; exit 0' > /usr/sbin/dropbear; chmod +x /usr/sbin/dropbear
# sshgate is a MODS/deck hook now: run it the way az-usbload does (picked stick in AZ_MNT/AZ_PORT)
mkdir -p /tmp/az-hookwrap; cp /d/boot.d/10-sshgate.sh /tmp/az-hookwrap/
cat > /usr/sbin/az-sshgate <<'W'
#!/bin/sh
log(){ :; }
. /usr/sbin/az-usbpick
MNT=""; PORT=""; pick_stick
export AZ_MNT="$MNT" AZ_PORT="$PORT" AZ_MODS="${MNT:+$MNT/MODS}" AZ_MODS_OFF=0 AZ_STAGE=/home/root/settings/az-usbload AZ_DECK=/tmp/az-deck AZ_LOG=/tmp/az-usbload.log
exec sh /tmp/az-hookwrap/10-sshgate.sh
W
chmod +x /usr/sbin/az-sshgate
P7=/home/root/settings/az-usbload; : > /tmp/ports
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }
# mk <name> : fresh FAT image /img.<name>; populate with remaining shell cmd run in /m
mk() { rm -f /img."$1"; dd if=/dev/zero of=/img."$1" bs=1M count=16 2>/dev/null; mkfs.vfat /img."$1" >/dev/null; mkdir -p /m; mount /img."$1" /m; ( cd /m || exit 1; eval "$2" ); umount /m; }
# plug <name> <sdX> <bus> : loop + /dev/sdX1 + ro stock mount at /media/usb/sdX1
plug() { lo=$(losetup -f); losetup "$lo" /img."$1"; IFS=: read -r _maj _min < /sys/block/"${lo#/dev/}"/dev; mknod /dev/"${2}"1 b "$_maj" "$_min"; echo "$2 $3" >> /tmp/ports; mkdir -p /media/usb/"${2}"1; mount -o ro /dev/"${2}"1 /media/usb/"${2}"1; eval "LO_$2=$lo"; }
unplugall() { awk '$2 ~ /^\/media\/usb\// {print $2}' /proc/mounts | while IFS= read -r m; do umount "$m"; done; rm -f /dev/sd??; for l in $(losetup -a | cut -d: -f1); do losetup -d "$l"; done; : > /tmp/ports; }
h() { md5sum /img."$1" | cut -d' ' -f1; }
# The cases before 4k test the gate with a key this deck has already approved (rev8): reset seeds
# settings/az-ssh-approved with the test key. 4k on test the approval itself (fresh_deck).
reset() { unplugall; rm -rf /run/az-usbload.env /home/root /root/.ssh /tmp/az_authkeys /tmp/systemctl.calls /run/az-ssh /run/az-sshgate
    mkdir -p /home/root/settings; echo "ssh-ed25519 AAAAtest" > /home/root/settings/az-ssh-approved; }
fresh_deck() { rm -f /home/root/settings/az-ssh-approved; rm -rf /home/root/settings/az-dropbear; }
run() { devflag; /usr/sbin/az-usbload; /usr/sbin/az-sshgate; }
logs() { grep -hE "using USB|ignored|no USB|no stock|UNKNOWN|USB1 has no|copied key|no MODS/azmod|preload" /tmp/az-usbload.log /tmp/az-sshgate.log | sed 's/^/    /'; }

echo "== 1 no stick"; reset; run; logs
chk "1 no p7" '[ ! -e $P7 ]'

echo "== 2 USB1 only, no MODS/"; reset; mk a 'echo x > t.mp3'; plug a sda 1-1; b=$(h a); run; logs; unplugall
chk "2 stick unchanged" '[ "$b" = "$(h a)" ]'; chk "2 no p7" '[ ! -e $P7 ]'

echo "== 3 USB1 only, mods"; reset; mk a 'mkdir MODS; echo one > MODS/azmod.so'; plug a sda 1-1; run; logs
chk "3 staged from USB1" '[ "$(cat $P7/azmod.so)" = one ]'; unplugall

echo "== 4 USB2 only (as sda!), mods + key + ssh.enable"; reset; mk b 'mkdir MODS; echo two > MODS/azmod.so; touch MODS/ssh.enable; echo "ssh-ed25519 AAAAtest k" > MODS/authorized_keys'; plug b sda 2-1; b4=$(h b); run; logs
sync; chk "4 CONTROL: image hash detects the breadcrumb/marker writes" '[ "$b4" != "$(h b)" ]'
chk "4 staged from USB2" '[ "$(cat $P7/azmod.so)" = two ]'; chk "4 sshgate read USB2 key" 'grep -q "copied key from /media/usb/sda1" /tmp/az-sshgate.log'
chk "4 dropbear runs as its OWN unit (survives the loader exiting)" '[ -f /run/systemd/system/az-dropbear.service ] && grep -q "ExecStart=/usr/sbin/dropbear -F" /run/systemd/system/az-dropbear.service && grep -q "restart az-dropbear.service" /tmp/systemctl.calls'
unplugall
# 4b: the same deck, SSH unticked in AZ-MODS (both files removed) -> a re-run turns it OFF
# (SSH must be switchable off, not only on). Case 4 is the control.
mount /img.b /m; rm -f /m/MODS/ssh.enable /m/MODS/authorized_keys; umount /m
plug b sda 2-1; : > /tmp/systemctl.calls; /usr/sbin/az-sshgate; logs
chk "4b off: az-dropbear stopped" 'grep -q "stop az-dropbear.service" /tmp/systemctl.calls'
chk "4b off: unit + installed keys removed" '[ ! -e /run/systemd/system/az-dropbear.service ] && [ ! -e /home/root/.ssh/authorized_keys ] && [ ! -e /root/.ssh/authorized_keys ]'
unplugall
echo "== 4c key but no ssh.enable (a stick an older AZ-MODS half-cleared) -> SSH off"; reset
mk b 'mkdir MODS; echo two > MODS/azmod.so; echo "ssh-ed25519 AAAAtest k" > MODS/authorized_keys'; plug b sda 2-1; run; logs
chk "4c no ssh.enable: no dropbear" '[ ! -e /run/systemd/system/az-dropbear.service ] && ! grep -q "restart az-dropbear" /tmp/systemctl.calls 2>/dev/null'
chk "4c no ssh.enable: logged why" 'grep -q "SSH off: no MODS/ssh.enable" /tmp/az-sshgate.log'
unplugall

echo "== 4d firmware 2.00: no /usr/sbin/dropbear -> the stick's MODS/deck/lib/dropbear, run from /run"; reset
mv /usr/sbin/dropbear /usr/sbin/dropbear.130
mk b 'mkdir -p MODS/deck/lib; echo two > MODS/azmod.so; touch MODS/ssh.enable; echo "ssh-ed25519 AAAAtest k" > MODS/authorized_keys; printf "%s\n" "#!/bin/sh" "[ \"\${1:-}\" = -V ] && echo \"Dropbear v-stub(stick)\"; exit 0" > MODS/deck/lib/dropbear'
plug b sda 2-1; run; logs
chk "4d unit runs the stick's copy from /run" '[ -f /run/systemd/system/az-dropbear.service ] && grep -q "ExecStart=/run/az-dropbear -F -E -s -g -R" /run/systemd/system/az-dropbear.service && grep -q "restart az-dropbear.service" /tmp/systemctl.calls'
chk "4d copy is executable and is the stick's" '[ -x /run/az-dropbear ] && /run/az-dropbear -V | grep -q "stub(stick)"'
chk "4d logged which dropbear" 'grep -q "using the stick" /tmp/az-sshgate.log'
unplugall
echo "== 4e firmware 2.00, stick has NO dropbear -> SSH stays off (fail-safe; CONTROL for 4d)"; reset; rm -f /run/az-dropbear
mk b 'mkdir -p MODS/deck; echo two > MODS/azmod.so; touch MODS/ssh.enable; echo "ssh-ed25519 AAAAtest k" > MODS/authorized_keys'
plug b sda 2-1; run; logs
chk "4e no dropbear anywhere: no unit, no start" '[ ! -e /run/systemd/system/az-dropbear.service ] && ! grep -q "restart az-dropbear" /tmp/systemctl.calls 2>/dev/null'
chk "4e logged why" 'grep -q "no dropbear on this firmware" /tmp/az-sshgate.log'
chk "4e installed keys removed (off is an action)" '[ ! -e /home/root/.ssh/authorized_keys ] && [ ! -e /root/.ssh/authorized_keys ]'
unplugall; mv /usr/sbin/dropbear.130 /usr/sbin/dropbear

echo "== 4f rev7: dropbear at normal priority; the EP147 start window (stick carries deck/lib/az-sshprio)"; reset
rm -rf /run/systemd/system/EP147.service.d /run/systemd/system/az-sshwindow.service /run/az-sshprio
mk b 'mkdir -p MODS/deck/lib; echo two > MODS/azmod.so; touch MODS/ssh.enable; echo "ssh-ed25519 AAAAtest k" > MODS/authorized_keys; cp /d/lib/az-sshprio MODS/deck/lib/'
plug b sda 2-1; run; logs
U=/run/systemd/system/az-dropbear.service; W=/run/systemd/system/az-sshwindow.service; DI=/run/systemd/system/EP147.service.d/50-az-sshwindow.conf
chk "4f dropbear's own unit raises nothing (no chrt, no CPUScheduling)" '[ -f $U ] && ! grep -qiE "chrt|CPUScheduling" $U'
chk "4f window unit: az-sshprio window 120, itself at RR 51" '[ -f $W ] && grep -q "ExecStart=/bin/sh /run/az-sshprio window 120" $W && grep -q "CPUSchedulingPolicy=rr" $W && grep -q "CPUSchedulingPriority=51" $W && grep -q "ExecStopPost=-/bin/sh /run/az-sshprio normal" $W'
chk "4f EP147 drop-in starts the window before every EP147 start" '[ -f $DI ] && grep -q "^ExecStartPre=-/bin/systemctl --no-block restart az-sshwindow.service" $DI'
chk "4f the boot gets one window too, after dropbear started" 'grep -n "restart az-dropbear.service\|no-block restart az-sshwindow.service" /tmp/systemctl.calls | tail -1 | grep -q az-sshwindow'
chk "4f az-sshprio copied to /run and is the stick's" 'cmp -s /run/az-sshprio /d/lib/az-sshprio'
unplugall
# 4g: SSH turned off on the same deck -> window unit, drop-in and helper removed (4f is the control)
mount /img.b /m; rm -f /m/MODS/ssh.enable; umount /m
plug b sda 2-1; : > /tmp/systemctl.calls; /usr/sbin/az-sshgate; logs
chk "4g off: window stopped (its ExecStopPost demotes)" 'grep -q "stop az-sshwindow.service" /tmp/systemctl.calls'
chk "4g off: window unit, drop-in and helper removed" '[ ! -e $W ] && [ ! -e $DI ] && [ ! -e /run/az-sshprio ]'
unplugall
echo "== 4h no az-sshprio on the stick (CONTROL for 4f): SSH on, no window"; reset; rm -f $W $DI /run/az-sshprio
mk b 'mkdir MODS; echo two > MODS/azmod.so; touch MODS/ssh.enable; echo "ssh-ed25519 AAAAtest k" > MODS/authorized_keys'
plug b sda 2-1; run; logs
chk "4h dropbear on, no window unit, no drop-in, logged" '[ -f $U ] && [ ! -e $W ] && [ ! -e $DI ] && grep -q "no EP147 start window" /tmp/az-sshgate.log'
unplugall

echo "== 4j same boot, a set without az-sshprio replaces one with it: the old window goes"; reset
mk b 'mkdir -p MODS/deck/lib; echo two > MODS/azmod.so; touch MODS/ssh.enable; echo "ssh-ed25519 AAAAtest k" > MODS/authorized_keys; cp /d/lib/az-sshprio MODS/deck/lib/'
plug b sda 2-1; run; logs
chk "4j CONTROL: the first run made the window" '[ -f $W ] && [ -f $DI ]'
unplugall; mount /img.b /m; rm -f /m/MODS/deck/lib/az-sshprio; umount /m
plug b sda 2-1; : > /tmp/systemctl.calls; run; logs
chk "4j re-run without the helper: window stopped and removed, SSH still on" 'grep -q "stop az-sshwindow.service" /tmp/systemctl.calls && [ ! -e $W ] && [ ! -e $DI ] && [ -f $U ]'
unplugall

echo "== 4i host key on 1.30: the image's shared key is never served or kept"; reset
rm -rf /etc/dropbear; mkdir -p /etc/dropbear; echo IMAGE-KEY > /etc/dropbear/dropbear_ecdsa_host_key
chk "4i CONTROL: the image key is there before the hook runs" '[ -d /etc/dropbear ] && [ ! -L /etc/dropbear ] && grep -q IMAGE-KEY /etc/dropbear/dropbear_ecdsa_host_key'
mk b 'mkdir MODS; echo two > MODS/azmod.so; touch MODS/ssh.enable; echo "ssh-ed25519 AAAAtest k" > MODS/authorized_keys'
plug b sda 2-1; run; logs
KD=/home/root/settings/az-dropbear
chk "4i /etc/dropbear links to this deck's p7 key dir" '[ "$(readlink /etc/dropbear)" = $KD ] && [ -d $KD ]'
chk "4i the image key is gone and was not copied to p7" '! grep -rqs IMAGE-KEY $KD /etc/dropbear/'
chk "4i dropbear generates the deck's own (-R) and SSH is on" 'grep -q "ExecStart=/usr/sbin/dropbear -F -E -s -g -R" $U && grep -q "host key: this deck" /tmp/az-sshgate.log'
# same boot, a re-run (mods update): the deck's key, already made, is kept as it is
echo DECK-KEY > $KD/dropbear_ed25519_host_key; : > /tmp/systemctl.calls; /usr/sbin/az-sshgate
chk "4i re-run keeps the deck's key" '[ "$(cat $KD/dropbear_ed25519_host_key)" = DECK-KEY ] && [ "$(readlink /etc/dropbear)" = $KD ]'
unplugall; rm -rf /etc/dropbear

echo "== 4k (rev8) a key this deck has not approved is held pending: no login, no dropbear"; reset; fresh_deck
apk add -q openssl >/dev/null 2>&1
K1='ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAREAMyUly6LPnwWcQ+hRMtuQp18K8PAT0rba7kTKDwm laptop'
mk b "mkdir MODS; echo two > MODS/azmod.so; touch MODS/ssh.enable; echo '$K1' > MODS/authorized_keys"
plug b sda 2-1; run; logs
FP1=$(cut -d' ' -f1 /run/az-ssh/pending 2>/dev/null)
chk "4k pending file names the key by its SHA256 fingerprint" '[ -n "$FP1" ] && echo "$FP1" | grep -qE "^SHA256:[A-Za-z0-9+/]{43}$" && grep -q "AAAAIAREAMyUly6LPnwWcQ" /run/az-ssh/pending'
chk "4k fingerprint is exactly what ssh-keygen -l prints for this key" '[ "$FP1" = "SHA256:neIIrosesU6hYZdFGuofTdK+6slBDWGs3Iq+u6DvLbU" ]'
chk "4k not installed: no authorized_keys anywhere" '[ ! -e /home/root/.ssh/authorized_keys ] && [ ! -e /root/.ssh/authorized_keys ]'
chk "4k dropbear not started" '! grep -q "restart az-dropbear.service" /tmp/systemctl.calls'
chk "4k the approval path unit is armed" 'grep -q "PathExists=/run/az-ssh/approve" /run/systemd/system/az-sshapprove.path && grep -q "ExecStart=/bin/sh /run/az-sshgate approve" /run/systemd/system/az-sshapprove.service && grep -q "restart az-sshapprove.path" /tmp/systemctl.calls'
chk "4k logged why" 'grep -q "waiting for approval on the deck" /tmp/az-sshgate.log'
unplugall
echo "== 4k CONTROL: the rev7 hook (before this change) installs the same key at once"; reset; fresh_deck
cp /t/fixtures/sshgate-rev7.sh /tmp/az-hookwrap/10-sshgate.sh; plug b sda 2-1; run
chk "4k CONTROL rev7: key installed and dropbear started immediately" 'grep -q AAAAIAREAMyUly6LPnwWcQ /home/root/.ssh/authorized_keys && grep -q "restart az-dropbear.service" /tmp/systemctl.calls'
cp /d/boot.d/10-sshgate.sh /tmp/az-hookwrap/10-sshgate.sh; unplugall

echo "== 4l no answer / Cancel: the key stays pending across a re-run; a wrong fingerprint changes nothing"; reset; fresh_deck
plug b sda 2-1; run; : > /tmp/systemctl.calls; /usr/sbin/az-sshgate
chk "4l re-run without an approval: still pending, still no login" '[ -s /run/az-ssh/pending ] && [ ! -e /home/root/.ssh/authorized_keys ] && ! grep -q "restart az-dropbear" /tmp/systemctl.calls'
echo "SHA256:notthiskey" > /run/az-ssh/approve; sh /run/az-sshgate approve
chk "4l a fingerprint that is not pending approves nothing" '[ ! -s /home/root/settings/az-ssh-approved ] && [ ! -e /home/root/.ssh/authorized_keys ] && [ ! -e /run/az-ssh/approve ]'

echo "== 4m Allow in the Mod Menu: the approval records the key and lets it log in"
FP1=$(cut -d' ' -f1 /run/az-ssh/pending); : > /tmp/systemctl.calls
echo "$FP1" > /run/az-ssh/approve; sh /run/az-sshgate approve
chk "4m key saved on p7 (type + base64)" 'grep -qx "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAREAMyUly6LPnwWcQ+hRMtuQp18K8PAT0rba7kTKDwm" /home/root/settings/az-ssh-approved'
chk "4m installed in both homes, pending cleared, dropbear started" 'grep -q AAAAIAREAMyUly6LPnwWcQ /home/root/.ssh/authorized_keys && grep -q AAAAIAREAMyUly6LPnwWcQ /root/.ssh/authorized_keys && [ ! -e /run/az-ssh/pending ] && grep -q "restart az-dropbear.service" /tmp/systemctl.calls'
chk "4m the approval appended to the boot log (not truncated)" 'grep -q "waiting for approval" /tmp/az-sshgate.log && grep -q "approved on the deck" /tmp/az-sshgate.log'
echo "== 4n next boot: an approved key logs in at once"; unplugall; rm -rf /run/az-ssh /run/az-sshgate /home/root/.ssh /root/.ssh /tmp/systemctl.calls
plug b sda 2-1; run
chk "4n approved key installed and dropbear started on the next boot" 'grep -q AAAAIAREAMyUly6LPnwWcQ /home/root/.ssh/authorized_keys && grep -q "restart az-dropbear.service" /tmp/systemctl.calls && [ ! -e /run/az-ssh/pending ]'
echo "== 4o a second, unknown key beside the approved one: only the approved one logs in"; unplugall
K2='ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAah0aPK4hfd+1LNsSt5NWy2YN7XhchdZaJUzYvkaw9n other'
mount /img.b /m; echo "$K2" >> /m/MODS/authorized_keys; umount /m; : > /tmp/systemctl.calls
plug b sda 2-1; run
chk "4o approved key in, unknown key out and pending" 'grep -q AAAAIAREAMyUly6LPnwWcQ /home/root/.ssh/authorized_keys && ! grep -q AAAAIAah0aPK4hfd /home/root/.ssh/authorized_keys && grep -q AAAAIAah0aPK4hfd /run/az-ssh/pending && grep -q "restart az-dropbear.service" /tmp/systemctl.calls'
echo "== 4p ssh.enable removed: SSH off as before; the approval stays on the deck"; unplugall
mount /img.b /m; rm -f /m/MODS/ssh.enable; umount /m; : > /tmp/systemctl.calls; plug b sda 2-1; /usr/sbin/az-sshgate
chk "4p off: no keys, no pending, approval path removed, approved list kept" '[ ! -e /home/root/.ssh/authorized_keys ] && [ ! -e /run/az-ssh ] && [ ! -e /run/systemd/system/az-sshapprove.path ] && [ -s /home/root/settings/az-ssh-approved ] && grep -q "stop az-dropbear.service" /tmp/systemctl.calls'
unplugall
echo "== 4q existing setup: the first run on a deck that had SSH on approves the stick's key as it is"; reset; fresh_deck
mkdir -p /home/root/settings/az-dropbear; echo DECK-KEY > /home/root/settings/az-dropbear/dropbear_ed25519_host_key
mk b "mkdir MODS; echo two > MODS/azmod.so; touch MODS/ssh.enable; echo '$K1' > MODS/authorized_keys"
plug b sda 2-1; run
chk "4q migrated: key installed and dropbear started without a dialog" 'grep -q AAAAIAREAMyUly6LPnwWcQ /home/root/.ssh/authorized_keys && grep -q "restart az-dropbear.service" /tmp/systemctl.calls && [ ! -e /run/az-ssh/pending ]'
chk "4q recorded once (the list exists now, so a later new key will be asked)" 'grep -qx "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAREAMyUly6LPnwWcQ+hRMtuQp18K8PAT0rba7kTKDwm" /home/root/settings/az-ssh-approved'
unplugall; mount /img.b /m; echo "$K2" >> /m/MODS/authorized_keys; umount /m; plug b sda 2-1; run
chk "4q after migration a new key is pending, not approved" 'grep -q AAAAIAah0aPK4hfd /run/az-ssh/pending && ! grep -q AAAAIAah0aPK4hfd /home/root/.ssh/authorized_keys'
unplugall

echo "== 5 both with MODS (USB1 enumerates as sdb); USB2 has mods.off + key"; reset
mk a 'mkdir MODS; echo one > MODS/azmod.so'; mk b 'mkdir MODS; echo two > MODS/azmod.so; touch MODS/mods.off; echo "ssh-ed25519 AAAAtest k" > MODS/authorized_keys'
plug b sda 2-1; plug a sdb 1-1; b2=$(h b); run; logs; unplugall
chk "5 staged from USB1" '[ "$(cat $P7/azmod.so)" = one ]'
chk "5 USB2 mods.off ignored (env set)" '[ -n "$(cat /home/root/settings/az-usbload/az-preload.env)" ]'
chk "5 USB2 logged ignored" 'grep -q "USB2 MODS ignored (USB1 present)" /tmp/az-usbload.log'
chk "5 USB2 key not used" '! grep -q "copied key" /tmp/az-sshgate.log'
chk "5 USB2 stick unchanged" '[ "$b2" = "$(h b)" ]'

echo "== 6 USB1 no MODS/, USB2 mods"; reset; mk a 'echo x > t.mp3'; mk b 'mkdir MODS; echo two > MODS/azmod.so'
plug a sda 1-1; plug b sdb 2-1; a1=$(h a); run; logs; unplugall
chk "6 staged from USB2" '[ "$(cat $P7/azmod.so)" = two ]'; chk "6 USB1 unchanged" '[ "$a1" = "$(h a)" ]'
# USB1 is mounted and has no MODS/: settled, so USB2 is picked at once instead of after
# the 25s wait (which would stretch every on-deck update)
chk "6 picked USB2 at once (USB1 settled)" 'grep -q "using USB2 (USB1 settled after 0s)" /tmp/az-usbload.log'

echo "== 6b USB1 present but NOT mounted yet, USB2 mods -> must still wait (control for 6)"; reset
mk a 'echo x > t.mp3'; mk b 'mkdir MODS; echo two > MODS/azmod.so'
plug a sda 1-1; umount /media/usb/sda1; plug b sdb 2-1; t0=$(cut -d. -f1 /proc/uptime); devflag; /usr/sbin/az-usbload; t1=$(cut -d. -f1 /proc/uptime); logs; unplugall
chk "6b staged from USB2" '[ "$(cat $P7/azmod.so)" = two ]'
chk "6b did NOT call USB1 settled" '! grep -q "USB1 settled" /tmp/az-usbload.log'
chk "6b waited the full pick (>=20s, took $((t1 - t0))s)" '[ $((t1 - t0)) -ge 20 ]'

echo "== 7 unknown port (hub 1-1.2) with MODS"; reset; mk a 'mkdir MODS; echo one > MODS/azmod.so'; plug a sda 1-1.2; b=$(h a); run; logs; unplugall
chk "7 ignored, no p7" '[ ! -e $P7 ]'; chk "7 stick unchanged" '[ "$b" = "$(h a)" ]'

# 8: a MODS/ folder alone is not a mods stick. The desktop app creates an empty MODS/ on
# every stick it prepares; that one in USB1 must not win the pick over the real set in USB2
# (the deck would boot stock). 8 and 8c are the pair, in the same round: the same USB1 layout
# with and without a switch the user set, so the predicate is shown to tell them apart.
echo "== 8 USB1 empty MODS/ (app-prepared music stick), USB2 mods + key -> USB2"; reset
mk a 'mkdir MODS STEMS "DRUM ROLLS"; echo x > t.mp3'; mk b 'mkdir MODS; echo two > MODS/azmod.so; touch MODS/ssh.enable; echo "ssh-ed25519 AAAAtest k" > MODS/authorized_keys'
plug a sda 1-1; plug b sdb 2-1; a8=$(h a); run; logs; unplugall
chk "8 staged from USB2" '[ "$(cat $P7/azmod.so 2>/dev/null)" = two ]'
chk "8 env preloads the set (not stock)" 'grep -q "azmod.so" /home/root/settings/az-usbload/az-preload.env 2>/dev/null'
chk "8 picked USB2 at once (USB1 settled, no mod set)" 'grep -q "USB1 has no mod set -> using USB2 (USB1 settled after 0s)" /tmp/az-usbload.log'
chk "8 sshgate read the USB2 key (hooks get the same pick)" 'grep -q "copied key from /media/usb/sdb1" /tmp/az-sshgate.log'
chk "8 USB1 stick unchanged" '[ "$a8" = "$(h a)" ]'

echo "== 8b USB1 MODS/ with community/ and deck/ but no core, USB2 mods -> USB2"; reset
mk a 'mkdir -p MODS/community-mods/x-mod MODS/deck; echo m > MODS/community-mods/x-mod/x-mod.so; touch MODS/mods.on'; mk b 'mkdir MODS; echo two > MODS/azmod.so'
plug a sda 1-1; plug b sdb 2-1; run; logs; unplugall
chk "8b staged from USB2" '[ "$(cat $P7/azmod.so 2>/dev/null)" = two ]'

echo "== 8c USB1 MODS/ with mods.off only (no .so), USB2 mods -> USB1 wins, stock"; reset
mk a 'mkdir MODS; touch MODS/mods.off'; mk b 'mkdir MODS; echo two > MODS/azmod.so'
plug a sda 1-1; plug b sdb 2-1; run; logs; unplugall
chk "8c picked USB1" 'grep -q "using USB1 stock mount /media/usb/sda1" /tmp/az-usbload.log'
chk "8c mods.off honored: nothing staged, env stock" '[ ! -e $P7/azmod.so ] && [ -z "$(cat $P7/az-preload.env 2>/dev/null)" ]'

echo "== 8d USB1 MODS/azmod-core.so (split core), USB2 mods -> USB1"; reset
mk a 'mkdir MODS; echo core > MODS/azmod-core.so'; mk b 'mkdir MODS; echo two > MODS/azmod.so'
plug a sda 1-1; plug b sdb 2-1; run; logs; unplugall
chk "8d staged the USB1 core" '[ "$(cat $P7/azmod-core.so 2>/dev/null)" = core ] && [ ! -e $P7/azmod.so ]'

echo "== 8e USB1 MODS/uninstall only, USB2 mods -> USB1 (the request is honored)"; reset
mk a 'mkdir MODS; touch MODS/uninstall'; mk b 'mkdir MODS; echo two > MODS/azmod.so'
plug a sda 1-1; plug b sdb 2-1; run; logs; unplugall
chk "8e picked USB1" 'grep -q "using USB1 stock mount /media/usb/sda1" /tmp/az-usbload.log'
chk "8e nothing staged from USB2" '[ ! -e $P7/azmod.so ]'
