/* SPDX-License-Identifier: GPL-3.0-only */
/* The Mod Menu's community catalog (menu.c cm_load / cm_layout) against catalog files:
 *   - every repository the fetcher can list (64 + 32) is held, and every mod line up to CM_NCAT;
 *   - mods past what the menu holds are never dropped silently: the Browse tab says
 *     "Showing the first N of M mods." (lines past the array plus the fetcher's more|<n>);
 *   - every tile drawn can be touched (a control per tile);
 *   - a mod from a repository that is not in the Mod Index carries a corner mark on its
 *     tile and the "Not in the Mod Index" note on its page; a listed one carries neither;
 *   - the same id elsewhere (fields 17-19): a line whose id another installed copy holds says so
 *     on its tile and page and offers no Install; a line others also offer says so; the name of
 *     an installed id never comes from another author's line;
 *   - Browse (field 20 and cm_browse.h through the real screen): the category buttons and their
 *     counts, the orders, the search field and its keyboard page, the clear square, and Add
 *     repository's keyboard still typing the address.
 * menu.c is #included (MENU_C) so its statics are visible; CM_CATALOG points at a temp file.
 * Text is captured through azmod_ttf_menu, which every c_text call goes through first. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include MENU_C

static char g_seen[1 << 16];
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

#define W 1280
#define H 800
static uint8_t g_px[W * H * 4];
static azmod_fb g_fb = {g_px, W, H, W * 4};
static azmod_state g_s;
static cm_ctl g_ctl[CM_MAXCTL];
static int g_nctl;

static void layout(int view, int tab, int cat) {
    memset(g_px, 0, sizeof g_px);
    g_seen_n = 0;
    g_seen[0] = 0;
    g_cm_full = 1;
    g_cm_view = view;
    g_cm_tab = tab;
    g_cm_cat = cat;
    g_cm_scroll = 0;
    g_nctl = cm_layout(&g_s, &g_fb, g_ctl, 1);
}
static int nkind(int kind) {
    int k = 0;
    for (int i = 0; i < g_nctl; i++) k += g_ctl[i].kind == kind;
    return k;
}
static uint32_t corner(int tile_idx) {   /* the pixel just inside a tile's top-right corner */
    for (int i = 0; i < g_nctl; i++)
        if (g_ctl[i].kind == CMC_TILE && g_ctl[i].idx == tile_idx) {
            int x = g_ctl[i].x + g_ctl[i].w - 2, y = g_ctl[i].y + 1;
            if (x < 0 || y < 0 || x >= W || y >= H) return 0xFFFFFFFFu;
            uint32_t v;
            memcpy(&v, g_px + (size_t) y * g_fb.stride + (size_t) x * 4, 4);
            return v & 0x00FFFFFFu;
        }
    return 0xFFFFFFFFu;
}

static void write_catalog(int nrepo, int nmod, int more) {
    FILE *f = fopen(CM_CATALOG ".new", "w");
    fprintf(f, "v1\n");
    for (int r = 0; r < nrepo; r++)   /* repository 0 is in the Mod Index, the rest were added */
        fprintf(f, "repo|https://example.com/r%d/azmods-repo.json|Repo %d|1|%d|\n", r, r, r == 0);
    for (int i = 0; i < nmod; i++)    /* mod 1 comes from an added repository, every other from repo 0 */
        fprintf(f, "mod|https://example.com/r%d/azmods-repo.json|mod-%03d|Mod %d|1.0.0|someone|Does a thing|tools|0|any|1000|||||0\n",
                i == 1 ? 1 : 0, i, i);
    if (more) fprintf(f, "more|%d\n", more);
    fclose(f);
    rename(CM_CATALOG ".new", CM_CATALOG);
    usleep(300000);   /* cm_load re-reads at most 4x a second */
    cm_load();
}

static void write_raw(const char *body) {
    FILE *f = fopen(CM_CATALOG ".new", "w");
    fprintf(f, "v1\nrepo|https://example.com/r0/azmods-repo.json|Repo 0|1|1|\n%s", body);
    fclose(f);
    rename(CM_CATALOG ".new", CM_CATALOG);
    usleep(300000);
    cm_load();
}
static int ctl_of(int kind, int idx) {
    for (int i = 0; i < g_nctl; i++)
        if (g_ctl[i].kind == kind && g_ctl[i].idx == idx) return i;
    return -1;
}
/* the control itself; one off the screen when there is none (a check on it then fails) */
static const cm_ctl *ctl_p(int kind, int idx) {
    static const cm_ctl none = {-1, -1, -1000, -1000, 1, 1};
    int i = ctl_of(kind, idx);
    return i < 0 ? &none : &g_ctl[i];
}

int main(void) {
    ut_suite = "catalog";
    /* the fetcher's most: 64 + 32 repositories; more mods than the menu holds */
    write_catalog(96, 200, 50);
    CHECK("all 96 repositories the fetcher can list are held", g_ncr == 96 && g_cr_more == 0);
    CHECK("the first 128 mod lines are held", g_ncc == 128);
    CHECK("mods past the array and the fetcher's more| line are counted (72 + 50)", g_cc_more == 122);
    layout(CMV_HOME, 1, -1);
    CHECK("Browse says it shows only the first ones", seen("Showing the first 128 of 250 mods."));
    CHECK("every one of the 128 tiles can be touched", nkind(CMC_TILE) == 128);
    CHECK("a mod from an added repository has the corner mark on its tile", corner(1) == C_AVAIL);
    CHECK("CONTROL: a mod from the Mod Index has none", corner(0) != C_AVAIL && corner(0) != 0xFFFFFFFFu);
    layout(CMV_HOME, 2, -1);
    CHECK("every added repository has its Remove control (95)", nkind(CMC_DELREPO) == 95);
    CHECK("no repository note when none was dropped", !seen("repositories."));
    layout(CMV_CAT, 0, 1);
    CHECK("an added repository's mod page says \"" CM_UNLISTED "\"", seen(CM_UNLISTED));
    layout(CMV_CAT, 0, 0);
    CHECK("CONTROL: a listed repository's mod page does not", !seen(CM_UNLISTED) && seen("Works on this deck."));

    /* a small catalog: everything shown, nothing to say */
    write_catalog(2, 3, 0);
    layout(CMV_HOME, 1, -1);
    CHECK("CONTROL: a catalog the menu holds whole has no note", g_ncc == 3 && g_cc_more == 0 && !seen("Showing the first") && nkind(CMC_TILE) == 3);

    /* more repository lines than the menu holds (a newer fetcher): said, not dropped silently */
    write_catalog(100, 3, 0);
    layout(CMV_HOME, 2, -1);
    CHECK("repository lines past the array are counted and said", g_ncr == 96 && g_cr_more == 4 && seen("Showing the first 96 of 100 repositories."));
    /* the same id elsewhere: line 0 is held by a copy from a repository that is gone, line 1 is
     * offered by two other repositories too, line 2 by nobody else (the control) */
    write_raw("mod|https://example.com/r0/azmods-repo.json|beat-tool|Beat Tool|1.0.0|alice|s|tools|0|any|1000|||||0|example.com/bob|example.com/bob/azmods-repo.json|1\n"
              "mod|https://example.com/r0/azmods-repo.json|loop-tool|Loop Tool|1.0.0|carol|s|tools|0|any|1000|||||0||someone/loops|2\n"
              "mod|https://example.com/r0/azmods-repo.json|plain-tool|Plain Tool|1.0.0|dave|s|tools|0|any|1000|||||0||0\n");
    CHECK("fields 17-19 are read (held, also, nalso)", g_ncc == 3 && !strcmp(g_cc[0].held, "example.com/bob") && g_cc[1].nalso == 2 && !strcmp(g_cc[1].also, "someone/loops"));
    layout(CMV_HOME, 1, -1);
    CHECK("a tile whose id another installed copy holds says so", seen("Its ID is in use"));
    layout(CMV_CAT, 0, 0);
    CHECK("its page names where the installed one came from", seen("A different mod with this ID is installed (from example.com/bob). Remove it first."));
    CHECK("its page offers no Install (the control is drawn inactive: the press is refused too)", ctl_of(CMC_ACT, CMA_INSTALL) >= 0);
    layout(CMV_CAT, 0, 1);
    CHECK("a line others also offer says how many", seen("someone/loops and 1 more also offer a mod with this ID. Only one can be installed."));
    layout(CMV_CAT, 0, 2);
    CHECK("CONTROL: a line nobody else offers carries neither note", !seen("this ID") && seen("Works on this deck."));
    CHECK("the name of a held id is not taken from the other author's line", !strcmp(cm_cat_name("beat-tool"), "beat-tool"));
    CHECK("CONTROL: an id nobody holds is named from its line", !strcmp(cm_cat_name("plain-tool"), "Plain Tool"));
    write_raw("mod|https://example.com/r0/azmods-repo.json|beat-tool|Beat Tool|1.0.0|alice|s|tools|0|any|1000|||||0\n");
    layout(CMV_CAT, 0, 0);
    CHECK("CONTROL: a 16-field line (an older fetcher) still reads, with no notes", g_ncc == 1 && !g_cc[0].held[0] && g_cc[0].nalso == 0 && !seen("this ID"));

    /* Browse: field 20 (updated), the category row, the orders and the search (cm_browse.h) */
    write_raw("mod|https://example.com/r0/azmods-repo.json|echo-out|Echo Out|1.0.0|dubwise|s|audio|0|any|1000|||||0||||2019-09-14\n"
              "mod|https://example.com/r0/azmods-repo.json|big-clock|Big Clock|1.0.0|nightowl|s|visual|0|any|1000|||||0||||2019-10-12\n"
              "mod|https://example.com/r0/azmods-repo.json|old-date|Aged|1.0.0|dubwise|s|tools|0|any|1000|||||0||||20191008\n");
    CHECK("field 20 is read; a malformed date is dropped", !strcmp(g_cc[0].upd, "2019-09-14") && !strcmp(g_cc[1].upd, "2019-10-12") && !g_cc[2].upd[0]);
    layout(CMV_HOME, 1, -1);
    CHECK("Browse: a search field, three orders and six category buttons", nkind(CMC_SEARCH) == 1 && nkind(CMC_SORT) == 3 && nkind(CMC_FCAT) == 6 && nkind(CMC_QCLEAR) == 0);
    CHECK("Browse: the buttons count the mods", seen("All  3") && seen("Audio  1") && seen("Visual  1") && seen("Tools  1") && seen("Other  0"));
    CHECK("A-Z: Aged, Big Clock, Echo Out", ctl_p(CMC_TILE, 2)->x < ctl_p(CMC_TILE, 1)->x && ctl_p(CMC_TILE, 1)->x < ctl_p(CMC_TILE, 0)->x);
    cm_fire(ctl_p(CMC_SORT, CMB_SORT_UPDATED));
    layout(CMV_HOME, 1, -1);
    CHECK("Updated: the newest first, the undated last", g_cm_sort == CMB_SORT_UPDATED && ctl_p(CMC_TILE, 1)->x < ctl_p(CMC_TILE, 0)->x &&
                                                             ctl_p(CMC_TILE, 0)->x < ctl_p(CMC_TILE, 2)->x);
    g_cm_scroll = 300;
    cm_fire(ctl_p(CMC_FCAT, 0));
    CHECK("a category button picks it and goes back to the top", g_cm_fcat == 0 && g_cm_scroll == 0);
    layout(CMV_HOME, 1, -1);
    CHECK("Visual: only its mod", nkind(CMC_TILE) == 1 && ctl_of(CMC_TILE, 1) >= 0);
    cm_fire(ctl_p(CMC_SEARCH, 0));
    CHECK("the search field opens the keyboard for the search", g_cm_view == CMV_KBD && g_cm_kbsearch);
    for (const char *k = "dub"; *k; k++) cm_fire(&(cm_ctl) {CMC_KEY, (unsigned char) *k, 0, 0, 1, 1});
    CHECK("keys go into the search, not the repository address", !strcmp(g_cm_q, "dub") && !g_cm_kb[0]);
    g_cm_fcat = CMB_ALL;
    layout(CMV_KBD, 1, -1);
    CHECK("the keyboard page shows the matches as they are typed", seen("2 mods match: Echo Out, Aged") && seen("Done") && seen("Clear") && !seen("github.com/"));
    layout(CMV_HOME, 1, -1);
    CHECK("the search narrows the grid and the counts; the field can be cleared", nkind(CMC_TILE) == 2 && seen("All  2") && seen("Visual  0") && nkind(CMC_QCLEAR) == 1);
    snprintf(g_cm_q, sizeof g_cm_q, "zzz");
    layout(CMV_HOME, 1, -1);
    CHECK("nothing found: said", nkind(CMC_TILE) == 0 && seen("No mods match the search here. Try All, or clear the search."));
    cm_fire(ctl_p(CMC_QCLEAR, 0));
    CHECK("the clear square empties the search", !g_cm_q[0]);
    snprintf(g_cm_q, sizeof g_cm_q, "dub");
    g_cm_view = CMV_KBD;
    cm_fire(&(cm_ctl) {CMC_KB_NO, 0, 0, 0, 1, 1});
    CHECK("Clear on the keyboard empties the search and stays", !g_cm_q[0] && g_cm_view == CMV_KBD);
    cm_fire(&(cm_ctl) {CMC_KB_OK, 0, 0, 0, 1, 1});
    CHECK("Done goes back to the grid", g_cm_view == CMV_HOME);
    cm_fire(&(cm_ctl) {CMC_ADDREPO, 0, 0, 0, 1, 1});
    cm_fire(&(cm_ctl) {CMC_KEY, 'a', 0, 0, 1, 1});
    CHECK("CONTROL: Add repository's keyboard still types the address", !g_cm_kbsearch && !strcmp(g_cm_kb, "a") && !g_cm_q[0]);
    g_cm_kb[0] = 0;
    g_cm_sort = CMB_SORT_NAME;
    unlink(CM_CATALOG);
    /* the header: the breadcrumb on every page, the trust note on the Installed tab only */
    layout(CMV_HOME, 0, -1);
    CHECK("Installed: \"Community Mods  >  Installed\" and the trust note",
          seen("Community Mods  >  Installed") && seen(CM_TRUST_NOTE));
    layout(CMV_HOME, 1, -1);
    CHECK("Browse: its breadcrumb, no trust note", seen("Community Mods  >  Browse") && !seen("Made by other"));
    layout(CMV_HOME, 2, -1);
    CHECK("Repositories: its breadcrumb, no trust note", seen("Community Mods  >  Repositories") && !seen("Made by other"));
    if (g_ncc > 0) {
        char want[256];
        snprintf(want, sizeof want, "Community Mods  >  Browse  >  %s", g_cc[0].name);
        layout(CMV_CAT, 1, 0);
        CHECK("a mod's card opened from Browse: \"Community Mods  >  Browse  >  <name>\"", seen(want));
        snprintf(want, sizeof want, "Community Mods  >  Installed  >  %s", g_cc[0].name);
        layout(CMV_CAT, 0, 0);
        CHECK("... from Installed: \"Community Mods  >  Installed  >  <name>\"", seen(want) && !seen("Made by other"));
        snprintf(want, sizeof want, "Community Mods  >  Browse  >  %s  >  Install", g_cc[0].name);
        layout(CMV_TRUST, 1, 0);
        CHECK("its install confirmation: \"...  >  <name>  >  Install\"", seen(want));
    } else
        CHECK("the catalog has a mod for the breadcrumb checks", 0);
    g_cm_kbsearch = 1;
    layout(CMV_KBD, 1, -1);
    CHECK("the search keyboard: \"Community Mods  >  Browse  >  Search\"", seen("Community Mods  >  Browse  >  Search"));
    g_cm_kbsearch = 0;
    layout(CMV_KBD, 2, -1);
    CHECK("Add repository: \"Community Mods  >  Repositories  >  Add repository\"",
          seen("Community Mods  >  Repositories  >  Add repository"));
    {
        char t[CM_CRUMB_MAX];
        g_cm_view = CMV_KBD;
        g_cm_tab = 1;
        g_cm_kbsearch = 1;
        cm_crumbs(t, sizeof t, 0, 20);
        CHECK("room: the whole path", !strcmp(t, "Community Mods  >  Browse  >  Search"));
        cm_crumbs(t, sizeof t, cm_tw("Community Mods  >  Search", 20) + 2, 20);
        CHECK("too narrow for the tab: the tab left out, the page kept", !strcmp(t, "Community Mods  >  Search"));
        g_cm_view = CMV_RELOAD;
        cm_crumbs(t, sizeof t, 0, 20);
        CHECK("the reload page: \"Community Mods  >  Reload\"", !strcmp(t, "Community Mods  >  Reload"));
        g_cm_view = CMV_HOME;
        g_cm_kbsearch = 0;
    }
    return ut_summary();
}
