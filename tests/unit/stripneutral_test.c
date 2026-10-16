/* SPDX-License-Identifier: GPL-3.0-only */
/* dm_strip_neutral (extracted from azmod/day_mode.c by run.sh): on a light theme a
 * strip's neutral strokes (a white overview after a load) go dark; colored
 * strokes and their dimmed (played) copies stay exactly as they are. Pixels are
 * premultiplied ARGB, as dm_draw_additive's black conversion leaves them (alpha = max). */
#include <stdint.h>
#include STRIP_INC
#include "check.h"
static int lum(uint32_t p) { return (int) (((p >> 16) & 0xff) + ((p >> 8) & 0xff) + (p & 0xff)) / 3; }
int main(void) {
    ut_suite = "strip neutral";
    CHECK("opaque white -> dark", lum(dm_strip_neutral(0xffffffffu)) <= 60);
    CHECK("(238,255,255) -> dark", lum(dm_strip_neutral(0xffeeffffu)) <= 70);
    CHECK("(255,255,220) -> dark", lum(dm_strip_neutral(0xffffffdcu)) <= 80);
    CHECK("alpha kept", (dm_strip_neutral(0x80808080u) >> 24) == 0x80);
    CHECK("half-alpha gray -> darker, still premultiplied", (dm_strip_neutral(0x80808080u) & 0xff) <= 0x80 &&
                                                                (dm_strip_neutral(0x80808080u) & 0xff) < 0x40);
    CHECK("pure red unchanged", dm_strip_neutral(0xffff0000u) == 0xffff0000u);
    CHECK("RGB-waveform pink unchanged", dm_strip_neutral(0xffff60c0u) == 0xffff60c0u);
    CHECK("dimmed (played) green unchanged", dm_strip_neutral(0x60003000u | 0x0000600u) == (0x60003000u | 0x0000600u));
    CHECK("transparent unchanged", dm_strip_neutral(0) == 0);
    return ut_summary();
}
