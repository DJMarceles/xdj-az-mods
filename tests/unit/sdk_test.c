/* SPDX-License-Identifier: GPL-3.0-only */
/* The deck and UI services for community mods (guest/azmod/sdk_logic.h, guest/azstem/sdk_export.c).
 *
 * run.sh builds this against the real sources: SDK_LOGIC_INC = sdk_logic.h, GRID_INC = drumroll.c's
 * beat-grid seqlock and its writer plus sdk_export.c's azstem_deck_beat_phase, extracted by text.
 * Each control is the same file with one rule taken out, and must fail:
 *   grid     a beat phase is never built from two grids whose phases differ (a torn read gives a
 *            third value): pinned single-threaded (the writer stopped mid-publish between the
 *            reader's two looks at seq), then one writer flipping between them at full speed
 *   playing  the playhead moving forward recently, nothing else
 *   events   edges only between known states, in order; the first known state is a baseline
 *   confirm  every question taken gets exactly one answer, on every path; Cancel first, BACK = no
 *   queue    many producers, one consumer: every job exactly once, each producer's in order;
 *            pinned single-threaded (a second push lands between a push's claim check and its
 *            claim), then 4 producer threads
 *   thread   thread_start's refusals
 *   padmode  BEAT LOOP and SLIP LOOP mapped, everything else unknown */
#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "check.h"

/* Test hook, harness only: every atomic_load_explicit in the code under test goes through this
 * wrapper, which can run one injected call right after the load of one chosen variable. The
 * pinned cases below use it to put another thread's step at an exact point of this one's,
 * single-threaded, instead of hoping the scheduler produces that interleaving. Production is
 * unchanged; with nothing armed it is a plain load. */
static const volatile void *ut_hook_at; /* the variable whose load triggers the injection */
static void (*ut_hook)(void);           /* run once, then disarmed */
static inline void ut_hook_fire(const volatile void *p) {
    if (ut_hook && p == ut_hook_at) {
        void (*f)(void) = ut_hook;
        ut_hook = 0;
        f();
    }
}
#ifdef __clang__
#define UT_LOAD(p, o) __c11_atomic_load(p, o)
#else
#define UT_LOAD(p, o) __extension__({ __auto_type ut_p_ = (p); __typeof__((void) 0, *ut_p_) ut_t_; __atomic_load(ut_p_, &ut_t_, (o)); ut_t_; })
#endif
#undef atomic_load_explicit
#define atomic_load_explicit(p, o) __extension__({ __auto_type ut_v_ = UT_LOAD(p, o); ut_hook_fire(p); ut_v_; })

#ifndef SDK_LOGIC_INC
#define SDK_LOGIC_INC "sdk_logic.h"
#endif
#include SDK_LOGIC_INC

/* ---- what the extracted azstem code expects around it ---- */
#define NDECK 4
#define DRUM_SIDES 4
#define AZ_EP147_RATE 44100
static _Atomic long long g_play_obs[NDECK];
#define logln(...) ((void) 0)
#ifdef GRID_INC
#include GRID_INC
#endif

static double secs_arg = 1.0;
static long mono_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}

/* ---------------------------------------------------------------- grid */
#ifdef GRID_INC
static _Atomic int g_stop;
static void *grid_writer(void *a) {
    (void) a;
    while (!atomic_load(&g_stop)) {
        drum_grid_publish(0, 1, 0, 1000.0);   /* grid A: S=1700 -> phase 0.7 */
        drum_grid_publish(0, 1, 500, 2000.0); /* grid B: S=1700 -> phase 0.6 */
    }
    return NULL;
}
/* drum_grid_publish(0, 1, 500, 2000.0) stopped after its first field store: seq odd (the
 * writer is inside), the anchor already grid B's, the beat still grid A's. A reader that serves
 * this builds a phase from two grids: S=1700 -> 0.2, neither A's 0.7 nor B's 0.6. */
static unsigned g_half_seq;
static void grid_writer_half(void) {
    drum_grid *g = &g_drum_grid[0];
    g_half_seq = atomic_load_explicit(&g->seq, memory_order_relaxed);
    atomic_store_explicit(&g->seq, g_half_seq + 1, memory_order_relaxed);
    atomic_store_explicit(&g->anchor_src, 500, memory_order_relaxed);
}
static void grid_writer_finish(void) { /* the rest of that publish, through the real writer */
    atomic_store_explicit(&g_drum_grid[0].seq, g_half_seq, memory_order_relaxed);
    drum_grid_publish(0, 1, 500, 2000.0);
}
static void test_grid(void) {
    ut_suite = "grid";
    atomic_store(&g_play_obs[0], 1700);
    double ph = -1;
    CHECK("no grid yet: -1", azstem_deck_beat_phase(0, &ph) == -1);
    CHECK("the first request asks the flusher for grids", atomic_load(&g_grid_demand) == 1);
    drum_grid_publish(0, 1, 0, 1000.0);
    CHECK("a grid: the phase within the beat", azstem_deck_beat_phase(0, &ph) == 0 && fabs(ph - 0.7) < 1e-9);
    drum_grid_publish(0, 1, 2000, 1000.0);
    CHECK("before the first beat: still 0..1", azstem_deck_beat_phase(0, &ph) == 0 && fabs(ph - 0.7) < 1e-9);
    atomic_store(&g_play_obs[0], -1);
    CHECK("no playhead: -1", azstem_deck_beat_phase(0, &ph) == -1);
    drum_grid_publish(0, 0, 0, 0.0);
    atomic_store(&g_play_obs[0], 1700);
    CHECK("an invalidated grid: -1", azstem_deck_beat_phase(0, &ph) == -1);
    CHECK("bad deck: -1", azstem_deck_beat_phase(7, &ph) == -1 && azstem_deck_beat_phase(-1, &ph) == -1);

    /* The torn read, pinned: right after the reader's first look at seq, the writer starts
     * publishing grid B and is stopped after its first field. */
    drum_grid_publish(0, 1, 0, 1000.0); /* grid A, stable */
    ut_hook_at = &g_drum_grid[0].seq;
    ut_hook = grid_writer_half;
    ph = -1;
    int r = azstem_deck_beat_phase(0, &ph);
    CHECK("pinned: the reader looked at seq once before the writer came in", !ut_hook);
    char tn[128];
    snprintf(tn, sizeof tn, "pinned: a grid caught mid-publish is not served (got %d, phase %.3f)", r, ph);
    CHECK(tn, r == -1 || fabs(ph - 0.7) < 1e-9 || fabs(ph - 0.6) < 1e-9);
    ut_hook = 0;
    grid_writer_finish();
    CHECK("pinned: the finished publish is served", azstem_deck_beat_phase(0, &ph) == 0 && fabs(ph - 0.6) < 1e-9);

    /* the same race at full speed, as load on the real code */
    atomic_store(&g_stop, 0);
    pthread_t w;
    pthread_create(&w, NULL, grid_writer, NULL);
    long t0 = mono_ms(), reads = 0, torn = 0;
    /* one torn phase settles it: stop there (a control's run then ends early; its time limit is
     * long because whether a tear lands in the window depends on scheduling) */
    while (mono_ms() - t0 < (long) (secs_arg * 1000) && !torn) {
        if (azstem_deck_beat_phase(0, &ph) == 0) {
            reads++;
            if (fabs(ph - 0.7) > 1e-9 && fabs(ph - 0.6) > 1e-9)
                torn++;
        }
    }
    atomic_store(&g_stop, 1);
    pthread_join(w, NULL);
    char n[96];
    snprintf(n, sizeof n, "concurrent writer: reads happened (%ld)", reads);
    CHECK(n, reads > 1000);
    snprintf(n, sizeof n, "concurrent writer: no phase from two grids (%ld torn)", torn);
    CHECK(n, torn == 0);
}
#endif

/* ---------------------------------------------------------------- playing */
static void test_playing(void) {
    ut_suite = "playing";
    struct sd_play p;
    memset(&p, 0, sizeof p);
    CHECK("unknown deck: unknown", sd_play_step(&p, -1, 100, 0) == -1);
    CHECK("first sample: unknown", sd_play_step(&p, 1, 100, 0) == -1);
    CHECK("did not move: not playing", sd_play_step(&p, 1, 100, 30) == 0);
    CHECK("moved forward: playing", sd_play_step(&p, 1, 2000, 60) == 1);
    CHECK("still moving: playing", sd_play_step(&p, 1, 3000, 90) == 1);
    CHECK("stopped, within the hold: still playing", sd_play_step(&p, 1, 3000, 90 + SD_PLAY_HOLD_MS) == 1);
    CHECK("stopped past the hold: not playing", sd_play_step(&p, 1, 3000, 91 + SD_PLAY_HOLD_MS) == 0);
    CHECK("cue back to the start: not playing", sd_play_step(&p, 1, 0, 700) == 0);
    CHECK("ejected: not playing, and forgotten", sd_play_step(&p, 0, -1, 800) == 0 && !p.have);
    CHECK("loaded again: unknown until it can be judged", sd_play_step(&p, 1, 0, 830) == -1);
    CHECK("unknown playhead: unknown", sd_play_step(&p, 1, -1, 860) == -1);
}

/* ---------------------------------------------------------------- events */
static int evs(struct sd_prev *pv, int loaded, unsigned cid, int playing, char *out) {
    int e[4], n = sd_events(pv, loaded, cid, playing, e);
    out[0] = 0;
    for (int i = 0; i < n; i++)
        strcat(out, e[i] == SD_EV_LOADED ? "L" : e[i] == SD_EV_EJECTED ? "E"
                                             : e[i] == SD_EV_PLAY      ? "P"
                                                                       : "S");
    return n;
}
static void test_events(void) {
    ut_suite = "events";
    struct sd_prev pv = SD_PREV_INIT;
    char o[8];
    evs(&pv, 1, 39, 1, o);
    CHECK("the first known state is a baseline (no event)", !strcmp(o, ""));
    evs(&pv, -1, 0, -1, o);
    CHECK("unknown changes nothing", !strcmp(o, "") && pv.loaded == 1 && pv.playing == 1);
    evs(&pv, 1, 39, 0, o);
    CHECK("pause: STOP", !strcmp(o, "S"));
    evs(&pv, 1, 31, 0, o);
    CHECK("another track on the deck: LOADED", !strcmp(o, "L"));
    evs(&pv, 1, 31, 1, o);
    CHECK("play: PLAY", !strcmp(o, "P"));
    evs(&pv, 0, 0, 0, o);
    CHECK("eject while playing: STOP, then EJECTED", !strcmp(o, "SE"));
    evs(&pv, 1, 39, 1, o);
    CHECK("load and play at once: LOADED, then PLAY", !strcmp(o, "LP"));
    evs(&pv, 1, 0, 1, o);
    CHECK("the same deck, its id briefly unknown: nothing", !strcmp(o, ""));
}

/* ---------------------------------------------------------------- confirm */
static int g_ans[16], g_nans;
static void on_ans(int yes, void *user) {
    (void) user;
    g_ans[g_nans++ % 16] = yes;
}
static void deliver(struct sc_answer a) {
    if (a.fn)
        a.fn(a.yes, a.user);
}
static void test_confirm(void) {
    ut_suite = "confirm";
    struct sd_confirm c;
    memset(&c, 0, sizeof c);
    struct sc_answer a;
    CHECK("a question without a callback is refused", sc_request(&c, 0, "T", "x", "Yes", NULL, NULL) == -1);
    CHECK("a question is taken", sc_request(&c, 0, "Remove?", "text", "Remove", on_ans, NULL) == 0);
    CHECK("a second one while it is open is refused", sc_request(&c, 1, "B", "", "", on_ans, NULL) == -1);
    g_nans = 0;
    CHECK("the overlay is up: shown", sc_tick(&c, 1, &a) == 1 && !a.fn && c.state == SC_SHOWN);
    CHECK("shown: not shown twice", sc_tick(&c, 1, &a) == 0 && !a.fn);
    deliver(sc_answer(&c, 1));
    CHECK("the user says yes: one answer, yes", g_nans == 1 && g_ans[0] == 1 && c.state == SC_IDLE);
    deliver(sc_answer(&c, 1));
    CHECK("a second press answers nothing", g_nans == 1);

    sc_request(&c, 0, "", NULL, NULL, on_ans, NULL);
    CHECK("defaults: a title and a yes label", c.title[0] && !strcmp(c.yes, "OK"));
    sc_tick(&c, 0, &a);
    deliver(a);
    CHECK("nowhere to show it: answered no at once", g_nans == 2 && g_ans[1] == 0 && c.state == SC_IDLE);

    sc_request(&c, 0, "Q", "", "Go", on_ans, NULL);
    sc_tick(&c, 1, &a);
    sc_tick(&c, 0, &a);
    deliver(a);
    CHECK("the dialog went away under it: answered no", g_nans == 3 && g_ans[2] == 0 && c.state == SC_IDLE);

    sc_request(&c, 0, "Q", "", "Go", on_ans, NULL);
    sc_tick(&c, 1, &a);
    deliver(sc_answer(&c, 0));
    CHECK("cancel: answered no", g_nans == 4 && g_ans[3] == 0);
    CHECK("a new question after an answer is taken", sc_request(&c, 0, "Q", "", "", on_ans, NULL) == 0);

    int sel = 0; /* where the dialog starts: Cancel */
    CHECK("click on Cancel: no", sc_input(&sel, SC_IN_CLICK, 0, -1) == 0);
    CHECK("knob right: no answer yet, yes focused", sc_input(&sel, SC_IN_KNOB, 1, -1) == -1 && sel == 1);
    CHECK("knob right again: stays on yes", sc_input(&sel, SC_IN_KNOB, 1, -1) == -1 && sel == 1);
    CHECK("click on yes: yes", sc_input(&sel, SC_IN_CLICK, 0, -1) == 1);
    CHECK("BACK with yes focused: no", sc_input(&sel, SC_IN_BACK, 0, -1) == 0);
    CHECK("knob left: Cancel focused", sc_input(&sel, SC_IN_KNOB, -1, -1) == -1 && sel == 0);
    CHECK("tap on yes: yes; on Cancel: no; elsewhere: nothing",
          sc_input(&sel, SC_IN_TAP, 0, 1) == 1 && sc_input(&sel, SC_IN_TAP, 0, 0) == 0 &&
              sc_input(&sel, SC_IN_TAP, 0, -1) == -1);
}

/* ---------------------------------------------------------------- queue */
#define QP 4
#define QJOBS 20000
static struct sdq g_q;
static _Atomic int g_qstop; /* the consumer gave up: producers stop retrying */
static _Atomic int g_qdone; /* producers that pushed all their jobs and returned */
static void *producer(void *arg) {
    int me = (int) (intptr_t) arg;
    for (int i = 0; i < QJOBS; i++) {
        struct sd_job j;
        memset(&j, 0, sizeof j);
        j.owner = me;
        j.ms = i;
        while (sdq_push(&g_q, &j) != 0) {
            if (atomic_load(&g_qstop))
                return NULL;
            sched_yield(); /* full: the consumer catches up */
        }
    }
    atomic_fetch_add(&g_qdone, 1);
    return NULL;
}
static int g_push2_rc = -1;
static void push_second(void) { /* producer 2, injected inside producer 1's push */
    struct sd_job j2;
    memset(&j2, 0, sizeof j2);
    j2.owner = 2;
    g_push2_rc = sdq_push(&g_q, &j2);
}
static void test_queue(void) {
    ut_suite = "queue";
    sdq_init(&g_q);
    struct sd_job j;
    CHECK("empty: nothing", sdq_pop(&g_q, &j) == 0);
    int full = 0;
    for (int i = 0; i < SDQ_N + 4; i++) {
        memset(&j, 0, sizeof j);
        j.ms = i;
        if (sdq_push(&g_q, &j) != 0)
            full++;
    }
    CHECK("bounded: refuses when full", full == 4);
    int okfifo = 1;
    for (int i = 0; i < SDQ_N; i++)
        okfifo &= sdq_pop(&g_q, &j) == 1 && j.ms == i;
    CHECK("drained in order", okfifo && sdq_pop(&g_q, &j) == 0);

    /* Two producers on one cell, pinned: producer 1 has seen its cell free and is about to claim
     * it when producer 2's whole push lands. Both jobs must come out, each once. */
    size_t head = atomic_load(&g_q.head);
    ut_hook_at = &g_q.cell[head & (SDQ_N - 1)].seq;
    ut_hook = push_second;
    memset(&j, 0, sizeof j);
    j.owner = 1;
    int r1 = sdq_push(&g_q, &j);
    int got1 = 0, got2 = 0, other = 0;
    for (int i = 0; i < SDQ_N && sdq_pop(&g_q, &j); i++) {
        if (j.owner == 1)
            got1++;
        else if (j.owner == 2)
            got2++;
        else
            other++;
    }
    char pn[128];
    snprintf(pn, sizeof pn, "pinned: two pushes racing for one cell both arrive once (%d, %d, %d other)", got1,
             got2, other);
    int pinned_ok = !ut_hook && r1 == 0 && g_push2_rc == 0 && got1 == 1 && got2 == 1 && other == 0;
    CHECK(pn, pinned_ok);
    ut_hook = 0;
    if (!pinned_ok) /* the threads below are load for a queue that works: a broken one can leave */
        return;     /* its producers spinning inside push, which no flag reaches */

    sdq_init(&g_q);
    pthread_t t[QP];
    for (int p = 0; p < QP; p++)
        pthread_create(&t[p], NULL, producer, (void *) (intptr_t) p);
    int next[QP] = {0}, bad_order = 0, got = 0;
    long last = mono_ms();
    /* Ends when every job arrived, or when every producer has returned and the queue is empty
     * (a lost job never arrives). The bound is on progress, not on the whole run: a loaded
     * machine slows a working queue down (measured: 45000 of 80000 jobs in 10 s), it does not
     * stop it for 10 s; only a queue that leaves its producers spinning does. */
    while (got < QP * QJOBS && mono_ms() - last < 10000) {
        int done = atomic_load(&g_qdone) == QP; /* read before the pop: nothing is pushed after */
        if (!sdq_pop(&g_q, &j)) {
            if (done)
                break;
        } else {
            last = mono_ms();
            if (j.owner < 0 || j.owner >= QP || j.ms != next[j.owner])
                bad_order++;
            else
                next[j.owner]++;
            got++;
        }
    }
    atomic_store(&g_qstop, 1);
    for (int p = 0; p < QP; p++)
        pthread_join(t[p], NULL);
    int extra = 0;
    while (extra < 10 * SDQ_N && sdq_pop(&g_q, &j)) /* bounded: a broken queue may never look empty */
        extra++;
    char n[96];
    snprintf(n, sizeof n, "%d producers: every job once (%d of %d, %d extra)", QP, got, QP * QJOBS, extra);
    CHECK(n, got == QP * QJOBS && extra == 0);
    snprintf(n, sizeof n, "each producer's jobs in order (%d out of order)", bad_order);
    CHECK(n, bad_order == 0);
}

/* ---------------------------------------------------------------- thread_start */
static void test_thread(void) {
    ut_suite = "thread";
    uintptr_t lo = 0x1000, hi = 0x2000;
    CHECK("the mod's own code, loaded: allowed", sd_thread_check(0, 1, 0x1800, lo, hi, 0) == SDT_OK);
    CHECK("from on_frame: refused", sd_thread_check(1, 1, 0x1800, lo, hi, 0) == SDT_FRAME);
    CHECK("not loaded: refused", sd_thread_check(0, 0, 0x1800, lo, hi, 0) == SDT_STATE);
    CHECK("entry point outside the mod: refused", sd_thread_check(0, 1, 0x2000, lo, hi, 0) == SDT_FN &&
                                                      sd_thread_check(0, 1, 0x0fff, lo, hi, 0) == SDT_FN);
    CHECK("unknown range: refused", sd_thread_check(0, 1, 0x1800, 0, 0, 0) == SDT_FN);
    CHECK("too many: refused", sd_thread_check(0, 1, 0x1800, lo, hi, SD_MAX_THREADS) == SDT_MANY &&
                                   sd_thread_check(0, 1, 0x1800, lo, hi, SD_MAX_THREADS - 1) == SDT_OK);
}

/* ---------------------------------------------------------------- pad mode */
static void test_padmode(void) {
    ut_suite = "padmode";
    CHECK("BEAT LOOP page 1", sd_pad_mode(3 + 1, 7) == SD_PAD_BEATLOOP);
    CHECK("BEAT LOOP page 2", sd_pad_mode(4 + 1, 7) == SD_PAD_BEATLOOP);
    CHECK("SLIP LOOP", sd_pad_mode(7 + 1, 7) == SD_PAD_SLIPLOOP);
    CHECK("an unmapped mode: unknown", sd_pad_mode(1 + 1, 7) == -1 && sd_pad_mode(0 + 1, 7) == -1);
    CHECK("not committed / stale: unknown", sd_pad_mode(0, 7) == -1 && sd_pad_mode(-1, 7) == -1);
}

int main(int argc, char **argv) {
    if (argc > 1)
        secs_arg = atof(argv[1]);
    alarm(120); /* a broken queue can spin inside push or pop: a hang is a failure, not a wait */
#ifdef GRID_INC
    test_grid();
#endif
    test_playing();
    test_events();
    test_confirm();
    test_queue();
    test_thread();
    test_padmode();
    ut_suite = "sdk";
    return ut_summary();
}
