#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# az-bootlogo (release payload): restore only. Puts the deck's own stock boot logo
# back when asked (MODS/logo.restore or MODS/uninstall).
# A MODS/deck/boot.d hook: az-usbload runs it with `sh` on every boot, after EP147
# is up (see the hook contract below).
#
# Nothing installs a logo, so a deck taken back to stock has nothing left over. This
# exists for decks that got the AZ MODS logo from the v1.0/1.31 firmware, which backed
# up the stock originals on p7. A one-shot ordered
# After=EP147.service (never before it): the resource partition is the SoC boot
# logo, read by U-Boot/kernel long before this runs, so a write after EP147 is up is
# safe and only takes effect on the next boot.
#
# Rockchip RSCE resource partition (/dev/mmcblk1p3): a 512-byte header (magic
# 'RSCE' at 0, entry count LE-u32 at 12) then 512-byte entries at 0x200+512*i
# (name at +4, entry SHA-1 at +224, data offset in 512-blocks LE-u32 at +260,
# byte size LE-u32 at +264). The hook replaces the `logo.bmp` and `logo_kernel.bmp`
# entries in place (identical size; the DTB and every other entry byte-unchanged)
# and recompute their entry SHA-1s.
#
# Backup: the v1.0/1.31 firmware copied the still-stock logo.bmp /
# logo_kernel.bmp entry bytes plus their entry SHA-1s to
# /home/root/settings/az-bootlogo/orig/{logo.bmp,logo_kernel.bmp,meta} before
# it patched. That backup is the deck's only copy of its own logo: this hook only
# reads it.
#
# Restore: if the chosen stick's MODS/ carries a `logo.restore` marker
# (az-usbpick's port rule, via the stock mount, read-only; the hook never mounts),
# the backed-up originals go back into p3 in place (SHA-1 fixup, fsync, read-back
# verification) and RESTORED is logged. The marker is honored even when mods = off is
# present (this hook owns no mods gate): restoring the logo is part of going back to
# stock. No backup -> "no backup, nothing to restore".
#
# MODS/uninstall (the full back-to-stock marker, see az-uninstall) implies the same
# restore. No marker (or no stick) -> SKIPPED, p3 untouched. The old opt-in marker
# MODS/logo.on is ignored.
#
# Safe by default: it does nothing unless the partition parses as an XDJ-AZ RSCE
# image and the backup verifies against its own recorded SHA-1s, and it is
# idempotent (a second run finds the stock logo already there and skips). Every
# decision (SKIPPED / RESTORED / REFUSED / FAILED) is logged, with a
# capped copy on p7 that survives the /tmp wipe. AZ_BOOTLOGO_DEV
# overrides the device path for testing against an image file.
set -u
# Hook contract (az-usbload boot.d, firmware >= 1.32): this script rides the
# stick in MODS/deck/boot.d/ and az-usbload runs it after it has picked the
# stick (az-usbpick's port rule) and staged the mod set, with the pick in the
# environment: AZ_MNT (the stock mount, "" = no mods stick this boot), AZ_PORT
# (1|2), AZ_MODS ($AZ_MNT/MODS), AZ_MODS_OFF (1 when mods = off),
# AZ_STAGE (the p7 cache), AZ_DECK (the /tmp copy of MODS/deck this run is
# executing from). It never picks or mounts anything itself.

P3=${AZ_BOOTLOGO_DEV:-/dev/mmcblk1p3}
LOG=/tmp/az-bootlogo.log
P7LOG=/home/root/settings/az-bootlogo.log
ORIG=/home/root/settings/az-bootlogo/orig   # the stock-original backup (p7)
W=/tmp/az-bootlogo.p3                   # private working copy (never the stick)

: > "$LOG" 2>/dev/null
azlog_cap() { return 0; }   # replaced below by deck/lib/az-logcap.sh, the shared cap
for _azl_f in "${0%/*}/az-logcap.sh" "${0%/*}/../lib/az-logcap.sh" "${0%/*}/lib/az-logcap.sh" \
    /tmp/az-deck/lib/az-logcap.sh; do
    # shellcheck source=../lib/az-logcap.sh
    [ -f "$_azl_f" ] && { . "$_azl_f"; break; }
done
log(){ azlog_cap "$LOG"; echo "[bootlogo $(date '+%H:%M:%S' 2>/dev/null || true)] $*" >> "$LOG" 2>/dev/null; }
# capped p7 log: keep the last 40 lines so it can never fill p7 (SETTINGS.DAT lives there).
p7log(){
    [ -d /home/root/settings ] || return 0
    { [ -f "$P7LOG" ] && tail -n 40 "$P7LOG" 2>/dev/null
      echo "[$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null || true)] $*"
    } > "$P7LOG.tmp" 2>/dev/null && mv -f "$P7LOG.tmp" "$P7LOG" 2>/dev/null
}
decide(){ log "$1"; p7log "$1"; }

# Clear the marker once the job is done. Otherwise MODS/logo.restore would re-run the
# restore on every boot until the DJ removes it. Only ever called after p3 is
# provably stock (RESTORED, or already stock), never after REFUSED or a missing
# backup, because clearing it then would drop the request without doing the work.
# Same remount-rw-only-if-it-was-ro dance az-uninstall uses; a failure here is
# logged and ignored, since the logo is already correct either way.
clear_marker(){
    [ -n "${MNT:-}" ] && [ -f "$MNT/MODS/logo.restore" ] || return 0
    ro=$(awk -v m="$MNT" '$2==m{print ($4 ~ /(^|,)ro(,|$)/)?1:0; exit}' /proc/mounts 2>/dev/null)
    [ "$ro" = 1 ] && { mount -o remount,rw "$MNT" 2>/dev/null || { log "marker: cannot remount rw, left in place"; return 0; }; }
    if rm -f "$MNT/MODS/logo.restore" 2>/dev/null; then log "marker: MODS/logo.restore cleared (restore done)"
    else log "marker: could not clear MODS/logo.restore"; fi
    sync 2>/dev/null
    [ "$ro" = 1 ] && mount -o remount,ro "$MNT" 2>/dev/null
    return 0
}
bail(){ decide "$1"; rm -f "$W" 2>/dev/null; exit 0; }   # always exit 0: a boot unit must never fail the boot

# LE-u32 at byte offset $2 of file $1, printed as decimal. busybox ash supports
# ${var:off:len}, so the little-endian hex is byte-swapped and printf converts it.
le32(){
    h=$(xxd -s "$2" -l 4 -p "$1" 2>/dev/null | tr -d ' \t\r\n')   # busybox xxd -p pads with spaces
    [ ${#h} -eq 8 ] || return 1
    printf '%d' "0x${h:6:2}${h:4:2}${h:2:2}${h:0:2}"
}

log "start dev=$P3 (restore-only)"

[ -e "$P3" ] || bail "REFUSED: $P3 absent"

# Parse from a private read-only copy (4 MiB in /tmp). Nothing is ever written
# back from it: the restore writes only the changed regions, straight to $P3.
rm -f "$W" 2>/dev/null
dd if="$P3" of="$W" bs=1M 2>/dev/null || bail "REFUSED: cannot read $P3"

magic=$(xxd -s 0 -l 4 -p "$W" 2>/dev/null | tr -d ' \t\r\n')
[ "$magic" = "52534345" ] || bail "REFUSED: not an RSCE image (magic=$magic); not an XDJ-AZ resource partition"

n=$(le32 "$W" 12) || bail "REFUSED: cannot read RSCE entry count"
{ [ "$n" -ge 1 ] && [ "$n" -le 64 ]; } 2>/dev/null || bail "REFUSED: implausible RSCE entry count ($n)"

lb_e=""; lb_off=0; lb_sz=0; lb_stored=""
lk_e=""; lk_off=0; lk_sz=0; lk_stored=""
dtb_e=""; dtb_off=0; dtb_sz=0; dtb_before=""
i=0
while [ "$i" -lt "$n" ]; do
    e=$((512 + 512 * i))
    nm=$(dd if="$W" bs=1 skip=$((e + 4)) count=16 2>/dev/null | tr -d '\000')
    o512=$(le32 "$W" $((e + 260))) || bail "REFUSED: entry $i offset unreadable"
    sz=$(le32 "$W" $((e + 264)))   || bail "REFUSED: entry $i size unreadable"
    off=$((o512 * 512))
    case "$nm" in
        logo.bmp)        lb_e=$e; lb_off=$off; lb_sz=$sz; lb_stored=$(xxd -s $((e + 224)) -l 20 -p "$W" 2>/dev/null | tr -d ' \t\r\n') ;;
        logo_kernel.bmp) lk_e=$e; lk_off=$off; lk_sz=$sz; lk_stored=$(xxd -s $((e + 224)) -l 20 -p "$W" 2>/dev/null | tr -d ' \t\r\n') ;;
        *)               if [ -z "$dtb_e" ]; then     # first non-logo entry = the untouched-proof witness
                             dtb_e=$e; dtb_off=$off; dtb_sz=$sz
                             dtb_before=$(dd if="$W" bs=512 skip="$o512" count=$(((sz + 511) / 512)) 2>/dev/null | head -c "$sz" | sha1sum | cut -d' ' -f1)
                         fi ;;
    esac
    i=$((i + 1))
done

{ [ -n "$lb_e" ] && [ -n "$lk_e" ]; } || bail "REFUSED: logo.bmp/logo_kernel.bmp not both present; not an XDJ-AZ logo layout"

# ---- the write: only the bytes that change, in place, journaled ------------
# p3 also holds the DTB the kernel boots with. Rewriting the whole partition would put
# the DTB inside every write, and a power cut mid-write could leave the deck unable to
# boot. So exactly two kinds of region are written, nothing else: each logo's data
# bytes, then its 20-byte entry SHA-1 field. The DTB and the table are never inside a
# write.
#
# Order, per logo: data (fsync, read back), then its SHA-1 field (fsync, read
# back). The field is what the already-stock check reads first, so a run cut
# anywhere before the field lands still reads "not stock" and the next boot
# redoes it.
#
# Undo + journal ($UNDO on p7, ~140 KB): before the first byte is written, the
# exact bytes about to be replaced (both data regions and both SHA-1 fields)
# are saved there with a journal saying state=writing. A run that dies midway
# leaves state=writing behind; the next boot sees it, rolls forward to the
# stock original (the backup is still on p7 and still verifies) even if the
# marker is gone, and keeps the first attempt's undo bytes rather than
# overwriting them with half-written ones. state=done after the read-back.
UNDO=/home/root/settings/az-bootlogo/undo
RESERVE_KB=${AZ_BOOTLOGO_P7_RESERVE_KB:-4096}   # p7 is 61 MB and holds SETTINGS.DAT
hexsha_bytes(){ echo "$1" | sed 's/../\\x&/g'; }   # hex -> \xNN (busybox xxd has no -r)
p3_data_sha(){ dd if="$P3" bs=512 skip=$(($1 / 512)) count=$((($2 + 511) / 512)) 2>/dev/null | head -c "$2" | sha1sum | cut -d' ' -f1; }
p3_field(){ xxd -s $(($1 + 224)) -l 20 -p "$P3" 2>/dev/null | tr -d ' \t\r\n'; }
journal_state(){ sed -n 's/^state=//p' "$UNDO/journal" 2>/dev/null | head -1; }

# save the bytes about to be replaced, unless an interrupted run already did
# (then p3 holds half-written bytes and the first attempt's copy is the true one)
save_undo(){
    if [ "$(journal_state)" = writing ]; then log "undo: keeping the interrupted run's backup of the replaced bytes"; return 0; fi
    # whole 4 KB blocks per file (round up), in KB
    # shellcheck disable=SC2017
    need=$(( (lb_sz + 4095) / 4096 * 4 + (lk_sz + 4095) / 4096 * 4 + 8 ))
    free=$(df -k "${ORIG%/orig}" 2>/dev/null | awk 'NR==2{print $4}' | tr -cd 0-9)
    { [ -n "$free" ] && [ $((free - need)) -ge "$RESERVE_KB" ]; } \
        || bail "REFUSED: p7 has ${free:-?} KB free, the undo copy needs $need KB and p7 keeps $RESERVE_KB KB; $P3 untouched"
    rm -rf "$UNDO.new" 2>/dev/null; mkdir -p "$UNDO.new" 2>/dev/null || bail "REFUSED: cannot create $UNDO.new; $P3 untouched"
    dd if="$P3" bs=512 skip=$((lb_off / 512)) count=$(((lb_sz + 511) / 512)) 2>/dev/null | head -c "$lb_sz" > "$UNDO.new/logo.bmp" \
        && dd if="$P3" bs=512 skip=$((lk_off / 512)) count=$(((lk_sz + 511) / 512)) 2>/dev/null | head -c "$lk_sz" > "$UNDO.new/logo_kernel.bmp" \
        || { rm -rf "$UNDO.new"; bail "REFUSED: cannot save the bytes to be replaced; $P3 untouched"; }
    { echo "state=writing"
      echo "dev=$P3"
      echo "logo.bmp entry=$lb_e off=$lb_off size=$lb_sz field=$lb_stored data=$(sha1sum < "$UNDO.new/logo.bmp" | cut -d' ' -f1) new=$m_lb"
      echo "logo_kernel.bmp entry=$lk_e off=$lk_off size=$lk_sz field=$lk_stored data=$(sha1sum < "$UNDO.new/logo_kernel.bmp" | cut -d' ' -f1) new=$m_lk"
    } > "$UNDO.new/journal" || { rm -rf "$UNDO.new"; bail "REFUSED: cannot write the journal; $P3 untouched"; }
    sync
    rm -rf "$UNDO" 2>/dev/null; mv -f "$UNDO.new" "$UNDO" || bail "REFUSED: cannot place $UNDO; $P3 untouched"
    sync
    log "undo: saved the replaced bytes + journal (state=writing) in $UNDO"
}
# write one logo in place: $1=file $2=entry offset $3=data offset $4=sha1 $5=size
put_one(){
    dd if="$1" of="$P3" bs=512 seek=$(($3 / 512)) conv=notrunc,fsync 2>/dev/null \
        || bail "FAILED: data write of $1 at $3 failed; the next boot retries (journal kept)"
    rsha=$(p3_data_sha "$3" "$5")
    [ "$rsha" = "$4" ] || bail "FAILED: data at $3 reads back $rsha, want $4; the next boot retries (journal kept)"
    # the format is the \xNN escapes of the 20 digest bytes: printf writes them as binary
    # shellcheck disable=SC2059
    printf "$(hexsha_bytes "$4")" | dd of="$P3" bs=1 seek=$(($2 + 224)) count=20 conv=notrunc,fsync 2>/dev/null \
        || bail "FAILED: SHA-1 field write at $2 failed; the next boot retries (journal kept)"
    fsha=$(p3_field "$2")
    [ "$fsha" = "$4" ] || bail "FAILED: SHA-1 field at $2 reads back $fsha, want $4; the next boot retries (journal kept)"
}
# the whole restore: undo, two logos, the DTB witness, the journal closed. Ends this run.
restore_in_place(){
    save_undo
    put_one "$ORIG/logo.bmp"        "$lb_e" "$lb_off" "$m_lb" "$b_lbsz"
    put_one "$ORIG/logo_kernel.bmp" "$lk_e" "$lk_off" "$m_lk" "$b_lksz"
    sync 2>/dev/null
    dtb_after=""
    [ -n "$dtb_e" ] && dtb_after=$(p3_data_sha "$dtb_off" "$dtb_sz")
    if [ -n "$dtb_e" ] && [ "$dtb_after" != "$dtb_before" ]; then
        bail "FAILED: DTB region changed ($dtb_before -> $dtb_after) although no write touched it; journal kept"
    fi
    sed -i 's/^state=writing$/state=done/' "$UNDO/journal" 2>/dev/null; sync
    decide "$1 (in place: logo.bmp@$lb_off+$lb_sz logo_kernel.bmp@$lk_off+$lk_sz + 2 SHA-1 fields; read back ok, dtb unchanged=$dtb_after, sha=$2)"
    # Only here: p3 has been written, read back and verified. Clearing the
    # marker any earlier would throw away the DJ's request on a write that
    # then failed, and they would have no way to know it had not happened.
    clear_marker
    rm -f "$W" 2>/dev/null
    exit 0
}

# ---- the one marker on the chosen stick: MODS/logo.restore. Independent of
# the mods.on/off switch (that gate belongs to az-usbload, not to the logo).
RESTORE=0
MNT=${AZ_MNT:-}; PORT=${AZ_PORT:-}
if [ -n "$MNT" ]; then
    log "using USB$PORT stock mount $MNT (from az-usbload)"
    [ -e "$MNT/MODS/logo.restore" ] && { RESTORE=1; log "logo.restore marker present -> restore"; }
    # MODS/uninstall (az-uninstall's "back to stock" marker) implies the restore: one
    # marker for the whole no-trace revert, and az-uninstall only drops the
    # backup once this boot's log says the logo is stock again.
    [ -e "$MNT/MODS/uninstall" ] && { RESTORE=1; log "revert marker present -> restore"; }
else
    log "no mods stick (AZ_MNT empty) -> no logo marker this boot"
fi
# a restore that died midway (power cut, kill) is finished on the next boot even if
# the marker went with the stick: p3 must not stay half-written
if [ "$RESTORE" = 0 ] && [ "$(journal_state)" = writing ]; then
    RESTORE=1; log "journal: an interrupted restore (state=writing) -> finishing it"
fi

# ---- restore path ----------------------------------------------------------
if [ "$RESTORE" = 1 ]; then
    if { [ -f "$ORIG/logo.bmp" ] && [ -f "$ORIG/logo_kernel.bmp" ] && [ -f "$ORIG/meta" ]; }; then
        m_lb=$(sed -n 's/^logo[.]bmp //p' "$ORIG/meta" | tr -d '\r\n')
        m_lk=$(sed -n 's/^logo_kernel[.]bmp //p' "$ORIG/meta" | tr -d '\r\n')
        b_lb=$(sha1sum "$ORIG/logo.bmp" 2>/dev/null | cut -d' ' -f1)
        b_lk=$(sha1sum "$ORIG/logo_kernel.bmp" 2>/dev/null | cut -d' ' -f1)
        { [ -n "$m_lb" ] && [ -n "$m_lk" ] && [ "$b_lb" = "$m_lb" ] && [ "$b_lk" = "$m_lk" ]; } \
            || bail "REFUSED: backup fails its own recorded sha1 ($b_lb vs $m_lb, $b_lk vs $m_lk); not restoring"
        if [ "$lb_stored" = "$m_lb" ] && [ "$lk_stored" = "$m_lk" ] \
           && [ "$(p3_data_sha "$lb_off" "$lb_sz")" = "$m_lb" ] && [ "$(p3_data_sha "$lk_off" "$lk_sz")" = "$m_lk" ]; then
            [ "$(journal_state)" = writing ] && { sed -i 's/^state=writing$/state=done/' "$UNDO/journal" 2>/dev/null; log "journal: p3 already verifies -> state=done"; }
            clear_marker
            bail "SKIPPED: boot logo already the stock original (sha $m_lb)"
        fi
        b_lbsz=$(wc -c < "$ORIG/logo.bmp" 2>/dev/null | tr -d ' '); b_lksz=$(wc -c < "$ORIG/logo_kernel.bmp" 2>/dev/null | tr -d ' ')
        { [ "$lb_sz" = "$b_lbsz" ] && [ "$lk_sz" = "$b_lksz" ]; } \
            || bail "REFUSED: entry size(s) $lb_sz/$lk_sz != backup $b_lbsz/$b_lksz; refusing to resize"
        log "restoring: logo.bmp@$lb_off logo_kernel.bmp@$lk_off -> $m_lb; dtb sha_before=$dtb_before"
        restore_in_place "RESTORED: boot logo -> stock original" "$m_lb"
    fi
    bail "no backup, nothing to restore"
fi

bail "SKIPPED: no MODS/logo.restore marker (nor revert); boot logo left as it is (nothing installs a logo)"
