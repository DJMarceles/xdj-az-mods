/* SPDX-License-Identifier: GPL-3.0-only */
/* Bright Grid's pixel rule (guest/azmod/brightgrid.h), built natively. Two questions: is a
 * moving ruler's THEMED background left alone, and is every tick pixel brightened exactly as
 * before? The colors are the Midnight theme's: the ruler background is the page's own navy
 * (5,11,24), which a rule that brightens the background turns into (6,12,26) while a track
 * plays. old_row() is the 1.2.2 rule, kept here as the reference the ticks must still
 * match. run.sh also builds it from mutated copies of the header; each must
 * fail. */
#include <stdint.h>
#include <string.h>
#include "check.h"
#include "brightgrid.h"

#define W 64
static uint32_t argb(unsigned a, unsigned r, unsigned g, unsigned b) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}
/* the shipped table and loop body (its own copy, so a change to the header's table shows) */
static void old_lut(uint8_t lut[256], float g) {
    for (int i = 0; i < 256; i++) {
        int v = (int) (i * g + 0.5f);
        lut[i] = (uint8_t) (v > 255 ? 255 : v);
    }
}
static void old_row(uint32_t *row, int w, float g, uint32_t graypx) {
    uint8_t lut[256];
    old_lut(lut, g);
    int inked = 0;
    for (int x = 0; x < w && !inked; x++)
        inked = (row[x] & 0x00ffffffu) != 0;
    for (int x = 0; x < w; x++) {
        uint32_t px = row[x];
        if (!(px & 0x00ffffffu)) {
            if (graypx && inked)
                row[x] = graypx;
            continue;
        }
        unsigned R = lut[(px >> 16) & 255], G = lut[(px >> 8) & 255], B = lut[px & 255];
        unsigned A = px >> 24, m = R > G ? (R > B ? R : B) : (G > B ? G : B);
        if (A < m)
            A = m;
        row[x] = (A << 24) | (R << 16) | (G << 8) | B;
    }
}
static uint32_t rng = 12345;
static uint32_t next(void) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 1;
}

int main(void) {
    ut_suite = "bright grid";
    uint8_t lut[256];
    uint32_t row[W], ref[W];
    const uint32_t navy = argb(255, 5, 11, 24); /* Midnight's ground = the page background */
    const float gains[] = {1.1f, 1.43f};       /* moving, and gain x overdrive on a step frame */

    for (int k = 0; k < 2; k++) {
        bg_build_lut(lut, gains[k]);
        for (int x = 0; x < W; x++)
            row[x] = navy;
        bg_row(row, W, lut, navy, 0);
        int same = 1;
        for (int x = 0; x < W; x++)
            same &= row[x] == navy;
        CHECK(k ? "the themed background stays the page color on a step frame (1.43)"
                : "the themed background stays the page color while moving (1.1)",
              same);
    }

    /* ticks: identical to the shipped rule, themed or not, over many random rows */
    int ticks_same = 1, stock_same = 1, saw_tick = 0;
    for (int trial = 0; trial < 2000; trial++) {
        bg_build_lut(lut, gains[trial & 1]);
        for (int x = 0; x < W; x++) {
            uint32_t r = next() % 4;
            uint32_t v = next() & 255, a = 255;
            row[x] = r == 0 ? navy : r == 1 ? 0
                                            : argb(a, v, v, v); /* bg, transparent, tick */
        }
        memcpy(ref, row, sizeof row);
        old_row(ref, W, gains[trial & 1], 0);
        uint32_t before[W];
        memcpy(before, row, sizeof row);
        bg_row(row, W, lut, navy, 0);
        for (int x = 0; x < W; x++) {
            if (before[x] == navy || !(before[x] & 0x00ffffffu))
                continue;
            saw_tick = 1;
            ticks_same &= row[x] == ref[x];
        }
        /* stock: no theme background (0) = the shipped rule for every pixel */
        memcpy(row, before, sizeof row);
        memcpy(ref, before, sizeof row);
        old_row(ref, W, gains[trial & 1], 0);
        bg_row(row, W, lut, 0, 0);
        stock_same &= !memcmp(row, ref, sizeof row);
    }
    CHECK("every tick pixel is brightened exactly as before (themed ruler)", saw_tick && ticks_same);
    CHECK("stock (no theme background): the whole row matches the shipped rule", stock_same);

    /* the gray strip (a development override, off by default) */
    const uint32_t gray = argb(255, 24, 24, 24);
    bg_build_lut(lut, 1.1f);
    for (int x = 0; x < W; x++)
        row[x] = x < 8 ? navy : 0;
    bg_row(row, W, lut, navy, gray);
    CHECK("a row of background and transparency is not a tick row", row[12] == 0 && row[0] == navy);
    for (int x = 0; x < W; x++)
        row[x] = 0;
    row[3] = argb(255, 166, 166, 166);
    row[4] = navy;
    bg_row(row, W, lut, navy, gray);
    CHECK("a tick row gets the strip in its transparent pixels, not over its background",
          row[12] == gray && row[4] == navy);
    return ut_summary();
}
