#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# Test suite for the deck scripts' log caps (deck/lib/az-logcap.sh). Run by test/run.sh in
# its alpine container (`test/run.sh logcap`), against the STRIPPED payload (/p) and the
# staged MODS/deck (/d), exactly what ships.
#
# /tmp is RAM on the deck and these scripts run for the whole session: the updater every 30
# minutes, the re-announce after every EP147 restart, the loader's watcher re-runs. Each case
# runs a real script hundreds of times with a small cap and measures the file it wrote; c2 is
# the same measurement on a helper with its move removed, which must grow far past the cap
# (a capped result means nothing unless the uncapped one is visibly different).
set -u
chk(){ if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fi; }
H=/d/lib/az-logcap.sh
[ -f "$H" ] || { echo "FAIL fixture: $H is not in the staged MODS/deck"; exit 1; }
size(){ wc -c < "$1" 2>/dev/null | tr -cd 0-9; }
spam(){ # $1 helper, $2 log, $3 lines: what every log() does, $3 times
    # shellcheck source=../stick/deck/lib/az-logcap.sh disable=SC2034 # AZLOG_MAX is read by the sourced cap
    ( AZLOG_MAX=4096; . "$1"; i=0
      while [ $i -lt "$3" ]; do azlog_cap "$2"; echo "line $i of a deck script that keeps logging" >> "$2"; i=$((i+1)); done ) }

echo "== c1 the helper keeps a log under its cap, newest lines in the log"
rm -f /tmp/c1.log /tmp/c1.log.1; spam "$H" /tmp/c1.log 2000
chk "c1 log within the cap ($(size /tmp/c1.log) bytes, cap 4096)" '[ "$(size /tmp/c1.log)" -le $((4096 + 64)) ]'
chk "c1 the previous part kept as .1" '[ -s /tmp/c1.log.1 ]'
chk "c1 the newest line is in the log" 'tail -1 /tmp/c1.log | grep -q "^line 1999 "'

echo "== c2 CONTROL in the same round: the helper without its move grows without bound"
sed 's/ && mv -f "\$1" "\$1\.1" 2>\/dev\/null/ \&\& :/' "$H" > /tmp/nomv.sh
if grep -q 'mv -f' /tmp/nomv.sh; then echo "FAIL c2 fixture: the mutation did not apply"; fi
rm -f /tmp/c2.log; spam /tmp/nomv.sh /tmp/c2.log 2000
chk "c2 the uncapped log is far past the cap ($(size /tmp/c2.log) bytes)" '[ "$(size /tmp/c2.log)" -gt 40000 ]'

echo "== c3 deck/lib/gadget-renotify.sh (helper beside it): 400 runs"
rm -f /tmp/g.log /tmp/g.log.1
i=0; while [ $i -lt 400 ]; do AZLOG_MAX=2048 AZ_GADGET_BOX=/nonexistent AZ_GADGET_LOG=/tmp/g.log sh /d/lib/gadget-renotify.sh; i=$((i+1)); done
chk "c3 log within the cap ($(size /tmp/g.log) bytes, cap 2048)" '[ "$(size /tmp/g.log)" -le $((2048 + 200)) ] && [ -s /tmp/g.log.1 ]'
chk "c3 the script still logged its decision" 'grep -q "nothing to do" /tmp/g.log'

echo "== c4 usb-renotify.sh, firmware copy: finds the helper in this boot's deck copy"
mkdir -p /home/root/scripts /tmp/az-deck/lib; cp /p/home/root/scripts/usb-renotify.sh /home/root/scripts/
cp "$H" /tmp/az-deck/lib/az-logcap.sh; rm -f /usr/sbin/az-usbpick /tmp/r.log /tmp/r.log.1
i=0; while [ $i -lt 400 ]; do AZLOG_MAX=2048 AZ_RENOTIFY_LOG=/tmp/r.log sh /home/root/scripts/usb-renotify.sh; i=$((i+1)); done
chk "c4 log within the cap ($(size /tmp/r.log) bytes, cap 2048)" '[ "$(size /tmp/r.log)" -le $((2048 + 200)) ] && [ -s /tmp/r.log.1 ]'
echo "== c4b ...and with no helper anywhere it still runs (no cap, nothing breaks)"
rm -rf /tmp/az-deck /tmp/r2.log
i=0; while [ $i -lt 100 ]; do AZLOG_MAX=2048 AZ_RENOTIFY_LOG=/tmp/r2.log sh /home/root/scripts/usb-renotify.sh; i=$((i+1)); done
chk "c4b 100 lines logged without a helper" '[ "$(grep -c "az-usbpick absent" /tmp/r2.log)" -eq 100 ]'

echo "== c5 deck/lib/az-modupdate (/tmp/az-modupdate/log): 400 runs"
rm -rf /tmp/az-modupdate
i=0; while [ $i -lt 400 ]; do AZLOG_MAX=2048 AZ_MNT='' sh /d/lib/az-modupdate check; i=$((i+1)); done
chk "c5 log within the cap ($(size /tmp/az-modupdate/log) bytes, cap 2048)" \
    '[ "$(size /tmp/az-modupdate/log)" -le $((2048 + 200)) ] && [ -s /tmp/az-modupdate/log.1 ]'

echo "== c6 every stdout append to a log in a shipped deck script is capped first"
# A line appending stdout to $LOG / $LOG.hooks (2>> is a single command's stderr, bounded,
# and capped at the next log line) must call azlog_cap on that line or the line before.
lint(){ # files... -> prints offenders
    for f in "$@"; do
        [ -f "$f" ] || continue
        head -c 2 "$f" | grep -q '#!' || case "$f" in *.sh) ;; *) continue ;; esac
        awk -v F="$f" '
            { cur = $0 }
            cur ~ /(^|[^2])>> ?"\$LOG(\.hooks)?"/ && cur !~ /azlog_cap/ && prev !~ /azlog_cap/ { print F ":" NR ": " cur }
            cur !~ /^[ \t]*$/ { prev = cur }' "$f"
    done; }
OFF=$(lint /p/usr/sbin/az-* /p/home/root/scripts/*.sh /d/az-usbload /d/boot.d/*.sh /d/lib/*)
chk "c6 no uncapped log append${OFF:+: $OFF}" '[ -z "$OFF" ]'
mkdir -p /tmp/lint; printf '%s\n' '#!/bin/sh' 'LOG=/tmp/x.log' 'log(){ echo "$*" >> "$LOG"; }' > /tmp/lint/planted.sh
chk "c6b CONTROL: a planted uncapped log() is reported" '[ -n "$(lint /tmp/lint/planted.sh)" ]'
