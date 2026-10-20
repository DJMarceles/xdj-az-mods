/* SPDX-License-Identifier: GPL-3.0-only */
/* bj_joginfo_hook / bj_jog_case (extracted from azmod/beat_jump.c by run.sh): with BEAT JUMP
 * 2 First on, the jog display shows the lengths the pads perform. PlayerInfoData's store
 * (data, data + 0x4f8) is entered with the staging case swapped BEAT JUMP 3 <-> BEAT JUMP 2 7
 * (AZ grid variant 1 only) on a copy: the staging case is never written, so a second store of
 * the same staging area (EP147 does that when no new data arrived) swaps again instead of
 * flipping back, and what the store fills into its argument still lands in the staging area. Off, another page, another variant, a pointer that is not the object's own
 * staging area, a PlayerInfoData whose vtable does not match or that cannot be read: the store
 * sees EP147's byte untouched. The store runs exactly once per call in every case.
 *
 * own_q is faked: a read succeeds only inside the objects made here. The store fake records
 * the case it was entered with, fills two fields into its argument (as EP147's store fills the
 * timing fields) and copies the case to data + 0x6a5 as EP147's store does. A second
 * thread toggles the mod while the store runs (the Mod Menu does, from its own thread). */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"

#define EP_1fefa78 ((uintptr_t) 0x1fefa78)
#define EP_2df55e8 ((uintptr_t) 0x2df55e8)
#define AZF_BEAT_JUMP_2_FIRST 27
static int g_mod;
static bool az_on(int id) {
    return id == AZF_BEAT_JUMP_2_FIRST && __atomic_load_n(&g_mod, __ATOMIC_RELAXED) != 0;
}
#define OBJ_SIZE 0x6b0
static unsigned char *g_obj[4];
static int g_nobj;
static unsigned char *obj(uintptr_t vt) {
    unsigned char *p = aligned_alloc(16, OBJ_SIZE);
    memset(p, 0, OBJ_SIZE);
    memcpy(p, &vt, 8);
    g_obj[g_nobj++] = p;
    return p;
}
static int own_q(uintptr_t a, uintptr_t *out) {
    for (int i = 0; i < g_nobj; i++)
        if (a >= (uintptr_t) g_obj[i] && a + 8 <= (uintptr_t) g_obj[i] + OBJ_SIZE) {
            memcpy(out, (void *) a, 8);
            return 1;
        }
    *out = 0;
    return 0;
}
#include BJ_INC

static int g_calls, g_seen = -1;
static void fake_store(long data, unsigned char *info) {
    g_calls++;
    g_seen = info[BJ_JOG_CASE];
    info[0x1c] = 0x5a;              /* fields the store fills into its argument */
    memset(info + 0x60, 0xa5, 0x20);
    if (data) /* the null-object case below must not write */
        ((unsigned char *) data)[0x6a5] = info[BJ_JOG_CASE];
}
/* one store; returns the case the store saw, -1 when it was not called exactly once */
static int store(unsigned char *data, unsigned char *info) {
    g_calls = 0;
    g_seen = -1;
    bj_joginfo_hook((long) data, info);
    return g_calls == 1 ? g_seen : -1;
}
static void stage(unsigned char *data, int variant, int jcase) {
    data[BJ_JOG_STAGING + 0x1c] = 0;
    memset(data + BJ_JOG_STAGING + 0x60, 0, 0x20);
    data[BJ_JOG_STAGING + BJ_JOG_VARIANT] = (unsigned char) variant;
    data[BJ_JOG_STAGING + BJ_JOG_CASE] = (unsigned char) jcase;
}

static void *toggler(void *arg) {
    (void) arg;
    for (int i = 0; i < 20000; i++)
        __atomic_store_n(&g_mod, i & 1, __ATOMIC_RELAXED);
    return NULL;
}

int main(void) {
    ut_suite = "beat jump jog";
    g_orig_bj_joginfo = fake_store;
    unsigned char *d = obj(EP_2df55e8);
    unsigned char *info = d + BJ_JOG_STAGING;

    __atomic_store_n(&g_mod, 1, __ATOMIC_RELAXED);
    stage(d, 1, 3);
    CHECK("on: BEAT JUMP page stored as BEAT JUMP 2 (8 16 32 64)", store(d, info) == 7);
    CHECK("on: the stored copy is BEAT JUMP 2", d[0x6a5] == 7);
    CHECK("on: the staging case is still EP147's after the store", info[BJ_JOG_CASE] == 3);
    CHECK("on: what the store filled in reached the staging area",
          info[0x1c] == 0x5a && info[0x60] == 0xa5 && info[0x7f] == 0xa5);
    CHECK("on: the same staging stored again still shows BEAT JUMP 2", store(d, info) == 7);
    CHECK("on: staging still EP147's after the second store", info[BJ_JOG_CASE] == 3);
    stage(d, 1, 7);
    CHECK("on: BEAT JUMP 2 page stored as BEAT JUMP (1/2 1 2 4)", store(d, info) == 3);
    CHECK("on: staging case still 7", info[BJ_JOG_CASE] == 7);
    CHECK("on: the variant byte is never changed", info[BJ_JOG_VARIANT] == 1);

    int others[] = {0, 1, 2, 4, 5, 6, 8, 0x0a, 0x15};
    int ok = 1;
    for (unsigned i = 0; i < sizeof others / sizeof *others; i++) {
        stage(d, 1, others[i]);
        ok &= store(d, info) == others[i] && info[BJ_JOG_CASE] == others[i];
    }
    CHECK("on: every other page (hot cue, loops, stems...) stored untouched", ok);
    stage(d, 0, 3);
    CHECK("on: another layout variant (0) untouched", store(d, info) == 3);
    stage(d, 2, 7);
    CHECK("on: another layout variant (2) untouched", store(d, info) == 7);

    stage(d, 1, 3);
    unsigned char *other = info + 8; /* a PlayerInfo-shaped page that is not the staging area */
    other[BJ_JOG_VARIANT] = 1;
    other[BJ_JOG_CASE] = 3;
    CHECK("on: a pointer that is not the object's staging area: untouched",
          store(d, other) == 3 && other[BJ_JOG_CASE] == 3);
    unsigned char *wrong = obj(EP_2df55e8 + 0x10);
    stage(wrong, 1, 3);
    CHECK("on: wrong vtable: untouched", store(wrong, wrong + BJ_JOG_STAGING) == 3);
    unsigned char *unread = aligned_alloc(16, OBJ_SIZE); /* not an object own_q can read */
    memset(unread, 0, OBJ_SIZE);
    memcpy(unread, &(uintptr_t) {EP_2df55e8}, 8);
    stage(unread, 1, 3);
    CHECK("on: an unreadable object: untouched", store(unread, unread + BJ_JOG_STAGING) == 3);
    CHECK("on: a null object: the store still runs, untouched",
          (g_calls = 0, bj_joginfo_hook(0, info), g_calls == 1));
    CHECK("on: a misaligned object: untouched",
          store(d + 1, d + 1 + BJ_JOG_STAGING) == d[1 + BJ_JOG_STAGING + BJ_JOG_CASE]);

    __atomic_store_n(&g_mod, 0, __ATOMIC_RELAXED);
    stage(d, 1, 3);
    CHECK("off: BEAT JUMP stored as EP147 wrote it", store(d, info) == 3);
    stage(d, 1, 7);
    CHECK("off: BEAT JUMP 2 stored as EP147 wrote it", store(d, info) == 7);
    CHECK("off: what the store filled in reached the staging area",
          info[0x1c] == 0x5a && info[0x60] == 0xa5);

    /* the mod toggled from another thread while stores run: each store sees 3 or 7, and the
     * staging area is EP147's after every one of them */
    pthread_t t;
    pthread_create(&t, NULL, toggler, NULL);
    int seen_ok = 1, restored = 1, n = 0;
    for (int i = 0; i < 20000; i++) {
        stage(d, 1, 3);
        int s = store(d, info);
        seen_ok &= s == 3 || s == 7;
        restored &= info[BJ_JOG_CASE] == 3;
        n++;
    }
    pthread_join(t, NULL);
    CHECK("toggled from another thread: every store saw 3 or 7, once", seen_ok && n == 20000);
    CHECK("toggled from another thread: the staging case always EP147's", restored);

    free(unread);
    for (int i = 0; i < g_nobj; i++)
        free(g_obj[i]);
    return ut_summary();
}
