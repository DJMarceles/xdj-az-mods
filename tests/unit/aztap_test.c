/* SPDX-License-Identifier: GPL-3.0-only */
/* The audio tap ring (guest/common/aztap.h): one producer on the audio thread, one consumer.
 * A producer thread pushes numbered frames in 64-frame blocks as fast as it can while a
 * deliberately slow consumer drains. The ring must never make the producer wait, must drop
 * whole blocks it has no room for and count them, and must hand the consumer frames in order:
 * every frame it sees is either the next one or follows a counted gap. A ring nobody wants
 * stores nothing. The controls let the producer overwrite unread frames and drop the
 * "nobody wants it" check; each must fail. TAP_HDR = the header under test.
 * The overflow path is first tested single-threaded (a full ring, no consumer: whole blocks
 * dropped and counted, the stored frames intact, and a push that returns proves it did not
 * wait). The threaded run then steers itself instead of trusting timing: its very slow consumer
 * rounds wait until the ring has actually dropped a block, and it runs until it has seen enough
 * frames, so a loaded machine makes it slower, never a false FAIL. Push time is measured in
 * this thread's CPU time, which preemption does not inflate. */
#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include TAP_HDR

static struct aztap_ring q;
static _Atomic int stop;
static double max_push_us;
static unsigned pushed;

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static double cpu_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static void *producer(void *arg) {
    (void) arg;
    float L[64], R[64];
    unsigned n = 0;
    while (!atomic_load(&stop)) {
        for (int i = 0; i < 64; i++)
            L[i] = R[i] = (float) ((n + (unsigned) i) & 0xFFFFF);
        double t0 = cpu_us();
        aztap_push_planar(&q, L, R, 64);
        double dt = cpu_us() - t0;
        if (dt > max_push_us)
            max_push_us = dt;
        n += 64;
        struct timespec ts = {0, 20000}; /* a block every ~20 us: well past real time, never wraps */
        nanosleep(&ts, NULL);
    }
    pushed = n;
    return NULL;
}

int main(void) {
    ut_suite = "audio tap ring";
    /* nobody wants it: nothing is stored */
    float L[64] = {0}, R[64] = {0};
    aztap_push_planar(&q, L, R, 64);
    CHECK("a ring nobody wants stores nothing", atomic_load(&q.w) == 0 && atomic_load(&q.dropped) == 0);

    aztap_set_wanted(&q, 1);
    /* single-threaded overflow: 300 numbered blocks into a ring of AZTAP_FRAMES, nobody reading.
     * A push that waited for room would never return (the 10 s alarm ends the run). */
    alarm(10);
    for (unsigned b = 0; b < 300; b++) {
        for (int i = 0; i < 64; i++)
            L[i] = R[i] = (float) (b * 64 + (unsigned) i);
        aztap_push_planar(&q, L, R, 64);
    }
    alarm(0);
    unsigned fill = atomic_load(&q.w) - atomic_load(&q.r), dropped0 = atomic_load(&q.dropped);
    CHECK("a full ring drops whole blocks and counts them (no consumer, nothing overwritten)",
          fill == AZTAP_FRAMES && dropped0 == 300 * 64 - AZTAP_FRAMES);
    static float buf[AZTAP_FRAMES * 2];
    int got = aztap_pop(&q, buf, AZTAP_FRAMES);
    int intact = got == AZTAP_FRAMES;
    for (int i = 0; intact && i < got; i++)
        if (buf[i * 2] != (float) i || buf[i * 2 + 1] != (float) i)
            intact = 0;
    CHECK("...and the frames it kept are the oldest, intact and in order", intact);
    /* steer the threaded run toward a drop only when the ring can drop at all: a ring that
     * already failed the overflow check has failed this run, and would only wait out the cap */
    int steer = fill == AZTAP_FRAMES && dropped0 == 300 * 64 - AZTAP_FRAMES;

    pthread_t th;
    pthread_create(&th, NULL, producer, NULL);
    unsigned long seen = 0, in_order = 1;
    long expect = -1;
    double t_end = now_us() + 1.5e6, t_cap = now_us() + 60e6;
    int round = 0;
    /* at least 1.5 s; longer only until the run has seen 50000 frames and a drop (60 s cap) */
    while (now_us() < t_cap &&
           (now_us() < t_end || (steer && (seen <= 50000 || atomic_load(&q.dropped) == dropped0)))) {
        int n = aztap_pop(&q, buf, AZTAP_FRAMES);
        for (int i = 0; i < n; i++) {
            long v = (long) buf[i * 2];
            if (buf[i * 2 + 1] != buf[i * 2])
                in_order = 0; /* left and right of one frame come from one push */
            if (expect >= 0 && v != expect) {
                long d = (v - expect) & 0xFFFFF;
                if (d % 64 || d == 0 || d >= 0x80000)
                    in_order = 0; /* not a whole dropped block forward: torn, overwritten or repeated */
            }
            expect = (v + 1) & 0xFFFFF;
        }
        seen += (unsigned long) n;
        if (++round % 7) {
            usleep(2000); /* a slow consumer */
        } else { /* sometimes very slow: until the ring has dropped a block (10 s bound) */
            unsigned d0 = atomic_load(&q.dropped);
            for (int k = 0; k < (steer ? 10000 : 40) && atomic_load(&q.dropped) == d0; k++)
                usleep(1000);
        }
    }
    atomic_store(&stop, 1);
    pthread_join(th, NULL);
    unsigned long rest = 0;
    int n;
    while ((n = aztap_pop(&q, buf, AZTAP_FRAMES)) > 0)
        rest += (unsigned long) n;
    unsigned dropped = atomic_load(&q.dropped) - dropped0;
    char m[160];
    snprintf(m, sizeof m, "frames arrive in order; gaps are whole dropped blocks (%lu seen)", seen);
    CHECK(m, in_order && seen > 50000);
    snprintf(m, sizeof m, "every pushed frame is either delivered or counted as dropped (%u pushed)", pushed);
    CHECK(m, seen + rest + dropped == pushed);
    CHECK("the slow consumer made the ring drop blocks (this run tested the overflow path)", dropped > 0);
    snprintf(m, sizeof m, "the producer never waited (longest push %.0f us of its CPU time)", max_push_us);
    CHECK(m, max_push_us < 2000);
    aztap_set_wanted(&q, 0);
    unsigned w0 = atomic_load(&q.w);
    aztap_push_planar(&q, L, R, 64);
    CHECK("switched off again: nothing more is stored", atomic_load(&q.w) == w0);
    return ut_summary();
}
