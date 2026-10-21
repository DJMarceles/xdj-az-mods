/* SPDX-License-Identifier: GPL-3.0-only */
/* Column Edit's columns and the remembered PREVIEW / sub column (guest/azmod/browse_columns.c),
 * against models of EP147's TrackListHeader, the Browse function buttons and the track list
 * display facade:
 *   - while the Mod Menu's Column Edit row is on (and browse_edit.c is installed), the layout
 *     hook changes only the columns the DJ has set, in every full-width list (never a compact
 *     pane or another header), and enforces them again after EP147 resets one; a column never
 *     set stays EP147's in each list (# in a playlist); a change reaches the headers on screen
 *     and lays them out, and is saved (settings/azmod-colshow), loaded at the next start;
 *   - with the row off nothing is changed; turning it off gives every set column back as
 *     EP147 last set it (its own setColumnVisible calls are recorded, ours are not), and one
 *     whose EP147 state was never seen, or whose header was destroyed, is left as it is;
 *   - BPM and Key are columns 6 and 7 on v1.30, 7 and 8 on v2.00;
 *   - PREVIEW is saved once per change, from a real function button controller only, and set
 *     again on the next one built through the original setter (nothing written back); a
 *     damaged file or the kill switch sets nothing;
 *   - the sub column choice is saved when the DJ makes it; after a start it is asked for once
 *     per layout that shows it missing, through the original (nothing written back), until a
 *     layout after an ask shows it; never on a header just built that shows every column;
 *     a choice the DJ makes meanwhile wins; a saved 0 (EP147's default) asks for nothing;
 *   - the stem marks' column (stem_marks.c sm_hdr_lastcol) is the rightmost visible column of
 *     the list's header, 0 for anything that is not a TrackListHeader.
 * BC_SRC = the file under test, SM_INC = sm_hdr_lastcol extracted from stem_marks.c. */
#include "check.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- the azmod helpers browse_columns.c uses ---------------------------------------- */
#define AZF_COLUMN_EDIT 7
static int g_row; /* the Mod Menu's Column Edit row */
static int az_on(int id) { return id == AZF_COLUMN_EDIT && g_row; }
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
static int img_q(uintptr_t a, uintptr_t *out) {
    memcpy(out, (void *) a, sizeof *out);
    return 1;
}
static int own_q(uintptr_t a, uintptr_t *out) {
    memcpy(out, (void *) a, sizeof *out);
    return 1;
}
static long long g_now = 1000;
static long long drag_now_ms(void) { return g_now; }
/* an install hands back the function itself as the original: the hook calls the model */
static int azm_install_hook_at(uintptr_t f, void *h, void **o) {
    (void) h;
    *o = (void *) f;
    return 1;
}
#define azm_install_hook(f, h, o) azm_install_hook_at((f), (h), (o))
static int azm_patch_vtable(uintptr_t slot, void *h, void **o) {
    *o = *(void **) slot;
    *(void **) slot = h;
    return 1;
}

/* ---- gui::browse::TrackListHeader: vptr, visible[id] at +0x100, mode at +0x134 -------- */
static uintptr_t g_hdr_vt[4], g_other_vt[4];
struct hdr {
    unsigned char b[0x200];
};
static int g_setvis_n, g_layout_n;
static unsigned char *vis_of(long h) { return (unsigned char *) h + 0x100; }
static void fake_setvis(long h, int id, int v) {
    g_setvis_n++;
    vis_of(h)[id] = (unsigned char) (v != 0);
}
static int fake_isvis(long h, int id) { return vis_of(h)[id]; }
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
    *(uintptr_t *) (g_btn[k].b + 0x1d8) = 0x5000 + (uintptr_t) k; /* the button's id */
    *(int *) (g_btn[k].b + 0x1e0) = 1;                           /* EP147's default: on */
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
#define EP_248a8f0 ((uintptr_t) fake_setvis)
#define EP_24597e0 ((uintptr_t) fake_isvis)
#define EP_2b7d8d0 ((uintptr_t) g_hdr_vt)
#define EP_1c6e420 ((uintptr_t) fake_fb_ctor)
#define EP_2c51010 ((uintptr_t) g_fb_vt)
#define EP_1c6e788 ((uintptr_t) fake_pv_set)
#define EP_1f25838 ((uintptr_t) fake_fc_ctor)
#define EP_2dc7888 ((uintptr_t) g_fc_vt)
/* music_library::SqliteSortEntry: a row of the library's sort table (id, sequenceNo, isVisible) */
struct sortrow {
    int id, seq, vis;
};
static int fake_sort_id(void *r) { return ((struct sortrow *) r)->id; }
static int fake_sort_seq(void *r) { return ((struct sortrow *) r)->seq; }
static int fake_sort_vis(void *r) { return ((struct sortrow *) r)->vis; }
static void *g_sort_vt[8] = {0, 0, (void *) fake_sort_id, 0, (void *) fake_sort_seq, (void *) fake_sort_vis};
#define EP_284b090 ((uintptr_t) g_sort_vt)
static char BC_PV_PATH[64], BC_PV_OFF[64], BC_SC_PATH[64], BC_SC_OFF[64], BC_COLS_PATH[64];
#define BC_PV_PATH BC_PV_PATH
#include BC_SRC
/* stem_marks.c's sm_hdr_lastcol, extracted by run.sh: the stem mark's column, from the header */
#include SM_INC

static void rm_all(void) {
    unlink(BC_PV_PATH);
    unlink(BC_PV_OFF);
    unlink(BC_SC_PATH);
    unlink(BC_SC_OFF);
    unlink(BC_COLS_PATH);
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
/* browse_edit.c's side of a Column Edit change */
static int g_ce_calls, g_ce_last = -1;
static void fake_ce_changed(int on) {
    g_ce_calls++;
    g_ce_last = on;
}
/* EP147 setting a column itself: through the (recording) hook */
static void ep_setvis(struct hdr *h, int id, int v) { bc_setvis_hook((long) h, id, v); }
/* a fresh start: state as azmod's constructor leaves it */
static void start(void) {
    g_pv_off = g_sc_off = -1;
    g_pv_saved = -2;
    g_sc_saved = -1;
    g_sc_pending = g_sc_want = g_sc_tries = 0;
    g_sc_next_ms = 0;
    memset(g_sc_fc, 0, sizeof g_sc_fc);
    memset(g_bc_hdr, 0, sizeof g_bc_hdr);
    g_fc_vt[3] = (void *) fake_sc;
    g_sort_vt[4] = (void *) fake_sort_seq; /* each start patches the slots afresh */
    g_sort_vt[5] = (void *) fake_sort_vis;
    memset(g_bc_ep, 0, sizeof g_bc_ep);
    g_bc_ce = 0;
    install_browse_columns();
    g_bc_ce_changed = fake_ce_changed; /* browse_edit.c installed */
    __atomic_store_n(&g_bc_ep_ok, 1, __ATOMIC_RELEASE); /* browse_fields.c follows the headers */
}

int main(void) {
    ut_suite = "browse columns";
    snprintf(BC_PV_PATH, sizeof BC_PV_PATH, "/tmp/bct-pv.%d", (int) getpid());
    snprintf(BC_PV_OFF, sizeof BC_PV_OFF, "/tmp/bct-pv-off.%d", (int) getpid());
    snprintf(BC_SC_PATH, sizeof BC_SC_PATH, "/tmp/bct-sc.%d", (int) getpid());
    snprintf(BC_SC_OFF, sizeof BC_SC_OFF, "/tmp/bct-sc-off.%d", (int) getpid());
    snprintf(BC_COLS_PATH, sizeof BC_COLS_PATH, "/tmp/bct-cols.%d", (int) getpid());
    rm_all();
    start();

    /* ---- columns ---- */
    static struct hdr full, pane, other;
    hdr_init(&full, g_hdr_vt, 0);
    hdr_init(&pane, g_hdr_vt, 1);
    hdr_init(&other, g_other_vt, 0);
    bc_hdr_built((uintptr_t) &full);
    bc_hdr_built((uintptr_t) &pane);
    ep_setvis(&full, 3, 0); /* a TRACK list: EP147 hides # */
    bc_layout_hook((long) &full, 52);
    CHECK("EP147's own columns: a layout changes nothing", g_setvis_n == 1 && g_layout_n == 1);
    /* a playlist: EP147 shows # (the entry order) itself */
    static struct hdr pl;
    hdr_init(&pl, g_hdr_vt, 0);
    bc_hdr_built((uintptr_t) &pl);
    ep_setvis(&pl, 3, 1);
    bc_layout_hook((long) &pl, 52);
    g_setvis_n = g_layout_n = 0;
    /* artwork off, # on, both set by the DJ, with the Column Edit row still off */
    bc_set_mask(AZMOD_BCOL_NUM | AZMOD_BCOL_BPM | AZMOD_BCOL_KEY | AZMOD_BCOL_SET(AZMOD_BCOL_ART | AZMOD_BCOL_NUM));
    CHECK("a change: saved", file_int(BC_COLS_PATH) == (int) (AZMOD_BCOL_NUM | AZMOD_BCOL_BPM | AZMOD_BCOL_KEY |
                                                              AZMOD_BCOL_SET(AZMOD_BCOL_ART | AZMOD_BCOL_NUM)));
    CHECK("Column Edit off: the columns stay EP147's", g_setvis_n == 0 && full.b[0x102] == 1 && full.b[0x103] == 0);
    g_row = 1;
    g_bc_ce_changed = 0;
    bc_tick();
    CHECK("the row on without browse_edit.c: not applied", !bc_ce_on() && full.b[0x102] == 1);
    g_bc_ce_changed = fake_ce_changed;
    g_layout_n = 0;
    bc_tick();
    CHECK("the row on: Column Edit on, browse_edit.c told", bc_ce_on() && g_ce_calls == 1 && g_ce_last == 1);
    CHECK("... the header on screen hides the artwork and shows #", full.b[0x102] == 0 && full.b[0x103] == 1);
    CHECK("... and both headers on screen are laid out again", g_layout_n == 2);
    CHECK("... BPM and Key untouched", full.b[0x106] == 1 && full.b[0x107] == 1);
    g_ce_calls = 0;
    bc_tick();
    CHECK("no change of the row: nothing told", g_ce_calls == 0);
    ep_setvis(&full, 3, 0); /* EP147 hides # again on the next track list */
    bc_layout_hook((long) &full, 52);
    CHECK("EP147 hiding # again: the layout shows it", full.b[0x103] == 1);
    g_setvis_n = 0;
    bc_layout_hook((long) &pane, 52);
    bc_layout_hook((long) &other, 52);
    CHECK("a compact pane and another header: untouched", g_setvis_n == 0 && pane.b[0x102] == 1 && other.b[0x102] == 1);
    bc_set_mask(AZMOD_BCOL_ART | AZMOD_BCOL_NUM | AZMOD_BCOL_SET(AZMOD_BCOL_ALL)); /* BPM and Key off */
    CHECK("v1.30: BPM and Key are columns 6 and 7", full.b[0x106] == 0 && full.b[0x107] == 0 && full.b[0x108] == 1);
    /* the DJ turns everything back: # off, now set, so off in every list */
    bc_set_mask(AZMOD_BCOL_DEFAULT | AZMOD_BCOL_SET(AZMOD_BCOL_ALL));
    CHECK("back to the defaults: artwork, BPM, Key shown and # hidden again",
          full.b[0x102] == 1 && full.b[0x103] == 0 && full.b[0x106] == 1 && full.b[0x107] == 1);
    bc_layout_hook((long) &full, 52);
    CHECK("... and the next layout keeps them", full.b[0x102] == 1 && full.b[0x103] == 0 && full.b[0x106] == 1);
    ep_setvis(&pl, 3, 1); /* EP147 shows # on the next playlist */
    bc_layout_hook((long) &pl, 52);
    CHECK("# set off by the DJ: hidden in a playlist too", pl.b[0x103] == 0);
    bc_set_mask(AZMOD_BCOL_DEFAULT | AZMOD_BCOL_NUM | AZMOD_BCOL_SET(AZMOD_BCOL_NUM));
    CHECK("# set on: a playlist's header shows it again at once", pl.b[0x103] == 1);
    CHECK("... and the TRACK list's", full.b[0x103] == 1);
    bc_set_mask(AZMOD_BCOL_DEFAULT);
    CHECK("nothing set (RESET): # back as EP147 last set it, in each list", full.b[0x103] == 0 && pl.b[0x103] == 1);

    /* ---- the row off: every set column EP147's again ---- */
    bc_set_mask(AZMOD_BCOL_NUM | AZMOD_BCOL_SET(AZMOD_BCOL_ALL)); /* # only */
    CHECK("# only, on both lists", full.b[0x103] == 1 && pl.b[0x103] == 1 && full.b[0x102] == 0 && pl.b[0x106] == 0);
    static struct hdr unseen;
    hdr_init(&unseen, g_hdr_vt, 0); /* never seen built, never set by EP147 */
    bc_layout_hook((long) &unseen, 52);
    static struct hdr gone;
    hdr_init(&gone, g_hdr_vt, 0);
    bc_hdr_built((uintptr_t) &gone);
    ep_setvis(&gone, 3, 0);
    bc_layout_hook((long) &gone, 52);
    bc_hdr_gone((uintptr_t) &gone); /* destroyed; the memory is EP147's next header (not built through us) */
    CHECK("... on the others too", unseen.b[0x103] == 1 && unseen.b[0x102] == 0 && gone.b[0x103] == 1);
    g_row = 0;
    g_ce_calls = 0;
    g_layout_n = 0;
    bc_tick();
    CHECK("the row off: browse_edit.c told", !bc_ce_on() && g_ce_calls == 1 && g_ce_last == 0);
    CHECK("... the TRACK list as EP147 set it: # hidden, artwork, BPM, Key shown",
          full.b[0x103] == 0 && full.b[0x102] == 1 && full.b[0x106] == 1 && full.b[0x107] == 1);
    CHECK("... the playlist as EP147 set it: # shown", pl.b[0x103] == 1 && pl.b[0x102] == 1);
    CHECK("... a header whose EP147 state was never seen: left as it is", unseen.b[0x103] == 1 && unseen.b[0x102] == 0);
    CHECK("... a destroyed header's record not used", gone.b[0x103] == 1);
    CHECK("... and the headers laid out again", g_layout_n >= 2);
    g_setvis_n = 0;
    bc_layout_hook((long) &full, 52);
    CHECK("... and the next layout changes nothing", g_setvis_n == 0);
    CHECK("... what the DJ set is kept", file_int(BC_COLS_PATH) == (int) (AZMOD_BCOL_NUM | AZMOD_BCOL_SET(AZMOD_BCOL_ALL)));
    g_row = 1;
    bc_tick();
    CHECK("the row on again: the DJ's columns back", full.b[0x103] == 1 && full.b[0x102] == 0);
    g_v200 = 1;
    static struct hdr f2;
    hdr_init(&f2, g_hdr_vt, 0);
    bc_set_mask(AZMOD_BCOL_ART | AZMOD_BCOL_SET(AZMOD_BCOL_ALL));
    bc_layout_hook((long) &f2, 52);
    CHECK("v2.00: BPM and Key are columns 7 and 8", f2.b[0x107] == 0 && f2.b[0x108] == 0 && f2.b[0x106] == 1);
    g_v200 = 0;

    /* ---- a restart ---- */
    start();
    CHECK("a restart: the saved columns loaded", bc_mask() == (AZMOD_BCOL_ART | AZMOD_BCOL_SET(AZMOD_BCOL_ALL)));
    CHECK("... Column Edit not applied before the first tick", !bc_ce_on());
    put(BC_COLS_PATH, "x\n");
    start();
    CHECK("a damaged file: nothing set", bc_mask() == AZMOD_BCOL_DEFAULT);
    put(BC_COLS_PATH, "999\n");
    start();
    CHECK("a value out of range: nothing set", bc_mask() == AZMOD_BCOL_DEFAULT);
    unlink(BC_COLS_PATH);
    g_row = 0;

    /* ---- PREVIEW ---- */
    static long fb[16], fb2[16], junk[16];
    fake_fb_ctor((long) fb, 0, 0, 0);
    g_writes = 0;
    bc_pv_set_hook((long) fb, 0);
    CHECK("the PREVIEW button off: saved", file_int(BC_PV_PATH) == 0 && g_writes == 1);
    bc_pv_set_hook((long) fb, 0);
    CHECK("... once per change", g_writes == 1);
    junk[0] = 0;
    bc_pv_set_hook((long) junk, 1);
    CHECK("a set on something that is not the function buttons: not saved", file_int(BC_PV_PATH) == 0);
    start(); /* a restart */
    g_writes = g_pv_sets = 0;
    bc_fb_ctor_hook((long) fb2, 0, 0, 0);
    CHECK("the next function buttons built: PREVIEW off again", pv_of((long) fb2) == 0);
    CHECK("... through the original setter: nothing written back", g_writes == 0);
    put(BC_PV_PATH, "x\n");
    start();
    bc_fb_ctor_hook((long) fb2, 0, 0, 0);
    CHECK("a damaged file: EP147's default", pv_of((long) fb2) == 1);
    put(BC_PV_PATH, "0\n");
    put(BC_PV_OFF, "");
    start();
    bc_fb_ctor_hook((long) fb2, 0, 0, 0);
    CHECK("kill switch: nothing set", pv_of((long) fb2) == 1);
    unlink(BC_PV_OFF);

    /* ---- the sub column ---- */
    static long fc[4];
    start();
    fake_fc_ctor((long) fc, 0, 0, 0);
    bc_fc_ctor_hook((long) fc, 0, 0, 0);
    g_fc_vt[3] = (void *) fake_sc; /* the model: the patched slot calls straight into it */
    g_sc_calls = 0;
    bc_sc_hook((long) fc, 3);
    CHECK("the DJ's sub column choice: saved", file_int(BC_SC_PATH) == 3);
    start(); /* a restart */
    bc_fc_ctor_hook((long) fc, 0, 0, 0);
    g_sc_calls = 0;
    g_writes = 0;
    static struct hdr h3;
    hdr_init(&h3, g_hdr_vt, 0);
    bc_layout_hook((long) &h3, 52); /* just built: every column shows */
    g_now += 1000;
    bc_tick();
    CHECK("a header just built: not taken as done, nothing asked yet", g_sc_pending == 1 && g_sc_calls == 0);
    h3.b[0x105] = 0; /* EP147 sets the list up with its default: no sub column */
    bc_layout_hook((long) &h3, 52);
    g_now += 1000;
    bc_tick();
    CHECK("a list without it: the saved choice asked for", g_sc_calls == 1 && g_sc_last == 3);
    CHECK("... through the original: nothing written back", g_writes == 0);
    g_now += 1000;
    bc_tick();
    CHECK("... once per layout", g_sc_calls == 1);
    h3.b[0x105] = 1; /* EP147 took it */
    bc_layout_hook((long) &h3, 52);
    CHECK("a layout showing it after the ask: done", g_sc_pending == 0);
    h3.b[0x105] = 0;
    bc_layout_hook((long) &h3, 52);
    g_now += 1000;
    bc_tick();
    CHECK("... and never asked again", g_sc_calls == 1);
    start();
    bc_fc_ctor_hook((long) fc, 0, 0, 0);
    g_sc_calls = 0;
    h3.b[0x105] = 0;
    for (int i = 0; i < 40; i++) {
        bc_layout_hook((long) &h3, 52);
        g_now += 1000;
        bc_tick();
    }
    CHECK("never taken: gives up after BC_SC_TRIES asks", g_sc_calls == BC_SC_TRIES && g_sc_pending == 0);
    start();
    bc_fc_ctor_hook((long) fc, 0, 0, 0);
    g_sc_calls = 0;
    bc_sc_hook((long) fc, 1); /* the DJ chooses first */
    bc_layout_hook((long) &h3, 52);
    g_now += 1000;
    bc_tick();
    CHECK("a choice the DJ makes after a start wins: nothing asked", g_sc_calls == 1 && g_sc_last == 1 && file_int(BC_SC_PATH) == 1);
    put(BC_SC_PATH, "0\n");
    start();
    CHECK("a saved 0 (EP147's default): nothing to restore", g_sc_pending == 0);

    /* ---- the stem marks' column: the rightmost visible one, from the widget's header ---- */
    static struct hdr hs;
    static unsigned char widget[0x100];
    uintptr_t hp = (uintptr_t) &hs;
    memcpy(widget + 0xd0, &hp, sizeof hp);
    g_v200 = 1;
    hdr_init(&hs, g_hdr_vt, 0);
    CHECK("stem marks: every column shown: Key (8)", sm_hdr_lastcol((uintptr_t) widget) == 8);
    hs.b[0x107] = hs.b[0x108] = 0;
    CHECK("stem marks: BPM and Key hidden: the sub column (6)", sm_hdr_lastcol((uintptr_t) widget) == 6);
    hs.b[0x106] = hs.b[0x103] = hs.b[0x104] = hs.b[0x101] = 0;
    CHECK("stem marks: artwork and title only: the title (5)", sm_hdr_lastcol((uintptr_t) widget) == 5);
    hdr_init(&hs, g_other_vt, 0);
    CHECK("stem marks: not a TrackListHeader: 0 (learned instead)", sm_hdr_lastcol((uintptr_t) widget) == 0);
    g_v200 = 0;

    /* ---- every library field: the sort rows, as EP147's library thread reads them ---- */
    {
        int (*vis)(void *) = (int (*)(void *)) g_sort_vt[5], (*seq)(void *) = (int (*)(void *)) g_sort_vt[4];
        struct sortrow shown = {12, 7, 1}, hidden = {6, 0, 0};
        CHECK("all fields: both slots hooked", vis != (void *) fake_sort_vis && seq != (void *) fake_sort_seq);
        g_row = 0;
        CHECK("Column Edit off: a hidden row stays hidden, its order EP147's",
              vis(&hidden) == 0 && seq(&hidden) == 0 && vis(&shown) == 1 && seq(&shown) == 7);
        g_row = 1;
        CHECK("Column Edit on: a hidden row offered", vis(&hidden) == 1);
        CHECK("... after every visible row, in the library's row order",
              seq(&hidden) == 1006 && seq(&hidden) > seq(&shown));
        CHECK("... a visible row keeps its place", vis(&shown) == 1 && seq(&shown) == 7);
        struct sortrow odd = {-5, 0, 0};
        CHECK("... a row id out of range still sorts after the visible ones", seq(&odd) >= 1000);
        g_row = 0;
    }

    rm_all();
    return ut_summary();
}
