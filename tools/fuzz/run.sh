#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# tools/fuzz/run.sh: libFuzzer harnesses for the parsers that read untrusted input
# (tools/fuzz/lib.sh lists them and what each one feeds).
#
#   run.sh <name|all> <seconds>   fuzz each named harness for <seconds>, one process (no -jobs),
#                                 starting from tools/fuzz/corpus/<name>/; new coverage is kept
#                                 in a scratch corpus, crashes are written to $FZ_OUT (default: a
#                                 new temp directory, printed) and fail the run
#   run.sh --replay <name|all>    run every input in tools/fuzz/corpus/<name>/ once under
#                                 ASan+UBSan (any host; libFuzzer not needed)
#   run.sh --list                 the harness names
#
# In the published source layout, run it from the tree tests/run.sh assembles (tests/.work/tree).
# Fuzzing needs clang-10 with libFuzzer (docker/azmod-test.Dockerfile). On a host without it,
# run.sh re-runs itself inside azmod-test:18.04 with the repository mounted at the same path.
set -euo pipefail
cd "$(dirname "$0")/../.."
if [ ! -d guest ]; then
    echo "tools/fuzz/run.sh runs on the development layout, which the published source does not have." >&2
    echo "Run tests/run.sh: it assembles that layout in tests/.work/tree and replays every corpus;" >&2
    echo "then fuzz there: tests/.work/tree/tools/fuzz/run.sh <name|all> <seconds>" >&2
    exit 2
fi
. tools/fuzz/lib.sh
usage() { awk 'NR > 1 && !/^#/ {exit} NR > 1 && !/SPDX-License/ {sub(/^# ?/, ""); print}' "$0"; exit 2; }
[ $# -ge 1 ] || usage
case $1 in
--list) echo "$FZ_NAMES"; exit 0 ;;
--replay) mode=replay; [ $# -eq 2 ] || usage; names=$2 ;;
*) mode=fuzz; [ $# -eq 2 ] || usage; names=$1; secs=$2 ;;
esac
[ "$names" = all ] && names=$FZ_NAMES
for n in $names; do
    case " $FZ_NAMES " in *" $n "*) ;; *) echo "unknown harness: $n (run.sh --list)"; exit 2 ;; esac
done

if [ $mode = fuzz ] && ! fz_has_libfuzzer; then
    img=${FZ_IMG:-azmod-test:18.04}
    [ "$(uname -s)" = Linux ] && [ -f /.dockerenv ] && { echo "clang-10/libFuzzer missing in this container: rebuild $img"; exit 2; }
    exec docker run --rm --platform=linux/arm64 -v "$PWD:$PWD" -w "$PWD" ${FZ_OUT:+-v "$FZ_OUT:$FZ_OUT"} \
        ${FZ_OUT:+-e FZ_OUT="$FZ_OUT"} "$img" bash tools/fuzz/run.sh "$@"
fi

W=$(mktemp -d "${TMPDIR:-/tmp}/azfuzz-build.XXXXXX")
trap 'rm -rf "$W"' EXIT
OUTD=${FZ_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/azfuzz-out.XXXXXX")}
mkdir -p "$OUTD"
rc=0
# fz_fail <label> <why> [log]: one FAIL line (plus the end of the log, indented) and a non-zero exit
fz_fail() {
    echo "FAIL  fuzz $1: $2"
    [ -z "${3:-}" ] || [ ! -s "$3" ] || tail -30 "$3" | sed 's/^/      /'
    rc=1
}
# Every harness is reported: a step that cannot finish (extract, build, run) is a FAIL for that
# harness, never a silent stop, so one broken harness cannot hide behind a passing run.
for n in $names; do
    mkdir -p "$W/$n"
    if [ $mode = replay ]; then label="replay $n"; else label=$n; fi
    if ! fz_prepare "$n" "$W/$n/src" > "$W/$n/prepare.log" 2>&1; then
        fz_fail "$label" "could not prepare the source" "$W/$n/prepare.log"; continue
    fi
    if [ ! -d "tools/fuzz/corpus/$n" ]; then
        fz_fail "$label" "no seed corpus at tools/fuzz/corpus/$n"; continue
    fi
    if [ $mode = replay ]; then
        if ! fz_compile "$n" "$W/$n/src" "$W/$n/bin" replay > "$W/$n/build.log" 2>&1 || [ ! -x "$W/$n/bin" ]; then
            fz_fail "$label" "build failed" "$W/$n/build.log"; continue
        fi
        if "$W/$n/bin" "tools/fuzz/corpus/$n" > "$W/$n/log" 2>&1; then
            echo "PASS  fuzz replay $n: $(tail -1 "$W/$n/log")"
        else
            fz_fail "$label" "a corpus input failed" "$W/$n/log"
        fi
        continue
    fi
    if ! fz_compile "$n" "$W/$n/src" "$W/$n/bin" fuzz > "$W/$n/build.log" 2>&1 || [ ! -x "$W/$n/bin" ]; then
        fz_fail "$label" "build failed" "$W/$n/build.log"; continue
    fi
    mkdir -p "$OUTD/$n/corpus" "$OUTD/$n/crashes"
    echo "=== fuzz $n: ${secs}s (scratch corpus $OUTD/$n/corpus, crashes $OUTD/$n/crashes) ==="
    # the seeds are read, never written: new inputs go to the scratch corpus
    if "$W/$n/bin" -max_total_time="$secs" -timeout=10 -rss_limit_mb=1536             -print_final_stats=1 -artifact_prefix="$OUTD/$n/crashes/" ${FZ_ARGS_EXTRA:-}             "$OUTD/$n/corpus" "tools/fuzz/corpus/$n" > "$OUTD/$n/fuzz.log" 2>&1; then
        echo "PASS  fuzz $n: $(grep -E '^#[0-9]+.*DONE' "$OUTD/$n/fuzz.log" | tail -1)"
        grep -E 'stat::number_of_executed_units|stat::new_units_added' "$OUTD/$n/fuzz.log" | sed 's/^/      /'
    else
        echo "FAIL  fuzz $n: see $OUTD/$n/fuzz.log"
        grep -E 'ERROR|runtime error|INVARIANT|SUMMARY|Test unit written' "$OUTD/$n/fuzz.log" | head -12 | sed 's/^/      /'
        rc=1
    fi
done
[ $mode = replay ] || echo "fuzz output kept in $OUTD"
exit $rc
