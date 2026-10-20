/* SPDX-License-Identifier: GPL-3.0-only */
/* The Mod Menu's community catalog (guest/azmod/screen_community.c cm_load, through menu.c,
 * #included as tools/unit-tests/cmcat_test.c does): the catalog and status files the fetcher
 * writes from what it downloaded, so any bytes. Every parse is followed by a layout of the
 * page the input picks, so the parsed fields reach the drawing code.
 *
 * Input: byte 0 picks the page (home tabs, a catalog mod's page, the search keyboard), its high
 * bits the Browse order and search; byte 1 the mod, or Browse's category; the rest is split at
 * the first NUL into the catalog and the status file (no NUL: no status file).
 * The 250 ms re-read throttle runs on a clock this harness advances by 1 s per input, and the
 * fixed catalog and status paths are redirected into the scratch directory. The rest of azmod
 * comes from lanetest.c, compiled as its own object, as in the unit suites. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "fuzz_common.h"

static long fz_now_s = 1000;
static int fz_clock(clockid_t c, struct timespec *t) {
    (void) c;
    t->tv_sec = fz_now_s;
    t->tv_nsec = 0;
    return 0;
}
static char fz_status_path[512], fz_catalog_path[512];
static const char *fz_map(const char *p) {
    return !strcmp(p, "/tmp/az-community/status") ? fz_status_path : !strcmp(p, "/fz/catalog") ? fz_catalog_path
                                                                                               : p;
}
#define CM_CATALOG "/fz/catalog"
static FILE *fz_fopen(const char *p, const char *m) { return fopen(fz_map(p), m); }
static int fz_stat(const char *p, struct stat *st) { return stat(fz_map(p), st); }
#define clock_gettime(c, t) fz_clock(c, t)
#define fopen(p, m) fz_fopen(p, m)
#define stat(p, s) fz_stat(p, s)
#include MENU_C
#undef clock_gettime
#undef fopen
#undef stat

int azmod_ttf_menu(const azmod_fb *fb, int x, int y, int w, int h, const char *s, int px, int track, int bold, uint32_t c) {
    (void) fb, (void) x, (void) y, (void) w, (void) h, (void) px, (void) track, (void) bold, (void) c;
    volatile size_t L = strlen(s); /* the text is a readable string */
    (void) L;
    return 1;
}
int azmod_ttf_menu2(const azmod_fb *fb, int x, int y, int w, int h, const char *s, int px, int track, int bold,
                    uint32_t c, int split, uint32_t c2) {
    (void) split, (void) c2;
    return azmod_ttf_menu(fb, x, y, w, h, s, px, track, bold, c);
}

#define W 1280
#define H 800
static uint8_t g_px[W * H * 4];
static azmod_fb g_fb = {g_px, W, H, W * 4};
static azmod_state g_s;
static cm_ctl g_ctl[CM_MAXCTL];

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (n < 2) return 0;
    uint8_t page = d[0], pick = d[1];
    d += 2;
    n -= 2;
    if (!fz_status_path[0]) {
        snprintf(fz_status_path, sizeof fz_status_path, "%s/status", fz_dir());
        snprintf(fz_catalog_path, sizeof fz_catalog_path, "%s/catalog", fz_dir());
    }
    const uint8_t *part[2];
    size_t len[2];
    int k = fz_split(d, n, 0, part, len, 2);
    fz_file("catalog", part[0], len[0]);
    if (k > 1) fz_file("status", part[1], len[1]);
    else fz_remove("status");
    fz_now_s++;
    cm_load();
    FZ_ASSERT(g_ncr >= 0 && g_ncr <= CM_NREPO && g_ncc >= 0 && g_ncc <= CM_NCAT && g_cc_more >= 0);
    g_cm_full = page >> 3 & 1;
    g_cm_scroll = 0;
    g_cm_sort = (page >> 4 & 3) % CMB_NSORT;
    g_cm_fcat = pick % (CMB_NCAT + 1) - 1;
    snprintf(g_cm_q, sizeof g_cm_q, "%s", page & 0x40 ? "e o" : "");
    g_cm_kbsearch = 0;
    if ((page & 7) == 7) {
        g_cm_view = CMV_KBD;
        g_cm_kbsearch = 1;
    } else if ((page & 7) < 3) {
        g_cm_view = CMV_HOME;
        g_cm_tab = page & 3;
        g_cm_cat = -1;
    } else {
        g_cm_view = CMV_CAT;
        g_cm_tab = 0;
        g_cm_cat = g_ncc ? pick % g_ncc : -1;
    }
    int nctl = cm_layout(&g_s, &g_fb, g_ctl, 1);
    FZ_ASSERT(nctl >= 0 && nctl <= CM_MAXCTL);
    return 0;
}
