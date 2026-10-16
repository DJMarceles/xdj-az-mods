/* SPDX-License-Identifier: GPL-3.0-only */
/* deck_slot (extracted out of guest/azstem/stem_playback.c by run.sh): which deck an audio unit
 * that reaches processBlockInternal is. Only by identity:
 *   - a unit seeded into its deck's slot (by the DjPlayerUnit constructor hook) is that deck;
 *   - a unit whose player number can be read takes its own deck's slot, once, and keeps it;
 *   - a unit with no readable number (an AudioRecorderUnit) is unknown (-1), never the next
 *     free slot;
 *   - a second unit claiming a deck already held is unknown and the conflict is recorded:
 *     never two units mixed as one deck;
 *   - units of four decks racing for their slots each get exactly their own.
 * ap_deck_of (which reads EP147's object) is a stand-in table here. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include "check.h"

#define NDECK 4
#define MAX_DECKS 8
static _Atomic(void *) g_deck_self[MAX_DECKS];
static _Atomic unsigned g_ap_seen, g_ap_conflict;
#include STEMSLOT_INC /* the STEM_SLOT table */

/* the units: [0..3] decks 1-4, [4..5] recorder units (no number), [6..9] a second unit of each deck */
static char g_unit[10][64];
static int ap_deck_of(void *self) {
    for (int i = 0; i < 10; i++)
        if (self == g_unit[i])
            return i < 4 ? i : i < 6 ? -1
                                     : i - 6;
    return -1;
}
#include DECKSLOT_INC

static void reset(void) {
    for (int i = 0; i < MAX_DECKS; i++)
        atomic_store(&g_deck_self[i], NULL);
    atomic_store(&g_ap_seen, 0);
    atomic_store(&g_ap_conflict, 0);
}
static void *racer(void *arg) {
    int d = (int) (intptr_t) arg, bad = 0;
    for (int k = 0; k < 2000; k++)
        bad += deck_slot(g_unit[d]) != STEM_SLOT[d];
    return (void *) (intptr_t) bad;
}

int main(void) {
    ut_suite = "deck slot";
    reset();
    atomic_store(&g_deck_self[STEM_SLOT[2]], g_unit[2]);
    CHECK("a unit seeded by its constructor is its deck", deck_slot(g_unit[2]) == STEM_SLOT[2]);
    CHECK("a unit with a readable number takes its own deck's slot",
          deck_slot(g_unit[1]) == STEM_SLOT[1] && atomic_load(&g_deck_self[STEM_SLOT[1]]) == g_unit[1]);
    CHECK("... and keeps it", deck_slot(g_unit[1]) == STEM_SLOT[1] && (atomic_load(&g_ap_seen) & 2));
    void *before[MAX_DECKS];
    for (int i = 0; i < MAX_DECKS; i++)
        before[i] = atomic_load(&g_deck_self[i]);
    int same = 1;
    CHECK("a recorder unit (no number) is unknown", deck_slot(g_unit[4]) == -1 && deck_slot(g_unit[5]) == -1);
    for (int i = 0; i < MAX_DECKS; i++)
        same &= before[i] == atomic_load(&g_deck_self[i]);
    CHECK("... and takes no slot (never the next free one)", same);
    CHECK("a second unit for a deck already held is unknown", deck_slot(g_unit[7]) == -1);
    CHECK("... the conflict is recorded for that deck", atomic_load(&g_ap_conflict) == 2u);
    CHECK("... and the slot still belongs to the first", atomic_load(&g_deck_self[STEM_SLOT[1]]) == g_unit[1]);
    CHECK("an object no table knows is unknown", deck_slot(&same) == -1);

    reset();
    pthread_t t[4];
    for (int d = 0; d < 4; d++)
        pthread_create(&t[d], NULL, racer, (void *) (intptr_t) d);
    int bad = 0;
    for (int d = 0; d < 4; d++) {
        void *r;
        pthread_join(t[d], &r);
        bad += (int) (intptr_t) r;
    }
    int own = 1;
    for (int d = 0; d < 4; d++)
        own &= atomic_load(&g_deck_self[STEM_SLOT[d]]) == g_unit[d];
    CHECK("four decks racing for their slots: each always gets its own", bad == 0 && own);
    CHECK("... with no conflict recorded", atomic_load(&g_ap_conflict) == 0 && atomic_load(&g_ap_seen) == 15u);
    return ut_summary();
}
