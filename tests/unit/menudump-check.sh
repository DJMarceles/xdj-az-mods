#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# menudump-check.sh <dumper> <golden.gz> <scratch cfg path> [intended.sed]
# Runs the Mod Menu dump (menudump_test.c) and compares it byte for byte with the golden the
# code before the feature-table refactor produced. intended.sed (optional) applies the listed
# intended changes to the golden first; anything else that differs fails.
set -uo pipefail
bin=$1 golden=$2 cfg=$3 intended=${4:-}
out=$(mktemp "${TMPDIR:-/tmp}/menudump.XXXXXX"); want=$(mktemp "${TMPDIR:-/tmp}/menudump.XXXXXX")
trap 'rm -f "$out" "$want"' EXIT
[ -x "$bin" ] || { echo "FAIL  menudump: the dumper exited 127 (not built: $bin)"; exit 127; }
"$bin" "$cfg" > "$out" || { echo "FAIL  menudump: the dumper exited $?"; exit 1; }
if [ -n "$intended" ]; then gzip -dc "$golden" | sed -f "$intended" > "$want"; else gzip -dc "$golden" > "$want"; fi
if cmp -s "$out" "$want"; then
    echo "PASS  menudump: $(grep -c '^ FRAME' "$out") frames, $(grep -c '^  [a-z]' "$out") states identical to $(basename "$golden")"
    exit 0
fi
echo "FAIL  menudump: differs from $(basename "$golden") ($(diff "$want" "$out" | grep -c '^[<>]') lines)"
diff "$want" "$out" | head -20 | sed 's/^/      /'
exit 1
