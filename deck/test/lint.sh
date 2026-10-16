#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Static check of every deck script (the trees the find below lists) and every test script
# (test/*.sh, test/stub/*) with shellcheck, as busybox ash, the shell the deck and the
# test container run them in. Runs on the host through docker, like the suites, from a
# pinned image so every machine gets the same verdict. Settings: ../.shellcheckrc.
#
# The scripts are found by their first line, so a new one is checked without being
# listed here. The control in the same round is a copy of a real deck script with one
# planted defect: shellcheck must refuse it, or this check proves nothing.
cd "$(dirname "$0")/.." || exit 1   # tools/release
IMG=koalaman/shellcheck:v0.10.0@sha256:2097951f02e735b613f4a34de20c40f937a6c8f18ecb170612c88c34517221fb
sc() { MSYS_NO_PATHCONV=1 docker run --rm -v "$PWD:/mnt" -w /mnt "$IMG" -x -s busybox "$@"; }

FILES=$(
    for f in $(find payload stick dev-deck -type f ! -path '*/.*' | sort); do
        case "$(head -n 1 "$f" 2>/dev/null)" in
            ('#!/bin/sh'*|'#!/bin/ash'*|'# shellcheck shell='*) echo "$f" ;;
        esac
    done
    ls test/*.sh test/stub/*
)
n=$(echo "$FILES" | grep -c .)
[ "$n" -ge 20 ] || { echo "FAIL lint: found only $n scripts (the discovery is broken)"; exit 1; }

rc=0  # every FAIL line below also makes the exit status 1, for a standalone run
# shellcheck disable=SC2086 # one path per word: none of these names has a space
if out=$(sc $FILES 2>&1); then
    echo "PASS lint: shellcheck clean over $n scripts"
else
    printf '%s\n' "$out" | sed -n '1,60s/^/    /p'
    echo "FAIL lint: shellcheck findings (above) in the deck or test scripts"
    rc=1
fi

# control: a deck script with an unquoted expansion in a test must be refused
T=$(mktemp -d ./.lint.XXXXXX) || { echo "FAIL lint: no temp dir"; exit 1; }
trap 'rm -rf "$T"' EXIT
cp stick/deck/boot.d/05-keep-testmode.sh "$T/planted.sh"
printf '%s\n' '[ -n $AZ_LINT_PLANTED ] && echo planted' >> "$T/planted.sh"
if sc "$T/planted.sh" >/dev/null 2>&1; then
    echo "FAIL lint CONTROL: a planted unquoted expansion passed (the check cannot fail)"
    rc=1
else
    echo "PASS lint CONTROL: a planted unquoted expansion is refused"
fi
exit "$rc"
