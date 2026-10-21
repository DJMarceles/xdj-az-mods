/* SPDX-License-Identifier: GPL-3.0-only */
/* Community Mods > Updates (menu.c, the Updates tab) against catalog and status files:
 *   - the rail's fourth tab, its count while there are updates, the breadcrumb, the catalog size
 *     (drawn after the header's band, so it shows);
 *   - exactly the mods the Installed list marks "Update available": a loaded one and one installed
 *     since the start; never one this deck cannot take, one removed or one up to date;
 *   - a row: name, installed > offered version, Update sending the mod page's install;
 *   - Update all: the first install sent, the button "Updating 1 of 2..." and inactive, no other
 *     request sent meanwhile, the next one sent from the overlay's tick once the first is done and
 *     the catalog read again; an error stops it with its message; a reload ends it; the Reload
 *     note only once it ended;
 *   - nothing to update: one line.
 *   - the Installed rows: a checkbox left of the > that a touch toggles (the page's Enable /
 *     Disable), a touch elsewhere opening the page; the knob moving the focus row by row, never
 *     onto a checkbox, its press opening the page; the Updates tab's knob reaching each Update.
 * menu.c is #included (MENU_C); CM_CATALOG, CM_STATUS and CM_REQ point at temp files, and
 * azcm_count / azcm_info / azcm_ver_newer are this file's. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include MENU_C

static char g_seen[1 << 16];
static size_t g_seen_n;
#define INK 0x00ABCDEFu /* the header count's text leaves this mark where it is drawn */
int azmod_ttf_menu(const azmod_fb *fb, int x, int y, int w, int h, const char *s, int px, int track, int bold, uint32_t c) {
    (void) w;
    if (strstr(s, " mods") && x >= 0 && x < fb->w && y + h / 2 >= 0 && y + h / 2 < fb->h)
        memcpy(fb->px + (size_t) (y + h / 2) * fb->stride + (size_t) x * 4, &(uint32_t) {INK}, 4);
    (void) px;
    (void) track;
    (void) bold;
    (void) c;
    size_t L = strlen(s);
    if (g_seen_n + L + 2 < sizeof g_seen) {
        memcpy(g_seen + g_seen_n, s, L);
        g_seen_n += L;
        g_seen[g_seen_n++] = '\n';
        g_seen[g_seen_n] = 0;
    }
    return 1;
}
int azmod_ttf_menu2(const azmod_fb *fb, int x, int y, int w, int h, const char *s, int px, int track, int bold,
                    uint32_t c, int split, uint32_t c2) {
    (void) split;
    (void) c2;
    return azmod_ttf_menu(fb, x, y, w, h, s, px, track, bold, c);
}
static int seen(const char *s) { return strstr(g_seen, s) != NULL; }

/* one mod loaded at the start: echo-out 1.0.0 */
int azcm_count(void) { return 1; }
int azcm_info(int i, azcm_info_t *out) {
    if (i != 0) return -1;
    memset(out, 0, sizeof *out);
    strcpy(out->id, "echo-out");
    strcpy(out->name, "Echo Out");
    strcpy(out->version, "1.0.0");
    out->state = AZCM_LOADED;
    return 0;
}
int azcm_ver_newer(const char *a, const char *b) { return strcmp(a, b) > 0; } /* single-digit versions here */

#define W 1280
#define H 800
static uint8_t g_px[W * H * 4];
static azmod_fb g_fb = {g_px, W, H, W * 4};
static azmod_state g_s;
static cm_ctl g_ctl[CM_MAXCTL];
static int g_nctl;

static void layout(int tab) {
    memset(g_px, 0, sizeof g_px);
    g_seen_n = 0;
    g_seen[0] = 0;
    g_cm_full = 1;
    g_cm_view = CMV_HOME;
    g_cm_tab = tab;
    g_cm_scroll = 0;
    g_nctl = cm_layout(&g_s, &g_fb, g_ctl, 1);
}
static int nkind(int kind) {
    int k = 0;
    for (int i = 0; i < g_nctl; i++) k += g_ctl[i].kind == kind;
    return k;
}
static const cm_ctl *ctl_p(int kind, int idx) {
    static const cm_ctl none = {-1, -1, -1000, -1000, 1, 1};
    for (int i = 0; i < g_nctl; i++)
        if (g_ctl[i].kind == kind && g_ctl[i].idx == idx) return &g_ctl[i];
    return &none;
}
static int fb_w40(void) { return W / 40; }
static void put(const char *path, const char *body) {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *f = fopen(tmp, "w");
    fputs(body, f);
    fclose(f);
    rename(tmp, path);
}
static void reload_files(void) {
    usleep(300000); /* cm_load re-reads at most 4x a second */
    cm_load();
}
/* the request the menu sent last, "" for none; reading it takes it (as the watcher does) */
static const char *req(void) {
    static char line[512];
    line[0] = 0;
    FILE *f = fopen(CM_REQ, "r");
    if (f) {
        if (!fgets(line, sizeof line, f)) line[0] = 0;
        fclose(f);
        line[strcspn(line, "\n")] = 0;
        unlink(CM_REQ);
    }
    return line;
}
#define R0 "https://example.com/r0/azmods-repo.json"
/* echo-out 1.0.0 loaded, offered 1.1.0; big-clock installed since the start (0.9.0), offered
 * 1.0.0; old-deck installed, its 2.0.0 not for this deck's firmware; loop-kit up to date;
 * new-one not installed */
static void catalog(const char *echo_inst, const char *clock_inst) {
    char b[2048];
    snprintf(b, sizeof b,
             "v1\nrepo|" R0 "|Repo 0|1|1|\n"
             "mod|" R0 "|echo-out|Echo Out|1.1.0|dubwise|s|audio|0|any|1000||%s|||0\n"
             "mod|" R0 "|big-clock|Big Clock|1.0.0|nightowl|s|visual|0|any|1000||%s|||0\n"
             "mod|" R0 "|old-deck|Old Deck|2.0.0|x|s|tools|0|XDJ-XZ-1.00|1000||1.0.0|||0\n"
             "mod|" R0 "|loop-kit|Loop Kit|1.0.0|carol|s|audio|0|any|1000||1.0.0|||0\n"
             "mod|" R0 "|new-one|New One|1.0.0|eve|s|audio|0|any|1000|||||0\n",
             echo_inst, clock_inst);
    put(CM_CATALOG, b);
}
static void status(const char *state, const char *op, const char *id, const char *msg) {
    char b[512];
    snprintf(b, sizeof b, "state=%s\nop=%s\nid=%s\ndone=0\ntotal=0\nmsg=%s\n", state, op, id, msg);
    put(CM_STATUS, b);
}

int main(void) {
    ut_suite = "community updates tab";
    unlink(CM_REQ);
    unlink(CM_STATUS);
    catalog("1.0.0", "0.9.0");
    reload_files();

    layout(3);
    CHECK("the rail: four tabs, the fourth \"Updates 2\" while two updates are there", nkind(CMC_TAB) == 4 && seen("Updates 2"));
    CHECK("the breadcrumb \"Community Mods  >  Updates\"", seen("Community Mods  >  Updates"));
    {
        int ink = 0;
        for (int yy = 0; yy < H && !ink; yy++)
            for (int xx = W / 2; xx < W && !ink; xx++) {
                uint32_t v;
                memcpy(&v, g_px + (size_t) yy * g_fb.stride + (size_t) xx * 4, 4);
                ink = (v & 0x00FFFFFFu) == INK;
            }
        CHECK("the catalog's size at the right of the header, drawn over the band (not under it)", seen("5 mods") && ink);
    }
    CHECK("two rows: the loaded mod and the one installed since the start, in the Installed list's order",
          nkind(CMC_UPDROW) == 2 && ctl_p(CMC_UPDROW, 0)->y < ctl_p(CMC_UPDROW, 1)->y);
    CHECK("CONTROL: not the mod this deck cannot take, the up-to-date one or the one not installed",
          ctl_p(CMC_UPDROW, 2)->x < 0 && ctl_p(CMC_UPDROW, 3)->x < 0 && ctl_p(CMC_UPDROW, 4)->x < 0);
    CHECK("a row: its name and installed > offered", seen("Echo Out") && seen("1.0.0 > 1.1.0") && seen("Big Clock") && seen("0.9.0 > 1.0.0"));
    CHECK("Update all (2) above the list", seen("Update all (2)") && nkind(CMC_UPDALL) == 1 && ctl_p(CMC_UPDALL, 0)->y < ctl_p(CMC_UPDROW, 0)->y);
    CHECK("each row's Update is inside the pane", ctl_p(CMC_UPDROW, 0)->x + ctl_p(CMC_UPDROW, 0)->w <= W);
    layout(1);
    CHECK("the count shows on the rail from any tab", seen("Updates 2"));

    /* a row's Update: the mod page's install */
    layout(3);
    cm_fire(ctl_p(CMC_UPDROW, 1));
    CHECK("a row's Update sends \"install <repo> <id>\"", !strcmp(req(), "install " R0 " big-clock"));

    /* a removed mod is not offered */
    cm_removed_add("big-clock");
    layout(3);
    CHECK("a mod removed this start is not listed", nkind(CMC_UPDROW) == 1 && seen("Update all (1)") && seen("Updates 1"));
    g_cm_removed[0][0] = 0;

    /* Update all */
    layout(3);
    cm_fire(ctl_p(CMC_UPDALL, 0));
    CHECK("Update all sends the first install at once", !strcmp(req(), "install " R0 " echo-out") && g_cm_ua.on);
    layout(3);
    CHECK("the button reads \"Updating 1 of 2...\"; the row being updated says so", seen("Updating 1 of 2...") && seen("Updating..."));
    cm_fire(ctl_p(CMC_UPDROW, 1));
    cm_fire(ctl_p(CMC_UPDALL, 0));
    cm_fire(&(cm_ctl) {CMC_REFRESH, 0, 0, 0, 1, 1});
    CHECK("while it runs nothing else is sent (a row's Update, Update all, Refresh)", !strcmp(req(), ""));
    azmod_cm_bg_tick();
    CHECK("the overlay's tick sends nothing before the fetcher answers", !strcmp(req(), ""));
    status("downloading", "install", "echo-out", "Echo Out");
    reload_files();
    azmod_cm_bg_tick();
    CHECK("nor while it downloads", !strcmp(req(), ""));
    catalog("1.1.0", "0.9.0");
    status("done", "install", "echo-out", "Echo Out 1.1.0 installed. Press Reload now to start it.");
    reload_files();
    layout(3);
    CHECK("between two installs: no Reload note yet", !seen("Changes take effect after a reload") && ctl_p(CMC_APPLY, 0)->x < 0);
    azmod_cm_bg_tick();
    CHECK("done and the catalog read again: the overlay's tick sends the next one", !strcmp(req(), "install " R0 " big-clock"));
    layout(3);
    CHECK("\"Updating 2 of 2...\", one row left", seen("Updating 2 of 2...") && nkind(CMC_UPDROW) == 1);
    g_s.mode = AZ_CLOSED; /* the menu closed: the run goes on */
    catalog("1.1.0", "1.0.0");
    status("done", "install", "big-clock", "Big Clock 1.0.0 installed. Press Reload now to start it.");
    reload_files();
    azmod_cm_bg_tick();
    CHECK("the menu closed: the run still ends", !g_cm_ua.on && g_cm_ua.result == CMU_FINISHED && !strcmp(req(), ""));
    layout(3);
    CHECK("the end: nothing to update, one line", seen("All your community mods are up to date.") && nkind(CMC_UPDROW) == 0 && nkind(CMC_UPDALL) == 0);
    CHECK("the end: the header's Reload note and Reload now", seen("Changes take effect after a reload") && ctl_p(CMC_APPLY, 0)->x >= 0);
    CHECK("no updates: the tab says \"Updates\" with no count", seen("Updates\n") && !seen("Updates 0"));

    /* an error stops the run and its message stays */
    catalog("1.0.0", "0.9.0");
    status("idle", "", "", "");
    reload_files();
    layout(3);
    cm_fire(ctl_p(CMC_UPDALL, 0));
    CHECK("a new run: the first install", !strcmp(req(), "install " R0 " echo-out"));
    status("error", "install", "echo-out", "Could not download Echo Out: no internet connection.");
    reload_files();
    azmod_cm_bg_tick();
    CHECK("an error stops it; the second is not sent", !g_cm_ua.on && g_cm_ua.result == CMU_FAILED && !strcmp(req(), ""));
    layout(3);
    CHECK("its message stays on the tab; Update all offered again", seen("Could not download Echo Out: no internet connection.") && seen("Update all (2)"));

    /* a reload ends it */
    status("idle", "", "", "");
    reload_files();
    layout(3);
    cm_fire(ctl_p(CMC_UPDALL, 0));
    req();
    cm_fire(&(cm_ctl) {CMC_RELOAD_OK, 0, 0, 0, 1, 1});
    CHECK("a reload ends the run", !g_cm_ua.on && !strcmp(req(), "apply"));
    status("done", "install", "echo-out", "x");
    catalog("1.1.0", "0.9.0");
    reload_files();
    azmod_cm_bg_tick();
    CHECK("CONTROL: after it, nothing more is sent", !strcmp(req(), ""));

    /* the Installed tab's rows: the checkbox left of the >, the knob, a touch */
    catalog("1.0.0", "0.9.0");
    status("idle", "", "", "");
    reload_files();
    req();
    g_full = 1;
    g_s.mode = AZ_COMMUNITY;
    g_s.pane = 1;
    g_cm_knob = 0;
    g_cm_hfoc = -1;
    layout(0);
    {
        const cm_ctl *row = ctl_p(CMC_MOD, 0), *box = ctl_p(CMC_CHK, 0), *row2 = ctl_p(CMC_INSTCAT, 1), *box2 = ctl_p(CMC_CHK, CM_CHK_CAT | 1);
        CHECK("each row has a checkbox, inside the row, clear of the > at its right edge",
              box->x > row->x && box->x + box->w < row->x + row->w - fb_w40() && box->y == row->y && box2->x == box->x && box2->y == row2->y);
        CHECK("the mod not installed has no row and no checkbox", ctl_p(CMC_INSTCAT, 4)->x < 0 && ctl_p(CMC_CHK, CM_CHK_CAT | 4)->x < 0);
        CHECK("the rows keep their state texts", seen("Running") && seen("Installed. Starts after Reload.") && seen("Update available"));
        int cx = box->x + box->w / 2, cy = box->y + box->h / 2, ox = row->x + row->w / 3;
        CHECK("a touch on the checkbox is the checkbox's", cm_home_ctl_at(&g_s, &g_fb, cx, cy) == (CMC_CHK << 16 | 0));
        cm_home_fire(&g_s, &g_fb, cm_home_ctl_at(&g_s, &g_fb, cx, cy));
        CHECK("... and it sends the page's Disable, opening nothing", !strcmp(req(), "disable echo-out") && g_cm_view == CMV_HOME);
        {
            int on = 1;
            CHECK("the box shows off at the tap, before the fetcher answers", cm_chk(0, NULL, &on) && on == 0);
        }
        layout(0);
        cm_home_fire(&g_s, &g_fb, cm_home_ctl_at(&g_s, &g_fb, box2->x + box2->w / 2, box2->y + box2->h / 2));
        CHECK("a second toggle while the first is unanswered is not sent (the fetcher would drop it)", !strcmp(req(), ""));
        status("done", "disable", "echo-out", "");
        reload_files();
        layout(0);
        cm_home_fire(&g_s, &g_fb, cm_home_ctl_at(&g_s, &g_fb, ox, cy));
        CHECK("CONTROL: a touch elsewhere on the row selects it (its page beside the list) and sends nothing",
              g_cm_view == CMV_HOME && g_cm_isel == (CMC_MOD << 16 | 0) && !strcmp(req(), ""));
        g_cm_view = CMV_HOME;
        layout(0);
        cm_home_fire(&g_s, &g_fb, cm_home_ctl_at(&g_s, &g_fb, box2->x + box2->w / 2, box2->y + box2->h / 2));
        CHECK("the checkbox of a mod installed since the start: the catalog page's Disable", !strcmp(req(), "disable big-clock"));
        status("done", "disable", "big-clock", "");   /* answered: the next toggle may go */
        reload_files();
    }
    status("done", "disable", "echo-out", "");
    reload_files();
    {
        int on = 1;
        CHECK("after the fetcher's done, the box shows the pending state (off)", cm_chk(0, NULL, &on) && on == 0);
        layout(0);
        const cm_ctl *bx0 = ctl_p(CMC_CHK, 0);
        cm_home_fire(&g_s, &g_fb, cm_home_ctl_at(&g_s, &g_fb, bx0->x + bx0->w / 2, bx0->y + bx0->h / 2));
        CHECK("tapped again: Enable sent, the box on at once", !strcmp(req(), "enable echo-out") && cm_chk(0, NULL, &on) && on == 1);
        status("error", "enable", "echo-out", "The USB is read-only.");
        reload_files();
        CHECK("the fetcher's error puts the box back (off)", cm_chk(0, NULL, &on) && on == 0);
    }
    cm_removed_add("big-clock");
    layout(0);
    CHECK("a removed mod has no checkbox", ctl_p(CMC_CHK, CM_CHK_CAT | 1)->x < 0 && ctl_p(CMC_INSTCAT, 1)->x >= 0);
    g_cm_removed[0][0] = 0;
    g_cm_view = CMV_HOME;
    g_cm_tab = 0;
    g_cm_knob = 0;
    cm_input(&g_s, &g_fb, 0, 0, false, 1, false);
    CHECK("the first detent shows the focus on the first row", g_cm_knob && g_cm_hfoc == (CMC_MOD << 16 | 0));
    layout(0);
    {
        const cm_ctl *row = ctl_p(CMC_MOD, 0);
        uint32_t v;
        memcpy(&v, g_px + (size_t) (row->y + 2) * g_fb.stride + (size_t) (row->x + 2) * 4, 4);
        CHECK("the focused row is drawn lit (split: the Mod Menu list's selected blue)",
              (v & 0x00FFFFFFu) == (cm_isplit() ? 0x00007DE1u : C_FOCUS_BAND));
    }
    cm_input(&g_s, &g_fb, 0, 0, false, 1, false);
    CHECK("the next detent: the next row, never the checkbox", g_cm_hfoc == (CMC_INSTCAT << 16 | 1));
    cm_input(&g_s, &g_fb, 0, 0, false, 5, false);
    CHECK("past the last row (Loop Kit): it stays on the last", g_cm_hfoc == (CMC_INSTCAT << 16 | 3));
    cm_input(&g_s, &g_fb, 0, 0, false, 0, true);
    CHECK("the knob's press goes into the focused row's page beside the list and toggles nothing",
          g_cm_view == CMV_HOME && g_cm_in && g_cm_isel == (CMC_INSTCAT << 16 | 3) && !strcmp(req(), ""));
    cm_back();
    CHECK("BACK comes out of the page to the list", g_cm_view == CMV_HOME && !g_cm_in);
    cm_input(&g_s, &g_fb, 0, 0, false, -2, false);
    CHECK("two rows up", g_cm_hfoc == (CMC_INSTCAT << 16 | 1));
    cm_input(&g_s, &g_fb, 0, 0, false, -1, false);
    CHECK("back up a row", g_cm_hfoc == (CMC_MOD << 16 | 0));
    cm_input(&g_s, &g_fb, 0, 0, false, 0, true);
    CHECK("its press goes into the loaded mod's page", g_cm_view == CMV_HOME && g_cm_in && g_cm_page == 0 && !strcmp(req(), ""));
    cm_back();
    /* the Updates tab: the knob reaches Update all and each row's Update */
    catalog("1.0.0", "0.9.0");
    status("idle", "", "", "");
    reload_files();
    g_cm_tab = 3;
    g_cm_hfoc = -1;
    g_cm_knob = 0;
    cm_input(&g_s, &g_fb, 0, 0, false, 1, false);
    cm_input(&g_s, &g_fb, 0, 0, false, 1, false);
    CHECK("Updates: the knob goes from Update all to the first row's Update", g_cm_hfoc == (CMC_UPDROW << 16 | 0));
    cm_input(&g_s, &g_fb, 0, 0, false, 0, true);
    CHECK("... and its press sends that install", !strcmp(req(), "install " R0 " echo-out"));
    g_full = 0;

    unlink(CM_CATALOG);
    unlink(CM_STATUS);
    /* opening the full screen refreshes: the first time, then at most once per CM_AUTO_MS, never
     * while the fetcher is busy */
    status("done", "install", "x", "");
    reload_files();
    g_cm_auto_at = 0;
    g_cm_full = 0;
    cm_full_open(&g_s);
    CHECK("opening Community Mods refreshes the catalog", !strcmp(req(), "refresh"));
    g_cm_full = 0;
    cm_full_open(&g_s);
    CHECK("opened again within 10 minutes: no second refresh", !strcmp(req(), ""));
    status("error", "refresh", "", "No internet: showing mods from the last refresh.");
    reload_files();
    g_cm_full = 0;
    cm_full_open(&g_s);
    CHECK("the last refresh failed: the next opening refreshes at once (no 10-minute wait)", !strcmp(req(), "refresh"));
    g_cm_auto_at = 0;
    status("downloading", "install", "echo-out", "Echo Out");
    reload_files();
    g_cm_full = 0;
    cm_full_open(&g_s);
    CHECK("the fetcher busy: no refresh sent (it would be dropped)", !strcmp(req(), ""));
    return ut_summary();
}
