/* SPDX-License-Identifier: GPL-3.0-only */
/* Stem Volume and PRO DJ LINK players (guest/azmod/link_members.h): CH3/CH4 EQ sets stem
 * volumes only while both channels are free, and a link player holding number 3 or 4 owns its
 * channel. The member set is written by EP147's link thread and read by the overlay thread. */
#include <pthread.h>
#include <stdio.h>
#include "link_members.h"
#include "check.h"

static uint64_t g_set;
static int g_stop;

static void *churn(void *arg) {
    (void) arg;
    for (int i = 0; i < 200000; i++) {
        lm_note(&g_set, 3, i & 1);
        lm_note(&g_set, 17, !(i & 1));
    }
    __atomic_store_n(&g_stop, 1, __ATOMIC_RELEASE);
    return NULL;
}

int main(void) {
    ut_suite = "stem volume link";
    const uint64_t P3 = 1ull << 3, P4 = 1ull << 4, P1 = 1ull << 1, P2 = 1ull << 2;

    CHECK("all free: CH3/CH4 set stem volumes", !stemvol_stand_down(1, 0, 0, 1, 0));
    CHECK("the AZ's own players 1 and 2 leave the channels free",
          !stemvol_stand_down(1, 0, 0, 1, P1 | P2));
    CHECK("a rekordbox laptop (17) leaves the channels free",
          !stemvol_stand_down(1, 0, 0, 1, 1ull << 17));
    CHECK("link player 3 present: stand down", stemvol_stand_down(1, 0, 0, 1, P3));
    CHECK("link player 4 present: stand down", stemvol_stand_down(1, 0, 0, 1, P4));
    CHECK("link players 3 and 4: stand down", stemvol_stand_down(1, 0, 0, 1, P3 | P4));
    CHECK("internal deck 3 has a track: stand down", stemvol_stand_down(1, 1, 0, 1, 0));
    CHECK("internal deck 4 has a track: stand down", stemvol_stand_down(1, 0, 1, 1, 0));
    CHECK("no deck-track export: unknown, stand down", stemvol_stand_down(0, 0, 0, 1, 0));
    CHECK("member watch not installed: unknown, stand down", stemvol_stand_down(1, 0, 0, 0, 0));

    uint64_t s = 0;
    lm_note(&s, 3, 1);
    CHECK("a join records the number", s == P3);
    lm_note(&s, 4, 1);
    lm_note(&s, 3, 0);
    CHECK("a leave clears only its number", s == P4);
    lm_note(&s, 0, 1);
    lm_note(&s, 64, 1);
    lm_note(&s, 0xffffffffu, 1);
    CHECK("numbers 0 and above 63 are never recorded", s == P4);
    lm_note(&s, 4, 0);
    lm_note(&s, 4, 0);
    CHECK("a second leave is harmless", s == 0);

    pthread_t t;
    pthread_create(&t, NULL, churn, NULL);
    int bad = 0;
    while (!__atomic_load_n(&g_stop, __ATOMIC_ACQUIRE)) {
        uint64_t v = __atomic_load_n(&g_set, __ATOMIC_ACQUIRE);
        if (v & ~(P3 | (1ull << 17)))
            bad++;
    }
    pthread_join(t, NULL);
    CHECK("joins and leaves from another thread touch only their own bits", bad == 0);
    CHECK("the last join and leave stand (3 in, 17 out)", g_set == P3);
    return ut_summary();
}
