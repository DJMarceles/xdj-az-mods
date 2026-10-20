#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# Every command the shipped deck scripts run exists on the deck. The suites run in alpine,
# whose busybox has applets the deck's (1.27.2) does not, so a script can pass every suite
# and still fail on a deck: the settings watcher read `stat`, which the deck lacks, and
# silently never wrote the Mod Menu's settings to the stick.
#   the deck:     command names in the 2.01 rootfs (/lower: bin, sbin, usr/bin, usr/sbin)
#   candidates:   command names this container has (busybox applets + its PATH dirs)
#   scanned:      the stripped payload (/p) and the staged MODS/deck (/d), i.e. what ships
#                 (comment lines skipped: a file staged from the emu repo keeps its comments)
# A candidate missing on the deck that a script uses at a command position is a FAIL.
# `openssl base64` is openssl's subcommand, not the base64 applet.
set -u
[ -x /lower/bin/busybox ] || { echo "FAIL no deck rootfs mounted at /lower (set AZ_DECK_ROOTFS for run.sh)"; exit 1; }
W=/tmp/deckcmds; mkdir -p "$W"
for d in bin sbin usr/bin usr/sbin; do ls -1 "/lower/$d" 2>/dev/null; done | sort -u > "$W/deck"
{ busybox --list; for d in /bin /sbin /usr/bin /usr/sbin; do ls -1 "$d"; done; } | sort -u > "$W/cand"
grep -vxFf "$W/deck" "$W/cand" > "$W/missing"
chk() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }
chk "CONTROL: the deck list is the deck's (md5sum and busybox in it, stat not)" \
    'grep -qx md5sum "$W/deck" && grep -qx busybox "$W/deck" && ! grep -qx stat "$W/deck" && grep -qx stat "$W/missing"'

# shell scripts among the shipped files: text, not a license, a version stamp or a unit
scripts() {
    find /p /d -type f | while read -r f; do
        case "$f" in *.LICENSE|*/version|*.service|*.conf) continue ;; esac
        # text: no NUL byte (busybox grep has no -I)
        [ "$(tr -d '\000' < "$f" | wc -c)" = "$(wc -c < "$f")" ] && echo "$f"
    done
}
# $@ = files -> "file: command" for each deck-missing command used at a command position
scan() {
    while read -r c; do
        for f in "$@"; do
            grep -vE '^[[:space:]]*#' "$f" 2>/dev/null | grep -E "(^|[;&|({\` ]|\\\$\\()$c( |;|\\)|\$)" \
                | grep -vE "openssl $c( |\$)" | grep -q . \
                && echo "$f: $c"
        done
    done < "$W/missing"
}
FILES=$(scripts)
n=$(printf '%s\n' "$FILES" | grep -c .)
chk "CONTROL: the scan sees the shipped scripts ($n files, az-usbload and az-conf.sh among them)" \
    '[ "$n" -ge 10 ] && printf "%s\n" "$FILES" | grep -q "/az-usbload$" && printf "%s\n" "$FILES" | grep -q "/az-conf.sh$"'
# shellcheck disable=SC2086 # one path per word: none of these names has a space
out=$(scan $FILES)
if [ -z "$out" ]; then echo "PASS every command the shipped deck scripts run is on the deck"
else printf '%s\n' "$out" | sed 's/^/    /'; echo "FAIL the shipped deck scripts run commands the deck does not have (above)"; fi

# controls in the same round: a planted stat call is found, a planted md5sum call is not
cp /d/lib/az-conf.sh "$W/planted"; printf '%s\n' '_x=$(stat -c %s /x)' >> "$W/planted"
chk "CONTROL: a planted stat call is found" '[ "$(scan "$W/planted")" = "$W/planted: stat" ]'
cp /d/lib/az-conf.sh "$W/planted"; printf '%s\n' '_x=$(md5sum /x)' >> "$W/planted"
chk "CONTROL: a planted md5sum call is not" '[ -z "$(scan "$W/planted")" ]'
