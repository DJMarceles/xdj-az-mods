/* SPDX-License-Identifier: GPL-3.0-only */
/* Community Mods when the core did not load them this start (menu.c, screen_community.c):
 *   - the notice shows only on the Installed tab, under the tabs: its title, the reason (restarts or
 *     crashes, from the core's reason), the names, Load next time and Keep off; no other tab, the
 *     left-pane card or the mod rows say "held back"; a row says "Not loaded";
 *   - after Load next time the notice says the mods load again after a reload and points to Reload,
 *     the header's square at the top right (the one Reload everywhere), in a box of the same size, so
 *     the rows below do not move; once pressed the square waits ("Please wait...", no control);
 *   - after Keep off the notice says they stay disabled, with no buttons, in the same box;
 *   - Reload after Load next time sends the "apply" request, by a tap (press and release), with or
 *     without a track loaded (the question
 *     first), and the page keeps saying that the mods load again (it is not reset to the question
 *     that Load next time already answered);
 *   - the Browse tab has no search field.
 * menu.c is #included (MENU_C) so its statics are visible. azcm_* are this file's, so a case sets
 * what the core reports; CM_REQ is a temp file the test reads back. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include MENU_C

static char g_seen[1 << 15];
static size_t g_seen_n;
int azmod_ttf_menu(const azmod_fb *fb, int x, int y, int w, int h, const char *s, int px, int track, int bold, uint32_t c) {
    (void) fb;
    (void) x;
    (void) y;
    (void) w;
    (void) h;
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

/* what the core reports */
static int g_suspended = 1, g_loaded, g_resume_calls, g_reload_calls;
static char g_reason[160];
static azcm_info_t g_mods[2];
int azcm_count(void) { return 2; }
int azcm_info(int i, azcm_info_t *out) {
    if (i < 0 || i > 1) return -1;
    *out = g_mods[i];
    return 0;
}
int azcm_suspended(void) { return g_suspended; }
void azcm_resume(void) { g_resume_calls++; }   /* the core stays suspended until the next start */
int azcm_any_loaded(void) { return g_loaded; }
void azcm_reload_requested(void) { g_reload_calls++; }

#define W 1280
#define H 800
static uint8_t g_px[W * H * 4];
static azmod_fb g_fb = {g_px, W, H, W * 4};
static azmod_state g_s;
static cm_ctl g_ctl[CM_MAXCTL];
static int g_nctl;
static long g_t = 1000;

static void redraw(void) {
    memset(g_px, 0, sizeof g_px);
    g_seen_n = 0;
    g_seen[0] = 0;
    g_nctl = cm_layout(&g_s, &g_fb, g_ctl, 1);
}
static void draw_page(int tab) {   /* the Installed (0), Browse (1) or Repositories (2) tab, from the top */
    g_s.mode = AZ_COMMUNITY;
    g_cm_full = 1;
    g_cm_dir = 0;
    g_cm_view = CMV_HOME;
    g_cm_tab = tab;
    g_cm_scroll = 0;
    redraw();
}
static const cm_ctl *ctl_p(int kind, int idx) {
    static const cm_ctl none = {-1, -1, -1000, -1000, 1, 1};
    int n = cm_layout(&g_s, &g_fb, g_ctl, 0);
    for (int i = 0; i < n; i++)
        if (g_ctl[i].kind == kind && g_ctl[i].idx == idx) return &g_ctl[i];
    return &none;
}
/* the deck's stream: press, then release, nothing between */
static void tap(const cm_ctl *c, long ms) {
    int x = c->x + c->w / 2, y = c->y + c->h / 2;
    g_t += 1000;
    azmod_menu_clock(g_t);
    azmod_input(&g_s, &g_fb, x, y, true, 0, false);
    azmod_menu_clock(g_t + ms);
    azmod_pane_drag(&g_s, &g_fb, x, y, 2);
}
static void write_catalog(int nmod) {
    FILE *f = fopen(CM_CATALOG ".new", "w");
    fprintf(f, "v1\nrepo|https://example.com/r0/azmods-repo.json|Repo 0|1|1|\n");
    for (int i = 0; i < nmod; i++)
        fprintf(f, "mod|https://example.com/r0/azmods-repo.json|mod-%03d|Mod %d|1.0.0|someone|Does a thing|tools|0|any|1000|||||0\n", i, i);
    fclose(f);
    rename(CM_CATALOG ".new", CM_CATALOG);
    usleep(300000); /* cm_load re-reads at most 4x a second */
    cm_load();
}
static void write_catalog_named(void) {   /* the catalog names the two installed mods */
    FILE *f = fopen(CM_CATALOG ".new", "w");
    fprintf(f, "v1\nrepo|https://example.com/r0/azmods-repo.json|Repo 0|1|1|\n"
               "mod|https://example.com/r0/azmods-repo.json|echo-out|Echo Out|1.0.0|someone|Downloads|tools|0|any|1000|||||0\n"
               "mod|https://example.com/r0/azmods-repo.json|beat-meter|Beat Meter|1.0.0|someone|Plays|tools|0|any|1000|||||0\n");
    fclose(f);
    rename(CM_CATALOG ".new", CM_CATALOG);
    usleep(300000);
    cm_load();
}
static void set_mods(void) {
    memset(g_mods, 0, sizeof g_mods);
    strcpy(g_mods[0].id, "echo-out");
    strcpy(g_mods[1].id, "beat-meter");
    for (int i = 0; i < 2; i++) {
        strcpy(g_mods[i].version, "1.0.0");
        g_mods[i].state = AZCM_SUSPENDED;
        snprintf(g_mods[i].reason, sizeof g_mods[i].reason, "%s", g_reason);
    }
    strcpy(g_mods[0].name, "Echo Out");
    strcpy(g_mods[1].name, "Beat Meter");
}
static void reset(const char *reason) {
    snprintf(g_reason, sizeof g_reason, "%s", reason);
    g_suspended = 1;
    g_loaded = 0;
    g_resume_calls = g_reload_calls = 0;
    g_cm_resumed = g_cm_keptoff = 0;
    g_cm_reload = g_cm_reloading = 0;
    g_cm_pending_off = -1;
    set_mods();
    unlink(CM_REQ);
}
static int req_is(const char *want) {   /* the request file holds exactly this line, then it is removed */
    char line[64] = "";
    FILE *f = fopen(CM_REQ, "r");
    if (f) {
        if (!fgets(line, sizeof line, f)) line[0] = 0;
        fclose(f);
    }
    line[strcspn(line, "\n")] = 0;
    unlink(CM_REQ);
    return f && !strcmp(line, want);
}
/* the Reload control below the header (the notice's) or in it (Reload now) */
static const cm_ctl *apply_ctl(int in_header) {
    static const cm_ctl none = {-1, -1, -1000, -1000, 1, 1};
    int n = cm_layout(&g_s, &g_fb, g_ctl, 0), hb = dock_y(&g_fb) + menu_hdrh(&g_fb);
    for (int i = 0; i < n; i++)
        if (g_ctl[i].kind == CMC_APPLY && (g_ctl[i].y < hb) == (in_header != 0)) return &g_ctl[i];
    return &none;
}
static int row_y(void) {   /* the first mod row */
    return ctl_p(CMC_MOD, 0)->y;
}

int main(void) {
    ut_suite = "community not loaded";
    azmod_state_init(&g_s);
    azmod_set_docked(true);
    g_s.menu_focused = 1;
    const char *restarts = "Suspended: the deck restarted twice right after starting with community mods";

    /* the notice, on the Installed tab only */
    reset(restarts);
    write_catalog_named();
    draw_page(0);
    CHECK("Installed: the title", seen("Community mods were not loaded this time."));
    CHECK("Installed: the reason for restarts", seen("The deck restarted twice while they were starting."));
    CHECK("Installed: the names", seen("Not loaded: Echo Out, Beat Meter"));
    CHECK("Installed: Load next time and Keep off", seen("Load next time") && seen("Keep off") && ctl_p(CMC_RESUME, 0)->x >= 0 &&
                                                        ctl_p(CMC_KEEPOFF, 0)->x >= 0);
    CHECK("the old wording is gone", !seen("held back") && !seen("Held back") && !seen("Resume"));
    CHECK("a row says Not loaded", seen("\nNot loaded\n"));
    {
        const cm_ctl *tab = ctl_p(CMC_TAB, 0), *res = ctl_p(CMC_RESUME, 0);
        CHECK("the notice sits directly under the tabs", res->y > tab->y + tab->h && res->y < tab->y + tab->h + H / 4);
    }
    draw_page(1);
    CHECK("Browse: no notice", !seen("not loaded") && !seen("Not loaded") && ctl_p(CMC_RESUME, 0)->x < 0);
    draw_page(2);
    CHECK("Repositories: no notice", !seen("not loaded") && !seen("Not loaded") && ctl_p(CMC_RESUME, 0)->x < 0);
    memset(g_px, 0, sizeof g_px);
    g_seen_n = 0;
    g_seen[0] = 0;
    g_cm_full = 0;
    cm_draw_card(&g_s, &g_fb);
    g_cm_full = 1;
    CHECK("the left-pane card has no notice line", !seen("held back") && !seen("Held back") && !seen("not loaded") && !seen("Not loaded"));
    reset("Suspended: the deck crashed 3 times in 30 minutes with community mods on");
    draw_page(0);
    CHECK("Installed: the reason for crashes", seen("The deck crashed 3 times in 30 minutes with them on."));

    /* Load next time: the notice changes, nothing below it moves */
    reset(restarts);
    draw_page(0);
    int y0 = row_y(), tab_y = ctl_p(CMC_TAB, 0)->y;
    tap(ctl_p(CMC_RESUME, 0), 60);
    draw_page(0);
    CHECK("Load next time was pressed once", g_resume_calls == 1 && g_cm_resumed);
    CHECK("after it: they load again after a reload", seen("They load again after a reload.") && !seen("Community mods were not loaded this time."));
    CHECK("after it: Reload in the header (top right), none in the notice, which points to it",
          apply_ctl(0)->x < 0 && apply_ctl(1)->x >= 0 && seen("Press Reload at the top right to load them now."));
    {
        const cm_ctl *rl = apply_ctl(1);
        CHECK("the header's Reload is a square at the top right", rl->w == rl->h && rl->x + rl->w > W * 9 / 10);
    }
    CHECK("the rows and the tabs did not move", row_y() == y0 && ctl_p(CMC_TAB, 0)->y == tab_y);
    CHECK("CONTROL: before Load next time the page had no Reload button", (reset(restarts), draw_page(0), 1) && apply_ctl(0)->x < 0 && apply_ctl(1)->x < 0);

    /* Reload after Load next time sends apply, like Reload now */
    reset(restarts);
    draw_page(0);
    tap(ctl_p(CMC_RESUME, 0), 60);
    draw_page(0);
    CHECK("the header's Reload is there", apply_ctl(1)->x >= 0);
    tap(apply_ctl(1), 60);
    CHECK("Reload (no track loaded) sends the apply request", req_is("apply") && g_reload_calls == 1);
    draw_page(0);
    CHECK("the page still says they load again after a reload (not reset to the question)",
          g_cm_resumed && seen("They load again after a reload.") && !seen("Community mods were not loaded this time."));
    CHECK("pressed, Reload waits: Please wait..., no control to press again", seen("Please wait...") && apply_ctl(1)->x < 0);

    /* with a track loaded Reload asks first */
    reset(restarts);
    g_loaded = 1;
    draw_page(0);
    tap(ctl_p(CMC_RESUME, 0), 60);
    draw_page(0);
    tap(apply_ctl(1), 60);
    redraw();
    CHECK("a track loaded: Reload asks first and sends nothing yet", g_cm_view == CMV_RELOAD && !req_is("apply") && seen("Reload now?"));
    tap(ctl_p(CMC_RELOAD_OK, 0), 60);
    CHECK("... and the question's Reload sends the apply request", req_is("apply") && g_reload_calls == 1);
    CHECK("... and the page returns to Installed saying they load again", g_cm_view == CMV_HOME && g_cm_resumed);

    /* the same through the header's button */
    reset(restarts);
    draw_page(0);
    tap(ctl_p(CMC_RESUME, 0), 60);
    draw_page(0);
    CHECK("the header carries Reload", apply_ctl(1)->x >= 0);
    tap(apply_ctl(1), 60);
    CHECK("Reload in the header sends the apply request", req_is("apply"));

    /* Reload: a press under a second restarts, one held a full second is taken back; the
     * notice's other buttons keep the 400 ms */
    {
        long holds[] = {700, 999, 1000};
        int want[] = {1, 1, 0}, ok = 1;
        for (int i = 0; i < 3; i++) {
            reset(restarts);
            draw_page(0);
            tap(ctl_p(CMC_RESUME, 0), 60);
            draw_page(0);
            tap(apply_ctl(1), holds[i]);
            ok &= req_is("apply") == want[i];
        }
        CHECK("Reload: held 700 and 999 ms sends apply, 1000 ms nothing", ok);
        reset(restarts);
        draw_page(0);
        tap(ctl_p(CMC_RESUME, 0), 700);
        CHECK("Load next time held 700 ms does nothing (the 400 ms rule)", g_resume_calls == 0 && !g_cm_resumed);
    }

    /* Keep off: the notice says so in the same box, the rows say so, nothing below it moves */
    reset(restarts);
    draw_page(0);
    y0 = row_y();
    tap(ctl_p(CMC_KEEPOFF, 0), 60);
    draw_page(0);
    CHECK("Keep off: the notice says they stay disabled, the rows say all community mods are disabled",
          !seen("Community mods were not loaded this time.") && seen("Community mods are disabled. They stay disabled after a reload.") &&
              seen("Disabled (all community mods are disabled)"));
    CHECK("Keep off: no buttons left in the notice", ctl_p(CMC_RESUME, 0)->x < 0 && ctl_p(CMC_KEEPOFF, 0)->x < 0 && apply_ctl(0)->x < 0);
    CHECK("Keep off: the rows did not move", row_y() == y0);

    /* the Mod Menu's card (the Community Mods row highlighted): the master switch offers Reload,
     * never sends it; the card's Reload square sends it once and then waits */
    reset(restarts);
    g_suspended = 0;
    cm_master_toggle();
    memset(g_px, 0, sizeof g_px);
    g_seen_n = 0;
    g_seen[0] = 0;
    g_cm_full = 0;
    cm_draw_card(&g_s, &g_fb);
    CHECK("card: the master switch toggled, Reload offered with its note", cm_card_rshow() && seen("Changes take effect after a reload"));
    CHECK("card: toggling sends no reload by itself", !req_is("apply"));
    CHECK("card: its Reload square sends the apply request", cm_card_tap_reload() && req_is("apply") && g_cm_reloading);
    memset(g_px, 0, sizeof g_px);
    g_seen_n = 0;
    g_seen[0] = 0;
    cm_draw_card(&g_s, &g_fb);
    CHECK("card: then it waits (Please wait...), and a second tap sends nothing", seen("Please wait...") && !cm_card_tap_reload() && !req_is("apply"));
    g_cm_full = 1;
    cm_master_toggle();   /* back as it was */
    g_cm_pending_off = -1;

    /* Browse has no search field */
    reset(restarts);
    g_suspended = 0;
    write_catalog(5);
    g_cm_fcat = CMB_ALL;
    draw_page(1);
    CHECK("Browse: the Sort button is there, no search field", ctl_p(CMC_SORTBTN, 0)->x >= 0 && !seen("Search name, author or description"));
    return ut_summary();
}
