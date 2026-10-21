/* SPDX-License-Identifier: GPL-3.0-only */
/* Extended Lead-In (guest/azmod/spin_back.c), against a model of dj_player's jog clamp
 * and player_basis::PlayerCore: with the row on, the PlayerCore's pre-roll is the long cap
 * before EP147's clamp runs; with it off, the stock 2 s comes back; a pre-roll EP147 built
 * with any other value is never touched; a strategy whose core is not a PlayerCore, or that
 * has none, is left alone; and the clamp itself always runs, with its arguments as given.
 * Toggling on one thread while the clamp runs on another is race-free. SB_SRC = the file
 * under test. */
#include "check.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum {
    AZF_EXTENDED_LEAD_IN = 5
};
static int g_row; /* __atomic only */
static int az_value(int f) { return f == AZF_EXTENDED_LEAD_IN && __atomic_load_n(&g_row, __ATOMIC_RELAXED); }
static FILE *azlog_open(const char *p, const char *m) {
    (void) p;
    (void) m;
    return 0;
}
static int own_q(uintptr_t a, uintptr_t *out) {
    *out = __atomic_load_n((uintptr_t *) a, __ATOMIC_RELAXED);
    return 1;
}
static int img_q(uintptr_t a, uintptr_t *out) { return own_q(a, out); }
static int azm_install_hook(uintptr_t f, void *h, void **o) {
    (void) f;
    (void) h;
    (void) o;
    return 1;
}

/* PlayerCore: vtable, pre-roll at +0x2f0 */
static uintptr_t g_core_vt[4], g_other_vt[4];
static _Alignas(16) unsigned char g_core[0x300];
static _Alignas(16) unsigned char g_strategy[0x40];
static int g_clamps;
static long g_seen_preroll, g_args[4];
static void fake_clamp(long strategy, long a1, long pos, long readable, long flag) {
    (void) strategy;
    g_clamps++;
    g_seen_preroll = __atomic_load_n((long *) (g_core + 0x2f0), __ATOMIC_RELAXED);
    g_args[0] = a1;
    g_args[1] = pos;
    g_args[2] = readable;
    g_args[3] = flag;
}
#define EP_14a3e10 ((uintptr_t) fake_clamp)
#define EP_2874158 ((uintptr_t) g_core_vt)
/* the beat jump's checks and their return addresses (the lead-in jump, leadin_jump.h has its own suite) */
#define EP_1453e80 ((uintptr_t) 0x1453e80)
#define EP_14553d8 ((uintptr_t) 0x14553d8)
#define EP_1457b4c ((uintptr_t) 0x1457b4c)
#define EP_14574f8 ((uintptr_t) 0x14574f8)
#include SB_SRC

static void model(long preroll) {
    memset(g_core, 0, sizeof g_core);
    memset(g_strategy, 0, sizeof g_strategy);
    *(uintptr_t *) g_core = (uintptr_t) g_core_vt;
    *(long *) (g_core + 0x2f0) = preroll;
    *(uintptr_t *) (g_strategy + 0x18) = (uintptr_t) g_core;
    g_clamps = 0;
}
static long preroll(void) { return __atomic_load_n((long *) (g_core + 0x2f0), __ATOMIC_RELAXED); }
static void jog(void) { sb_clamp_hook((long) g_strategy, 11, 22, 33, 1); }
static void *toggler(void *arg) {
    (void) arg;
    for (int k = 0; k < 4000; k++)
        __atomic_store_n(&g_row, k & 1, __ATOMIC_RELAXED);
    return 0;
}

int main(void) {
    ut_suite = "extended lead-in";
    g_o_sb_clamp = fake_clamp;

    model(-2000);
    g_row = 0;
    jog();
    CHECK("row off: the stock 2 s stays", preroll() == -2000 && g_seen_preroll == -2000 && g_clamps == 1);
    CHECK("the clamp runs with its arguments as given",
          g_args[0] == 11 && g_args[1] == 22 && g_args[2] == 33 && g_args[3] == 1);
    g_row = 1;
    jog();
    CHECK("row on: the clamp already sees the long pre-roll", g_seen_preroll == SB_CAP_MS && preroll() == SB_CAP_MS);
    CHECK("the cap is far past 2 s and far from overflow (5 min)", SB_CAP_MS == -300000L);
    g_row = 0;
    jog();
    CHECK("row off again: back to the stock 2 s, before the clamp", g_seen_preroll == -2000 && preroll() == -2000);

    model(-5000);
    g_row = 1;
    jog();
    CHECK("a pre-roll EP147 built with another value is never touched", preroll() == -5000 && g_clamps == 1);

    model(-2000);
    *(uintptr_t *) g_core = (uintptr_t) g_other_vt;
    jog();
    CHECK("a core that is not a PlayerCore is left alone", preroll() == -2000 && g_clamps == 1);
    model(-2000);
    *(uintptr_t *) (g_strategy + 0x18) = 0;
    jog();
    CHECK("a strategy with no core is left alone, and the clamp still runs", preroll() == -2000 && g_clamps == 1);
    model(-2000);
    { /* a core 4 bytes off that would otherwise pass: a PlayerCore vtable and -2000 there */
        uintptr_t vt = (uintptr_t) g_core_vt;
        long stock = -2000;
        memcpy(g_core + 4, &vt, sizeof vt);
        memcpy(g_core + 4 + 0x2f0, &stock, sizeof stock);
        unsigned char before[0x300];
        memcpy(before, g_core, sizeof before);
        *(uintptr_t *) (g_strategy + 0x18) = (uintptr_t) (g_core + 4);
        jog();
        CHECK("a misaligned core is left alone", !memcmp(before, g_core, sizeof before) && g_clamps == 1);
    }

    model(-2000);
    pthread_t t;
    pthread_create(&t, 0, toggler, 0);
    for (int k = 0; k < 4000; k++)
        jog();
    pthread_join(t, 0);
    long v = preroll();
    CHECK("toggling during the clamp leaves one of the two known values", v == -2000 || v == SB_CAP_MS);

    return ut_summary();
}
