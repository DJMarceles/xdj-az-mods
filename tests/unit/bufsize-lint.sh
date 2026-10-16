#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# bufsize-lint.sh [GUEST_DIR]: a bounded write into a char array field of the menu state
# (guest/azmod/menu.h: char NAME[...][LEN] or char NAME[LEN]) names that field's own LEN or
# uses sizeof. A neighbor's macro compiles and runs, and cuts the text short (or overruns
# the field) only once the two sizes differ.
#
# Fails on, in guest/azmod/*.c and guest/azmod/dev/*.c:
#   snprintf(<...>NAME[...], OTHER_LEN, ...)    OTHER_LEN is not the field's own size
#   strncpy(<...>NAME[...], <src>, OTHER_LEN)
#
# Control: the same scan over a copy with a planted update_line write sized by
# AZMOD_INFO_LINE_LEN must report exactly that line.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
G=$(cd "${1:-$here/../../guest}" && pwd)

scan() { # dir -> prints violations, exit 1 if any
    local d=$1 files=()
    for f in "$d"/azmod/*.c "$d"/azmod/dev/*.c; do
        [ -f "$f" ] && files+=("$f")
    done
    perl -e '
        my ($hdr, @files) = @ARGV;
        open my $h, "<", $hdr or die "$hdr: $!";
        my %len;
        while (<$h>) {
            $len{$1} = $2 while /\bchar\s+(\w+)\s*(?:\[[^\]]*\]\s*)*\[\s*([A-Z_][A-Z0-9_]*)\s*\]\s*[;,]/g;
        }
        die "no char array fields found in $hdr\n" unless %len;
        my $bad = 0;
        for my $f (@files) {
            open my $c, "<", $f or die "$f: $!";
            while (my $l = <$c>) {
                my @hit;
                while ($l =~ /\bsnprintf\s*\(\s*[\w.>\-]*?\b(\w+)\s*(?:\[[^\]]*\]\s*)*,\s*([A-Z_][A-Z0-9_]*)\s*,/g) { push @hit, [$1, $2] }
                while ($l =~ /\bstrncpy\s*\(\s*[\w.>\-]*?\b(\w+)\s*(?:\[[^\]]*\]\s*)*,[^,]+,\s*([A-Z_][A-Z0-9_]*)\s*\)/g) { push @hit, [$1, $2] }
                for (@hit) {
                    my ($n, $m) = @$_;
                    next unless exists $len{$n} && $len{$n} ne $m;
                    $bad++; chomp $l;
                    printf "  %s:%d: %s is char [%s], written with %s\n      %s\n", $f, $., $n, $len{$n}, $m, $l;
                }
            }
        }
        exit($bad ? 1 : 0);
    ' "$d/azmod/menu.h" "${files[@]}"
}

fail=0
if out=$(scan "$G"); then
    echo "PASS  bufsize: every snprintf/strncpy into a menu state field uses that field's size"
else
    echo "FAIL  bufsize: writes sized by another field's macro:"; echo "$out"; fail=1
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/az-bufsize.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/azmod/dev"
cp "$G"/azmod/*.c "$G"/azmod/menu.h "$tmp/azmod/"; cp "$G"/azmod/dev/*.c "$tmp/azmod/dev/" 2>/dev/null
cat >> "$tmp/azmod/menu.c" <<'X'
static void planted(struct az_state *s) { snprintf(s->update_line[4], AZMOD_INFO_LINE_LEN, "Please wait"); }
X
cout=$(scan "$tmp"); crc=$?
n=$(printf '%s\n' "$cout" | grep -c 'planted')
if [ $crc -ne 0 ] && [ "$n" -eq 1 ]; then
    echo "CONTROL FAILED AS REQUIRED  bufsize: the planted update_line write sized by AZMOD_INFO_LINE_LEN is reported"
else
    echo "CONTROL DID NOT FAIL  bufsize: the planted write reported $n/1 (instrument broken)"; fail=1
fi
[ $fail = 0 ] && echo "BUFSIZE LINT: OK" || echo "BUFSIZE LINT: FAILED"
exit $fail
