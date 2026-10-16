/* SPDX-License-Identifier: GPL-3.0-only */
/* Waveform stem scaling and the stem-lane zoom model, by deck identity (guest/azmod/waveform.c
 * azm_wp_scale, wave_upd_deck, wwb_deck_by_content, wave_zoom_tick, azmod_wave_zoom), extracted
 * and built against stand-in globals.
 *
 * The rules under test:
 *   - a point is scaled with the curve of the deck whose update() is running, or whose Content
 *     the WWB ctor recorded; with neither, it is left as EP147 drew it (no other deck's curve);
 *   - the zoom model is kept per [deck][color mode]: two decks' providers at one stack address
 *     (EP147 builds them as stack temporaries) keep separate models, and a point of unknown
 *     deck feeds none;
 *   - azmod_wave_zoom answers only from that deck's own model.
 * run.sh also builds this from mutated copies: one that binds an unknown provider by its point
 * count (the guess this code replaced), one that takes deck 0 for an unknown deck, and one that
 * keys the zoom model by color mode alone (one model shared by every deck, as a provider-keyed
 * table did for providers sharing an address). Each must fail. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define AZMOD_STEM_DECKS 4
#define ATT_N 16384
#define FULLREDRAW 1000
#define WAVEPROV_CNT 0x14

static int g_wave_any, g_wave_scale, g_wave_color;
static int g_att_have[AZMOD_STEM_DECKS], g_att_ident[AZMOD_STEM_DECKS]; /* _Atomic in azmod: clang
                                                                         * refuses __atomic_* on it */
static unsigned short g_att[AZMOD_STEM_DECKS][ATT_N];
static unsigned g_col[AZMOD_STEM_DECKS][ATT_N];
static int g_upd_deck = -1;
static struct {
    unsigned long content;
    int deck;
} g_ctmap[16];
static int g_ctmap_n, g_ctprobe;
static unsigned long g_wp_calls, g_wp_updhit, g_wp_cthit, g_wp_gateoff, g_wp_nocnt, g_wp_nodeck;
static unsigned long g_wp_gated[AZMOD_STEM_DECKS], g_wp_scaled[AZMOD_STEM_DECKS];
static void wwb_log(const char *fmt, ...) { (void) fmt; }

/* azstem's view of each deck: the playhead and whether the stems are decoded. */
static float g_pos[AZMOD_STEM_DECKS];
static int g_stems_ready[AZMOD_STEM_DECKS];
static int stem_wave_stub(int deck, const unsigned char **env, int *n, int *nstem, float *pos) {
    static const unsigned char e[4];
    *env = e;
    *n = 1;
    *nstem = 4;
    *pos = g_pos[deck];
    return g_stems_ready[deck];
}
static int (*azmod_stem_wave)(int, const unsigned char **, int *, int *, float *) = stem_wave_stub;

/* The point-count guess a mutated control reinstates: an unbound provider goes to the first
 * deck whose track is as long as its point count says (150 points/s, +-2%). */
static int g_track_s[AZMOD_STEM_DECKS]; /* each deck's track length, seconds (0 = empty) */
static __attribute__((unused)) int ctl_guess_deck(long self) {
    int cnt = *(volatile int *) (self + WAVEPROV_CNT);
    for (int d = 0; d < AZMOD_STEM_DECKS; d++) {
        double want = g_track_s[d] * 150.0;
        if (want > 0 && cnt > want * 0.98 && cnt < want * 1.02)
            return d;
    }
    return -1;
}

#include WZ_INC
#include "check.h"

/* A DataProvider as EP147 builds it: +0 vtable, +8 Content, +0x14 point count. */
typedef struct {
    long vt;
    long content;
    int flag;
    int cnt;
} prov_t;
_Static_assert(__builtin_offsetof(prov_t, cnt) == WAVEPROV_CNT, "provider layout");

/* One point through the scaler as the shim calls it: `deck` is the update() running (-1 =
 * none); returns the height EP147 will draw for a stock height of 200. */
static int point(int deck, prov_t *p, int idx, int kind) {
    unsigned char out[5] = {200, 1, 2, 3, 4};
    g_upd_deck = deck;
    azm_wp_scale((long) p, idx, out, 0, kind);
    g_upd_deck = -1;
    return out[0];
}
/* A full-band redraw: every point of [lo, hi] for that deck and color mode. */
static void redraw(int deck, prov_t *p, int lo, int hi, int kind) {
    for (int i = lo; i <= hi; i++)
        point(deck, p, i, kind);
}
static void reset_zoom(void) {
    memset(g_wz, 0, sizeof g_wz);
}

int main(void) {
    ut_suite = "wave identity";
    g_wave_any = g_wave_scale = 1;
    /* deck 1 (index 0): a stem muted, heights halved; deck 2 (index 1): every stem on */
    for (int i = 0; i < ATT_N; i++) {
        g_att[0][i] = 128;
        g_att[1][i] = 256;
    }
    g_att_have[0] = g_att_have[1] = 1;
    g_att_ident[1] = 1;
    /* the same track on decks 1 and 2: the worst case for any length-based binding */
    g_track_s[0] = g_track_s[1] = 120;

    /* One stack temporary serves both decks, as EP147's update() builds it. */
    prov_t p = {0, 0x7f0000500000L, 1, 120 * 150};

    CHECK("deck 1's update: its point is scaled with deck 1's curve", point(0, &p, 10, WSK_RGB) == 100);
    CHECK("deck 2's update at the same provider address: deck 2's curve (untouched)",
          point(1, &p, 10, WSK_RGB) == 200);
    CHECK("deck 1 again after deck 2: deck 1's curve", point(0, &p, 10, WSK_RGB) == 100);
    CHECK("no update running, Content unknown: left as EP147 drew it", point(-1, &p, 10, WSK_RGB) == 200);
    CHECK("... and counted as unknown", g_wp_nodeck > 0);

    /* the Content table, published by the WWB ctor with the deck it was given */
    g_ctmap[0].content = (unsigned long) p.content;
    g_ctmap[0].deck = 0;
    g_ctmap_n = 1;
    CHECK("no update running, Content recorded for deck 1: deck 1's curve", point(-1, &p, 10, WSK_RGB) == 100);
    g_ctmap_n = 0;

    g_wave_noid = 1;
    CHECK("identity switched off (test switch): left as EP147 drew it, though deck 1's update runs",
          point(0, &p, 10, WSK_RGB) == 200);
    g_wave_noid = 0;
    CHECK("an out-of-range color mode is not scaled", point(0, &p, 10, 7) == 200);

    /* ---- zoom model: decks share the provider address, never the model */
    reset_zoom();
    g_ncol_hint = 101;
    g_stems_ready[0] = g_stems_ready[1] = 1;
    g_pos[0] = 0.1f;
    g_pos[1] = 0.3f;
    for (int t = 0; t < 2; t++) { /* the anchor needs a still playhead across two ticks */
        redraw(0, &p, 1000, 2000, WSK_RGB);
        redraw(1, &p, 5000, 7000, WSK_RGB);
        wave_zoom_tick();
    }
    double ppc = 0, ph = 0, nc = 0, tot = 0;
    int ok0 = azmod_wave_zoom(0, &ppc, &ph, &nc, &tot);
    CHECK("deck 1's model: its own redraw (10 points per column)", ok0 && ppc > 9.99 && ppc < 10.01);
    CHECK("deck 1's playhead column from deck 1's position and range",
          ok0 && ph > ((0.1 * 18000 - 1000) / 10.0) - 0.01 && ph < ((0.1 * 18000 - 1000) / 10.0) + 0.01);
    int ok1 = azmod_wave_zoom(1, &ppc, &ph, &nc, &tot);
    CHECK("deck 2's model: its own redraw (20 points per column)", ok1 && ppc > 19.99 && ppc < 20.01);
    CHECK("deck 3 drew nothing: no model (never another deck's)", !azmod_wave_zoom(2, &ppc, &ph, &nc, &tot));
    CHECK("an out-of-range deck: no model", !azmod_wave_zoom(-1, &ppc, &ph, &nc, &tot) &&
                                                !azmod_wave_zoom(AZMOD_STEM_DECKS, &ppc, &ph, &nc, &tot));

    /* ---- points of unknown deck feed no model */
    reset_zoom();
    for (int t = 0; t < 2; t++) {
        redraw(-1, &p, 1000, 2000, WSK_RGB);
        wave_zoom_tick();
    }
    int any = 0;
    for (int d = 0; d < AZMOD_STEM_DECKS; d++)
        any |= azmod_wave_zoom(d, &ppc, &ph, &nc, &tot);
    CHECK("redraws of unknown deck: no deck has a zoom model", !any);

    /* ---- the color mode on screen wins, within the deck */
    reset_zoom();
    prov_t p3 = {0, 0x7f0000600000L, 1, 120 * 100}; /* 3-band: a different point count */
    for (int t = 0; t < 2; t++) {
        redraw(0, &p, 1000, 3000, WSK_RGB);  /* 2001 points: on screen */
        redraw(0, &p3, 100, 1200, WSK_3BAND); /* 1101 points: a smaller redraw */
        wave_zoom_tick();
    }
    CHECK("two color modes on one deck: the one asking for more points is used",
          azmod_wave_zoom(0, &ppc, &ph, &nc, &tot) && ppc > 19.99 && ppc < 20.01 && tot == 18000);

    /* ---- a new track on the deck drops the old model until a redraw of the new one */
    p.cnt = 90 * 150;
    point(0, &p, 10, WSK_RGB);
    CHECK("a new track on deck 1: its RGB model is dropped", !__atomic_load_n(&g_wz[0][WSK_RGB].ok, __ATOMIC_RELAXED));
    p.cnt = 120 * 150;

    /* ---- a redraw that lands before azstem has the stems is held, not lost */
    reset_zoom();
    g_pos[0] = 0.0f; /* a paused deck at the start: the playhead never moves */
    g_stems_ready[0] = 0;
    redraw(0, &p, 1000, 2000, WSK_RGB); /* the one full redraw a paused deck gets at load */
    wave_zoom_tick();
    CHECK("stems not decoded yet: no model", !azmod_wave_zoom(0, &ppc, &ph, &nc, &tot));
    g_stems_ready[0] = 1;
    wave_zoom_tick(); /* no new redraw: only the parked one */
    CHECK("stems decoded: the model forms from the held redraw",
          azmod_wave_zoom(0, &ppc, &ph, &nc, &tot) && ppc > 9.99 && ppc < 10.01);

    return ut_summary();
}
