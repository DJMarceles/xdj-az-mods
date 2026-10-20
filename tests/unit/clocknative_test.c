/* SPDX-License-Identifier: GPL-3.0-only */
/* The Clock in the place of EP147's status bar readout (guest/azmod/clock_native.h):
 *   - EP147's label is hidden and the Clock drawn only while the Clock is on, the label was found
 *     and checked, and EP147's recording icon is out (image 0); recording (1), an icon that cannot
 *     be read (-1) or any other index leaves EP147's label as it is;
 *   - a screen rect packs into one word and back; a rect off the screen or empty packs to 0
 *     (nothing drawn). */
#include "check.h"
#include "clock_native.h"

int main(void) {
    ut_suite = "clock native";
    CHECK("Clock on, label found, not recording: the Clock", cn_take(1, 0, 1) == 1);
    CHECK("recording: EP147's label", cn_take(1, 1, 1) == 0);
    CHECK("icon not readable: EP147's label", cn_take(1, -1, 1) == 0);
    CHECK("an icon index EP147 never uses: EP147's label", cn_take(1, 2, 1) == 0);
    CHECK("label not found or not checked: EP147's label", cn_take(1, 0, 0) == 0);
    CHECK("Clock off: EP147's label", cn_take(0, 0, 1) == 0 && cn_take(0, 1, 1) == 0);
    int x, y, w, h;
    uint64_t r = cn_rect_pack(1129, 25, 68, 20);
    CHECK("rect round trip", r && cn_rect_unpack(r, &x, &y, &w, &h) && x == 1129 && y == 25 && w == 68 && h == 20);
    CHECK("rect at the origin packs (not taken as none)", cn_rect_pack(0, 0, 1, 1) != 0);
    CHECK("empty rect: none", cn_rect_pack(10, 10, 0, 20) == 0 && cn_rect_pack(10, 10, 20, 0) == 0);
    CHECK("off the screen: none", cn_rect_pack(-1, 0, 10, 10) == 0 && cn_rect_pack(0, -1, 10, 10) == 0);
    CHECK("past 4095: none", cn_rect_pack(4096, 0, 10, 10) == 0 && cn_rect_pack(0, 0, 4096, 10) == 0);
    CHECK("none unpacks to nothing", cn_rect_unpack(0, &x, &y, &w, &h) == 0);
    return ut_summary();
}
