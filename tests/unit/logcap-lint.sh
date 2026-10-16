#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# logcap-lint.sh [GUEST_DIR]: every file azmod/azstem append to is size-capped. /tmp on the
# deck is RAM, and an append-only file outlives every EP147 restart, so a writer without a
# cap (a record per crash, a line per start, a diagnostic left on) grows until it takes
# memory from EP147.
#
# Fails on, in guest/azmod/*.c, guest/azmod/dev/*.c, guest/azstem/*.c, guest/common/*.h:
#   fopen(..., "a...")           an append-mode fopen (use azlog_open or azlog_fopen_capped)
#   open(... O_APPEND ...)       an append-mode open
# A writer with its own cap carries the marker "logcap-ok: <why>" on the same line.
#
# Control: the same scan over a copy with a planted fopen "ae" and a planted O_APPEND open
# must report exactly those two.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
G=$(cd "${1:-$here/../../guest}" && pwd)

scan() { # dir -> prints violations, exit 1 if any
    local d=$1 files=()
    for f in "$d"/azmod/*.c "$d"/azmod/dev/*.c "$d"/azstem/*.c "$d"/common/*.h; do
        [ -f "$f" ] && files+=("$f")
    done
    perl -ne '
        BEGIN { $bad = 0 }
        $ok = /logcap-ok:/;
        $l = $_;
        if ($inc) { if ($l =~ s{^.*?\*/}{}) { $inc = 0 } else { $l = "" } }
        $l =~ s{/\*.*?\*/}{}g;
        if ($l =~ s{/\*.*$}{}) { $inc = 1 }
        $l =~ s{//.*$}{} unless $l =~ m{"[^"]*//};
        $l = "" if $ok;
        my @why;
        while ($l =~ /\bfopen\s*\(((?:[^;"]|"[^"]*")*?),\s*"(a[^"]*)"\s*\)/g) { push @why, "fopen mode \"$2\" appends without a cap" }
        (my $c = $l) =~ s/"(?:[^"\\]|\\.)*"/""/g;
        if ($c =~ /(?<![\w.>])open(?:64|at)?\s*\(/ && $c =~ /O_APPEND/) { push @why, "open with O_APPEND without a cap" }
        if (@why) { $bad++; chomp; printf "  %s:%d: %s\n      %s\n", $ARGV, $., join("; ", @why), $_ }
        if (eof) { close ARGV; $inc = 0 }
        END { exit($bad ? 1 : 0) }
    ' "${files[@]}"
}

fail=0
if out=$(scan "$G"); then
    echo "PASS  logcap: every append in azmod + azstem is size-capped"
else
    echo "FAIL  logcap: appends without a size cap:"; echo "$out"; fail=1
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/az-logcap.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/azmod/dev" "$tmp/azstem" "$tmp/common"
cp "$G"/azmod/*.c "$tmp/azmod/"; cp "$G"/azmod/dev/*.c "$tmp/azmod/dev/" 2>/dev/null
cp "$G"/azstem/*.c "$tmp/azstem/"; cp "$G"/common/*.h "$tmp/common/"
cat >> "$tmp/azmod/menu.c" <<'X'
static void planted_a(void) { FILE *f = fopen("/tmp/azmod-x.log", "ae"); if (f) fclose(f); }
static void planted_b(void) { int fd = open("/tmp/x", O_WRONLY | O_APPEND | O_CLOEXEC); if (fd >= 0) close(fd); }
X
cout=$(scan "$tmp"); crc=$?
n=$(printf '%s\n' "$cout" | grep -c 'planted_')
if [ $crc -ne 0 ] && [ "$n" -eq 2 ]; then
    echo "CONTROL FAILED AS REQUIRED  logcap: planted fopen \"ae\" and O_APPEND open both reported"
else
    echo "CONTROL DID NOT FAIL  logcap: planted violations reported $n/2 (instrument broken)"; fail=1
fi
[ $fail = 0 ] && echo "LOGCAP LINT: OK" || echo "LOGCAP LINT: FAILED"
exit $fail
