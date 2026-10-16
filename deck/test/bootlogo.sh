#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# shellcheck disable=SC2034  # captured values are read inside chk's single-quoted eval strings
# Test suite for az-bootlogo (boot.d/20-bootlogo.sh, restore-only since 1.32): the
# MODS/logo.restore path, run against a synthetic RSCE p3 image with the real
# stock geometry (mirrors the dumped stock p3: 3 entries, rk-kernel.dtb@0x800 sz
# 101425 first, logo.bmp@0x19600 and logo_kernel.bmp@0x2a400, both 68758 B, 4 MiB
# image) and a FAT loop stick with MODS/ (az-usbpick + stub udevadm, same as the
# loader tests). Run by test/run.sh in its alpine container (`test/run.sh bootlogo`).
#
# A "deck that got the logo from v1.0" is set up synthetically: patched() writes
# a foreign logo into both entries (sha fixed up) and mkbackup() writes the p7
# backup exactly as the v1.0 firmware did. Nothing here can patch any more, so
# the control pair (no marker -> untouched, marker -> RESTORED) runs on that.
# Every case asserts on /tmp/az-bootlogo.log and on the p3 image bytes.
set -u
apk add -q dosfstools >/dev/null 2>&1 || { echo NO_DOSFSTOOLS; exit 1; }
export PATH=/stub:$PATH
cp /p/usr/sbin/az-usbpick /usr/sbin/
# The hook is run the way az-usbload runs it: picked stick in AZ_MNT/AZ_PORT/AZ_MODS,
# via `sh` from a /tmp copy. This wrapper IS that contract (see run_hooks in az-usbload).
hookwrap(){ mkdir -p /tmp/az-hookwrap; cp "$2" /tmp/az-hookwrap/; cat > "/usr/sbin/$1" <<W
#!/bin/sh
log(){ :; }
. /usr/sbin/az-usbpick
MNT=""; PORT=""; pick_stick
export AZ_MNT="\$MNT" AZ_PORT="\$PORT" AZ_MODS="\${MNT:+\$MNT/MODS}" AZ_MODS_OFF=0 AZ_STAGE=/home/root/settings/az-usbload AZ_DECK=/tmp/az-deck AZ_LOG=/tmp/az-usbload.log
exec sh /tmp/az-hookwrap/${2##*/} "\$@"
W
chmod +x "/usr/sbin/$1"; }
hookwrap az-bootlogo /d/boot.d/20-bootlogo.sh
P7S=/home/root/settings; mkdir -p "$P7S" /m /media/usb/sda1
chk(){ if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }
LSZ=68758; DSZ=101425

# these formats are \xNN escapes: printf writes the bytes they name
# shellcheck disable=SC2059
w32(){ h=$(printf '%08x' "$3"); printf "\\x${h:6:2}\\x${h:4:2}\\x${h:2:2}\\x${h:0:2}" | dd of="$1" bs=1 seek="$2" count=4 conv=notrunc 2>/dev/null; }
# shellcheck disable=SC2059
wsha(){ e=$(echo "$3" | sed 's/../\\x&/g'); printf "$e" | dd of="$1" bs=1 seek="$2" count=20 conv=notrunc 2>/dev/null; }
wname(){ printf '%s' "$3" | dd of="$1" bs=1 seek=$(($2 + 4)) conv=notrunc 2>/dev/null; }

build_p3(){
    mkdir -p /ref
    dd if=/dev/zero of="$1" bs=1M count=4 2>/dev/null
    head -c $DSZ /dev/urandom > /ref/dtb
    head -c $LSZ /dev/urandom > /ref/lb
    head -c $LSZ /dev/urandom > /ref/lk
    head -c $LSZ /dev/urandom > /ref/az       # "the AZ MODS logo" a v1.0 deck carries
    printf 'RSCE' | dd of="$1" bs=1 seek=0 conv=notrunc 2>/dev/null
    w32 "$1" 12 3
    wname "$1" 0x200 rk-kernel.dtb;   w32 "$1" $((0x200+260)) $((0x800/512));   w32 "$1" $((0x200+264)) $DSZ
    wsha "$1" $((0x200+224)) "$(sha1sum /ref/dtb | cut -d' ' -f1)"
    wname "$1" 0x400 logo.bmp;        w32 "$1" $((0x400+260)) $((0x19600/512)); w32 "$1" $((0x400+264)) $LSZ
    wsha "$1" $((0x400+224)) "$(sha1sum /ref/lb | cut -d' ' -f1)"
    wname "$1" 0x600 logo_kernel.bmp; w32 "$1" $((0x600+260)) $((0x2a400/512)); w32 "$1" $((0x600+264)) $LSZ
    wsha "$1" $((0x600+224)) "$(sha1sum /ref/lk | cut -d' ' -f1)"
    dd if=/ref/dtb of="$1" bs=512 seek=$((0x800/512)) conv=notrunc 2>/dev/null
    dd if=/ref/lb  of="$1" bs=512 seek=$((0x19600/512)) conv=notrunc 2>/dev/null
    dd if=/ref/lk  of="$1" bs=512 seek=$((0x2a400/512)) conv=notrunc 2>/dev/null
}
# patched: what the v1.0 firmware left in p3 (foreign logo in both entries)
patched(){
    a=$(sha1sum /ref/az | cut -d' ' -f1)
    dd if=/ref/az of="$1" bs=512 seek=$((0x19600/512)) conv=notrunc 2>/dev/null; wsha "$1" $((0x400+224)) "$a"
    dd if=/ref/az of="$1" bs=512 seek=$((0x2a400/512)) conv=notrunc 2>/dev/null; wsha "$1" $((0x600+224)) "$a"
}
# mkbackup: the p7 backup exactly as the v1.0 firmware wrote it
mkbackup(){
    rm -rf $P7S/az-bootlogo; mkdir -p $P7S/az-bootlogo/orig
    cp /ref/lb $P7S/az-bootlogo/orig/logo.bmp; cp /ref/lk $P7S/az-bootlogo/orig/logo_kernel.bmp
    printf 'logo.bmp %s\nlogo_kernel.bmp %s\n' "$(sha1sum /ref/lb | cut -d' ' -f1)" "$(sha1sum /ref/lk | cut -d' ' -f1)" > $P7S/az-bootlogo/orig/meta
}
p3md5(){ md5sum /p3 | cut -d' ' -f1; }
lbsha(){ dd if=/p3 bs=512 skip=$((0x19600/512)) count=135 2>/dev/null | head -c $LSZ | sha1sum | cut -d' ' -f1; }
origsum(){ ( cd $P7S/az-bootlogo/orig 2>/dev/null && md5sum logo.bmp logo_kernel.bmp meta 2>/dev/null | md5sum ); }
boot(){ rm -f /tmp/az-bootlogo.log; AZ_BOOTLOGO_DEV=/p3 /usr/sbin/az-bootlogo
        grep -hE 'PATCHED|RESTORED|SKIPPED|REFUSED|FAILED|no backup' /tmp/az-bootlogo.log | sed 's/^/    /'; }
# a v1.0 deck: stock image built, then patched, backup present
v10deck(){ build_p3 /p3; STOCK_MD5=$(p3md5); patched /p3; mkbackup; PATCHED_MD5=$(p3md5); }

rm -f /img; dd if=/dev/zero of=/img bs=1M count=16 2>/dev/null; mkfs.vfat /img >/dev/null
lo=$(losetup -f); losetup "$lo" /img; IFS=: read -r _maj _min < /sys/block/"${lo#/dev/}"/dev; mknod /dev/sda1 b "$_maj" "$_min"; echo "sda 1-1" > /tmp/ports
mount /dev/sda1 /m; mkdir /m/MODS; touch /m/MODS/mods.on; umount /m; mount -o ro /dev/sda1 /media/usb/sda1
st(){ umount /media/usb/sda1 2>/dev/null; mount /dev/sda1 /m; ( cd /m || exit 1; eval "$1" ); umount /m; mount -o ro /dev/sda1 /media/usb/sda1; }
unplug(){ umount /media/usb/sda1 2>/dev/null; mv /dev/sda1 /dev.sda1.off; }
replug(){ mv /dev.sda1.off /dev/sda1; mount -o ro /dev/sda1 /media/usb/sda1; }

echo "== 1 THE PAIR, same round: v1.0 deck, no marker -> untouched; marker -> RESTORED byte-exact"
v10deck; b0=$(origsum)
chk "1 setup: p3 carries the foreign logo" '[ "$(lbsha)" = "$(sha1sum /ref/az | cut -d" " -f1)" ]'
boot
chk "1a SKIPPED no marker" 'grep -q "SKIPPED: no MODS/logo.restore marker" /tmp/az-bootlogo.log'
chk "1a p3 untouched (logo kept)" '[ "$(p3md5)" = "$PATCHED_MD5" ]'
chk "1a nothing PATCHED ever" '! grep -q PATCHED /tmp/az-bootlogo.log'
st 'touch MODS/logo.restore'; boot
chk "1b RESTORED" 'grep -q "RESTORED: boot logo -> stock original" /tmp/az-bootlogo.log'
chk "1b p3 md5 == the original stock image" '[ "$(p3md5)" = "$STOCK_MD5" ]'
chk "1b backup left in place, unchanged" '[ "$b0" = "$(origsum)" ]'
chk "1b no working copy left" '[ ! -e /tmp/az-bootlogo.p3 ]'

echo "== 2 marker again on the already-stock p3: SKIPPED already stock, p3 unchanged"
p0=$(p3md5); st 'touch MODS/logo.restore'; boot
chk "2 SKIPPED already the stock original" 'grep -q "SKIPPED: boot logo already the stock original" /tmp/az-bootlogo.log'
chk "2 p3 unchanged" '[ "$p0" = "$(p3md5)" ]'

echo "== 3 the old opt-in marker logo.on never installs anything (stock deck, no backup)"
build_p3 /p3; m0=$(p3md5); rm -rf $P7S/az-bootlogo
st 'rm -f MODS/logo.restore; touch MODS/logo.on'; boot
chk "3 SKIPPED no restore marker" 'grep -q "SKIPPED: no MODS/logo.restore marker" /tmp/az-bootlogo.log'
chk "3 p3 untouched" '[ "$m0" = "$(p3md5)" ]'
chk "3 no backup written" '[ ! -e $P7S/az-bootlogo/orig ]'
st 'rm -f MODS/logo.on'

echo "== 4 marker honoured with mods.off present (no mods gate on the logo)"
v10deck; st 'touch MODS/logo.restore MODS/mods.off'; boot
chk "4 RESTORED despite mods.off" 'grep -q "RESTORED: boot logo -> stock original" /tmp/az-bootlogo.log'
chk "4 p3 == stock" '[ "$(p3md5)" = "$STOCK_MD5" ]'
st 'rm -f MODS/mods.off'

echo "== 5 marker + NO backup: nothing restored, nothing written"
v10deck; rm -rf $P7S/az-bootlogo; st 'touch MODS/logo.restore'; boot
chk "5 logged no backup, nothing to restore" 'grep -q "no backup, nothing to restore" /tmp/az-bootlogo.log'
chk "5 p3 unchanged" '[ "$(p3md5)" = "$PATCHED_MD5" ]'
chk "5 no backup appeared" '[ ! -e $P7S/az-bootlogo/orig ]'

echo "== 6 corrupted images refused, p3 untouched"
v10deck; printf 'XXXX' | dd of=/p3 bs=1 seek=0 conv=notrunc 2>/dev/null; m0=$(p3md5); st 'touch MODS/logo.restore'; boot
chk "6 bad magic REFUSED" 'grep -q "REFUSED: not an RSCE image" /tmp/az-bootlogo.log'
chk "6 p3 untouched" '[ "$m0" = "$(p3md5)" ]'
v10deck; w32 /p3 $((0x400+264)) 12345; m0=$(p3md5); st 'touch MODS/logo.restore'; boot
chk "6b entry size != backup REFUSED" 'grep -q "REFUSED: entry size" /tmp/az-bootlogo.log'
chk "6b p3 untouched" '[ "$m0" = "$(p3md5)" ]'

echo "== 7 corrupted backup REFUSES to restore"
v10deck; printf 'X' | dd of=$P7S/az-bootlogo/orig/logo.bmp bs=1 seek=100 conv=notrunc 2>/dev/null; st 'touch MODS/logo.restore'; boot
chk "7 REFUSED backup fails its own sha1" 'grep -q "REFUSED: backup fails its own recorded sha1" /tmp/az-bootlogo.log'
chk "7 p3 unchanged" '[ "$(p3md5)" = "$PATCHED_MD5" ]'
v10deck; rm $P7S/az-bootlogo/orig/meta; st 'touch MODS/logo.restore'; boot
chk "7b incomplete backup -> nothing to restore" 'grep -q "no backup, nothing to restore" /tmp/az-bootlogo.log'
chk "7b p3 unchanged" '[ "$(p3md5)" = "$PATCHED_MD5" ]'

echo "== 8 no stick at all: nothing happens"
v10deck; st 'touch MODS/logo.restore'; unplug; boot; replug
chk "8 SKIPPED no marker with no stick" 'grep -q "SKIPPED: no MODS/logo.restore marker" /tmp/az-bootlogo.log'
chk "8 p3 untouched" '[ "$(p3md5)" = "$PATCHED_MD5" ]'
chk "8 no-stick boot logged" 'grep -q "no mods stick (AZ_MNT empty)" /tmp/az-bootlogo.log'

echo "== 9 the restore needs nothing bundled; p3 == stock"
st 'touch MODS/logo.restore'   # the hook clears the marker once p3 is stock, so each case sets its own
v10deck; mount -t tmpfs -o ro tmpfs /tmp/ro 2>/dev/null || mkdir -p /tmp/ro
boot
chk "9 RESTORED" 'grep -q "RESTORED: boot logo -> stock original" /tmp/az-bootlogo.log'
chk "9 p3 == stock" '[ "$(p3md5)" = "$STOCK_MD5" ]'
chk "9 nothing bundled is needed (no logo.bmp in the payload)" '[ ! -e /p/usr/share/az-bootlogo/logo.bmp ]'

echo "== 10 MODS/uninstall alone implies the restore (one marker for the whole revert)"
v10deck; st 'rm -f MODS/logo.restore; touch MODS/uninstall'; boot
chk "10 RESTORED on revert marker" 'grep -q "RESTORED: boot logo -> stock original" /tmp/az-bootlogo.log'
chk "10 logged the revert marker as the reason" 'grep -q "revert marker present -> restore" /tmp/az-bootlogo.log'
chk "10 p3 == stock" '[ "$(p3md5)" = "$STOCK_MD5" ]'
st 'rm -f MODS/uninstall; touch MODS/logo.restore'

echo "== 11 the marker CLEARS ITSELF once p3 is provably stock"
v10deck                      # a v1.0 deck: foreign logo in p3, good backup on p7
st 'rm -f MODS/uninstall; touch MODS/logo.restore'; boot
chk "11 RESTORED" 'grep -q "RESTORED: boot logo -> stock original" /tmp/az-bootlogo.log'
chk "11 p3 == stock" '[ "$(p3md5)" = "$STOCK_MD5" ]'
chk "11 marker cleared from the stick" '[ ! -e /media/usb/sda1/MODS/logo.restore ]'
chk "11 logged the clear" 'grep -q "marker: MODS/logo.restore cleared" /tmp/az-bootlogo.log'

echo "== 12 already stock -> the marker clears too (nothing to do, stop asking)"
st 'touch MODS/logo.restore'; boot
chk "12 SKIPPED already stock" 'grep -q "already the stock original" /tmp/az-bootlogo.log'
chk "12 marker cleared" '[ ! -e /media/usb/sda1/MODS/logo.restore ]'
chk "12 p3 still stock" '[ "$(p3md5)" = "$STOCK_MD5" ]'

echo "== 13 CONTROL for 11: a REFUSED restore must NOT clear the marker"
v10deck; printf 'X' | dd of=$P7S/az-bootlogo/orig/logo.bmp bs=1 seek=100 conv=notrunc 2>/dev/null
st 'touch MODS/logo.restore'; boot
chk "13 REFUSED (backup fails its own sha)" 'grep -q "REFUSED: backup fails its own recorded sha1" /tmp/az-bootlogo.log'
chk "13 marker KEPT so the request is not lost" '[ -e /media/usb/sda1/MODS/logo.restore ]'
chk "13 p3 untouched" '[ "$(p3md5)" = "$PATCHED_MD5" ]'
st 'rm -f MODS/logo.restore'

echo "== 14 BOTH markers together: they must not fight"
v10deck
st 'touch MODS/logo.restore MODS/uninstall'; boot
chk "14 RESTORED once" 'grep -q "RESTORED: boot logo -> stock original" /tmp/az-bootlogo.log'
chk "14 p3 == stock" '[ "$(p3md5)" = "$STOCK_MD5" ]'
chk "14 logo.restore cleared (its job is done)" '[ ! -e /media/usb/sda1/MODS/logo.restore ]'
chk "14 MODS/uninstall NOT touched (the app owns it)" '[ -e /media/usb/sda1/MODS/uninstall ]'

echo "== 15 uninstall alone: az-bootlogo must not invent a logo.restore marker"
v10deck; st 'rm -f MODS/logo.restore'; boot
chk "15 RESTORED on the uninstall marker" 'grep -q "RESTORED: boot logo -> stock original" /tmp/az-bootlogo.log'
chk "15 no logo.restore was created" '[ ! -e /media/usb/sda1/MODS/logo.restore ]'
chk "15 MODS/uninstall still there" '[ -e /media/usb/sda1/MODS/uninstall ]'
st 'rm -f MODS/uninstall'

# ---- in-place writes: only the logos and their SHA-1 fields, never the DTB -------------
# A dd wrapper ahead of busybox's logs every write whose of= is /p3 as "start len".
# Bytes alone cannot tell "wrote only the logos" from "rewrote 4 MiB of identical bytes",
# so the write log is the instrument, and case 16c proves it can say NO in this round.
# DD_CUT=1: the first data write to /p3 lands HALF its bytes and kills the hook (-9),
# which is a power cut midway as far as p3 and the journal can tell.
mkdir -p /ddw; cat > /ddw/dd <<'W'
#!/bin/sh
of=; bs=512; seek=0; inf=; cnt=
for a in "$@"; do case "$a" in of=*) of=${a#of=};; bs=*) bs=${a#bs=};; seek=*) seek=${a#seek=};; if=*) inf=${a#if=};; count=*) cnt=${a#count=};; esac; done
if [ "$of" = /p3 ]; then
  case "$bs" in *M) b=$(( ${bs%M} * 1048576 ));; *k|*K) b=$(( ${bs%?} * 1024 ));; *) b=$bs;; esac
  if [ -n "$inf" ]; then len=$(wc -c < "$inf"); else len=$(( ${cnt:-0} * b )); fi
  echo "$(( seek * b )) $len" >> /tmp/ddlog
  if [ -n "${DD_CUT:-}" ] && [ ! -e /tmp/ddcut.done ] && [ -n "$inf" ]; then
     touch /tmp/ddcut.done
     head -c $(( len / 2 )) "$inf" | busybox dd of=/p3 bs=512 seek="$seek" conv=notrunc,fsync 2>/dev/null
     kill -9 "$PPID"; exit 137
  fi
fi
exec busybox dd "$@"
W
chmod +x /ddw/dd
# every logged write inside a logo data region or a logo entry's SHA-1 field
in_regions(){ [ -s /tmp/ddlog ] && awk -v L=$LSZ '
  { s=$1; e=$1+$2; ok=0
    if (s>=0x19600 && e<=0x19600+L) ok=1; if (s>=0x2a400 && e<=0x2a400+L) ok=1
    if (s>=0x400+224 && e<=0x400+244) ok=1; if (s>=0x600+224 && e<=0x600+244) ok=1
    if (!ok) bad=1 }
  END{ exit bad }' /tmp/ddlog; }
dtbsha(){ dd if=/p3 bs=512 skip=4 count=$(((DSZ + 511) / 512)) 2>/dev/null | head -c $DSZ | sha1sum | cut -d' ' -f1; }
ddboot(){ rm -f /tmp/ddlog /tmp/ddcut.done; PATH=/ddw:$PATH boot; }

echo "== 16 the restore writes ONLY the two logo regions + their SHA-1 fields (control 16c in the same round)"
v10deck; st 'touch MODS/logo.restore'; ddboot
chk "16 RESTORED" 'grep -q "RESTORED: boot logo -> stock original (in place" /tmp/az-bootlogo.log'
chk "16 p3 == stock byte-exact" '[ "$(p3md5)" = "$STOCK_MD5" ]'
chk "16a writes were logged (the instrument saw them)" '[ "$(wc -l < /tmp/ddlog)" -ge 4 ]'
chk "16b every write inside a logo region or SHA-1 field" 'in_regions'
cp /p3 /p3.copy; rm -f /tmp/ddlog; PATH=/ddw:$PATH dd if=/p3.copy of=/p3 bs=1M conv=fsync 2>/dev/null
chk "16c CONTROL: a whole-partition dd through the same wrapper is flagged" '! in_regions'
chk "16d the undo holds exactly the bytes replaced (the foreign logo)" 'cmp -s $P7S/az-bootlogo/undo/logo.bmp /ref/az && cmp -s $P7S/az-bootlogo/undo/logo_kernel.bmp /ref/az'
chk "16e journal closed (state=done) and names the old field" 'grep -qx "state=done" $P7S/az-bootlogo/undo/journal && grep -q "field=$(sha1sum /ref/az | cut -d" " -f1)" $P7S/az-bootlogo/undo/journal'

echo "== 17 a write killed midway is recovered on the next boot, even with the marker gone"
v10deck; d0=$(dtbsha); st 'touch MODS/logo.restore'; DD_CUT=1 ddboot
chk "17a the cut really damaged p3 (neither stock nor the v1.0 image)" '[ "$(p3md5)" != "$STOCK_MD5" ] && [ "$(p3md5)" != "$PATCHED_MD5" ]'
chk "17b journal left at state=writing" 'grep -qx "state=writing" $P7S/az-bootlogo/undo/journal'
chk "17c no RESTORED claimed" '! grep -q RESTORED /tmp/az-bootlogo.log'
chk "17d DTB untouched by the cut write" '[ "$(dtbsha)" = "$d0" ]'
chk "17e the undo still holds the ORIGINAL replaced bytes" 'cmp -s $P7S/az-bootlogo/undo/logo.bmp /ref/az'
st 'rm -f MODS/logo.restore'; ddboot
chk "17f next boot finishes it from the journal (no marker)" 'grep -q "interrupted restore (state=writing) -> finishing it" /tmp/az-bootlogo.log && grep -q "RESTORED" /tmp/az-bootlogo.log'
chk "17g p3 == stock byte-exact after recovery" '[ "$(p3md5)" = "$STOCK_MD5" ]'
chk "17h the recovery kept the first attempt's undo (not the half-written bytes)" 'grep -q "keeping the interrupted run" /tmp/az-bootlogo.log && cmp -s $P7S/az-bootlogo/undo/logo.bmp /ref/az'
chk "17i journal closed" 'grep -qx "state=done" $P7S/az-bootlogo/undo/journal'
chk "17j recovery writes stayed inside the regions too" 'in_regions'

echo "== 18 p7 too full for the undo copy: REFUSED, p3 untouched (control: case 16 restored with room)"
v10deck; cp -a $P7S/az-bootlogo /tmp/bl.save; mount -t tmpfs -o size=1m tiny $P7S
cp -a /tmp/bl.save $P7S/az-bootlogo; st 'touch MODS/logo.restore'; ddboot
chk "18 REFUSED for space" 'grep -q "REFUSED: p7 has .* KB free" /tmp/az-bootlogo.log'
chk "18 p3 untouched, nothing written" '[ "$(p3md5)" = "$PATCHED_MD5" ] && [ ! -s /tmp/ddlog ]'
chk "18 marker kept" '[ -e /media/usb/sda1/MODS/logo.restore ]'
umount $P7S; st 'rm -f MODS/logo.restore'
