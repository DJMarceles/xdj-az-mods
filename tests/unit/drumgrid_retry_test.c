/* SPDX-License-Identifier: GPL-3.0-only */
/* Drum Roll beat sync, the grid service (azstem/drumroll.c drum_grid_service, extracted by
 * run.sh), driven pass by pass against a model of EP147's grid: a deck's grid can appear some
 * passes after its track change, as it does for a streamed track loaded for the first time.
 *
 * The grid is invalidated on every track change and asked for again on every pass until it is
 * there; a later track change starts over; Beat Sync off asks for nothing. */
#include <stdatomic.h>
#include <stdio.h>
#define DRUM_SIDES 4
#define NDECK 4
#define DRUM_SR 44100
#define DRUM_SYNCOFF_PATH "/nonexistent/azstem.drumsyncoffset"
static _Atomic unsigned g_deck_trackgen[NDECK];
static _Atomic int g_drum_sync_on = 1;
static _Atomic int g_grid_demand = 0;
static _Atomic long g_drum_sync_off = 0;
static unsigned g_drum_grid_seen[DRUM_SIDES];
static int g_drum_grid_retry[DRUM_SIDES];
static int g_drum_grid_init = 0;

/* EP147, as the service sees it: the grid of a deck is there or not, and is asked for */
static int ep_ready[DRUM_SIDES], ep_asks[DRUM_SIDES];
static int drum_grid_native(int deck, long long *anchor, double *beat) {
    ep_asks[deck]++;
    if (!ep_ready[deck])
        return 0;
    *anchor = 100 * (deck + 1);
    *beat = 20671.9;
    return 1;
}
/* what the RT side reads */
static int pub_valid[DRUM_SIDES], pub_count[DRUM_SIDES];
static long long pub_anchor[DRUM_SIDES];
static void drum_grid_publish(int side, int valid, long long anchor, double beat) {
    (void) beat;
    pub_valid[side] = valid;
    pub_anchor[side] = anchor;
    pub_count[side]++;
}
static int log_lines;
static void logln(const char *fmt, ...) {
    (void) fmt;
    log_lines++;
}

#include GRID_INC

#include "check.h"

static void load(int d) {
    atomic_fetch_add(&g_deck_trackgen[d], 1u);
}
static void passes(int n) {
    while (n-- > 0)
        drum_grid_service();
}
static void asks_reset(void) {
    for (int d = 0; d < DRUM_SIDES; d++)
        ep_asks[d] = 0;
}

int main(void) {
    passes(1); /* start-up: every deck counts as changed, all empty */
    CHECK("start-up: no grid, free-running", !pub_valid[0] && !pub_valid[1]);

    ep_ready[0] = 1; /* a track already on deck 1 whose grid arrives after start-up */
    passes(1);
    CHECK("start-up track: its grid is picked up once it is there", pub_valid[0] &&
                                                                        pub_anchor[0] == 100);

    ep_ready[0] = 0; /* a streamed track loaded for the first time: no grid yet */
    load(0);
    passes(1);
    CHECK("new track, grid not there yet: invalidated, free-running", !pub_valid[0]);
    passes(5);
    CHECK("still no grid: still free-running, no stale grid", !pub_valid[0]);
    ep_ready[0] = 1; /* EP147 has analysed it */
    passes(1);
    CHECK("the grid arrives later: LOCKED without another track change", pub_valid[0]);

    int count = pub_count[0];
    asks_reset();
    passes(10);
    CHECK("once locked: not asked again, not republished", ep_asks[0] == 0 && pub_count[0] == count);

    load(0); /* the next track, its grid there at once */
    passes(1);
    CHECK("next track with its grid ready: LOCKED on the change", pub_valid[0]);

    ep_ready[1] = 0; /* deck 2: a track whose grid never comes (or an empty deck) */
    load(1);
    passes(1);
    asks_reset();
    passes(10);
    CHECK("no grid at all: asked on every pass, stays free-running", ep_asks[1] == 10 &&
                                                                         !pub_valid[1]);
    CHECK("other decks unaffected", pub_valid[0] && ep_asks[0] == 0);

    int lines = log_lines;
    passes(50);
    CHECK("asking again does not flood the log", log_lines == lines);

    load(1); /* a track change while it was waiting: starts over, the old track's wait ends */
    ep_ready[1] = 1;
    passes(1);
    CHECK("track change while waiting: the new track's grid", pub_valid[1] && pub_anchor[1] == 200);
    asks_reset();
    passes(5);
    CHECK("then no more asking", ep_asks[1] == 0);

    ep_ready[2] = 0; /* Beat Sync off: nothing asked, the change is kept for later */
    load(2);
    atomic_store(&g_drum_sync_on, 0);
    asks_reset();
    passes(5);
    CHECK("Beat Sync off: nothing asked", ep_asks[2] == 0);
    ep_ready[2] = 1;
    atomic_store(&g_drum_sync_on, 1);
    passes(1);
    CHECK("Beat Sync on again: the waiting track's grid", pub_valid[2]);

    ep_ready[3] = 0; /* waiting, then Beat Sync off and on: still waiting, then locked */
    load(3);
    passes(2);
    atomic_store(&g_drum_sync_on, 0);
    passes(3);
    ep_ready[3] = 1;
    atomic_store(&g_drum_sync_on, 1);
    passes(1);
    CHECK("waiting across Beat Sync off and on: LOCKED", pub_valid[3]);
    return ut_summary();
}
