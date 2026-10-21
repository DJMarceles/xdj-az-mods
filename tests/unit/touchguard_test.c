/* SPDX-License-Identifier: GPL-3.0-only */
/* The touch reader guard (guest/azmod/touch_read_guard.h).
 *
 * EP147's touch reader passes read()'s result to its parser as a signed int, widened to 64 bits;
 * the parser treats anything above 23 as (count / 24) events. A failed read (-1) therefore
 * becomes a loop of about 7.7e17 events over a 0x600 byte stack buffer. The model below is that
 * parser's arithmetic, so the test can state the property that matters: after the guard, no
 * result the reader can see asks for more events than the buffer holds (0x600 / 24 = 64).
 *   - the descriptor is the touch reader's only by exact path, and only until it is closed;
 *   - a failed read on it is 0, every other result of it is unchanged;
 *   - a failed read on any other descriptor is unchanged (the CONTROL for the guard);
 *   - two threads: one opens and closes descriptors, one asks about them; run under TSan.
 * A build with TG_INC set to a mutated copy of the header must fail. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "check.h"

#include TG_INC

/* the parser's event count for a read() result handed over as an int */
static uint64_t events_for(ssize_t r) {
    int w1 = (int) r;
    uint64_t x1 = (uint64_t) (int64_t) w1; /* sxtw */
    return x1 <= 0x17 ? 0 : x1 / 24;
}
#define MAX_EVENTS (0x600 / 24)

static int g_stop, g_bad; /* __atomic only */
static void *churn(void *a) {
    (void) a;
    for (int i = 0; i < 300000; i++) {
        tg_note(40 + (i & 7), (i & 1) ? TG_PATH : "/dev/input/event0");
        tg_forget(40 + ((i + 3) & 7));
    }
    __atomic_store_n(&g_stop, 1, __ATOMIC_RELEASE);
    return 0;
}
static void *asker(void *a) {
    (void) a;
    while (!__atomic_load_n(&g_stop, __ATOMIC_ACQUIRE)) {
        if (tg_result(7, -1) != 0) /* permanently the reader */
            __atomic_store_n(&g_bad, 1, __ATOMIC_RELAXED);
        if (tg_result(99, -1) != -1) /* never noted */
            __atomic_store_n(&g_bad, 1, __ATOMIC_RELAXED);
        for (int fd = 40; fd < 48; fd++) {
            ssize_t v = tg_result(fd, -1);
            if (v != 0 && v != -1)
                __atomic_store_n(&g_bad, 1, __ATOMIC_RELAXED);
        }
    }
    return 0;
}

int main(void) {
    ut_suite = "touch read guard";

    CHECK("the unguarded parser takes -1 as an enormous event count", events_for(-1) > MAX_EVENTS * 1000000);
    CHECK("the model: 0 and short reads ask for no events", events_for(0) == 0 && events_for(23) == 0);
    CHECK("the model: a full buffer asks for 64", events_for(0x600) == MAX_EVENTS);

    CHECK("nothing noted: a failed read is left alone", tg_result(7, -1) == -1);
    tg_note(7, "/dev/input/by-id/gt928");
    CHECK("the touch reader's descriptor is tracked", tg_tracked(7));
    CHECK("its failed read is reported as no data", tg_result(7, -1) == 0);
    CHECK("... which the parser handles (no events)", events_for(tg_result(7, -1)) == 0);
    CHECK("its successful reads are unchanged", tg_result(7, 48) == 48 && tg_result(7, 0x600) == 0x600);
    CHECK("its end of file is unchanged", tg_result(7, 0) == 0);

    CHECK("CONTROL: an unrelated descriptor's failed read is unchanged", tg_result(8, -1) == -1);
    tg_note(9, "/dev/input/by-id/gt928-other");
    CHECK("a path that only starts the same is not the reader", !tg_tracked(9) && tg_result(9, -1) == -1);
    tg_note(10, "/dev/input/event0");
    CHECK("another input device is not the reader", !tg_tracked(10) && tg_result(10, -1) == -1);
    tg_note(11, NULL);
    CHECK("a NULL path tracks nothing", !tg_tracked(11));
    CHECK("a negative descriptor is never tracked", !tg_tracked(-1) && tg_result(-1, -1) == -1);

    tg_forget(7);
    CHECK("closed: the number is no longer the reader", !tg_tracked(7) && tg_result(7, -1) == -1);
    tg_note(7, "/dev/input/by-id/gt928");
    tg_note(7, "/tmp/unrelated"); /* open() handed the number out again for another file */
    CHECK("the number reused by another file is forgotten", !tg_tracked(7));

    tg_note(20, TG_PATH);
    tg_note(21, TG_PATH);
    tg_note(22, TG_PATH);
    tg_note(23, TG_PATH);
    tg_note(24, TG_PATH); /* more than the table holds: not tracked, never a wrong answer */
    CHECK("the table's capacity is respected", tg_tracked(20) && tg_tracked(23) && !tg_tracked(24));
    for (int fd = 20; fd < 25; fd++)
        tg_forget(fd);
    CHECK("all forgotten", !tg_tracked(20) && !tg_tracked(23));
    tg_forget(-5); /* harmless */

    /* two threads: churn the table while another thread asks */
    tg_note(7, TG_PATH);
    pthread_t t1, t2;
    pthread_create(&t1, 0, churn, 0);
    pthread_create(&t2, 0, asker, 0);
    pthread_join(t1, 0);
    pthread_join(t2, 0);
    CHECK("under concurrent open/close every answer is 0 or the raw -1, the fixed ones exact",
          !__atomic_load_n(&g_bad, __ATOMIC_RELAXED));
    return ut_summary();
}
