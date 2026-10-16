/* SPDX-License-Identifier: GPL-3.0-only */
/* The drawing services community mods call (menu.c azcm_*): extreme values must never write
 * outside the framebuffer. A 64x48 buffer sits between guard bands; every call below must leave
 * the guards untouched, and an in-range call must still draw (the control). */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "menu.h"

#define W 64
#define H 48
#define GUARD 4096
static uint8_t mem[GUARD + W * H * 4 + GUARD];

/* the menu face's width, as menu.o calls it here (run.sh builds menu.o with
 * -Dazmod_ttf_menu_width=draw_ttf_width): the px it is asked for are recorded, since the real one
 * renders the glyphs at that size */
static int g_px_min = INT_MAX, g_px_max = INT_MIN;
int draw_ttf_width(const char *s, int n, int px, int bold) {
    (void) s, (void) bold;
    g_px_min = px < g_px_min ? px : g_px_min;
    g_px_max = px > g_px_max ? px : g_px_max;
    return n * (px / 2 > 0 ? px / 2 : 1);
}
static int guards_clean(void) {
    for (int i = 0; i < GUARD; i++)
        if (mem[i] != 0xA5 || mem[GUARD + W * H * 4 + i] != 0xA5) return 0;
    return 1;
}
static int fb_touched(void) {
    for (int i = 0; i < W * H * 4; i++)
        if (mem[GUARD + i] != 0) return 1;
    return 0;
}
static void reset(void) {
    memset(mem, 0xA5, sizeof mem);
    memset(mem + GUARD, 0, W * H * 4);
}

int main(void) {
    ut_suite = "draw";
    azmod_fb fb = {mem + GUARD, W, H, W * 4};
    az_draw d;
    az_rect area = {0, 0, W, H};
    azcm_draw_init(&d, &fb, area);
    struct {
        const char *name;
        int x, y, w, h;
    } cases[] = {
        {"w = INT_MAX (fill to the edge)", 10, 10, INT_MAX, 5},
        {"h = INT_MAX", 10, 10, 5, INT_MAX},
        {"x = INT_MIN", INT_MIN, 10, 20, 5},
        {"x = INT_MAX", INT_MAX, 10, 20, 5},
        {"y = INT_MIN, h = INT_MAX", 10, INT_MIN, 5, INT_MAX},
        {"negative w", 10, 10, -100, 5},
    };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        reset();
        azcm_fill_rect(&d, cases[k].x, cases[k].y, cases[k].w, cases[k].h, 0xFFFFFF);
        azcm_draw_rect(&d, cases[k].x, cases[k].y, cases[k].w, cases[k].h, INT_MAX, 0xFFFFFF);
        azcm_draw_checkbox(&d, cases[k].x, cases[k].y, cases[k].w, 1, 0);
        CHECK(cases[k].name, guards_clean());
    }
    /* text: positions at the int limits, any px, a long string */
    static char longs[20000];
    memset(longs, 'W', sizeof longs - 1);
    const int pxs[] = {INT_MIN, -1, 0, 8, 96, 97, INT_MAX};
    int bad = 0;
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++)
        for (size_t q = 0; q < sizeof pxs / sizeof pxs[0]; q++) {
            reset();
            azcm_draw_text(&d, cases[k].x, cases[k].y, "Text", pxs[q], 0xFFFFFF);
            azcm_draw_text(&d, cases[k].x, cases[k].y, longs, pxs[q], 0xFFFFFF);
            bad += !guards_clean();
        }
    CHECK("draw_text at the int limits, any px, a 20 KB string: nothing outside the buffer", bad == 0);
    reset();
    azcm_draw_text(&d, 2, 2, "Hi", 12, 0xFFFFFF);
    CHECK("draw_text in range draws (CONTROL)", fb_touched() && guards_clean());
    /* a header on an area narrower than its padding */
    az_draw nd;
    az_rect narrow = {0, 0, 3, H};
    azcm_draw_init(&nd, &fb, narrow);
    reset();
    azcm_draw_header(&nd, "A title wider than its area");
    CHECK("draw_header on an area narrower than its padding: nothing outside the buffer", guards_clean());
    /* text_width: what draw_text would use, never an overflowed or negative width, and the face
     * never asked for a size draw_text would not draw */
    g_px_min = INT_MAX, g_px_max = INT_MIN;
    azcm_text_width("abc", INT_MAX);
    azcm_text_width("abc", INT_MIN);
    CHECK("text_width: the face is asked only for 8..96 px, whatever the mod passes", g_px_min >= 8 && g_px_max <= 96);
    int w8 = azcm_text_width("abc", 8), w96 = azcm_text_width("abc", 96);
    CHECK("text_width: px below 8 and above 96 measure as 8 and 96, as draw_text draws them",
          azcm_text_width("abc", INT_MIN) == w8 && azcm_text_width("abc", -5) == w8 &&
              azcm_text_width("abc", INT_MAX) == w96 && azcm_text_width("abc", 97) == w96);
    CHECK("text_width: a long string at the largest px stays positive", azcm_text_width(longs, INT_MAX) > 0);
    CHECK("text_width: NULL is 0, longer text is wider (CONTROL)", azcm_text_width(NULL, 12) == 0 &&
                                                                       azcm_text_width("abcd", 12) > azcm_text_width("ab", 12));
    reset();
    azcm_fill_rect(&d, 2, 2, 4, 4, 0xFFFFFF);
    CHECK("in range: draws inside the buffer (CONTROL)", fb_touched() && guards_clean());
    return ut_summary();
}
