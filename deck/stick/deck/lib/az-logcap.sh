# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=busybox
# az-logcap.sh: sourced (never run) by every deck script that appends to a log.
#
# azlog_cap FILE [MAX]: when FILE has grown past MAX bytes (default AZLOG_MAX, 64 KiB), move
# it to FILE.1, replacing the previous FILE.1. Call it before an append, so a log never holds
# more than about MAX plus one write, and the newest lines are always in FILE. /tmp is RAM on
# the deck and the scripts run for the whole session (the updater wakes every 30 minutes, the
# re-announce runs after every EP147 restart); p7 is 61 MB and holds the deck's settings, so it must never fill.
#
# Each script finds this file next to itself (deck/lib), in deck/lib above a boot.d hook, or in
# this boot's deck/ copy (/tmp/az-deck, which the loader has already checked against the signed
# trust.pins), and falls back to a no-op azlog_cap when none exists (a firmware copy running
# before any mods set was ever staged). The p7 cache is sourced only by the loader, and only
# after that copy verifies (cached_ok): this file runs as root.
AZLOG_MAX=${AZLOG_MAX:-65536}
azlog_cap() {
    _azl_n=$(wc -c < "$1" 2>/dev/null | tr -cd 0-9)
    [ "${_azl_n:-0}" -gt "${2:-$AZLOG_MAX}" ] && mv -f "$1" "$1.1" 2>/dev/null
    return 0
}
