/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum roll's beat grid read through EP147's reference protocol (guest/azstem/drumroll.c
 * drum_grid_native + meow_*, and the LoadState bracket in stem_playback.c, extracted by run.sh).
 *   - EP147's swapped-object protocol: slot, type, acquire only while the count is above zero,
 *     guard again, use, release; the release that reaches zero destroys through the owner.
 *   - Under concurrency (EP147 swaps the grid in its slot and drops the old one while the
 *     flusher reads it): every grid read is one whole grid that was never destroyed while read,
 *     and every retired grid is destroyed exactly once.
 *   - The LoadState bracket: a reader that got the LoadState from ls_enter never sees it torn
 *     down before ls_leave, while EP147 destroys and rebuilds it.
 * Usage: meowgrid_test [seconds per phase]. */
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define NDECK 4
#define MAX_DECKS 8
#define logln(...) ((void) 0)
#define AZ_EP147_RATE 44100
#define EP_2ddb878 ((uintptr_t) 0x2ddb878) /* LoadState */
#define EP_2754518 ((uintptr_t) 0x2754518) /* UpdatableSharedObject<BeatGrid> */
#define EP_2798328 ((uintptr_t) 0x2798328) /* BeatGrid::Content */
#define EP_22043f0 ((uintptr_t) 0x22043f0)
#include GRID_INC
#include "check.h"

/* ---- fake meow objects: count +0x0c, guard +0x10, owner +0x18 */
#define OWNER ((void *) 0x0bad0000)
struct grid {
    uintptr_t content; /* the Content object */
    long long anchor;   /* what drum_grid_native must answer */
    double beat;
};
static _Atomic long destroyed, destroy_wrong_owner, reads_bad, reads_ok;
static void poison(uintptr_t o, size_t n) {
    *(volatile uint32_t *) (o + MEOW_GUARD_OFF) = 0; /* EP147's teardown clears the guard */
    memset((void *) (o + 0x18), 0x5a, n - 0x18);       /* and the memory is reused */
}
static void fake_destroy(void *owner, void *o) {
    if (owner != OWNER)
        atomic_fetch_add(&destroy_wrong_owner, 1);
    uintptr_t cells = *(uintptr_t *) ((uintptr_t) o + BG_CELLS_OFF);
    int32_t n = *(int32_t *) ((uintptr_t) o + BG_NBEATS_OFF);
    if (*(uintptr_t *) o == BG_CONTENT_VT && cells && n > 0)
        memset((void *) cells, 0x5a, (size_t) n * 16); /* reused: the cells read as garbage */
    poison((uintptr_t) o, 0x40);
    atomic_fetch_add(&destroyed, 1); /* never freed: the memory stays mapped, as EP147's does */
}
static uintptr_t mk_obj(uintptr_t vt, int count) {
    uintptr_t o = (uintptr_t) aligned_alloc(16, 0x40);
    memset((void *) o, 0, 0x40);
    *(uintptr_t *) o = vt;
    *(int *) (o + MEOW_RC_OFF) = count;
    *(uint32_t *) (o + MEOW_GUARD_OFF) = BG_XOCR;
    *(void **) (o + MEOW_OWNER_OFF) = OWNER;
    return o;
}
/* A grid of n beats starting at p0, `step` source samples apart, at 120..ish BPM consistent with
 * 44100 Hz, so bg_grid_rate measures 44100. */
static struct grid mk_grid(int n, long long p0, long long step) {
    struct grid g;
    unsigned char *mem = aligned_alloc(16, (size_t) n * 16 + 32);
    memset(mem, 0xAF, 16);
    memset(mem + 16 + (size_t) n * 16, 0xEF, 16);
    unsigned char *cells = mem + 16;
    double bpm = 60.0 * 44100.0 / (double) step;
    for (int i = 0; i < n; i++) {
        long long pos = p0 + step * i;
        memcpy(cells + (size_t) i * 16, &pos, 8);
        memcpy(cells + (size_t) i * 16 + 8, &bpm, 8);
    }
    g.content = mk_obj(BG_CONTENT_VT, 1); /* EP147's reference (the slot's) */
    *(uintptr_t *) (g.content + BG_CELLS_OFF) = (uintptr_t) cells;
    *(int32_t *) (g.content + BG_NBEATS_OFF) = n;
    g.anchor = p0;
    g.beat = (double) step;
    return g;
}
static void *mk_loadstate(uintptr_t uso) {
    unsigned char *p = aligned_alloc(16, 0x4b0);
    memset(p, 0, 0x4b0);
    uintptr_t vt = EP_2ddb878;
    unsigned d = 0, m = LS_MAGIC;
    memcpy(p, &vt, 8);
    memcpy(p + LS_DECK_OFF, &d, 4);
    memcpy(p + LS_MAGIC_OFF, &m, 4);
    memcpy(p + BG_PROV_OFF, &uso, 8);
    return p;
}
#define NG 64
static struct grid grids[NG];
static _Atomic int stop;
static uintptr_t g_uso;
static long retired;
/* EP147's side: swap a new Content into the slot, then drop the slot's reference on the old one. */
static void *swapper(void *arg) {
    (void) arg;
    int k = 1;
    while (!atomic_load(&stop)) {
        uintptr_t nc = grids[k % NG].content;
        /* a new object per swap, as EP147 builds a new Content */
        struct grid g = mk_grid(16 + (k % 7) * 8, 1000 * (k % NG), 20000 + (k % NG) * 10);
        grids[k % NG] = g;
        nc = g.content;
        uintptr_t old = atomic_exchange((_Atomic uintptr_t *) (g_uso + BG_CONTENT_OFF), nc);
        meow_release(old);
        retired++;
        k++;
    }
    return NULL;
}
static void *reader(void *arg) {
    (void) arg;
    while (!atomic_load(&stop)) {
        long long anchor = 0;
        double beat = 0;
        if (!drum_grid_native(0, &anchor, &beat))
            continue;
        /* one whole grid: anchor = 1000*i and beat = 20000 + 10*i for the same i */
        long long i = anchor / 1000;
        if (anchor % 1000 || i < 0 || i >= NG || beat != 20000.0 + 10.0 * (double) i)
            atomic_fetch_add(&reads_bad, 1);
        else
            atomic_fetch_add(&reads_ok, 1);
    }
    return NULL;
}
static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---- the LoadState bracket against EP147 destroying and rebuilding it */
#define POISON_SLOT ((uintptr_t) 0xdead0000)
static _Atomic int bracket_bad;
static _Atomic long bracket_reads;
static void fake_ls_teardown(void *ls) { /* the original destructor: the memory is reused */
    uintptr_t p = POISON_SLOT;
    memcpy((char *) ls + BG_PROV_OFF, &p, 8);
    memset(ls, 0, 8);
}
static void *bracket_reader(void *arg) {
    (void) arg;
    while (!atomic_load(&stop)) {
        uintptr_t ls = ls_enter(0);
        if (ls) {
            for (int i = 0; i < 50; i++) /* a slow reader: stays inside a while */
                if (*(volatile uintptr_t *) (ls + BG_PROV_OFF) == POISON_SLOT)
                    atomic_store(&bracket_bad, 1);
            atomic_fetch_add(&bracket_reads, 1);
        }
        ls_leave(0);
        /* Between brackets, as the flusher is: two readers that re-enter at once keep the count
         * above zero most of the time, and the teardown's wait (and with it the round count this
         * check needs) then depends on scheduling luck, worst under a sanitizer. */
        sched_yield();
    }
    return NULL;
}

/* The same bracket, the interleaving pinned down: one reader holds the bracket until the
 * teardown side has reached one of its two possible ends: the teardown ran (a failure), or
 * ls_wait_readers counted the reader inside (g_ls_dtor_waits moved: it is waiting, as it must).
 * Then it looks at the LoadState and leaves. Decided by the code, not by the scheduler; the
 * 10 s bound only turns a hang into a FAIL. */
static _Atomic int det_inside, det_torn, det_bad, det_decided;
static void *det_reader(void *arg) {
    unsigned waits0 = *(unsigned *) arg;
    uintptr_t ls = ls_enter(0);
    atomic_store(&det_inside, ls ? 1 : -1);
    double t0 = now_s();
    while (ls && now_s() - t0 < 10.0 && !atomic_load(&det_torn) &&
           atomic_load(&g_ls_dtor_waits) == waits0)
        usleep(10);
    atomic_store(&det_decided, atomic_load(&det_torn) || atomic_load(&g_ls_dtor_waits) != waits0);
    if (ls && *(volatile uintptr_t *) (ls + BG_PROV_OFF) == POISON_SLOT)
        atomic_store(&det_bad, 1);
    ls_leave(0);
    return NULL;
}
static int det_bracket_round(void) {
    atomic_store(&det_inside, 0), atomic_store(&det_torn, 0), atomic_store(&det_bad, 0);
    atomic_store(&det_decided, 0);
    void *l = mk_loadstate(g_uso);
    atomic_store(&g_ls_self[0], l);
    unsigned waits0 = atomic_load(&g_ls_dtor_waits);
    pthread_t t;
    pthread_create(&t, NULL, det_reader, &waits0);
    while (!atomic_load(&det_inside))
        usleep(10);
    void *exp = l; /* lsdtor_hook's order: unpublish, wait for the readers, then the original */
    atomic_compare_exchange_strong(&g_ls_self[0], &exp, NULL);
    ls_wait_readers(0);
    fake_ls_teardown(l);
    atomic_store(&det_torn, 1);
    pthread_join(t, NULL);
    return atomic_load(&det_inside) == 1 && atomic_load(&det_decided) && !atomic_load(&det_bad);
}

int main(int argc, char **argv) {
    ut_suite = "meowgrid";
    double secs = argc > 1 ? atof(argv[1]) : 1.0;
    long long anchor = 0;
    double beat = 0;

    /* ---- deterministic: the protocol itself */
    g_uso = mk_obj(BG_USO_VT, 1);
    struct grid g0 = mk_grid(32, 0, 20000);
    grids[0] = g0;
    *(uintptr_t *) (g_uso + BG_CONTENT_OFF) = g0.content;
    void *ls = mk_loadstate(g_uso);
    atomic_store(&g_ls_self[0], ls);
    CHECK("no release path known: unknown, nothing touched",
          !drum_grid_native(0, &anchor, &beat) && *(int *) (g0.content + MEOW_RC_OFF) == 1);
    g_meow_destroy_fn = fake_destroy;
    int ok = drum_grid_native(0, &anchor, &beat);
    CHECK("a live grid is read: anchor and beat from one Content", ok && anchor == 0 && beat == 20000.0);
    CHECK("every reference taken is given back",
          *(int *) (g0.content + MEOW_RC_OFF) == 1 && *(int *) (g_uso + MEOW_RC_OFF) == 1 &&
              atomic_load(&destroyed) == 0);
    CHECK("the LoadState bracket is balanced", atomic_load(&g_ls_readers[0]) == 0);
    *(int *) (g0.content + MEOW_RC_OFF) = 0; /* on its way out */
    CHECK("a Content whose count reached zero is not resurrected",
          !drum_grid_native(0, &anchor, &beat) && *(int *) (g0.content + MEOW_RC_OFF) == 0);
    *(int *) (g0.content + MEOW_RC_OFF) = 1;
    *(uint32_t *) (g0.content + MEOW_GUARD_OFF) = 0;
    CHECK("a Content without its guard is refused", !drum_grid_native(0, &anchor, &beat) &&
                                                        *(int *) (g0.content + MEOW_RC_OFF) == 1);
    *(uint32_t *) (g0.content + MEOW_GUARD_OFF) = BG_XOCR;
    unsigned char *cells = (unsigned char *) *(uintptr_t *) (g0.content + BG_CELLS_OFF);
    cells[-1] = 0;
    CHECK("a broken red zone is refused, and its reference given back",
          !drum_grid_native(0, &anchor, &beat) && *(int *) (g0.content + MEOW_RC_OFF) == 1);
    cells[-1] = 0xAF;
    uintptr_t vt = *(uintptr_t *) g_uso;
    *(uintptr_t *) g_uso = 0x1234560;
    CHECK("a slot holding another type is refused", !drum_grid_native(0, &anchor, &beat) &&
                                                        *(int *) (g_uso + MEOW_RC_OFF) == 1);
    *(uintptr_t *) g_uso = vt;
    /* the reader's release is the last one: EP147 dropped its reference while the reader held one */
    uintptr_t lone = mk_obj(BG_CONTENT_VT, 1);
    CHECK("acquire on a live object", meow_acquire(lone) && *(int *) (lone + MEOW_RC_OFF) == 2);
    *(int *) (lone + MEOW_RC_OFF) = 1;
    meow_release(lone);
    CHECK("the release that reaches zero destroys through the owner, once",
          atomic_load(&destroyed) == 1 && atomic_load(&destroy_wrong_owner) == 0);
    atomic_store(&destroyed, 0);
    atomic_store(&g_ls_self[0], NULL);
    CHECK("no LoadState: unknown", !drum_grid_native(0, &anchor, &beat));
    atomic_store(&g_ls_self[0], ls);

    /* ---- concurrency: EP147 swaps grids while the flusher reads them */
    pthread_t s, r1, r2;
    pthread_create(&s, NULL, swapper, NULL);
    pthread_create(&r1, NULL, reader, NULL);
    pthread_create(&r2, NULL, reader, NULL);
    double t0 = now_s();
    while (now_s() - t0 < 60.0 && (now_s() - t0 < secs || atomic_load(&reads_ok) <= 1000))
        usleep(10000);
    atomic_store(&stop, 1);
    pthread_join(s, NULL), pthread_join(r1, NULL), pthread_join(r2, NULL);
    printf("      swaps %ld, good reads %ld, bad reads %ld, destroyed %ld\n", retired,
           atomic_load(&reads_ok), atomic_load(&reads_bad), atomic_load(&destroyed));
    CHECK("under swaps every read is one whole, live grid",
          atomic_load(&reads_bad) == 0 && atomic_load(&reads_ok) > 1000);
    CHECK("every retired grid destroyed exactly once, through its owner",
          atomic_load(&destroyed) == retired && atomic_load(&destroy_wrong_owner) == 0);

    /* ---- the LoadState bracket against teardown: pinned down, then under load */
    CHECK("a LoadState is not torn down while a reader holds the bracket (interleaving pinned)",
          det_bracket_round());
    atomic_store(&stop, 0);
    pthread_t br1, br2;
    pthread_create(&br1, NULL, bracket_reader, NULL);
    pthread_create(&br2, NULL, bracket_reader, NULL);
    long rounds = 0;
    t0 = now_s();
    /* at least `secs`, then until both sides have done enough to mean something (a loaded
     * machine gets through few teardowns a second, as few as a dozen);
     * the 60 s cap only ends a run that makes no progress */
    while (now_s() - t0 < 60.0 &&
           (now_s() - t0 < secs || rounds <= 100 || atomic_load(&bracket_reads) <= 100)) {
        void *l = mk_loadstate(g_uso);
        atomic_store(&g_ls_self[0], l); /* what lsctor_hook publishes */
        usleep(50);
        /* lsdtor_hook's order: unpublish, wait for the readers, then the original runs */
        void *exp = l;
        atomic_compare_exchange_strong(&g_ls_self[0], &exp, NULL);
        ls_wait_readers(0);
        fake_ls_teardown(l);
        rounds++;
    }
    atomic_store(&stop, 1);
    pthread_join(br1, NULL), pthread_join(br2, NULL);
    printf("      %ld teardowns, %ld bracket reads\n", rounds, atomic_load(&bracket_reads));
    CHECK("a LoadState is never torn down while a reader is inside ls_enter/ls_leave",
          !atomic_load(&bracket_bad) && rounds > 100 && atomic_load(&bracket_reads) > 100);
    return ut_summary();
}
