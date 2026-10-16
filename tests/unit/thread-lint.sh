#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# thread-lint.sh [GUEST_DIR]: every thread azmod/azstem start goes through az_thread_create
# (guest/common/azaltstack.h), which gives it an alternate signal stack: the crash reporter's
# handlers are SA_ONSTACK, and SA_ONSTACK does nothing on a thread that never called
# sigaltstack, so a stack overflow there would leave no record.
#
# Fails on a call to pthread_create( in guest/azmod/*.c, guest/azmod/dev/*.c, guest/azstem/*.c
# or guest/common/*.h (comments and strings ignored), except a line marked "altstack-ok:".
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
G=$(cd "${1:-$here/../../guest}" && pwd)
files=()
for f in "$G"/azmod/*.c "$G"/azmod/dev/*.c "$G"/azstem/*.c "$G"/common/*.h; do
    [ -f "$f" ] && files+=("$f")
done
out=$(perl -ne '
    $ok = /altstack-ok:/;
    $l = $_;
    if ($inc) { if ($l =~ s{^.*?\*/}{}) { $inc = 0 } else { $l = "" } }
    $l =~ s{/\*.*?\*/}{}g;
    if ($l =~ s{/\*.*$}{}) { $inc = 1 }
    $l =~ s/"(?:[^"\\]|\\.)*"/""/g;
    $l =~ s{//.*$}{};
    print "$ARGV:$.: raw pthread_create (use az_thread_create)\n" if !$ok && $l =~ /\bpthread_create\s*\(/;
    close ARGV if eof;' "${files[@]}")
if [ -n "$out" ]; then
    echo "$out" | sed "s|^$G/|FAIL  |"
    exit 1
fi
echo "PASS  thread lint: every thread starts through az_thread_create (alternate signal stack)"
