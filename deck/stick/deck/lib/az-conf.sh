# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# az-conf.sh: sourced (never run) by az-usbload and az-community. MODS/azmods.cfg, the
# settings file on the mods stick, read and written by the same rules as azmod's azconf.c:
#
#   # azmods.cfg: AZ-MODS settings
#   version = 1
#   <key> = <value>
#
# `#` lines are comments; whitespace around `=` and at the line ends is ignored; the last line
# of a key wins. A file without a `version = 1` line, or over 64 KB, is not a settings file:
# it is never imported and never overwritten. The kill switches (mods, deck, community-mods) are
# the exception on reading: `off` is the safe answer, so they are honored from any file, and a
# file holding only `mods = off` turns the mods off.
#
# The stick is the source of truth. The deck keeps a working copy (CFG_DECK), written only by
# azmod. az-usbload hands the stick's file to azmod (CFG_IMPORT) when the stick's file changed
# since the last sync, and writes the deck copy back to the stick when the deck copy changed.
# CFG_SYNC (p7) records the sync point: line 1 the md5 of the stick's file, line 2 the md5 of
# the deck copy, or "pending" while azmod has not taken an import yet.
CFG_NAME=azmods.cfg
# shellcheck disable=SC2034 # read by the scripts that source this file
CFG_DECK=${AZ_CFG_DECK:-/home/root/settings/azmod.conf}
# shellcheck disable=SC2034 # likewise
CFG_IMPORT=${AZ_CFG_IMPORT:-/run/azmods-cfg.import}
CFG_MAX=65537
# cfg_get FILE KEY: the value (the last line wins); nothing when absent
cfg_get() {
    [ -f "$1" ] || return 0
    tr -d '\r' < "$1" | awk -v k="$2" '
        { l = $0; sub(/^[ \t]+/, "", l); if (l ~ /^#/) next
          i = index(l, "="); if (!i) next
          key = substr(l, 1, i - 1); sub(/[ \t]+$/, "", key); if (key != k) next
          v = substr(l, i + 1); sub(/^[ \t]+/, "", v); sub(/[ \t]+$/, "", v); val = v; f = 1 }
        END { if (f) print val }'
}
# cfg_valid FILE: a settings file azmod loads (version 1, at most 64 KB)
cfg_valid() {
    [ -f "$1" ] || return 1
    _cn=$(wc -c < "$1" 2>/dev/null | tr -cd 0-9)
    [ "${_cn:-0}" -le "$CFG_MAX" ] && [ "$(cfg_get "$1" version)" = 1 ]
}
# cfg_off FILE KEY: KEY is off (off/0/false, any case), read from any file
cfg_off() {
    case "$(cfg_get "$1" "$2")" in [Oo][Ff][Ff]|0|[Ff][Aa][Ll][Ss][Ee]) return 0 ;; esac
    return 1
}
# cfg_set FILE KEY VALUE: KEY's line replaced where it is (later duplicates dropped), else
# appended; an empty VALUE removes KEY. FILE.new, then renamed over FILE.
cfg_set() {
    tr -d '\r' < "$1" | awk -v k="$2" -v v="$3" '
        { l = $0; sub(/^[ \t]+/, "", l); i = index(l, "=")
          if (l !~ /^#/ && i) { key = substr(l, 1, i - 1); sub(/[ \t]+$/, "", key)
              if (key == k) { if (!done && v != "") print k " = " v; done = 1; next } }
          print }
        END { if (!done && v != "") print k " = " v }' > "$1.new" && mv -f "$1.new" "$1" || { rm -f "$1.new"; return 1; }
}
cfg_new() { printf '# azmods.cfg: AZ-MODS settings\nversion = 1\n' > "$1"; }
cfg_md5() { md5sum "$1" 2>/dev/null | cut -d' ' -f1; }
# cfg_stick_put SRC MNT: SRC -> MNT/MODS/azmods.cfg. The stock mount is read-only: remounted
# rw for the write and back after. Written .azmods.cfg.new, compared, renamed, synced: a stick
# pulled mid-write keeps the previous file (a leftover .new is never read).
cfg_stick_put() {
    [ -n "$2" ] && [ -d "$2/MODS" ] || return 1
    _cro=$(awk -v m="$2" '$2==m{print ($4 ~ /(^|,)ro(,|$)/)?1:0; exit}' /proc/mounts 2>/dev/null)
    [ "$_cro" = 1 ] && { mount -o remount,rw "$2" 2>/dev/null || return 1; }
    _cd="$2/MODS"; _crc=1
    if cp -f "$1" "$_cd/.$CFG_NAME.new" 2>/dev/null && sync && cmp -s "$1" "$_cd/.$CFG_NAME.new" \
        && mv -f "$_cd/.$CFG_NAME.new" "$_cd/$CFG_NAME" 2>/dev/null; then _crc=0
    else rm -f "$_cd/.$CFG_NAME.new" 2>/dev/null; fi
    sync
    [ "$_cro" = 1 ] && mount -o remount,ro "$2" 2>/dev/null
    return $_crc
}
