/* SPDX-License-Identifier: GPL-3.0-only */
/* Vinyl Speed Adjust save handoff (guest/azmod/vinyl_slider.c): the touch handler on
 * EP147's message thread posts the value, the overlay thread writes it. One slot,
 * newest wins, consumed by the write. VS_INC is the production code, extracted. */
#include <pthread.h>
#include <stdatomic.h>
#include "check.h"

static int n_writes, last_write, order_ok = 1;
static void vs_save(int m) { /* the production vs_save returns on m < 0 */
    if (m < 0)
        return;
    if (n_writes && m < last_write)
        order_ok = 0; /* an older value written after a newer one */
    n_writes++;
    last_write = m;
}
#include VS_INC

static void *poster(void *a) {
    (void) a;
    for (int i = 1; i <= 20000; i++)
        vs_save_post(i);
    return 0;
}
int main(void) {
    ut_suite = "vinyl save handoff";
    vs_save_flush();
    CHECK("nothing posted -> nothing written", n_writes == 0);
    vs_save_post(500);
    CHECK("posting does not write (the message thread never touches p7)", n_writes == 0);
    vs_save_flush();
    CHECK("posted 500 -> one write of 500", n_writes == 1 && last_write == 500);
    vs_save_flush();
    CHECK("the write consumes the slot (no second write)", n_writes == 1);
    vs_save_post(400);
    vs_save_post(450);
    vs_save_flush();
    CHECK("two posts before a tick -> only the newest (450) is written",
          n_writes == 2 && last_write == 450);
    vs_save_post(-1);
    vs_save_flush();
    CHECK("an invalid value (-1) is never posted", n_writes == 2);
    /* concurrent: one poster, the flusher racing it */
    n_writes = 0;
    order_ok = 1; /* the serial cases above posted 450 after 500 on purpose */
    last_write = 0;
    pthread_t t;
    pthread_create(&t, 0, poster, 0);
    for (int i = 0; i < 200000; i++)
        vs_save_flush();
    pthread_join(t, 0);
    vs_save_flush();
    CHECK("racing poster: the final write is the last value posted", last_write == 20000);
    CHECK("racing poster: values are written in order, never an older after a newer", order_ok);
    return ut_summary();
}
