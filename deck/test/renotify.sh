#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# Test suite for usb-renotify.sh: the port a stick is announced ON. Run by
# test/run.sh in its alpine container (`test/run.sh renotify`). It covers the stock port
# rule: 4-1 routes to udev_usb2, 3-1 to udev_usb1, and an unknown bus announces nothing.
#
# The mailboxes are plain files here, so each case asserts which file received
# the announce: a log line saying "announced" never proves which port got it.
set -u
export PATH=/stub:$PATH
cp /p/usr/sbin/az-usbpick /usr/sbin/
mkdir -p /home/root/scripts /media/usb; cp /p/home/root/scripts/usb-renotify.sh /home/root/scripts/
chmod +x /home/root/scripts/usb-renotify.sh
chk(){ if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }

# FIXTURE: the script reads the DEVICE out of the mount table (awk $1), so the
# mountpoint must really be /dev/sda: a bind mount reports the underlying device
# instead, and every announce is skipped. Hence a loop device, and every step reports
# its own failure by name: a missing fixture would make every "untouched" assertion
# pass while testing nothing.
apk add -q dosfstools >/dev/null 2>&1 || { echo "FAIL fixture: apk add dosfstools"; exit 1; }
dd if=/dev/zero of=/img bs=1M count=8 2>/dev/null || { echo "FAIL fixture: dd"; exit 1; }
mkfs.vfat /img >/dev/null 2>&1 || { echo "FAIL fixture: mkfs.vfat"; exit 1; }
lo=$(losetup -f) || { echo "FAIL fixture: losetup -f"; exit 1; }
# losetup -f names the first index the KERNEL has free, but the container's /dev
# only carries a few loop nodes, so the node it names often does not exist here.
[ -b "$lo" ] || mknod "$lo" b 7 "${lo#/dev/loop}" || { echo "FAIL fixture: mknod $lo"; exit 1; }
losetup "$lo" /img || { echo "FAIL fixture: losetup attach $lo"; exit 1; }
rm -f /dev/sda
IFS=: read -r _maj _min < /sys/block/"${lo#/dev/}"/dev; mknod /dev/sda b "$_maj" "$_min" || { echo "FAIL fixture: mknod (lo=$lo)"; exit 1; }
mkdir -p /media/usb/sda
mount -o ro /dev/sda /media/usb/sda || { echo "FAIL fixture: mount"; exit 1; }
mount | grep -q "^/dev/sda /media/usb/sda \|^/dev/sda on /media/usb/sda " \
  || { echo "FAIL fixture: mount table does not show /dev/sda on /media/usb/sda -- [$(mount | grep /media/usb/sda)]"; exit 1; }
udevadm info --query=path --name=/dev/sda >/dev/null 2>&1 || { echo "FAIL fixture: udevadm stub not on PATH"; exit 1; }

# /proc cannot be faked in a container, so the mailboxes are plain files here
# via the same override pattern az-bootlogo uses for its device.
export AZ_RENOTIFY_BOX1=/mbox/usb1 AZ_RENOTIFY_BOX2=/mbox/usb2 AZ_RENOTIFY_LOG=/tmp/az-renotify.log
mkdir -p /mbox
run(){ # $1 = bus id written to /tmp/ports
    echo "sda $1" > /tmp/ports
    : > "$AZ_RENOTIFY_BOX1"; : > "$AZ_RENOTIFY_BOX2"; rm -f "$AZ_RENOTIFY_LOG"
    sh /home/root/scripts/usb-renotify.sh
}
box1(){ cat "$AZ_RENOTIFY_BOX1" 2>/dev/null; }
box2(){ cat "$AZ_RENOTIFY_BOX2" 2>/dev/null; }

echo "== 1 bus 1-1 -> USB1 (and NOT USB2)"
run 1-1
chk "1 announced on usb1" 'box1 | grep -q "^mount /media/usb/sda "'
chk "1 usb2 untouched" '[ -z "$(box2)" ]'

echo "== 2 bus 2-1 -> USB2 (and NOT USB1) -- the control for 1"
run 2-1
chk "2 announced on usb2" 'box2 | grep -q "^mount /media/usb/sda "'
chk "2 usb1 untouched" '[ -z "$(box1)" ]'

echo "== 3 bus 3-1 -> USB1 (stock 90-usb-caution.rules maps it there)"
run 3-1
chk "3 announced on usb1" 'box1 | grep -q "^mount /media/usb/sda "'
chk "3 usb2 untouched" '[ -z "$(box2)" ]'

echo "== 4 bus 4-1 -> USB2. THE REGRESSION: the old code sent this to USB1"
run 4-1
chk "4 announced on usb2" 'box2 | grep -q "^mount /media/usb/sda "'
chk "4 usb1 NOT touched (the bug)" '[ -z "$(box1)" ]'

echo "== 5 unknown bus (behind a hub) -> announce NOTHING, do not guess"
run 1-1.4
chk "5 usb1 untouched" '[ -z "$(box1)" ]'
chk "5 usb2 untouched" '[ -z "$(box2)" ]'
chk "5 logged the skip and the reason" 'grep -q "UNKNOWN port" /tmp/az-renotify.log'
chk "5 logged done: 0 announced" 'grep -q "done: 0 announced" /tmp/az-renotify.log'

echo "== 6 the announce payload keeps the stock format"
run 1-1
chk "6 mount <mp> <fs> protect:<ro>" 'box1 | grep -qE "^mount /media/usb/sda [a-z0-9]+ protect:[01]$"'
chk "6 logged which port it chose" 'grep -q "> USB1 " $AZ_RENOTIFY_LOG'

losetup -d "$lo" 2>/dev/null   # do not leak the loop device to the next suite

echo "== 7 the filesystem name is blkid's, the one stock would have sent"
# device-mount.sh announces blkid's ID_FS_TYPE. Parsing `mount` instead agrees
# for vfat and DISAGREES for exFAT (fuse -> "fuseblk"), which would hand EP147 a
# filesystem string stock never sends. Assert the source, not just the shape.
run 1-1
BLKID_FS=$(blkid /dev/sda 2>/dev/null | sed -n 's/.*[ :]TYPE="\([^"]*\)".*/\1/p' | head -1)
MOUNT_FS=$(mount | grep " /media/usb/sda " | sed -E 's/.*type ([^ ]+).*/\1/')
ANNOUNCED=$(box1 | awk '{print $3}')
echo "    blkid=[$BLKID_FS] mount=[$MOUNT_FS] announced=[$ANNOUNCED]"
chk "7 announced == blkid TYPE" '[ "$ANNOUNCED" = "$BLKID_FS" ]'
chk "7 blkid was actually consulted (non-empty)" '[ -n "$BLKID_FS" ]'
