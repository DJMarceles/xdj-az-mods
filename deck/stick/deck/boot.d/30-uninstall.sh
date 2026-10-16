#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# az-uninstall (release payload): the way back to stock. Removes every file
# AZ-MODS ever wrote to the SETTING partition (p7), so that after the DJ
# flashes stock firmware the deck carries no trace of the mods at all. A
# MODS/deck/boot.d hook: az-usbload runs it with `sh` every boot (and once more
# after its EP147 restart on an uninstall boot); it acts only on MODS/uninstall.
#
# Why: a stock .UPD replaces the kernel and the initramfs rootfs, and
# the rootfs is a tmpfs, so everything installed there disappears by itself.
# p7 does not: it is the settings partition, it survives flashing (which is why the
# deck keeps its WiFi password across an update), and the loader mirrors the .so
# files and art into it on every boot. Left alone, a "reverted" deck still holds
# /home/root/settings/az-usbload/azmod.so. This script makes revert mean revert.
#
# Trigger: the chosen stick's MODS/uninstall marker (az-usbpick's port rule, via the
# stock mount; it never mounts anything itself). No marker, no run.
#
# Order: after az-bootlogo (which owns p3 and its own backup) and after
# az-usbload (which must see the marker as an off-switch, so it stages nothing
# this boot and there is no cache to race). Only p7 is ever touched.
#
# The safety rule, and it is the whole design: deletion is by an explicit list (plus
# az-prefixed globs, below), and every entry is additionally required to start with
# "az". The deck's own files (SETTINGS.DAT, wifi.json, bluetooth_snk.json,
# cabinet.img, cabinet/) do not start with "az" and are never named here, so no input
# can make this script remove one. A glob sweep of settings/ would be one typo away
# from wiping the deck's own configuration.
#
# The boot logo is not this hook's to judge: az-bootlogo owns p3. Its backup is removed
# only when this boot's log says p3 is already stock again ("RESTORED:" or
# "SKIPPED: ... already the stock original"). Absent either line the
# backup is kept and reported, because that backup is the deck's only copy of its own
# logo and a wrong deletion here is unrecoverable.
#
# Result: MODS/.uninstall-result is written back to the stick, because the DJ has
# no shell; the app reads it and only then says it is safe to flash stock.
set -u
# Hook contract (az-usbload boot.d, firmware >= 1.32): this script rides the
# stick in MODS/deck/boot.d/ and az-usbload runs it after it has picked the
# stick (az-usbpick's port rule) and staged the mod set, with the pick in the
# environment: AZ_MNT (the stock mount, "" = no mods stick this boot), AZ_PORT
# (1|2), AZ_MODS ($AZ_MNT/MODS), AZ_MODS_OFF (1 when mods = off),
# AZ_STAGE (the p7 cache), AZ_DECK (the /tmp copy of MODS/deck this run is
# executing from). It never picks or mounts anything itself.

P7=/home/root/settings
LOG=/tmp/az-uninstall.log
P7LOG="$P7/az-uninstall.log"
BOOTLOGO_LOG=/tmp/az-bootlogo.log

: > "$LOG" 2>/dev/null
azlog_cap() { return 0; }   # replaced below by deck/lib/az-logcap.sh, the shared cap
for _azl_f in "${0%/*}/az-logcap.sh" "${0%/*}/../lib/az-logcap.sh" "${0%/*}/lib/az-logcap.sh" \
    /tmp/az-deck/lib/az-logcap.sh; do
    # shellcheck source=../lib/az-logcap.sh
    [ -f "$_azl_f" ] && { . "$_azl_f"; break; }
done
log(){ azlog_cap "$LOG"; echo "[uninstall $(date '+%H:%M:%S' 2>/dev/null || true)] $*" >> "$LOG" 2>/dev/null; }

# Everything AZ-MODS creates on p7. Explicit, not a glob. Keep in sync with any
# new path a mod writes under settings/: the test asserts this list covers the
# names azmod.so and azstem.so actually reference.
REMOVE="az-usbload
az-dropbear
az-ssh-approved
azmod
azmod.cfg
azmod.shot
azmod-shots
azmod-xpad
azmod-fxdiag
azmod-lanedbg
azmod-ledraw
azmod-paintbt
azmod-visdbg
azmod-pngpatch
azmod-srcrow.log
azmod-beatfx.log
azmod-browse.log
azmod-tileclass.log
azmod-tilefix.log
azmod-tileidx.log
azmod-flags-off
azstem.log
az-bootlog.log"

log "start"

# ---- the marker: MODS/uninstall on the chosen stick ---------------------------
MNT=${AZ_MNT:-}; PORT=${AZ_PORT:-}; MODS=""
[ -n "$MNT" ] || { log "no mods stick (AZ_MNT empty) -> nothing to do"; exit 0; }
log "using USB$PORT stock mount $MNT (from az-usbload)"
MODS="$MNT/MODS"

[ -e "$MODS/uninstall" ] || { log "no MODS/uninstall marker -> nothing to do"; exit 0; }
log "MODS/uninstall present -> purging AZ-MODS files from p7"

[ -d "$P7" ] || { log "no $P7 -> nothing to do"; exit 0; }

# ---- purge ------------------------------------------------------------------
REMOVED=""; FAILED=""
for n in $REMOVE; do
    # the invariant, enforced per entry rather than assumed of the list
    case "$n" in az*) ;; *) log "REFUSED: '$n' does not start with az; not deleting"; continue ;; esac
    [ -e "$P7/$n" ] || continue
    if rm -rf "${P7:?}/${n:?}" 2>>"$LOG" && [ ! -e "$P7/$n" ]; then
        REMOVED="$REMOVED $n"; log "removed $n"
    else
        FAILED="$FAILED $n"; log "FAILED to remove $n"
    fi
done

# the per-deck flag/state files: azmod-*.on/.off and azstem.* (avail, stems,
# curtrack, enable, and every diagnostic toggle). Same az-prefix invariant.
# Every name in the AZ-MODS namespaces, beyond the ones listed above: backups and
# diagnostics such as azmod-pngpatch.old-<date>, azmod.cfg.bak-<date> and az-restart-*
# are not enumerated. An explicit list is safe but incomplete, and
# "no trace" is not a promise an incomplete list can keep. These globs cover the
# namespaces AZ-MODS writes; the az-prefix invariant below still applies to every
# hit, and KEEP names the deck's own files so no glob can ever reach one.
KEEP="SETTINGS.DAT wifi.json bluetooth_snk.json cabinet.img cabinet"
for p in "$P7"/azmod* "$P7"/azstem* \
         "$P7"/az-usbload* "$P7"/az-restart* "$P7"/az-bootlog.log; do
    [ -e "$p" ] || continue
    n=${p##*/}
    case "$n" in az*) ;; *) log "REFUSED: '$n' does not start with az"; continue ;; esac
    skip=0
    for k in $KEEP; do [ "$n" = "$k" ] && skip=1; done
    [ "$skip" = 1 ] && { log "KEEP: '$n' is the deck's own"; continue; }
    [ "$n" = "az-uninstall.log" ] && continue
    if rm -rf "$p" 2>>"$LOG" && [ ! -e "$p" ]; then
        REMOVED="$REMOVED $n"; log "removed $n"
    else
        FAILED="$FAILED $n"; log "FAILED to remove $n"
    fi
done

# ---- the boot logo backup: only once p3 is provably stock again -------------
LOGO_NOTE=""

# Wait for the bootlogo verdict. Boot.d hooks run sequentially, so 20-bootlogo.sh has
# finished before this starts and its log is complete; the short wait below only
# guards against a log that is still being flushed.
bootlogo_verdict(){          # stock | notstock | unknown
    i=0
    while [ "$i" -lt 15 ]; do
        if [ -f "$BOOTLOGO_LOG" ]; then
            grep -q "RESTORED:" "$BOOTLOGO_LOG" 2>/dev/null && { echo stock; return; }
            grep -q "already the stock original" "$BOOTLOGO_LOG" 2>/dev/null && { echo stock; return; }
            grep -qE "REFUSED:|FAILED:|no backup, nothing to restore" "$BOOTLOGO_LOG" 2>/dev/null \
                && { echo notstock; return; }
        fi
        i=$((i + 1)); sleep 1
    done
    echo unknown
}

if [ -e "$P7/az-bootlogo" ] || [ -e "$P7/az-bootlogo.log" ]; then
    V=$(bootlogo_verdict)
    log "bootlogo verdict: $V"
    # Nothing to protect: no backup dir means the log is a stray, not evidence.
    if [ "$V" != stock ] && [ ! -d "$P7/az-bootlogo" ]; then
        rm -f "$P7/az-bootlogo.log" 2>>"$LOG" && { REMOVED="$REMOVED az-bootlogo.log"; log "removed az-bootlogo.log (no backup to protect)"; }
    fi
    if [ "$V" = stock ]; then
        rm -rf "$P7/az-bootlogo" "$P7/az-bootlogo.log" 2>>"$LOG"
        if [ ! -e "$P7/az-bootlogo" ] && [ ! -e "$P7/az-bootlogo.log" ]; then
            REMOVED="$REMOVED az-bootlogo az-bootlogo.log"; log "removed az-bootlogo (p3 is stock again)"
        else
            FAILED="$FAILED az-bootlogo"; log "FAILED to remove az-bootlogo"
        fi
    elif [ -d "$P7/az-bootlogo" ]; then
        LOGO_NOTE="the stock boot logo has NOT been restored yet, so its backup was KEPT"
        log "KEEPING az-bootlogo: verdict=$V, not deleting the deck's only copy of its own logo"
    fi
fi

sync 2>/dev/null

# ---- what is still there ----------------------------------------------------
LEFT=""
for p in "$P7"/az*; do
    [ -e "$p" ] || continue
    n=${p##*/}
    [ "$n" = "az-uninstall.log" ] && continue
    LEFT="$LEFT $n"
done

# the log says so in words; the breadcrumb's removed= stays empty (its contract: names only)
log "removed:${REMOVED:- (nothing, already clean)}"
[ -n "$LEFT" ] && log "still present:$LEFT"
[ -n "$FAILED" ] && log "FAILED:$FAILED"

# ---- tell the stick, because the DJ has no shell ----------------------------
if [ -z "$LEFT" ] && [ -z "$FAILED" ]; then
    VERDICT="CLEAN"
    MSG="No AZ-MODS files remain on the deck. You can now flash the stock firmware."
else
    VERDICT="INCOMPLETE"
    MSG="Still on the deck:$LEFT.${LOGO_NOTE:+ }${LOGO_NOTE}"
fi
log "$VERDICT"

# keep a copy on p7 only while it is useful; a CLEAN run leaves no log either.
if [ "$VERDICT" = CLEAN ]; then
    rm -f "$P7LOG" 2>/dev/null
else
    { cp -f "$LOG" "$P7LOG.new" && mv -f "$P7LOG.new" "$P7LOG"; } 2>/dev/null || rm -f "$P7LOG.new" 2>/dev/null
fi

# write the breadcrumb back through the stock mount (remount rw only if it was ro)
if [ -n "$MODS" ] && [ -d "$MODS" ]; then
    ro=$(awk -v m="$MNT" '$2==m{print ($4 ~ /(^|,)ro(,|$)/)?1:0; exit}' /proc/mounts 2>/dev/null)
    [ "$ro" = 1 ] && mount -o remount,rw "$MNT" 2>/dev/null
    # Format: key=value, one per line, LF, ASCII. A stable contract: the app
    # parses this and it is the only thing the DJ can see without a shell.
    # Keys are always all present, in this order; a value may be empty.
    #   result    CLEAN | INCOMPLETE       (CLEAN = safe to flash stock)
    #   removed   space-separated names removed this run ("" = nothing to do)
    #   remaining space-separated AZ-MODS names still on p7 ("" when CLEAN)
    #   failed    space-separated names that could not be removed
    #   message   one human sentence, may contain spaces; always last
    {   printf 'result=%s\n'    "$VERDICT"
        printf 'removed=%s\n'   "${REMOVED# }"
        printf 'remaining=%s\n' "${LEFT# }"
        printf 'failed=%s\n'    "${FAILED# }"
        printf 'message=%s\n'   "$MSG"
    } > "$MODS/.uninstall-result.new" 2>/dev/null \
        && mv -f "$MODS/.uninstall-result.new" "$MODS/.uninstall-result" 2>/dev/null \
        || rm -f "$MODS/.uninstall-result.new" 2>/dev/null
    sync 2>/dev/null
    [ "$ro" = 1 ] && mount -o remount,ro "$MNT" 2>/dev/null
fi
exit 0
