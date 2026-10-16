/* SPDX-License-Identifier: GPL-3.0-only */
/* decks_classify + decks_verdict (extracted from azstem/deck_track_map.c by run.sh):
 * what azstem publishes in /tmp/az-decks, from which the USB loader decides whether it may
 * restart EP147 (az-usbload RESTART POLICY) and the Mod Menu whether Reload must confirm.
 * A deck is empty only when its TrackID was zero on two samples in a row (a different song
 * passes through zero for ~200 ms); a refused read is unknown; unknown is never "empty". */
#include <string.h>
#include DECKS_INC
#include "check.h"

/* drive decks_classify through a sequence of samples like decks_tick does; returns the
 * state after the last one */
static char run(const int *ok, const int *zero, int n) {
    int prev = 0;
    char st = '?';
    for (int i = 0; i < n; i++) {
        st = decks_classify(ok[i], zero[i], prev);
        prev = ok[i] && zero[i];
    }
    return st;
}

int main(void) {
    ut_suite = "decks";
    CHECK("a loaded deck reads L", decks_classify(1, 0, 0) == 'L');
    CHECK("a loaded deck reads L whatever came before", decks_classify(1, 0, 1) == 'L');
    CHECK("a refused read is U", decks_classify(0, 0, 0) == 'U' && decks_classify(0, 1, 1) == 'U');
    CHECK("the first zero is U, not E (a song change passes through zero)", decks_classify(1, 1, 0) == 'U');
    CHECK("zero twice in a row is E", decks_classify(1, 1, 1) == 'E');
    {
        int ok[] = {1, 1, 1}, z[] = {0, 1, 0}; /* loaded, ~200 ms zero, the next song */
        CHECK("loaded -> one zero -> loaded never reads E", run(ok, z, 3) == 'L' && run(ok, z, 2) == 'U');
    }
    {
        int ok[] = {1, 0, 1}, z[] = {1, 1, 1}; /* zero, refused, zero: the refusal breaks the run */
        CHECK("a refused read between two zeros is not two zeros in a row", run(ok, z, 3) == 'U');
    }
    {
        int ok[] = {1, 1, 1}, z[] = {0, 1, 1}; /* unloaded and it stays unloaded */
        CHECK("an unload reads E from the second sample on", run(ok, z, 3) == 'E');
    }
    CHECK("verdict: every deck empty -> 0", decks_verdict("EEEE", 4) == 0);
    CHECK("verdict: a loaded deck -> 1", decks_verdict("EELE", 4) == 1);
    CHECK("verdict: a loaded deck wins over unknown -> 1", decks_verdict("UULU", 4) == 1);
    CHECK("verdict: one unknown deck -> -1 (never empty)", decks_verdict("EEEU", 4) == -1);
    CHECK("verdict: all unknown -> -1", decks_verdict("UUUU", 4) == -1);
    CHECK("verdict: any other byte counts as unknown", decks_verdict("EE?E", 4) == -1);
    return ut_summary();
}
