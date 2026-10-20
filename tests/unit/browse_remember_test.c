/* SPDX-License-Identifier: GPL-3.0-only */
/* The remembered PREVIEW button and sub column choice (guest/azmod/browse_remember.c), against
 * models of EP147's TrackListHeader, the Browse function buttons and the track list display
 * facade:
 *   - PREVIEW is saved once per change, from a real function button controller only, and set
 *     again on the next one built through the original setter (nothing written back); a
 *     damaged file or the kill switch sets nothing;
 *   - the sub column choice is saved when the DJ makes it; after a start it is asked for once
 *     per full-width layout that shows it missing, through the original (nothing written back),
 *     until a layout after an ask shows it; never on a header just built that shows every
 *     column, nor for a compact pane; a choice the DJ makes meanwhile wins; a saved 0 (EP147's
 *     default) asks for nothing; the kill switch hooks nothing;
 *   - the header's layout runs as EP147's own: the hook changes no column.
 * BR_SRC = the file under test. */
#include "check.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- the azmod helpers browse_remember.c uses --------------------------------------- */
static int g_v200;
static int ep_off(int v130, int v200) { return g_v200 ? v200 : v130; }
static FILE *azlog_open(const char *p, const char *m) {
    (void) p;
    (void) m;
    return 0;
}
static int g_writes;
static int settings_write(const char *path, const char *buf, size_t len, int durable) {
    (void) durable;
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fwrite(buf, 1, len, f);
    fclose(f);
    g_writes++;
    return 0;
}
static int own_q(uintptr_t a, uintptr_t *out) {
    memcpy(out, (void *) a, sizeof *out);
    return 1;
}
static long long g_now = 1000;
static long long drag_now_ms(void) { return g_now; }
/* an install hands back the function itself as the original: the hook calls the model */
static int g_installs;
static int azm_install_hook_at(uintptr_t f, void *h, void **o) {
    (void) h;
    *o = (void *) f;
    g_installs++;
    return 1;
}
#define azm_install_hook(f, h, o) azm_install_hook_at((f), (h), (o))
static int azm_patch_vtable(uintptr_t slot, void *h, void **o) {
    *o = *(void **) slot;
    *(void **) slot = h;
    g_installs++;
    return 1;
}

/* ---- gui::browse::TrackListHeader: vptr, visible[id] at +0x100, mode at +0x134 -------- */
static uintptr_t g_hdr_vt[4];
struct hdr {
    unsigned char b[0x200];
};
static int g_layout_n;
static int fake_isvis(long h, int id) { return ((unsigned char *) h + 0x100)[id]; }
static void fake_layout(long h, int rowh) {
    (void) h;
    (void) rowh;
    g_layout_n++;
}
static void hdr_init(struct hdr *h, uintptr_t *vt, int mode) {
    memset(h, 0, sizeof *h);
    uintptr_t v = (uintptr_t) vt;
    memcpy(h->b, &v, sizeof v);
    memcpy(h->b + 0x134, &mode, sizeof mode);
    for (int id = 1; id <= 8; id++)
        h->b[0x100 + id] = 1; /* a header just built shows every column */
}
/* ---- the Browse function buttons: the controller's widget at +8, its PREVIEW button at
 * +0xc8 (state +0x1e0) --------------------------------------------------------------------- */
static uintptr_t g_fb_vt[4];
static int g_pv_sets;
static struct {
    unsigned char b[0x200];
} g_owner[8], g_btn[8];
static int g_nfb;
static unsigned char *btn_of(long fb) {
    uintptr_t o = *(uintptr_t *) (fb + 8), b = *(uintptr_t *) (o + 0xc8);
    return (unsigned char *) b;
}
static int pv_of(long fb) { return *(int *) (btn_of(fb) + 0x1e0); }
static void fake_pv_set(long fb, int on) {
    g_pv_sets++;
    if (*(uintptr_t *) fb == (uintptr_t) g_fb_vt) /* EP147's setter on anything else: no button */
        *(int *) (btn_of(fb) + 0x1e0) = on & 0xff;
}
static void fake_fb_ctor(long fb, long a1, long a2, long a3) {
    (void) a1;
    (void) a2;
    (void) a3;
    int k = g_nfb++ % 8;
    memset(&g_owner[k], 0, sizeof g_owner[k]);
    memset(&g_btn[k], 0, sizeof g_btn[k]);
    *(uintptr_t *) fb = (uintptr_t) g_fb_vt;
    *(uintptr_t *) (fb + 8) = (uintptr_t) &g_owner[k];
    *(uintptr_t *) (g_owner[k].b + 0xc8) = (uintptr_t) &g_btn[k];
    *(int *) (g_btn[k].b + 0x1e0) = 1; /* EP147's default: on */
}
/* ---- the track list display facade: slot +0x18 changeSubCateory ------------------ */
static int g_sc_calls, g_sc_last = -1;
static void fake_sc(long fc, int idx) {
    (void) fc;
    g_sc_calls++;
    g_sc_last = idx;
}
static void *g_fc_vt[8] = {0, 0, 0, (void *) fake_sc};
static void fake_fc_ctor(long fc, long a1, long a2, long a3) {
    (void) a1;
    (void) a2;
    (void) a3;
    *(uintptr_t *) fc = (uintptr_t) g_fc_vt;
}

#define EP_1bd06b8 ((uintptr_t) fake_layout)
#define EP_24597e0 ((uintptr_t) fake_isvis)
#define EP_2b7d8d0 ((uintptr_t) g_hdr_vt)
#define EP_1c6e420 ((uintptr_t) fake_fb_ctor)
#define EP_2c51010 ((uintptr_t) g_fb_vt)
#define EP_1c6e788 ((uintptr_t) fake_pv_set)
#define EP_1f25838 ((uintptr_t) fake_fc_ctor)
#define EP_2dc7888 ((uintptr_t) g_fc_vt)
static char BR_PV_PATH[64], BR_PV_OFF[64], BR_SC_PATH[64], BR_SC_OFF[64];
#define BR_PV_PATH BR_PV_PATH
#include BR_SRC

static void rm_all(void) {
    unlink(BR_PV_PATH);
    unlink(BR_PV_OFF);
    unlink(BR_SC_PATH);
    unlink(BR_SC_OFF);
}
static void put(const char *p, const char *s) {
    FILE *f = fopen(p, "w");
    fputs(s, f);
    fclose(f);
}
static int file_int(const char *p) {
    FILE *f = fopen(p, "r");
    int v = -9;
    if (f) {
        if (fscanf(f, "%d", &v) != 1)
            v = -8;
        fclose(f);
    }
    return v;
}
/* a fresh start: state as azmod's constructor leaves it */
static void start(void) {
    g_pv_off = g_sc_off = -1;
    g_pv_saved = -2;
    g_sc_saved = -1;
    g_sc_pending = g_sc_want = g_sc_tries = 0;
    g_sc_next_ms = 0;
    memset(g_sc_fc, 0, sizeof g_sc_fc);
    g_fc_vt[3] = (void *) fake_sc;
    g_o_pv_set = 0;
    g_o_fb_ctor = g_o_fc_ctor = 0;
    g_o_sc = 0;
    g_o_br_layout = 0;
    g_installs = 0;
    install_browse_remember();
}

int main(void) {
    ut_suite = "browse remember";
    snprintf(BR_PV_PATH, sizeof BR_PV_PATH, "/tmp/brt-pv.%d", (int) getpid());
    snprintf(BR_PV_OFF, sizeof BR_PV_OFF, "/tmp/brt-pv-off.%d", (int) getpid());
    snprintf(BR_SC_PATH, sizeof BR_SC_PATH, "/tmp/brt-sc.%d", (int) getpid());
    snprintf(BR_SC_OFF, sizeof BR_SC_OFF, "/tmp/brt-sc-off.%d", (int) getpid());
    rm_all();
    start();

    /* ---- the header's layout: EP147's own ---- */
    static struct hdr full, pane;
    hdr_init(&full, g_hdr_vt, 0);
    hdr_init(&pane, g_hdr_vt, 1);
    struct hdr before = full;
    g_layout_n = 0;
    br_layout_hook((long) &full, 52);
    CHECK("a layout runs EP147's own, once, and changes no column",
          g_layout_n == 1 && !memcmp(&before, &full, sizeof full));

    /* ---- PREVIEW ---- */
    static long fb[16], fb2[16], junk[16];
    fake_fb_ctor((long) fb, 0, 0, 0);
    g_writes = 0;
    br_pv_set_hook((long) fb, 0);
    CHECK("the PREVIEW button off: saved", file_int(BR_PV_PATH) == 0 && g_writes == 1);
    br_pv_set_hook((long) fb, 0);
    CHECK("... once per change", g_writes == 1);
    junk[0] = 0;
    br_pv_set_hook((long) junk, 1);
    CHECK("a set on something that is not the function buttons: not saved", file_int(BR_PV_PATH) == 0);
    start(); /* a restart */
    g_writes = g_pv_sets = 0;
    br_fb_ctor_hook((long) fb2, 0, 0, 0);
    CHECK("the next function buttons built: PREVIEW off again", pv_of((long) fb2) == 0);
    CHECK("... through the original setter: nothing written back", g_writes == 0);
    put(BR_PV_PATH, "x\n");
    start();
    br_fb_ctor_hook((long) fb2, 0, 0, 0);
    CHECK("a damaged file: EP147's default", pv_of((long) fb2) == 1);
    put(BR_PV_PATH, "0\n");
    put(BR_PV_OFF, "");
    start();
    CHECK("PREVIEW kill switch: its two hooks not installed", g_o_pv_set == 0 && g_o_fb_ctor == 0);
    unlink(BR_PV_OFF);

    /* ---- the sub column ---- */
    static long fc[4];
    start();
    fake_fc_ctor((long) fc, 0, 0, 0);
    br_fc_ctor_hook((long) fc, 0, 0, 0);
    g_fc_vt[3] = (void *) fake_sc; /* the model: the patched slot calls straight into it */
    g_sc_calls = 0;
    br_sc_hook((long) fc, 3);
    CHECK("the DJ's sub column choice: saved", file_int(BR_SC_PATH) == 3);
    start(); /* a restart */
    br_fc_ctor_hook((long) fc, 0, 0, 0);
    g_sc_calls = 0;
    g_writes = 0;
    static struct hdr h3;
    hdr_init(&h3, g_hdr_vt, 0);
    br_layout_hook((long) &h3, 52); /* just built: every column shows */
    g_now += 1000;
    br_tick();
    CHECK("a header just built: not taken as done, nothing asked yet", g_sc_pending == 1 && g_sc_calls == 0);
    pane.b[0x105] = 0; /* a compact pane never shows a sub column */
    br_layout_hook((long) &pane, 52);
    g_now += 1000;
    br_tick();
    CHECK("a compact pane without it: nothing asked", g_sc_calls == 0);
    h3.b[0x105] = 0; /* EP147 sets the list up with its default: no sub column */
    br_layout_hook((long) &h3, 52);
    g_now += 1000;
    br_tick();
    CHECK("a list without it: the saved choice asked for", g_sc_calls == 1 && g_sc_last == 3);
    CHECK("... through the original: nothing written back", g_writes == 0);
    g_now += 1000;
    br_tick();
    CHECK("... once per layout", g_sc_calls == 1);
    h3.b[0x105] = 1; /* EP147 took it */
    br_layout_hook((long) &h3, 52);
    CHECK("a layout showing it after the ask: done", g_sc_pending == 0);
    h3.b[0x105] = 0;
    br_layout_hook((long) &h3, 52);
    g_now += 1000;
    br_tick();
    CHECK("... and never asked again", g_sc_calls == 1);
    start();
    br_fc_ctor_hook((long) fc, 0, 0, 0);
    g_sc_calls = 0;
    h3.b[0x105] = 0;
    for (int i = 0; i < 40; i++) {
        br_layout_hook((long) &h3, 52);
        g_now += 1000;
        br_tick();
    }
    CHECK("never taken: gives up after BR_SC_TRIES asks", g_sc_calls == BR_SC_TRIES && g_sc_pending == 0);
    start();
    br_fc_ctor_hook((long) fc, 0, 0, 0);
    g_sc_calls = 0;
    br_sc_hook((long) fc, 1); /* the DJ chooses first */
    br_layout_hook((long) &h3, 52);
    g_now += 1000;
    br_tick();
    CHECK("a choice the DJ makes after a start wins: nothing asked", g_sc_calls == 1 && g_sc_last == 1 && file_int(BR_SC_PATH) == 1);
    put(BR_SC_PATH, "0\n");
    start();
    CHECK("a saved 0 (EP147's default): nothing to restore", g_sc_pending == 0);
    put(BR_SC_PATH, "3\n");
    put(BR_SC_OFF, "");
    start();
    CHECK("sub column kill switch: no facade, slot or layout hook",
          g_o_fc_ctor == 0 && g_o_sc == 0 && g_o_br_layout == 0 && g_sc_pending == 0);

    rm_all();
    return ut_summary();
}
