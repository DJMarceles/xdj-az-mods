#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# usb-renotify.sh - re-announce already-mounted USB media to EP147 after an EP147
# (re)start. EP147 learns of media only from the /proc/udev_usb{1,2} mailbox that
# device-mount.sh writes at plug time; on an EP147 restart the USB stays mounted
# but EP147 drops it from the Source list. Run with `sh` by az-usbload after its own
# EP147 restart and by `az-usbload --post-start` after any other restart (a stick's
# deck/lib copy is preferred over this one).
#
# Port identity comes from az-usbpick's port_of(), the one copy of the stock rule.
# Matching only 1-1 and 2-1 would miss 3-1 -> usb1 and 4-1 -> usb2 (see
# 90-usb-caution.rules), and a fallback to "whichever mailbox exists first" would tell
# EP147 a port-2 stick is on port 1, so the deck could find no library on any stick.
#
# Unknown is not port 1. As az-usbpick puts it, anything else (a stick behind a
# hub) is unknown and ignored, with no fallback. Announcing a guess is worse than
# announcing nothing: the media is already mounted and EP147 learned it at
# plug time, so the cost of skipping is "invisible until replug after a restart",
# while the cost of guessing is a deck that reads no library at all.
#
# Every decision is logged, so a user report can be told apart from a healthy boot.
# AZ_RENOTIFY_BOX1/2 override the mailboxes for testing: /proc cannot be faked in a
# container.
BOX1=${AZ_RENOTIFY_BOX1:-/proc/udev_usb1}
BOX2=${AZ_RENOTIFY_BOX2:-/proc/udev_usb2}
LOG=${AZ_RENOTIFY_LOG:-/tmp/az-renotify.log}
azlog_cap() { return 0; }   # replaced below by deck/lib/az-logcap.sh, the shared cap
for _azl_f in "${0%/*}/az-logcap.sh" "${0%/*}/../lib/az-logcap.sh" "${0%/*}/lib/az-logcap.sh" \
    /tmp/az-deck/lib/az-logcap.sh; do
    # shellcheck source=../../../../stick/deck/lib/az-logcap.sh
    [ -f "$_azl_f" ] && { . "$_azl_f"; break; }
done
log(){ azlog_cap "$LOG"; echo "[renotify $(date '+%H:%M:%S' 2>/dev/null || true)] $*" >> "$LOG" 2>/dev/null; }

[ -f /usr/sbin/az-usbpick ] || { log "az-usbpick absent -> cannot resolve ports, announcing nothing"; exit 0; }
# shellcheck source=../../../usr/sbin/az-usbpick
. /usr/sbin/az-usbpick

n=0
for mp in /media/usb/sd*; do
    [ -d "$mp" ] || continue
    line=$(mount | grep " $mp ")
    [ -n "$line" ] || continue
    dev=$(echo "$line" | awk '{print $1}')
    # The filesystem name must be the one stock would have sent. device-mount.sh
    # announces blkid's ID_FS_TYPE ("vfat", "exfat", "hfsplus"). `mount` output is
    # not equivalent: exFAT is mounted through fuse and reports "fuseblk", a string
    # EP147 never receives from stock. Ask blkid, like stock does.
    # Parse TYPE="..." from blkid's default line rather than trusting -o value -s
    # TYPE: the deck's util-linux blkid honors those flags, busybox's (the test
    # container) ignores them and prints the whole line, which read as fs="/dev/sda:".
    fs=$(blkid "$dev" 2>/dev/null | sed -n 's/.*[ :]TYPE="\([^"]*\)".*/\1/p' | head -1)
    [ -n "$fs" ] || { log "SKIP $mp ($dev): blkid cannot name the filesystem; not announcing a guess"; continue; }
    base=$(echo "$dev" | sed -E 's#/dev/(sd[a-h]).*#\1#')
    ro=$(cat "/sys/block/$base/ro" 2>/dev/null || echo 0)
    port=$(port_of "$dev")
    case "$port" in
        1) box=$BOX1 ;;
        2) box=$BOX2 ;;
        *) log "SKIP $mp ($dev): UNKNOWN port (hub, or a bus the stock rule does not map); not guessing a mailbox"
           continue ;;
    esac
    [ -e "$box" ] || { log "SKIP $mp ($dev): port $port but $box absent"; continue; }
    echo -n "mount $mp $fs protect:$ro" > "$box"
    n=$((n + 1))
    log "announced $mp ($dev, $fs, protect:$ro) -> USB$port $box"
done
log "done: $n announced"
