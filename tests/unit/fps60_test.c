/* SPDX-License-Identifier: GPL-3.0-only */
/* The 60 FPS group (features.def FPS60: a GROUP whose left checkbox is a bypass with memory,
 * azfeatures.c azf_fps60_bypass / azf_fps60_member_changed / the load's normalization).
 *   - every starting state the UI or a 1.2.2 azmod.cfg can produce of slots 4, 19, 30, 31
 *     (60 FPS Waveforms, 60 FPS UI, and the group's memory of each): off -> on -> off and
 *     on -> off -> on return to the exact starting state, members and memory (a 1.2.2
 *     memory of both, (0,0,1,1), loads as the empty memory, which means the same);
 *   - after every tap the members (what the deck runs) equal 1.2.2's logic, run as the oracle
 *     (fps60_oracle.c over legacy-1.2.2/fps60.inc);
 *   - a member toggled by hand clears the memory (as 1.2.2 did).
 * -DOLD_AS_IMPL runs 1.2.2's logic as the implementation: the byte round trip must FAIL from
 * the dark-empty start (members off, nothing saved), which 1.2.2 does not return to. */
#include "menu.h"
#include <string.h>
#include "check.h"
void oracle_group_tap(int st[4]);
void oracle_hand_toggle(int st[4], int member);

static const int ID[4] = {AZF_FPS60_WAVEFORMS, AZF_FPS60_UI, AZF_FPS60_SAVED_WAVEFORMS, AZF_FPS60_SAVED_UI};
static void put(const int st[4]) {
    for (int k = 0; k < 4; k++) __atomic_store_n(&az_val[ID[k]], st[k], __ATOMIC_RELAXED);
    az_after_load(); /* what any load does after storing the values */
}
static void get(int st[4]) {
    for (int k = 0; k < 4; k++) st[k] = az_get(ID[k]);
}
static void tap(int st[4]) {
#ifdef OLD_AS_IMPL
    oracle_group_tap(st);
#else
    put(st);
    const struct az_row *g = az_row(AZF_FPS60);
    g->d.on_activate(g->d.user); /* the left checkbox: menu.c c_group_tap */
    get(st);
#endif
}
static int same(const int a[4], const int b[4]) { return !memcmp(a, b, 4 * sizeof(int)); }
static int members_same(const int a[4], const int b[4]) { return a[0] == b[0] && a[1] == b[1]; }

int main(void) {
    ut_suite = "60 FPS group";
    az_defaults();
    /* the reachable states: members anything with nothing saved; members off with one
     * saved; and 1.2.2's "both saved" */
    /* clang-format off */
    static const int START[][4] = {{0, 0, 0, 0}, {1, 0, 0, 0}, {0, 1, 0, 0}, {1, 1, 0, 0},
                                   {0, 0, 1, 0}, {0, 0, 0, 1}, {0, 0, 1, 1}};
    /* clang-format on */
    char name[160];
    for (size_t k = 0; k < sizeof START / sizeof START[0]; k++) {
        int st[4], ref[4], loaded[4];
        memcpy(st, START[k], sizeof st);
        memcpy(ref, START[k], sizeof ref);
#ifdef OLD_AS_IMPL
        memcpy(loaded, START[k], sizeof loaded);
#else
        put(st);
        get(loaded); /* the state as a load leaves it */
#endif
        int legacy_both = START[k][2] && START[k][3];
        snprintf(name, sizeof name, "start %d%d%d%d loads as itself%s", START[k][0], START[k][1],
                 START[k][2], START[k][3], legacy_both ? " (1.2.2's 'both saved': as the empty memory)" : "");
        static const int EMPTY_OFF[4] = {0, 0, 0, 0};
        CHECK(name, legacy_both ? same(loaded, EMPTY_OFF) : same(loaded, START[k]));
        memcpy(st, loaded, sizeof st);
        tap(st);
        oracle_group_tap(ref);
        snprintf(name, sizeof name, "start %d%d%d%d, tap 1: members as 1.2.2", START[k][0], START[k][1], START[k][2], START[k][3]);
        CHECK(name, members_same(st, ref));
        tap(st);
        oracle_group_tap(ref);
        snprintf(name, sizeof name, "start %d%d%d%d, tap 2: members as 1.2.2", START[k][0], START[k][1], START[k][2], START[k][3]);
        CHECK(name, members_same(st, ref));
        snprintf(name, sizeof name, "start %d%d%d%d: two taps return to the exact start (members and memory)",
                 START[k][0], START[k][1], START[k][2], START[k][3]);
        CHECK(name, same(st, loaded));
        tap(st);
        oracle_group_tap(ref);
        snprintf(name, sizeof name, "start %d%d%d%d, tap 3: members as 1.2.2", START[k][0], START[k][1], START[k][2], START[k][3]);
        CHECK(name, members_same(st, ref));
    }
#ifndef OLD_AS_IMPL
    /* a member chosen by hand while the group is lit clears the memory, as 1.2.2 did */
    for (int m = 0; m < 2; m++) {
        int st[4] = {1, 0, 1, 0}, ref[4] = {1, 0, 1, 0};
        put(st);
        az_toggle(ID[m]);
        get(st);
        oracle_hand_toggle(ref, m);
        snprintf(name, sizeof name, "a hand toggle of member %d while lit: memory cleared, as 1.2.2", m);
        CHECK(name, same(st, ref) && !st[2] && !st[3]);
    }
#endif
    return ut_summary();
}
