#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# 05-keep-testmode.sh - keep stock's /tmp cleaner from deleting /tmp/testmode.
# A MODS/deck/boot.d hook: az-usbload runs it with `sh` on every boot (the rootfs
# is RAM, so the tmpfiles exclusion it writes has to be rewritten each boot).
#
# Why: a warm EP147 restart can leave a black screen.
#   1. pre-setting.service runs subucom_read once at boot, which writes
#      /tmp/testmode ("off" in normal use). Nothing ever rewrites it.
#   2. apl_start.sh (EP147.service ExecStart) launches ./EP147 only if that file
#      exists; otherwise it prints "can't read /tmp/testmode" and exits 0.
#   3. The deck boots with its clock set months in the past and syncs to the real date
#      later, so the file's mtime/atime/ctime are months old once the clock jumps.
#   4. Stock tmp.conf has `q /tmp 1777 root root 10d`, and
#      systemd-tmpfiles-clean.timer runs it 15 min after boot: it deletes the file.
#   5. From then on any EP147 restart (Mod Menu RELOAD, the loader, ssh) runs
#      apl_start.sh, which exits 0 without starting EP147. Exit 0 is a success, so
#      Restart=on-failure never fires: black screen, no WiFi (wpa_supplicant is
#      EP147's child), dead power button, power-off only. A restart in the first
#      15 min works (it refreshes the file's atime), so the hang is intermittent.
# The emulator is not affected: its own /etc/tmpfiles.d/az-testmode.conf ("f" line)
# makes tmpfiles skip the path during cleanup.
#
# The hook uses tmpfiles' own exclusion, `x /tmp/testmode`. The rootfs is RAM, so this is
# written every boot. It keeps the file stock wrote, value untouched; it creates
# nothing and changes no other cleanup rule.
set -u
LOG=${AZ_LOG:-/tmp/az-usbload.log}
CONF=${AZ_TESTMODE_CONF:-/etc/tmpfiles.d/az-keep-testmode.conf}
azlog_cap() { return 0; }   # replaced below by deck/lib/az-logcap.sh, the shared cap
for _azl_f in "${0%/*}/az-logcap.sh" "${0%/*}/../lib/az-logcap.sh" "${0%/*}/lib/az-logcap.sh" \
    /tmp/az-deck/lib/az-logcap.sh; do
    # shellcheck source=../lib/az-logcap.sh
    [ -f "$_azl_f" ] && { . "$_azl_f"; break; }
done
log(){ azlog_cap "$LOG"; echo "[testmode-hook $(date '+%H:%M:%S' 2>/dev/null || true)] $*" >> "$LOG" 2>/dev/null; }

mkdir -p "${CONF%/*}" 2>/dev/null
if printf '%s\n' '# az-mods: apl_start.sh needs /tmp/testmode on every EP147 start' 'x /tmp/testmode' > "$CONF.new" 2>/dev/null \
   && mv -f "$CONF.new" "$CONF" 2>/dev/null; then
    log "wrote $CONF (x /tmp/testmode)"
else
    rm -f "$CONF.new" 2>/dev/null
    log "FAILED writing $CONF"
fi
# Log the file's state, so a later hang report can be read against it.
if [ -f /tmp/testmode ]; then log "/tmp/testmode present: [$(cat /tmp/testmode 2>/dev/null)]"
else log "/tmp/testmode is ALREADY MISSING; a warm EP147 restart this boot would not start EP147"; fi
exit 0
