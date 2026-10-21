/* SPDX-License-Identifier: GPL-3.0-only */
/* The Mod Menu's icon cache (ui_draw.c draw_item_icon): an official icon drawn on one color is
 * kept and copied back, and the copy must be the drawing, pixel for pixel.
 *   - every official icon, at every size the menus use, draws inside its own box (the cache
 *     keeps only the box, so a pixel outside it would be lost);
 *   - a second draw is served from the cache (a poisoned entry shows), and is identical to a
 *     fresh drawing;
 *   - the ground color, the ink, the theme and the size are each part of the key: changing one
 *     gives that drawing, never a kept one;
 *   - not kept: an icon on more than one color, an icon clipped by the buffer's edge, a
 *     community mod's icon;
 *   - more icons than entries: the evicted ones draw again, correctly.
 * menu.c is #included (MENU_C) for its statics; the text, theme and community hooks are this
 * file's stand-ins. */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "check.h"
#include MENU_C

int azmod_ttf_menu(const azmod_fb *fb, int x, int y, int w, int h, const char *s, int px, int track, int bold, uint32_t c) {
    (void) fb, (void) x, (void) y, (void) w, (void) h, (void) s, (void) px, (void) track, (void) bold, (void) c;
    return 1;
}
int azmod_ttf_menu2(const azmod_fb *fb, int x, int y, int w, int h, const char *s, int px, int track, int bold,
                    uint32_t c, int split, uint32_t c2) {
    (void) split, (void) c2;
    return azmod_ttf_menu(fb, x, y, w, h, s, px, track, bold, c);
}
static int g_theme_on;
int azmod_dm_on(void) { return g_theme_on; }
uint32_t azmod_dm_map(uint32_t c) { return (c & 0xff000000u) | (0x00ffffffu - (c & 0x00ffffffu)); }
int azmod_dm_key(void) { return g_theme_on; }
int azcm_count(void) { return 0; }
int azcm_info(int i, azcm_info_t *out) {
    (void) i, (void) out;
    return -1;
}
void azcm_screen_draw(int i, az_draw *d, az_rect area) { (void) i, (void) d, (void) area; }
int azcm_screen_input(int i, const az_event *ev) {
    (void) i, (void) ev;
    return 0;
}
void azcm_screen_enter(int i) { (void) i; }

#define W 320
#define H 240
static uint8_t g_a[W * H * 4], g_b[W * H * 4];
static azmod_fb fa = {g_a, W, H, W * 4}, fb_ = {g_b, W, H, W * 4};
static void ground(uint8_t *p, uint32_t g) {
    for (int i = 0; i < W * H; i++)
        ((uint32_t *) p)[i] = g;
}
static int entries(void) {
    int n = 0;
    for (int k = 0; g_icache && k < IC_CACHE_N; k++)
        n += g_icache[k].used != 0;
    return n;
}
static void flush(void) {
    if (g_icache)
        memset(g_icache, 0, IC_CACHE_N * sizeof *g_icache);
}
static const int SZ[] = {16, 22, 27, 33, 40, 48, 61, 96};
#define NSZ ((int) (sizeof SZ / sizeof SZ[0]))
/* the official items: the table's rows and the utility items, never a community mod's */
static int official(int *out) {
    int n = 0;
    for (int it = 0; it < AZI_COUNT; it++)
        if (!(it >= AZI_REG0 && it < AZI_ACT0))
            out[n++] = it;
    return n;
}

int main(void) {
    ut_suite = "icon cache";
    int items[AZI_COUNT], ni = official(items);
    const uint32_t G = 0x00343434u, INK = 0x00FFFFFFu;
    const int X = 100, Y = 80;

    /* every official icon stays inside its box */
    int spill = 0, spill_item = -1, spill_sz = 0, drew = 0;
    for (int i = 0; i < ni; i++)
        for (int s = 0; s < NSZ; s++) {
            ground(g_a, G);
            draw_item_icon_raw(&fa, X, Y, SZ[s], items[i], INK);
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    uint32_t p = ((uint32_t *) g_a)[y * W + x];
                    bool in = x >= X && x < X + SZ[s] && y >= Y && y < Y + SZ[s];
                    if (p != G && in) drew = 1;
                    if (p != G && !in && !spill++) spill_item = items[i], spill_sz = SZ[s];
                }
        }
    if (spill) printf("      first spill: item %d at %d px\n", spill_item, spill_sz);
    CHECK("every official icon, every size: nothing drawn outside its box", spill == 0);
    CHECK("the icons draw something (the check above could see a spill)", drew);

    /* a second draw: from the cache, and the same pixels */
    int same = 1, hits = 1, n_kept = 0;
    for (int i = 0; i < ni && same; i++)
        for (int s = 0; s < NSZ && same; s++) {
            flush();
            ground(g_a, G);
            draw_item_icon_raw(&fa, X, Y, SZ[s], items[i], INK);
            ground(g_b, G);
            draw_item_icon(&fb_, X, Y, SZ[s], items[i], INK); /* drawn and kept */
            n_kept += entries();
            ground(g_b, G);
            draw_item_icon(&fb_, X, Y, SZ[s], items[i], INK); /* from the cache */
            same = !memcmp(g_a, g_b, sizeof g_a);
            /* the kept pixels are what the second draw showed: poison them, draw again */
            for (int k = 0; k < IC_CACHE_N; k++)
                if (g_icache[k].used) g_icache[k].px[0] ^= 0x00010203u;
            ground(g_b, G);
            draw_item_icon(&fb_, X, Y, SZ[s], items[i], INK);
            if (((uint32_t *) g_b)[Y * W + X] == ((uint32_t *) g_a)[Y * W + X]) hits = 0;
        }
    CHECK("a first draw is kept (one entry per icon)", n_kept == ni * NSZ);
    CHECK("a second draw is identical to a fresh drawing, every icon and size", same);
    CHECK("a second draw comes from the cache (a poisoned entry shows)", hits);

    /* each part of the key matters */
    int it = AZF_PREVIEW_HOT_CUES, sz = 40;
    struct {
        const char *what;
        uint32_t g1, g2, i1, i2;
        int t1, t2, s1, s2;
    } K[] = {
        {"the ground color", 0x00343434u, 0x000E1830u, INK, INK, 0, 0, sz, sz},
        {"the ink", G, G, INK, 0x00FF8800u, 0, 0, sz, sz},
        {"the theme", G, G, INK, INK, 0, 1, sz, sz},
        {"the size", G, G, INK, INK, 0, 0, sz, sz + 6},
    };
    for (unsigned k = 0; k < sizeof K / sizeof K[0]; k++) {
        flush();
        g_theme_on = K[k].t1;
        ground(g_b, K[k].g1);
        draw_item_icon(&fb_, X, Y, K[k].s1, it, K[k].i1); /* kept */
        g_theme_on = K[k].t2;
        ground(g_a, K[k].g2);
        draw_item_icon_raw(&fa, X, Y, K[k].s2, it, K[k].i2);
        ground(g_b, K[k].g2);
        draw_item_icon(&fb_, X, Y, K[k].s2, it, K[k].i2);
        char nm[96];
        snprintf(nm, sizeof nm, "%s is part of the key: a change draws anew, correctly", K[k].what);
        CHECK(nm, !memcmp(g_a, g_b, sizeof g_a) && entries() == 2);
        g_theme_on = 0;
    }

    /* not kept */
    flush();
    ground(g_a, G);
    ground(g_b, G);
    ((uint32_t *) g_a)[(Y + 3) * W + X + 3] = ((uint32_t *) g_b)[(Y + 3) * W + X + 3] = 0x00FF0000u;
    draw_item_icon_raw(&fa, X, Y, sz, it, INK);
    draw_item_icon(&fb_, X, Y, sz, it, INK);
    CHECK("an icon on more than one color: drawn, not kept", !memcmp(g_a, g_b, sizeof g_a) && entries() == 0);
    flush();
    ground(g_a, G);
    ground(g_b, G);
    draw_item_icon_raw(&fa, W - 20, Y, sz, it, INK);
    draw_item_icon(&fb_, W - 20, Y, sz, it, INK);
    CHECK("an icon clipped by the buffer's edge: drawn, not kept", !memcmp(g_a, g_b, sizeof g_a) && entries() == 0);
    flush();
    ground(g_b, G);
    draw_item_icon(&fb_, X, Y, sz, AZI_REG0, INK);
    CHECK("a community mod's icon: not kept", entries() == 0);

    /* more icons than entries: the first ones are evicted and draw again, correctly */
    flush();
    int n = 0;
    for (int i = 0; i < ni && n <= IC_CACHE_N; i++)
        for (int s = 0; s < NSZ && n <= IC_CACHE_N; s++, n++) {
            ground(g_b, G);
            draw_item_icon(&fb_, X, Y, SZ[s], items[i], INK);
        }
    ground(g_a, G);
    draw_item_icon_raw(&fa, X, Y, SZ[0], items[0], INK);
    ground(g_b, G);
    draw_item_icon(&fb_, X, Y, SZ[0], items[0], INK);
    CHECK("more icons than entries: never more than the entries, an evicted icon draws correctly",
          n > IC_CACHE_N && entries() == IC_CACHE_N && !memcmp(g_a, g_b, sizeof g_a));
    return ut_summary();
}
