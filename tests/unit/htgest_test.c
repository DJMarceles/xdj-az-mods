/* SPDX-License-Identifier: GPL-3.0-only */
/* Hide Track Title's tap rule (guest/azmod/ht_tap.h): a title toggles only on a whole tap, one
 * finger pressed and lifted close to the press point within HT_TAP_MAX_MS, with no other
 * finger on the screen. The touch sequences are the X button events EP147's touch decoder
 * sends on the hardware (button 1 = the first finger, 12 and up = further fingers; a moving
 * finger sends a press at each step). HTTAP_HDR = the header under test. */
#include "check.h"
#include HTTAP_HDR

static struct ht_gest g;
static int tx, ty;

static int press(unsigned b, int x, int y, long ms) {
    return ht_gest_feed(&g, 1, b, x, y, ms, &tx, &ty);
}
static int lift(unsigned b, int x, int y, long ms) {
    return ht_gest_feed(&g, 0, b, x, y, ms, &tx, &ty);
}

int main(void) {
    ut_suite = "hide title tap";

    tx = ty = -1;
    CHECK("a press alone is not a tap", press(1, 300, 580, 1000) == 0 && tx == -1);
    CHECK("press and lift in place is a tap, at the press point",
          lift(1, 303, 578, 1080) == 1 && tx == 300 && ty == 580);

    press(1, 300, 580, 2000);
    CHECK("a drag that starts on the bar is not a tap", lift(1, 300, 700, 2150) == 0);

    press(1, 300, 580, 3000);
    CHECK("a finger resting on the bar is not a tap", lift(1, 300, 580, 3000 + HT_TAP_MAX_MS + 1) == 0);
    press(1, 300, 580, 3500);
    CHECK("a tap of exactly the longest time counts", lift(1, 300, 580, 3500 + HT_TAP_MAX_MS) == 1);

    press(1, 300, 580, 4000);
    CHECK("a lift exactly the slop away counts",
          lift(1, 300 + HT_TAP_SLOP, 580 - HT_TAP_SLOP, 4050) == 1);
    press(1, 300, 580, 4100);
    CHECK("a lift one px beyond the slop does not", lift(1, 300 + HT_TAP_SLOP + 1, 580, 4150) == 0);

    press(1, 300, 580, 5000);
    press(12, 900, 300, 5020);
    lift(12, 900, 300, 5040);
    CHECK("a second finger during the touch voids it", lift(1, 300, 580, 5060) == 0);

    press(12, 900, 300, 6000);
    press(1, 300, 580, 6020);
    CHECK("a touch while another finger rests on the screen is not a tap",
          lift(1, 300, 580, 6060) == 0);
    lift(12, 900, 300, 6100);
    press(1, 300, 580, 6200);
    CHECK("once that finger lifts, a tap counts again", lift(1, 300, 580, 6260) == 1);

    CHECK("a lift with no press is not a tap", lift(1, 300, 580, 7000) == 0);

    press(1, 300, 580, 8000);
    CHECK("a lift timed before its press is not a tap", lift(1, 300, 580, 7990) == 0);

    press(1, 300, 580, 9000);
    lift(1, 300, 700, 9050);
    press(1, 300, 580, 9100);
    CHECK("a tap right after a rejected drag counts", lift(1, 300, 580, 9150) == 1);

    /* the deck's stream for a drag that ends on the bar: a press at every step, ~60 ms apart */
    press(1, 300, 700, 10000);
    for (int k = 1; k <= 12; k++)
        press(1, 300, 700 - k * 10, 10000 + k * 60);
    CHECK("a drag sent as a press at every step is not a tap where it lifts",
          lift(1, 300, 580, 10000 + 12 * 60 + 5) == 0);

    press(1, 300, 580, 11000);
    press(1, 302, 581, 11060);
    press(1, 303, 579, 11120);
    CHECK("a finger that jitters within the slop is still a tap, at the first press",
          lift(1, 303, 579, 11150) == 1 && tx == 300 && ty == 580);

    press(1, 300, 580, 12000);
    press(1, 300, 600, 12060);
    press(1, 300, 581, 12120);
    CHECK("a finger that left the slop and came back is not a tap", lift(1, 300, 581, 12150) == 0);

    press(1, 900, 300, 13000); /* its release never arrives */
    press(1, 300, 580, 13000 + TOUCH_SEQ_GAP_MS + 1);
    CHECK("a press long after a lost release is a new touch",
          lift(1, 300, 580, 13000 + TOUCH_SEQ_GAP_MS + 40) == 1 && tx == 300 && ty == 580);

    return ut_summary();
}
