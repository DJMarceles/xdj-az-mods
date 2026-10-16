#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# gadget-renotify.sh - re-announce the USB gadget (the PC link that puts
# SOFTWARE CONTROL in the SOURCE list) to a freshly started EP147. Run with `sh`
# by az-usbload after its own EP147 restart and by `az-usbload --post-start` after
# any other restart. Never on a boot without a restart: EP147's first start reads
# the gadget's own "connect" from the mailbox, so there is nothing to repair.
#
# Why
# EP147 learns about devices by reading /proc/udev_* mailboxes. Storage already
# has this covered: after the loader restarts EP147, usb-renotify.sh re-writes
# `mount <mp> <fs> protect:<ro>` into /proc/udev_usb1|2 so the sticks come back.
# Without an equivalent for the PC link, an EP147 restart loses SOFTWARE CONTROL
# until the USB-C cable is replugged.
#
# Wire format, as captured from EP147's own reads of the mailbox:
#     [9.815]  /proc/udev_usbg1  r=7  "connect"       <- announced at boot
#     [25.818] /proc/udev_usbg1  r=10 "disconnect"    <- a real unplug
#     [35.821] /proc/udev_usbg1  r=7  "connect"       <- a real replug
# Exactly those two words, no payload, no trailing newline (r=7 and r=10 are the
# exact string lengths). EP147's own parser knows four verbs: "connect",
# "disconnect", "mount", "umount".
#
# Safety
#  - Refuses unless the UDC reports a host attached, so it can never
#    announce a phantom SOFTWARE CONTROL for a PC that is not there.
#  - Waits for EP147 to be genuinely up (HuiProcessor present), the same signal
#    az-usbload uses before re-announcing USB. Announcing into a half-started
#    EP147 is unsafe.
#  - Does nothing at all if the mailbox is absent (older firmware).
set -u
BOX=${AZ_GADGET_BOX:-/proc/udev_usbg1}
LOG=${AZ_GADGET_LOG:-/tmp/az-renotify.log}
GAP=${AZ_GADGET_GAP:-2}
WAIT=${AZ_GADGET_WAIT:-60}

azlog_cap() { return 0; }   # replaced below by deck/lib/az-logcap.sh, the shared cap
for _azl_f in "${0%/*}/az-logcap.sh" "${0%/*}/../lib/az-logcap.sh" "${0%/*}/lib/az-logcap.sh" \
    /tmp/az-deck/lib/az-logcap.sh; do
    # shellcheck source=az-logcap.sh
    [ -f "$_azl_f" ] && { . "$_azl_f"; break; }
done
log(){ azlog_cap "$LOG"; echo "[gadget-renotify $(date '+%H:%M:%S' 2>/dev/null || true)] $*" >> "$LOG" 2>/dev/null; }

[ -e "$BOX" ] || { log "no $BOX (older firmware?) -> nothing to do"; exit 0; }

# A host must actually be attached. "configured" is the dwc3 UDC's own word for
# "a host has enumerated us"; anything else means there is no PC to announce.
state=""
for f in /sys/class/udc/*/state; do
    [ -e "$f" ] || continue
    state=$(cat "$f" 2>/dev/null)
    break
done
if [ "$state" != "configured" ]; then
    log "UDC state is [${state:-unknown}], not 'configured' -> no PC attached, announcing nothing"
    exit 0
fi

# Wait for EP147 to be up, not merely spawned.
i=0; P=""
while [ "$i" -lt "$WAIT" ]; do
    P=$(pidof EP147 2>/dev/null | awk '{print $1}')
    if [ -n "$P" ] && grep -qx HuiProcessor /proc/"$P"/task/*/comm 2>/dev/null; then break; fi
    P=""; sleep 1; i=$((i + 1))
done
if [ -z "$P" ]; then
    log "EP147 not ready after ${i}s (no HuiProcessor) -> not announcing"
    exit 0
fi

log "EP147 pid $P ready after ${i}s, UDC configured -> disconnect/connect on $BOX"
printf 'disconnect' > "$BOX" 2>>"$LOG" || { log "write 'disconnect' FAILED"; exit 0; }
sleep "$GAP"
printf 'connect' > "$BOX" 2>>"$LOG" || { log "write 'connect' FAILED; SOFTWARE CONTROL may now be missing until a replug"; exit 1; }
log "done: software control re-announced"
exit 0
