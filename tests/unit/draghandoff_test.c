/* SPDX-License-Identifier: GPL-3.0-only */
/* drag handoff (guest/azmod/drag_handoff.h): how a playlist drag ends, recorded on the
 * mixer-bus read thread and performed on the message thread.
 *
 * Sequential cases pin the semantics: the first recorded end of a drag wins, a second one
 * (drop then BACK, two drops, BACK then drop) is refused, a message-thread end and a bus
 * end can never both act, a frame that saw an older drag cannot end a newer one, and a new
 * drag cannot open over an untaken end.
 *
 * The threaded case runs a producer (the bus thread's use) against a consumer (the
 * message tick's use) over many drags and checks, per drag: ended exactly once (no double
 * execution), every recorded end taken (no lost update), the taken record is the first
 * one recorded for that drag and its fields belong to that drag (no wrong order, no torn
 * or stale record). Built with -fsanitize=thread, the same case also proves the
 * publication order (the record is written before `full` is released).
 *
 * DH_HDR: the header under test (the real one, or a control with one protection removed).
 * Usage: t [drags]   (default 200000) */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include DH_HDR

static struct dh_intent mk(int op, int o, int t, int into, int disp) {
    struct dh_intent it = {op, o, t, into, disp};
    return it;
}

static void sequential(void) {
    struct dh_slot s = DH_SLOT_INIT;
    struct dh_intent got;
    unsigned st;

    /* nothing open at start */
    st = dh_state(&s);
    CHECK("no drag open at start: a recorded end is refused",
          !dh_post(&s, st, &(struct dh_intent) {DH_COMMIT, 1, 2, -1, -1}));
    CHECK("no drag open at start: the message thread cannot claim an end", !dh_claim_local(&s));
    CHECK("no drag open at start: nothing to take", !dh_take(&s, &got));

    /* drop, then BACK, in one drag: the drop stands */
    CHECK("open drag 1", dh_open(&s));
    st = dh_state(&s);
    CHECK("drag 1 is open", dh_is_open(st) && !dh_closed(&s));
    struct dh_intent c = mk(DH_COMMIT, 3, 7, -1, -1);
    CHECK("drop recorded", dh_post(&s, st, &c));
    CHECK("drag is closed once the drop is recorded", dh_closed(&s));
    CHECK("BACK after the drop refused (same frame)",
          !dh_post(&s, st, &(struct dh_intent) {DH_CANCEL, 3, -1, -1, -1}));
    CHECK("BACK after the drop refused (next frame)",
          !dh_post(&s, dh_state(&s), &(struct dh_intent) {DH_CANCEL, 3, -1, -1, -1}));
    memset(&got, 0, sizeof got);
    CHECK("the drop is taken", dh_take(&s, &got));
    CHECK("the taken record is the drop, rows intact",
          got.op == DH_COMMIT && got.origin == 3 && got.target == 7 && got.into == -1 &&
              got.into_disp == -1);
    CHECK("taken exactly once", !dh_take(&s, &got));

    /* two drops: the first one's rows are the ones performed */
    CHECK("open drag 2", dh_open(&s));
    st = dh_state(&s);
    CHECK("first drop recorded", dh_post(&s, st, &(struct dh_intent) {DH_COMMIT, 1, 5, 4, 6}));
    CHECK("second drop refused", !dh_post(&s, st, &(struct dh_intent) {DH_COMMIT, 1, 9, -1, -1}));
    CHECK("the first drop is taken", dh_take(&s, &got) && got.op == DH_COMMIT && got.target == 5 &&
                                         got.into == 4 && got.into_disp == 6);
    CHECK("the second drop never surfaces", !dh_take(&s, &got));

    /* BACK, then a drop: the cancel stands */
    CHECK("open drag 3", dh_open(&s));
    st = dh_state(&s);
    CHECK("BACK recorded", dh_post(&s, st, &(struct dh_intent) {DH_CANCEL, 2, -1, -1, -1}));
    CHECK("drop after BACK refused", !dh_post(&s, st, &(struct dh_intent) {DH_COMMIT, 2, 0, -1, -1}));
    CHECK("the cancel is taken", dh_take(&s, &got) && got.op == DH_CANCEL && got.origin == 2);

    /* the message thread ends the drag first: a later bus record is refused */
    CHECK("open drag 4", dh_open(&s));
    st = dh_state(&s);
    CHECK("message thread claims the end", dh_claim_local(&s));
    CHECK("bus drop after the local end refused",
          !dh_post(&s, st, &(struct dh_intent) {DH_COMMIT, 0, 1, -1, -1}));
    CHECK("nothing to take after a local end", !dh_take(&s, &got));
    CHECK("a second local claim refused", !dh_claim_local(&s));

    /* the bus records first: the message thread loses the claim and takes the record */
    CHECK("open drag 5", dh_open(&s));
    st = dh_state(&s);
    CHECK("bus drop recorded", dh_post(&s, st, &(struct dh_intent) {DH_COMMIT, 4, 2, -1, -1}));
    CHECK("local claim after the bus record refused", !dh_claim_local(&s));
    CHECK("the bus record is still there to take", dh_take(&s, &got) && got.origin == 4);

    /* a new drag cannot open over an untaken end */
    CHECK("open drag 6", dh_open(&s));
    st = dh_state(&s);
    CHECK("drop recorded (6)", dh_post(&s, st, &(struct dh_intent) {DH_COMMIT, 1, 2, -1, -1}));
    CHECK("GRAB refused while the end is untaken", !dh_open(&s));
    CHECK("the drag stays closed after the refused GRAB", dh_closed(&s));
    CHECK("the untaken end survives the refused GRAB", dh_take(&s, &got) && got.target == 2);
    CHECK("GRAB opens once it is taken", dh_open(&s));

    /* a frame that saw an older drag cannot end a newer one */
    unsigned old = dh_state(&s); /* drag 7 open */
    CHECK("drag 7 ended by the message thread", dh_claim_local(&s));
    CHECK("open drag 8", dh_open(&s));
    CHECK("drag 8 is a new generation", dh_state(&s) != old);
    CHECK("a record against drag 7's state is refused in drag 8",
          !dh_post(&s, old, &(struct dh_intent) {DH_COMMIT, 7, 7, -1, -1}));
    CHECK("drag 8 is still open after the stale record", !dh_closed(&s));
    CHECK("a record against drag 8's own state is accepted",
          dh_post(&s, dh_state(&s), &(struct dh_intent) {DH_COMMIT, 8, 1, -1, -1}) &&
              dh_take(&s, &got) && got.origin == 8);

    CHECK("counters: every accepted record taken", s.posted == s.taken);
}

/* ---- threaded: the bus thread's use against the message tick's use ---- */
static struct dh_slot g_s = DH_SLOT_INIT;
static int g_n;    /* drags the arrays hold: the cap */
static int g_done;
/* Which end wins a drag is a race the consumer's spin window steers: no spins and it claims
 * at once (a local end), many and the bus record usually lands first. The run goes in
 * batches until each kind of end has occurred at least DH_MIN_EACH times, each batch steered
 * toward the rarer kind, up to DH_BATCHES batches. Both kinds then occur in every run
 * instead of by chance: under emulation one kind could fall under 1% of a fixed window. */
#define DH_MIN_EACH 100
#define DH_BATCHES 8
static int g_last;      /* consumer: the last drag it ran (published at the end) */
static int g_by_bus, g_by_msg;
static int *first_op;  /* producer: the op of the first record accepted for drag k */
static int *accepted;  /* producer: records accepted for drag k */
static int *executed;  /* consumer: records taken and performed for drag k */
static int *local_end; /* consumer: drags k the message thread ended itself */
static int *wrong;     /* consumer: a taken record whose fields are not drag k's */
static int *wrong_op;  /* consumer: the op taken (checked against first_op after join) */
static unsigned rng(unsigned *x) {
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}

static void *producer(void *arg) {
    unsigned x = 0x9e3779b9u;
    (void) arg;
    while (!__atomic_load_n(&g_done, __ATOMIC_ACQUIRE)) {
        unsigned st = dh_state(&g_s); /* once per "frame", as the bus thread does */
        if (!dh_is_open(st))
            continue;
        int k = (int) (st >> 1); /* the drag this frame saw */
        if (k < 1 || k > g_n)
            continue;
        if (rng(&x) & 3)
            continue; /* most frames carry no drop or BACK */
        int op = (rng(&x) & 1) ? DH_COMMIT : DH_CANCEL;
        struct dh_intent it = {op, k, k * 3 + 1, -k, k ^ 0x55};
        if (dh_post(&g_s, st, &it)) {
            if (!accepted[k]++)
                first_op[k] = op;
            /* the other end on the same frame: must be refused */
            struct dh_intent it2 = {op == DH_COMMIT ? DH_CANCEL : DH_COMMIT, k, 0, 0, 0};
            if (dh_post(&g_s, st, &it2))
                accepted[k]++;
        }
    }
    return NULL;
}

static void *consumer(void *arg) {
    unsigned x = 0x12345u;
#ifndef DH_WINDOW0
#define DH_WINDOW0 64 /* the first batch's window; the tests also start it at 1 and 65536 */
#endif
    unsigned window = DH_WINDOW0; /* spins before claiming: rng % window */
    int batch = g_n / DH_BATCHES, k;
    (void) arg;
    for (k = 1; k <= g_n; k++) {
        if (k > 1 && (k - 1) % batch == 0) { /* a batch done: stop, or steer the next one */
            if (g_by_bus >= DH_MIN_EACH && g_by_msg >= DH_MIN_EACH)
                break;
            if (g_by_msg < g_by_bus)
                window = 1; /* claim at once: local ends */
            else if (window < 65536)
                window *= 16; /* wait longer: bus ends */
        }
        while (!dh_open(&g_s)) { /* never: every end below is taken before the next GRAB */
            struct dh_intent junk;
            if (dh_take(&g_s, &junk))
                wrong[k]++;
        }
        unsigned spins = rng(&x) % window;
        for (;;) {
            struct dh_intent it;
            if (dh_take(&g_s, &it)) {
                executed[k]++;
                g_by_bus++;
                wrong_op[k] = it.op;
                if (it.origin != k || it.target != k * 3 + 1 || it.into != -k ||
                    it.into_disp != (k ^ 0x55))
                    wrong[k]++;
                break;
            }
            if (spins-- == 0) {
                if (dh_claim_local(&g_s)) {
                    local_end[k]++;
                    g_by_msg++;
                    break;
                }
                /* lost the claim: the bus recorded an end, which is published right after */
                while (!dh_take(&g_s, &it)) {
                }
                executed[k]++;
                g_by_bus++;
                wrong_op[k] = it.op;
                if (it.origin != k || it.target != k * 3 + 1 || it.into != -k ||
                    it.into_disp != (k ^ 0x55))
                    wrong[k]++;
                break;
            }
        }
    }
    __atomic_store_n(&g_last, k - 1, __ATOMIC_RELAXED);
    __atomic_store_n(&g_done, 1, __ATOMIC_RELEASE);
    return NULL;
}

static void threaded(int n) {
    n *= DH_BATCHES; /* the cap; the run stops at the first batch that has both kinds */
    g_n = n;
    first_op = calloc(n + 2, sizeof *first_op);
    accepted = calloc(n + 2, sizeof *accepted);
    executed = calloc(n + 2, sizeof *executed);
    local_end = calloc(n + 2, sizeof *local_end);
    wrong = calloc(n + 2, sizeof *wrong);
    wrong_op = calloc(n + 2, sizeof *wrong_op);
    pthread_t p, c;
    pthread_create(&p, NULL, producer, NULL);
    pthread_create(&c, NULL, consumer, NULL);
    pthread_join(c, NULL);
    pthread_join(p, NULL);
    n = g_last; /* the drags actually run */
    int dbl = 0, none = 0, lost = 0, bad = 0, order = 0, twice = 0, by_bus = 0, by_msg = 0;
    for (int k = 1; k <= n; k++) {
        int ends = executed[k] + local_end[k];
        if (ends > 1)
            dbl++;
        if (ends == 0)
            none++;
        if (accepted[k] > 1)
            twice++;
        if (accepted[k] != executed[k])
            lost++;
        if (wrong[k])
            bad++;
        if (executed[k] && wrong_op[k] != first_op[k])
            order++;
        by_bus += executed[k] != 0;
        by_msg += local_end[k] != 0;
    }
    printf("      %d drags (cap %d): %d ended by a bus record, %d by the message thread\n", n, g_n, by_bus, by_msg);
    /* a precondition, not a property: both ends must occur. Steered batches make it hold in
     * every run unless one kind stays under DH_MIN_EACH through every batch. */
    CHECK("threaded: both kinds of end exercised", by_bus >= DH_MIN_EACH && by_msg >= DH_MIN_EACH);
    CHECK("threaded: no drag ended twice (double execution)", dbl == 0);
    CHECK("threaded: every drag ended", none == 0);
    CHECK("threaded: never two records accepted for one drag", twice == 0);
    CHECK("threaded: every accepted record taken (no lost update)", lost == 0);
    CHECK("threaded: the taken record is that drag's, untorn (no stale/torn record)", bad == 0);
    CHECK("threaded: the taken record is the drag's FIRST (no wrong order)", order == 0);
    CHECK("threaded: counters agree", g_s.posted == g_s.taken);
    if (dbl || none || twice || lost || bad || order)
        printf("      double=%d none=%d twice=%d lost=%d wrong=%d order=%d\n", dbl, none, twice,
               lost, bad, order);
}

/* A broken handoff can leave a thread waiting forever (an end that never clears, a claim
 * that never resolves): that is a failure, not a hang. */
static void *watchdog(void *arg) {
    (void) arg;
    sleep(60);
    printf("FAIL  drag handoff: threaded case did not finish in 60 s (an end never cleared or "
           "never published)\n");
    fflush(stdout);
    _exit(3);
}

int main(int argc, char **argv) {
    ut_suite = "drag handoff";
    pthread_t wd;
    pthread_create(&wd, NULL, watchdog, NULL);
    pthread_detach(wd);
    sequential();
    /* The threaded case looks for orderings the sequential cases cannot show (it is what
     * ThreadSanitizer watches). A handoff the sequential cases already failed is not run
     * through it: a broken one can leave a thread waiting until the 60 s watchdog. */
    if (ut_fail)
        printf("----  drag handoff: threaded case not run (the sequential cases failed)\n");
    else
        threaded(argc > 1 ? atoi(argv[1]) : 200000);
    return ut_summary();
}
