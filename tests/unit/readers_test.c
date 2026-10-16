/* SPDX-License-Identifier: GPL-3.0-only */
/* Reader count + destructor hook: the three EP147 objects azmod caches across calls and uses
 * from the message thread (extracted by run.sh, built once per mechanism with -DMECH=):
 *   MECH 1  TaglistEditor        playlist_actions.c  tc_note / tc_hold / tc_drop / tc_forget
 *   MECH 2  mixer Channel        bluetooth.c         mixch_ctor_hook / mixch_hold / mixch_get /
 *                                                    mixch_drop / mixch_forget
 *   MECH 3  VinylSpeedAdjustor   vinyl_brake.c       vb_set_hook / vb_forget, and
 *                                vinyl_slider.c      vs_apply (the reader)
 * The fake objects are never freed (EP147's freed memory stays mapped too). A destructor first
 * runs its body (the object's contents turn dead while its vtable is still in place), then the
 * allocator scribbles the vtable word; later the same memory is built again as a new object.
 * Cases:
 *   - nothing is cached while the destructor hooks are not installed;
 *   - off the message thread a reader gets nothing;
 *   - a destructor on another thread waits for a reader inside the count (deterministic: the
 *     reader holds the object until the destructor either ran its body or was seen yielding in
 *     ep_wait_readers, and its body must not have run);
 *   - a destructor on the message thread inside the reader's own call does not wait (no
 *     deadlock; a 10 s alarm fails the run) and the object is unpublished when it returns;
 *   - stress: a destroyer thread builds, publishes and destroys objects while the message
 *     thread reads them: every read sees a whole live object, and the destroyer was seen
 *     waiting for a reader (else the stress proved nothing).
 * Usage: readers_test [seconds of stress]. */
#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
/* Test hook, harness only: ep_wait_readers' sched_yield goes through ut_sched_yield, which
 * counts the yields of the destructor thread of case 3. A destructor that yields is spinning
 * in ep_wait_readers on a reader inside: the case's interleaving is then decided (it waits),
 * so the case no longer depends on how long the scheduler takes. Production is unchanged. */
static __thread int ut_in_dtor_thread;
static _Atomic long ut_dtor_yields;
static int ut_sched_yield(void) {
    if (ut_in_dtor_thread)
        atomic_fetch_add(&ut_dtor_yields, 1);
    return sched_yield();
}
#define sched_yield() ut_sched_yield()
#include "ep_mem.h"
#include "check.h"

#define FAKE_VT ((uintptr_t) 0x2a0000)
#define LIVE 0x4c4956454c495645UL /* the object's contents while it is alive */
#define DEAD 0x4445414444454144UL /* after its destructor body ran */
#define OBJ_SZ 0x100
#define BODY_OFF 0x80 /* contents checked by a reader: 8 words */
#define BODY_N 8

#if MECH == 1
#define TAGEDIT_VT FAKE_VT
#define TAGEDIT_ADD_SLOT (TAGEDIT_VT + 0x10)
#define TAGEDIT_CLEAR_SLOT (TAGEDIT_VT + 0x30)
static _Atomic int g_tag_touched;
static struct {
    unsigned media;
    uint64_t msid;
} g_tc_msid[4];
#define MECH_NAME "readers TaglistEditor"
#elif MECH == 2
#define MIX_CH_VT FAKE_VT
#define MECH_NAME "readers mixer Channel"
#elif MECH == 3
#define VB_ADJ_VT FAKE_VT
#define VS_ADJ_VT FAKE_VT
#define VB_NADJ 8
enum {
    AZF_FINE_VINYL_SPEED = 28
}; /* azfeatures.h: one int per row, an atomic load */
static int az_val[64];
static inline int az_on(int id) { return __atomic_load_n(&az_val[id], __ATOMIC_RELAXED) != 0; }
#define MECH_NAME "readers VinylSpeedAdjustor"
#else
#error MECH
#endif
#include RC_INC

static _Atomic long g_built, g_destroyed, g_bad, g_ok, g_contended;
static uintptr_t g_pool[48];
static _Atomic int g_stop;

static void body_set(uintptr_t o, uintptr_t v) {
    for (int i = 0; i < BODY_N; i++)
        *(volatile uintptr_t *) (o + BODY_OFF + 8 * i) = v;
}
/* A reader's use of a live object: its contents, read over a while. 1 = whole and alive. */
static int body_use(uintptr_t o) {
    for (int r = 0; r < 40; r++)
        for (int i = 0; i < BODY_N; i++)
            if (*(volatile uintptr_t *) (o + BODY_OFF + 8 * i) != LIVE)
                return 0;
    return 1;
}
static void spin_us(int us) {
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    do
        clock_gettime(CLOCK_MONOTONIC, &b);
    while ((b.tv_sec - a.tv_sec) * 1000000L + (b.tv_nsec - a.tv_nsec) / 1000 < us);
}
static void sleep_ms(int ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
/* EP147's destructor: the body runs (contents dead), a little later the allocator takes the
 * memory back (vtable word scribbled). */
static void fake_dtor(long self) {
    uintptr_t o = (uintptr_t) self;
    body_set(o, DEAD);
    spin_us(30);
    *(volatile uintptr_t *) o = 0x5a5a5a5a5a5a5a5aUL;
    atomic_fetch_add(&g_destroyed, 1);
}
static void construct(uintptr_t o, int idx) {
    memset((void *) o, 0, OBJ_SZ);
    *(volatile uintptr_t *) (o + 0x68) = (uintptr_t) 0 << 32;              /* Channel input: DECK */
    *(volatile uintptr_t *) (o + 0x70) = (uintptr_t) (unsigned) idx;        /* Channel index */
    *(volatile uintptr_t *) (o + 0x20) = 1UL | (1UL << 32);                 /* adjustor: both kinds */
    body_set(o, LIVE);
    *(volatile uintptr_t *) o = FAKE_VT;
    atomic_fetch_add(&g_built, 1);
}

/* ---------------- per mechanism: publish, destroy, the reader, "is it published" */
#if MECH == 1
static void *fake_add(void *s, void *a, void *b, void *c, void *d) {
    (void) s;
    (void) a;
    (void) b;
    (void) c;
    (void) d;
    return 0;
}
static void m_install(int ok) {
    g_tc_orig_add = fake_add;
    g_tc_orig_d0 = (void (*)(void *)) fake_dtor;
    g_tc_orig_d1 = (void (*)(void *)) fake_dtor;
    g_tc_del_ok = ok;
}
static void m_publish(uintptr_t o, int idx) {
    (void) idx;
    uint64_t id[2] = {(uint64_t) 0x0102 << 32 | 1, 7};
    construct(o, idx);
    tc_add_hook((void *) o, id, 0, 0, 0); /* EP147 tags a track through this editor */
}
static void m_destroy(uintptr_t o, int k) {
    if (k & 1)
        tc_d1_hook((void *) o);
    else
        tc_d0_hook((void *) o);
}
static int m_published(uintptr_t o) { return __atomic_load_n(&g_tc_self, __ATOMIC_SEQ_CST) == o; }
static _Atomic int *m_readers(void) { return &g_tc_readers; }
/* the reader: hold, use whatever it got (calling `inside` while held), drop. Returns what it got. */
static uintptr_t m_read(void (*inside)(uintptr_t)) {
    uintptr_t ed = tc_hold();
    if (ed) {
        if (body_use(ed))
            atomic_fetch_add(&g_ok, 1);
        else
            atomic_fetch_add(&g_bad, 1);
        if (inside)
            inside(ed);
    }
    tc_drop();
    return ed;
}
#elif MECH == 2
static void fake_ctor(long self, long p2, int *idx, long p4, long p5) {
    (void) p2;
    (void) p4;
    (void) p5;
    construct((uintptr_t) self, *idx);
}
static void m_install(int ok) {
    g_o_mixch_ctor = fake_ctor;
    g_o_mixch_d0 = fake_dtor;
    g_o_mixch_d1 = fake_dtor;
    g_mixch_dtor_ok = ok;
}
static void m_publish(uintptr_t o, int idx) {
    int i = 2 + (idx & 1); /* CH3 / CH4, the channels Remember Selected Channel uses */
    mixch_ctor_hook((long) o, 0, &i, 0, 0);
}
static void m_destroy(uintptr_t o, int k) {
    if (k & 1)
        mixch_d1_hook((long) o);
    else
        mixch_d0_hook((long) o);
}
static int m_published(uintptr_t o) {
    for (int i = 0; i < 4; i++)
        if (__atomic_load_n(&g_mixch[i], __ATOMIC_SEQ_CST) == o)
            return 1;
    return 0;
}
static _Atomic int *m_readers(void) { return &g_mixch_readers; }
static uintptr_t m_read(void (*inside)(uintptr_t)) {
    uintptr_t got = 0;
    mixch_hold();
    for (int i = 2; i < 4; i++) {
        uintptr_t c = mixch_get(i);
        if (!c)
            continue;
        got = c;
        if (body_use(c) && mixch_input(c) == 0)
            atomic_fetch_add(&g_ok, 1);
        else
            atomic_fetch_add(&g_bad, 1);
        if (inside)
            inside(c);
    }
    mixch_drop();
    return got;
}
#elif MECH == 3
static void (*g_inside)(uintptr_t);
static _Atomic uintptr_t g_got;
static void fake_set(long self, float v, int kind) {
    (void) v;
    (void) kind;
    if (!on_msg_thread())
        return; /* EP147's own setter call (the publish path) */
    uintptr_t a = (uintptr_t) self;
    atomic_store(&g_got, a);
    if (body_use(a))
        atomic_fetch_add(&g_ok, 1);
    else
        atomic_fetch_add(&g_bad, 1);
    if (g_inside) {
        void (*f)(uintptr_t) = g_inside;
        g_inside = 0; /* once per vs_apply */
        f(a);
    }
}
static void m_install(int ok) {
    g_orig_vb_set = fake_set;
    g_orig_vb_d0 = fake_dtor;
    g_orig_vb_d1 = fake_dtor;
    g_vb_dtor_ok = ok;
    g_vb_milli = 500;
    az_val[AZF_FINE_VINYL_SPEED] = 1;
}
static void m_publish(uintptr_t o, int idx) {
    construct(o, idx);
    vb_set_hook((long) o, 0.4f, 1); /* EP147 sets the adjustor: it is recorded */
}
/* case 4 only: one active kind, so vs_apply makes one call on the object */
static void m_one_call(uintptr_t o) { *(volatile uintptr_t *) (o + 0x20) = 1; }
static void m_destroy(uintptr_t o, int k) {
    if (k & 1)
        vb_d1_hook((long) o);
    else
        vb_d0_hook((long) o);
}
static int m_published(uintptr_t o) {
    for (int i = 0; i < VB_NADJ; i++)
        if ((uintptr_t) __atomic_load_n(&g_vb_adj[i], __ATOMIC_SEQ_CST) == o)
            return 1;
    return 0;
}
static _Atomic int *m_readers(void) { return &g_vb_readers; }
static uintptr_t m_read(void (*inside)(uintptr_t)) {
    atomic_store(&g_got, 0);
    g_inside = inside;
    vs_apply(500); /* the slider applies a value to every recorded adjustor */
    g_inside = 0;
    return atomic_load(&g_got);
}
#endif

/* ---------------- cases */
static void on_alarm(int s) {
    (void) s;
    static const char m[] = "FAIL  " MECH_NAME ": a destructor deadlocked (10 s alarm)\n";
    if (write(1, m, sizeof m - 1) < 0) {
    }
    _exit(1);
}
static uintptr_t new_obj(void) {
    uintptr_t o = (uintptr_t) aligned_alloc(64, OBJ_SZ);
    memset((void *) o, 0, OBJ_SZ);
    return o;
}
/* off the message thread */
static uintptr_t g_off_got = 1;
static void *off_reader(void *a) {
    (void) a;
    g_off_got = m_read(NULL);
    return NULL;
}
/* a destructor on another thread, started from inside the reader */
static uintptr_t g_victim;
static void *dtor_thread(void *a) {
    ut_in_dtor_thread = 1;
    m_destroy(g_victim, (int) (intptr_t) a);
    return NULL;
}
static pthread_t g_dt;
static long g_d_before;
static int g_held_ok;
static void inside_start_dtor(uintptr_t o) {
    if (o != g_victim)
        return;
    g_d_before = atomic_load(&g_destroyed);
    long y0 = atomic_load(&ut_dtor_yields);
    pthread_create(&g_dt, NULL, dtor_thread, (void *) (intptr_t) 0);
    /* Hold the object until the destructor thread reached one of its two possible ends: its
     * body ran (a failure) or it yields inside ep_wait_readers (it waits, as it must). Not a
     * fixed sleep, so a slow scheduler cannot make a failure look like a wait. 5 s bounds a hang
     * (the 10 s alarm around this case fails the run). */
    for (int ms = 0; ms < 5000 && atomic_load(&g_destroyed) == g_d_before &&
                     atomic_load(&ut_dtor_yields) == y0;
         ms++)
        sleep_ms(1);
    /* the destructor must still be waiting: its body has not run, the object is whole */
    g_held_ok = atomic_load(&ut_dtor_yields) != y0 && atomic_load(&g_destroyed) == g_d_before &&
                body_use(o) && atomic_load(m_readers()) > 0;
}
static int g_self_ok = -1;
static void inside_self_dtor(uintptr_t o) {
    if (o != g_victim)
        return;
    long before = atomic_load(&g_destroyed);
    m_destroy(o, 1); /* EP147's call, on the message thread, destroys the object */
    g_self_ok = atomic_load(&g_destroyed) == before + 1;
}
/* stress: the destroyer */
static void *destroyer(void *a) {
    (void) a;
    unsigned k = 0, seed = 12345;
    while (!atomic_load(&g_stop)) {
        uintptr_t o = g_pool[k % 48];
        m_publish(o, (int) k);
        spin_us((int) ((seed = seed * 1103515245u + 12345u) >> 16) % 150);
        if (atomic_load(m_readers()) > 0)
            atomic_fetch_add(&g_contended, 1);
        m_destroy(o, (int) k);
        k++;
    }
    return NULL;
}

int main(int argc, char **argv) {
    double secs = argc > 1 ? atof(argv[1]) : 1.0;
    ut_suite = MECH_NAME;
    signal(SIGALRM, on_alarm);
    msg_thread_note(); /* main is the message thread */

    /* 1. destructor hooks not installed: nothing cached, the reader gets nothing */
    m_install(0);
    uintptr_t o = new_obj();
    m_publish(o, 0);
    CHECK("destructor hooks not in: the object is not cached", !m_published(o));
    CHECK("destructor hooks not in: the reader gets nothing", m_read(NULL) == 0);
    m_install(1);
    m_destroy(o, 0);

    /* 2. off the message thread a reader gets nothing */
    o = new_obj();
    m_publish(o, 0);
    CHECK("hooks in: the object is cached", m_published(o));
    CHECK("message thread: the reader gets the live object", m_read(NULL) == o);
    pthread_t t;
    pthread_create(&t, NULL, off_reader, NULL);
    pthread_join(t, NULL);
    CHECK("another thread: the reader gets nothing", g_off_got == 0);

    /* 3. a destructor on another thread waits for the reader inside the count */
    g_victim = o;
    alarm(10);
    m_read(inside_start_dtor);
    pthread_join(g_dt, NULL);
    alarm(0);
    CHECK("destructor on another thread: waits while a reader holds the object (seen waiting)", g_held_ok);
    CHECK("destructor on another thread: runs once the reader lets go",
          atomic_load(&g_destroyed) == g_d_before + 1);
    CHECK("destructor on another thread: the object is no longer cached", !m_published(o));
    CHECK("destructor on another thread: the reader gets nothing afterwards", m_read(NULL) == 0);

    /* 4. a destructor on the message thread, inside the reader's own call */
    o = new_obj();
    m_publish(o, 1);
#if MECH == 3
    m_one_call(o);
#endif
    g_victim = o;
    alarm(10);
    uintptr_t got = m_read(inside_self_dtor);
    alarm(0);
    CHECK("destructor inside the reader's call: returns at once (no wait on its own reader)",
          got == o && g_self_ok == 1);
    CHECK("destructor inside the reader's call: the object is no longer cached", !m_published(o));

    /* 5. stress */
    for (int i = 0; i < 48; i++)
        g_pool[i] = new_obj();
    atomic_store(&g_ok, 0);
    atomic_store(&g_bad, 0);
    long b0 = atomic_load(&g_built), d0 = atomic_load(&g_destroyed);
    pthread_create(&t, NULL, destroyer, NULL);
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    do {
        m_read(NULL);
        clock_gettime(CLOCK_MONOTONIC, &b);
    } while ((double) (b.tv_sec - a.tv_sec) + (double) (b.tv_nsec - a.tv_nsec) / 1e9 < secs);
    atomic_store(&g_stop, 1);
    alarm(10);
    pthread_join(t, NULL);
    alarm(0);
    long ok = atomic_load(&g_ok), bad = atomic_load(&g_bad), con = atomic_load(&g_contended);
    long built = atomic_load(&g_built) - b0, dead = atomic_load(&g_destroyed) - d0;
    printf("      stress: %ld objects built, %ld destroyed, %ld reads whole, %ld torn/dead, "
           "%ld destructors met a reader\n",
           built, dead, ok, bad, con);
    CHECK("stress: every read saw a whole live object", bad == 0);
    CHECK("stress: the reader read objects (> 1000)", ok > 1000);
    CHECK("stress: destructors met a reader inside (the race was exercised)", con > 0);
    CHECK("stress: every object built was destroyed once", built == dead);
    return ut_summary();
}
