#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# seqlock-lint.sh [drumroll.c]: the Beat Sync grid seqlock (drum_grid, guest/azstem/drumroll.c)
# carries the ordering a weakly ordered CPU needs, checked in the source.
#
# Why a source check and not a run: the fences only matter where the CPU or the compiler
# reorders. A run without them tears only when the hardware happens to reorder inside the
# window, so a threaded test of a fence-less copy can pass by chance (measured on macOS arm64:
# it passed 6 runs in 30; on a strongly ordered CPU it may never fail). ThreadSanitizer does not
# judge the fences either: the grid fields are relaxed atomics, which it never reports as a race,
# with or without the fences. What the code must say is fixed, so this checks exactly that:
#   writer (the function that stores seq + 1 and seq + 2):
#     - a release (or acq_rel / seq_cst) fence after the odd store and before the first field store;
#       (a field store is a plain assignment or an atomic_store_explicit to the field)
#     - the even store after the last field store, release or seq_cst;
#   reader (every function that loads seq twice):
#     - the first load acquire (or seq_cst);
#     - an acquire (or acq_rel / seq_cst) fence after the last field load and before the second.
# Comments are stripped first. Exit 1 on a missing ordering, 2 if no writer or no reader is
# found (the code moved: the lint must not pass by finding nothing).
set -euo pipefail
f=${1:-"$(dirname "$0")/../../guest/azstem/drumroll.c"}
F="$f" perl -0e '
my $src = do { local $/; open my $h, "<", $ENV{F} or die "seqlock-lint: $ENV{F}: $!\n"; <$h> };
$src =~ s{/\*.*?\*/}{}gs;
$src =~ s{//[^\n]*}{}g;
my $field = qr/g->(?:anchor_src|beat_src|valid)\b/;
# a field store: a plain assignment, or a relaxed atomic store to the field
my $fstore = qr/(?:$field\s*=(?!=)|atomic_store_explicit\(\s*&$field\s*,)/;
my $rel   = qr/atomic_thread_fence\(\s*memory_order_(?:release|acq_rel|seq_cst)\s*\)/;
my $acq   = qr/atomic_thread_fence\(\s*memory_order_(?:acquire|acq_rel|seq_cst)\s*\)/;
my ($writers, $readers, $bad) = (0, 0, 0);
# every top-level function: "static ... name(...) {" up to the first line that is exactly "}"
while ($src =~ /^static[^\n;=]*?\b(\w+)\(([^)]*)\)\s*\{\n(.*?)^\}$/msg) {
    my ($name, $body) = ($1, $3);
    next unless $body =~ /&g->seq\b/;
    my @odd = ($body =~ /atomic_store_explicit\(&g->seq,\s*s \+ 1,/g);
    my @lds = ();
    while ($body =~ /atomic_load_explicit\(&g->seq,\s*memory_order_(\w+)\)/g) {
        push @lds, [$-[0], $+[0], $1];
    }
    if (@odd) {
        $writers++;
        my $o = index($body, "atomic_store_explicit(&g->seq, s + 1,");
        my $o_end = index($body, ";", $o);
        my $after = substr($body, $o_end);
        if ($after !~ /$fstore/) { print "FAIL  seqlock-lint: $name: no field store after the odd store\n"; $bad++; next; }
        my $first_store = $-[0];
        if (substr($after, 0, $first_store) !~ $rel) {
            print "FAIL  seqlock-lint: $name: no release fence between the odd store and the first field store\n"; $bad++;
        }
        my $last_store = 0;
        while ($body =~ /$fstore/g) { $last_store = $-[0]; }
        my $tail = substr($body, $last_store);
        if ($tail !~ /atomic_store_explicit\(&g->seq,\s*s \+ 2,\s*memory_order_(?:release|seq_cst)\)/) {
            print "FAIL  seqlock-lint: $name: the even store after the fields is not release\n"; $bad++;
        }
    } elsif (@lds >= 2) {
        $readers++;
        my ($l1, $l2) = ($lds[0], $lds[-1]);
        if ($l1->[2] !~ /^(?:acquire|seq_cst)$/) {
            print "FAIL  seqlock-lint: $name: the first load of seq is $l1->[2], not acquire\n"; $bad++;
        }
        my $mid = substr($body, $l1->[1], $l2->[0] - $l1->[1]);
        my $last_load = -1;
        while ($mid =~ /$field(?!\s*=[^=])/g) { $last_load = $+[0]; }
        if ($last_load < 0) { print "FAIL  seqlock-lint: $name: no field load between the two loads of seq\n"; $bad++; next; }
        if (substr($mid, $last_load) !~ $acq) {
            print "FAIL  seqlock-lint: $name: no acquire fence between the last field load and the second load of seq\n"; $bad++;
        }
    }
}
if (!$writers || !$readers) {
    print "seqlock-lint: found $writers writer(s) and $readers reader(s) of g->seq in $ENV{F}: the code moved, update the lint\n";
    exit 2;
}
exit 1 if $bad;
print "PASS  seqlock-lint: drum grid seqlock ordered ($writers writer, $readers readers checked)\n";
'
