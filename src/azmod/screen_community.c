/* SPDX-License-Identifier: GPL-3.0-only */
/* screen_community.c: Community Mods: the catalog and status files, the pages, the slide, the draw services a mod's page paints through.
 * Part of the Mod Menu's one translation unit: #included by menu.c, in the order listed there. */
/* ==== Community (Mod Menu > Community Mods) ============================================
 * Home: the quarantine banner, the master switch, the firmware tag and the trust note, then
 * three tabs: Installed (azcm_info rows), Browse (the catalog as a grid of tiles) and
 * Repositories (+ Add repository, Refresh). Pages: an installed mod (state, Turn back on, its
 * own screen), a catalog mod (Install / Update / Remove / Enable / Disable, progress), the
 * trust screen (first install, once), the Add-repository keyboard.
 * Reads: community.c through azcm_* (menu.h) and the fetcher's line files
 * (/tmp/az-community/catalog, /status). Writes: only /run/az-community.req (tmp + rename) and
 * the trust flag. Never the network, never a fork. Layout and hit-testing share one function
 * (cm_layout), so what is drawn is what is hit. Icons: azcm_icon (community_icons.c,
 * decoded by EP147's own JUCE PNG decoder); until one is ready a tile shows the first letter. */
#include <sys/stat.h>
/* every control of one page: up to CM_NCAT tiles, or CM_NREPO repository rows with a Remove each,
 * plus the tabs and buttons; a control past this would be drawn but could not be touched */
#define CM_MAXCTL 256
#define CM_BAD 0x00FF4040u   /* failed / suspended: the menu's warning red */
#ifndef CM_CATALOG           /* the host tests point it at a fixture */
#define CM_CATALOG "/tmp/az-community/catalog"
#endif
#define CM_STATUS "/tmp/az-community/status"
#define CM_REQ "/run/az-community.req"
#define CM_TRUSTED "/home/root/settings/azmod-community/.trusted"   /* confirmed once */
enum {
    CMC_RESUME = 1,
    CMC_KEEPOFF,
    CMC_MASTER,
    CMC_MOD,
    CMC_UNSUSPEND,
    CMC_TAB,
    CMC_TILE,
    CMC_REFRESH,
    CMC_ADDREPO,
    CMC_DELREPO,
    CMC_ACT,
    CMC_APPLY,
    CMC_TRUST_OK,
    CMC_TRUST_NO,
    CMC_KEY,
    CMC_KB_OK,
    CMC_KB_NO,
    CMC_BACK,
    CMC_MODACT,
    CMC_INSTCAT,
    CMC_RELOAD_OK,
    CMC_RELOAD_NO,
    CMC_MODUPD,
    CMC_ALLOW
};
enum {
    CMA_INSTALL = 1,
    CMA_UPDATE,
    CMA_REMOVE,
    CMA_ENABLE,
    CMA_DISABLE
};
enum {
    CMV_HOME = 0,
    CMV_MOD,
    CMV_CAT,
    CMV_TRUST,
    CMV_KBD,
    CMV_RELOAD
};
typedef struct {
    int kind, idx, x, y, w, h;
} cm_ctl;
/* sized to the fetcher's caps (repo 256, firmware list up to 32 tags), so nothing is cut */
typedef struct {
    char repo[260], name[48], err[112];
    int ok, listed;
} cm_crepo;
/* held: the id is installed from another repository (its short name) or copied by hand ("-");
 * also/nalso: the first other repository offering the same id, and how many do (the fetcher shows
 * one line per id: an id is one folder, one settings file and one data folder on the deck) */
typedef struct {
    char repo[260], id[33], name[48], ver[24], author[48], summary[120], cat[24], fw[800], icon[160], inst[24], mc[16],
        desc[600], held[128], also[128];
    int hooks, assets_kb, nalso;
    long size;
} cm_cmod;
/* CM_NREPO: every repository the fetcher can list (64 from the Mod Index + 32 the user
 * added). CM_NCAT: the catalog's mod lines (the fetcher's CAT_MODS); a cm_cmod is ~2.2 KB, so
 * 128 of them are ~280 KB. Mods past it are counted (the fetcher's closing more|<n> line, plus
 * any line past the array) and the Browse tab says it shows only the first ones. */
#define CM_NREPO 96
#define CM_NCAT 128
static cm_crepo g_cr[CM_NREPO];
static int g_ncr, g_cr_more;
static cm_cmod g_cc[CM_NCAT];
static int g_ncc, g_cc_more;
static struct {
    char state[16], op[64], id[33], msg[112];
    long done, total;
} g_cs;
static int g_cm_view, g_cm_page = -1, g_cm_cat = -1, g_cm_tab;
static int g_cm_sel;                /* knob focus: index into the current page's controls */
static int g_cm_knob;               /* the knob moved focus last: show it (a touch hides it) */
static int g_cm_press = -1;         /* the control under a finger: lit from press to lift */
static int g_cm_scroll;             /* px the home page is scrolled (the knob keeps its focus in view) */
static int g_cm_resumed, g_cm_keptoff, g_cm_pending_off = -1, g_cm_unsusp[16];
static int g_cm_reload;             /* a request changed what loads: offer Reload */
static int g_cm_allow_err = -1;     /* azcm index whose Allow could not be saved on the deck, or -1 */
static int g_cm_fresh;              /* the fetcher's status changed during this start (older lines are not shown) */
static int g_cm_home_sel = -1, g_cm_home_scroll;   /* home focus, restored when a page goes back */
static int g_cm_reload_from;        /* the view the Reload confirm returns to */
static int g_cm_row = -1;           /* the main list's SCREEN row that opened this page (cm_open_row), or -1 */
static int g_cm_row_opening;        /* cm_open_row is firing CMC_MOD: keep the row's screen */
static int g_cm_row_pane, g_cm_row_sel;   /* where that row was: BACK returns there */
/* A destructive button (Remove) asks first, in the core's yes/no dialog (azcm_confirm: Cancel
 * focused, BACK = no). The request is captured when asked and sent only on yes. One question at a
 * time: while one is open a second press does nothing, so it can never retarget the first. */
static struct {
    int open;
    const char *fmt;
    char arg[260];
} g_cm_ask;
static void cm_req(const char *fmt, const char *a, const char *b);
static int cm_busy(void);
static void cm_ask_done(int yes, void *user) {
    (void) user;
    g_cm_ask.open = 0;
    if (yes && !cm_busy())
        cm_req(g_cm_ask.fmt, g_cm_ask.arg, 0);
}
static void cm_ask(const char *title, const char *text, const char *fmt, const char *arg) {
    if (g_cm_ask.open)
        return;
    g_cm_ask.fmt = fmt;
    snprintf(g_cm_ask.arg, sizeof g_cm_ask.arg, "%s", arg);
    g_cm_ask.open = azcm_confirm(title, text, "Remove", cm_ask_done, 0) == 0;
}
/* mods removed this start (the button and the Installed list say so until the reload) */
static char g_cm_removed[8][33];
static void cm_removed_add(const char *id) {
    int k = 0;
    for (; k < 8 && g_cm_removed[k][0] && strcmp(g_cm_removed[k], id); k++) {}
    if (k == 8) k = 7;
    snprintf(g_cm_removed[k], sizeof g_cm_removed[k], "%s", id);
}
static int cm_removed(const char *id) {
    for (int k = 0; k < 8 && g_cm_removed[k][0]; k++)
        if (!strcmp(g_cm_removed[k], id)) return 1;
    return 0;
}
/* enable/disable requests the fetcher finished this start: the button shows the pending state
 * (what loads after the reload), not the state the core loaded at start */
static struct {
    char id[33];
    int on;
} g_cm_pend[8];
static void cm_pend_set(const char *id, int on) {
    int k = 0;
    for (; k < 8 && g_cm_pend[k].id[0] && strcmp(g_cm_pend[k].id, id); k++) {}
    if (k == 8) k = 7;
    snprintf(g_cm_pend[k].id, sizeof g_cm_pend[k].id, "%s", id);
    g_cm_pend[k].on = on;
}
static int cm_pend_get(const char *id) { /* 1 on, 0 off, -1 nothing pending */
    for (int k = 0; k < 8 && g_cm_pend[k].id[0]; k++)
        if (!strcmp(g_cm_pend[k].id, id)) return g_cm_pend[k].on;
    return -1;
}
static char g_cm_kb[96];
static int g_cm_kbnum;
static long cm_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}
/* ---- the fetcher's files ---------------------------------------------------------- */
static int cm_split(char *line, char **f, int max) {   /* '|' fields, in place */
    int n = 0;
    f[n++] = line;
    for (char *p = line; *p && n < max; p++)
        if (*p == '|') {
            *p = 0;
            f[n++] = p + 1;
        }
    return n;
}
static void cm_cpy(char *d, size_t n, const char *s) { snprintf(d, n, "%s", s ? s : ""); }
/* a mod's repository is in the Mod Index. A repository the catalog does not name counts as
 * not listed: the note it earns is the cautious answer. */
static int cm_repo_listed(const char *repo) {
    for (int i = 0; i < g_ncr; i++)
        if (!strcmp(g_cr[i].repo, repo)) return g_cr[i].listed;
    return 0;
}
#define CM_UNLISTED "Not in the Mod Index"   /* the wording the SDK and index docs promise */
#define CM_HELD_FROM "A different mod with this ID is installed (from %s). Remove it first."
#define CM_HELD_HAND "A different mod with this ID is on the USB (copied there by hand). Remove it first."

static void cm_load(void) {   /* re-read catalog + status when they changed; at most 4x a second */
    static long last;
    static time_t cmt;
    static off_t csz;
    static time_t smt;
    static off_t ssz;
    static ino_t cino, sino;   /* the fetcher replaces each file (tmp + mv): a new inode is a new file */
    static int seen;           /* the status file as found at start belongs to an earlier start */
    long now = cm_ms();
    if (now - last < 250) return;
    last = now;
    struct stat st;
    if (stat(CM_CATALOG, &st) == 0) {
        if (st.st_mtime != cmt || st.st_size != csz || st.st_ino != cino) {
            cmt = st.st_mtime;
            csz = st.st_size;
            cino = st.st_ino;
            g_ncr = g_ncc = g_cr_more = g_cc_more = 0;
            FILE *f = fopen(CM_CATALOG, "re");
            static char line[4096];
            int v1 = 0;
            while (f && fgets(line, sizeof line, f)) {
                line[strcspn(line, "\r\n")] = 0;
                if (!v1) {
                    v1 = !strcmp(line, "v1");
                    if (!v1) break;
                    continue;
                }   /* unknown version: show nothing */
                char *fl[19];
                int n = cm_split(line, fl, 19);
                if (n >= 6 && !strcmp(fl[0], "repo") && g_ncr >= CM_NREPO) g_cr_more++;
                else if (n >= 13 && !strcmp(fl[0], "mod") && g_ncc >= CM_NCAT) g_cc_more++;
                else if (n == 2 && !strcmp(fl[0], "more")) {
                    long k = strtol(fl[1], NULL, 10);
                    if (k > 0) g_cc_more += k > 100000 ? 100000 : (int) k;
                } else if (n >= 6 && !strcmp(fl[0], "repo")) {
                    cm_crepo *r = &g_cr[g_ncr++];
                    cm_cpy(r->repo, sizeof r->repo, fl[1]);
                    cm_cpy(r->name, sizeof r->name, fl[2]);
                    r->ok = atoi(fl[3]);
                    r->listed = atoi(fl[4]);
                    cm_cpy(r->err, sizeof r->err, fl[5]);
                } else if (n >= 13 && !strcmp(fl[0], "mod")) {
                    cm_cmod *m = &g_cc[g_ncc++];
                    cm_cpy(m->repo, sizeof m->repo, fl[1]);
                    cm_cpy(m->id, sizeof m->id, fl[2]);
                    cm_cpy(m->name, sizeof m->name, fl[3]);
                    cm_cpy(m->ver, sizeof m->ver, fl[4]);
                    cm_cpy(m->author, sizeof m->author, fl[5]);
                    cm_cpy(m->summary, sizeof m->summary, fl[6]);
                    cm_cpy(m->cat, sizeof m->cat, fl[7]);
                    m->hooks = atoi(fl[8]);
                    cm_cpy(m->fw, sizeof m->fw, fl[9]);
                    m->size = atol(fl[10]);
                    cm_cpy(m->icon, sizeof m->icon, fl[11]);
                    cm_cpy(m->inst, sizeof m->inst, fl[12]);
                    cm_cpy(m->mc, sizeof m->mc, n >= 14 ? fl[13] : ""); /* min_core (a fetcher without it: "") */
                    cm_cpy(m->desc, sizeof m->desc, n >= 15 ? fl[14] : "");
                    m->assets_kb = n >= 16 ? atoi(fl[15]) : 0;
                    cm_cpy(m->held, sizeof m->held, n >= 17 ? fl[16] : "");
                    cm_cpy(m->also, sizeof m->also, n >= 18 ? fl[17] : "");
                    m->nalso = n >= 19 ? atoi(fl[18]) : 0;
                }
            }
            if (f) fclose(f);
        }
    } else {
        g_ncr = g_ncc = g_cr_more = g_cc_more = 0;
        cmt = 0;
        csz = 0;
    }
    if (stat(CM_STATUS, &st) == 0) {
        if (st.st_mtime != smt || st.st_size != ssz || st.st_ino != sino) {
            smt = st.st_mtime;
            ssz = st.st_size;
            sino = st.st_ino;
            memset(&g_cs, 0, sizeof g_cs);
            FILE *f = fopen(CM_STATUS, "re");
            char line[256];
            while (f && fgets(line, sizeof line, f)) {
                line[strcspn(line, "\r\n")] = 0;
                char *eq = strchr(line, '=');
                if (!eq) continue;
                *eq = 0;
                const char *v = eq + 1;
                if (!strcmp(line, "state")) cm_cpy(g_cs.state, sizeof g_cs.state, v);
                else if (!strcmp(line, "op")) cm_cpy(g_cs.op, sizeof g_cs.op, v);
                else if (!strcmp(line, "id")) cm_cpy(g_cs.id, sizeof g_cs.id, v);
                else if (!strcmp(line, "msg")) cm_cpy(g_cs.msg, sizeof g_cs.msg, v);
                else if (!strcmp(line, "done")) g_cs.done = atol(v);
                else if (!strcmp(line, "total")) g_cs.total = atol(v);
            }
            if (f) fclose(f);
            /* offer Reload only once a request has changed what loads: a refused install
             * (sha256 mismatch, no network, ...) leaves the stick as it was */
            if (!strcmp(g_cs.state, "done") && (!strcmp(g_cs.op, "install") || !strcmp(g_cs.op, "remove"))) g_cm_reload = 1;
            if (!strcmp(g_cs.state, "done") && g_cs.id[0] && (!strcmp(g_cs.op, "enable") || !strcmp(g_cs.op, "disable")))
                cm_pend_set(g_cs.id, !strcmp(g_cs.op, "enable"));
            if (!strcmp(g_cs.state, "done") && g_cs.id[0] && !strcmp(g_cs.op, "remove")) cm_removed_add(g_cs.id);
            if (!strcmp(g_cs.state, "done") && g_cs.id[0] && !strcmp(g_cs.op, "install"))
                for (int k = 0; k < 8; k++)
                    if (!strcmp(g_cm_removed[k], g_cs.id)) g_cm_removed[k][0] = 0;
            if (!strcmp(g_cs.state, "done") && !strcmp(g_cs.op, "addrepo")) g_cm_kb[0] = 0;   /* added: the draft is done */
            if (seen) g_cm_fresh = 1;
        }
    } else {
        memset(&g_cs, 0, sizeof g_cs);
        smt = 0;
        ssz = 0;
    }
    seen = 1;
}
static int cm_busy(void) { return g_cs.state[0] && strcmp(g_cs.state, "idle") && strcmp(g_cs.state, "done") && strcmp(g_cs.state, "error"); }
static void cm_req(const char *fmt, const char *a, const char *b) {   /* one request line, tmp + rename */
    char line[512];   /* the watcher reads up to 512 bytes; a request that does not fit is not sent */
    int n = snprintf(line, sizeof line, fmt, a ? a : "", b ? b : "");
    if (n < 0 || n >= (int) sizeof line) return;
    FILE *f = fopen(CM_REQ ".new", "we");
    if (!f) return;
    fprintf(f, "%s\n", line);
    fclose(f);
    rename(CM_REQ ".new", CM_REQ);
}
/* 1 = this core is new enough for the mod (no min_core, or min_core <= AZ_CORE_VERSION) */
static int cm_core_ok(const cm_cmod *m) { return azcm_min_core_ok(m->mc); }
static int cm_works(const cm_cmod *m) {   /* "any", or this deck's tag in the list */
    if (!strcmp(m->fw, "any")) return 1;
    const char *t = azcm_fwtag();
    size_t tl = strlen(t);
    if (!tl) return 0;
    for (const char *p = m->fw; *p;) {
        const char *e = strchr(p, ',');
        size_t l = e ? (size_t) (e - p) : strlen(p);
        if (l == tl && !strncmp(p, t, tl)) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}
static int cm_installed_idx(const char *id) {   /* the azcm index of an installed mod, or -1 */
    for (int i = 0; i < azcm_count(); i++) {
        azcm_info_t m;
        if (!azcm_info(i, &m) && !strcmp(m.id, id)) return i;
    }
    return -1;
}
/* The catalog entry offering a newer version (not merely a different one: a repository that went
 * back to an older release must not offer a downgrade as an update) of the installed mod `id` that
 * this deck can run, or -1. */
static int cm_update_idx(const char *id) {
    for (int k = 0; k < g_ncc; k++)
        if (!strcmp(g_cc[k].id, id) && g_cc[k].inst[0] && azcm_ver_newer(g_cc[k].ver, g_cc[k].inst))
            return cm_works(&g_cc[k]) && cm_core_ok(&g_cc[k]) ? k : -1;
    return -1;
}
/* ---- small helpers ---------------------------------------------------------------- */
static void cm_reset(void) {
    g_cm_view = CMV_HOME;
    g_cm_page = g_cm_cat = -1;
    g_cm_sel = 0;
    g_cm_scroll = 0;
    g_cm_home_sel = -1;
    g_cm_row = -1;
}
static int cm_off_now(void) { return g_cm_pending_off >= 0 ? g_cm_pending_off : azcm_all_off(); }
static const char *cm_state_text(const azcm_info_t *m, char *buf, int n, uint32_t *col) {
    switch (m->state) {
    case AZCM_LOADED:
        if (cm_removed(m->id)) {
            *col = C_AVAIL;
            return "Removed. Unloads after Reload.";
        }
        if (cm_pend_get(m->id) == 0) {
            *col = C_AVAIL;
            return "Turns off after Reload.";
        }
        *col = C_OK;
        return "Running";
    case AZCM_DISABLED:
        if (cm_removed(m->id)) {
            *col = C_AVAIL;
            return "Removed.";
        }
        if (cm_pend_get(m->id) == 1) {
            *col = C_AVAIL;
            return "Turns on after Reload.";
        }
        *col = C_TEXT_DIM;
        snprintf(buf, n, "%s", m->reason[0] ? m->reason : "Turned off");
        return buf;
    case AZCM_SUSPENDED:
        if (cm_removed(m->id)) {
            *col = C_AVAIL;
            return "Removed.";
        }
        if (cm_pend_get(m->id) == 1) {
            *col = C_AVAIL;
            return "Loads again after Reload.";
        }   /* Try again */
        if (azcm_suspended() && g_cm_keptoff) {
            *col = C_TEXT_DIM;
            return "Turned off (all community mods kept off)";
        }
        if (azcm_suspended() && g_cm_resumed) {
            *col = C_AVAIL;
            return cm_off_now() ? "Stays off: community mods are turned off" : "Loads again after Reload.";
        }
        *col = CM_BAD;
        if (azcm_suspended()) snprintf(buf, n, "%s", "Held back (see above)");
        else if (strstr(m->reason, "while this mod was starting")) snprintf(buf, n, "Turned off: the deck stopped while it was starting");
        else if (strstr(m->reason, "crashed")) snprintf(buf, n, "Turned off: it crashed the deck");
        else snprintf(buf, n, "%s", m->reason[0] ? m->reason : "Turned off after a problem");
        return buf;
    case AZCM_FAILED:
        *col = CM_BAD;
        snprintf(buf, n, "Failed: %s", m->reason);
        return buf;
    case AZCM_NEW:       /* enabled on the USB, not yet approved on this deck */
        if (cm_removed(m->id)) {
            *col = C_AVAIL;
            return "Removed.";
        }
        if (m->allowed) {
            *col = C_AVAIL;
            return "Allowed. Loads after Reload.";
        }
        *col = C_HI;
        return "New mod found: allow?";
    default:
        if (cm_removed(m->id)) {
            *col = C_AVAIL;
            return "Removed.";
        }
        if (cm_pend_get(m->id) == 0) {
            *col = C_AVAIL;
            return "Turns off after Reload.";
        }
        *col = C_AVAIL;
        snprintf(buf, n, "Not loaded: %s", m->reason);
        return buf;
    }
}
/* held back on its own after a crash (blame) or a stop while starting; the global hold is the banner */
static int cm_crashed(const azcm_info_t *m);
/* 1 = the mod is turned on, whether or not it loaded. The core reads the Turn off mark before
 * any other check, so a mod refused at load (wrong firmware, newer AZ-MODS needed) or whose
 * init() failed is still turned on: its toggle says Turn off, as the catalog page's does. */
static int cm_turned_on(const azcm_info_t *m) {
    return m->state == AZCM_LOADED || m->state == AZCM_REFUSED || m->state == AZCM_FAILED;
}
/* 1 = something on this screen changes only after a reload: an install or remove, a Turn on/off
 * that differs from what loaded, Try again, Resume, or the master switch */
static int cm_need_reload(void) {
    if (g_cm_reload || cm_off_now() != azcm_all_off() || (g_cm_resumed && azcm_suspended())) return 1;
    for (int i = 0; i < azcm_count(); i++) {
        azcm_info_t m;
        if (azcm_info(i, &m)) continue;
        if (m.state == AZCM_NEW && m.allowed && !cm_removed(m.id)) return 1;   /* allowed: loads after the reload */
        int pend = cm_pend_get(m.id);
        if (pend == 1 && !cm_turned_on(&m)) return 1;   /* turned on, or Try again */
        if (pend == 0 && cm_turned_on(&m)) return 1;    /* turned off */
    }
    return 0;
}
static int cm_crashed(const azcm_info_t *m) { return m->state == AZCM_SUSPENDED && !azcm_suspended() && !strstr(m->reason, "community mods"); }
static const char *cm_cat_name(const char *id) {   /* the catalog's name for an id the core only knows by id */
    /* only the line of the installed copy names it: a line held by another installed copy is
     * another author's mod that merely shares the id */
    for (int i = 0; i < g_ncc; i++)
        if (!strcmp(g_cc[i].id, id) && g_cc[i].name[0] && !g_cc[i].held[0]) return g_cc[i].name;
    return id;
}
static int cm_status_shown(void) { return g_cs.state[0] && (g_cm_fresh || cm_busy()); }
static void cm_box(const azmod_fb *fb, int *x, int *y, int *w, int *h) {   /* the right pane (full: the whole menu) under the header */
    if (g_cm_full) {
        *x = fb->w / 40;
        *w = fb->w - fb->w / 20;
        *y = dock_y(fb) + menu_hdrh(fb) + fb->h * 12 / 800;
        *h = dock_y(fb) + dock_h(fb) - *y - fb->h * 8 / 800;
        return;
    }
    *x = dock_x(fb) + fb->w / 80;
    *w = dock_w(fb) - fb->w / 40;
    *y = dock_y(fb) + menu_hdrh(fb) + fb->h * 10 / 800;
    *h = dock_y(fb) + dock_h(fb) - *y - fb->h * 8 / 800;
}
/* text cut to a width with "..." (the TTF draw does not clip) */
static int cm_tw(const char *s, int px);
static void cm_textw(const azmod_fb *fb, int x, int y, int w, int h, const char *s, int px, uint32_t c) {
    char t[256];
    snprintf(t, sizeof t, "%s", s ? s : "");
    if (w > 0 && cm_tw(t, px) > w) {
        size_t L = strlen(t);
        if (L > sizeof t - 4) L = sizeof t - 4;   /* room for "..." and its NUL */
        while (L > 0 && (t[L - 1] & 0xC0) == 0x80) L--;   /* never split a UTF-8 sequence */
        while (L > 0) {
            L--;
            while (L > 0 && (t[L] & 0xC0) == 0x80) L--;
            memcpy(t + L, "...", 4);
            if (cm_tw(t, px) <= w) break;
        }
    }
    c_text(fb, x, y, 0, h, t, px, c);
}
static int cm_tw(const char *s, int px) {
    int w = azmod_ttf_menu_width ? azmod_ttf_menu_width(s, (int) strlen(s), px, 2) : 0;
    return w > 0 ? w : (int) strlen(s) * px / 2;
}
/* a button: filled orange (primary), outlined (secondary) or grayed (inactive) */
static void cm_button(const azmod_fb *fb, int x, int y, int w, int h, const char *t, int primary, int active, int foc) {
    if (primary && active) fill_rect(fb, x, y, w, h, C_HI);
    else draw_rect_outline(fb, x, y, w, h, 2, active ? C_CHECK_BOX : C_STEM_OFF_FG);
    if (foc) draw_rect_outline(fb, x - 5, y - 5, w + 10, h + 10, 4, C_TEXT);
    int px = menu_text_px(fb); /* the panes' text size, so a label reads like the text beside it */
    if (px > h * 62 / 100)
        px = h * 62 / 100;
    c_text(fb, x, y, w, h, t, px, primary && active ? C_HI_TEXT : active ? C_TEXT
                                                                         : C_STEM_OFF_FG);
}
/* the mod's tile picture: its icon (decoded by EP147's JUCE on the message thread, community_icons.c)
 * once ready; nothing while its decode is queued; with no icon, or one that cannot be read, its
 * first letter on a color from its id. Not themed: an icon is the author's artwork. */
static void cm_icon(const azmod_fb *fb, int x, int y, int sz, const char *id, const char *name, const char *icon) {
    int pending = 0;
    const uint32_t *ip = icon && icon[0] ? azcm_icon(icon, sz, &pending) : NULL;
    if (pending)
        return;
    if (ip) {
        for (int j = 0; j < sz; j++) {
            int yy = y + j;
            if (yy < 0 || yy >= fb->h) continue;
            uint32_t *row = (uint32_t *) (fb->px + (long) yy * fb->stride);
            for (int i = 0; i < sz; i++) {
                int xx = x + i;
                if (xx < 0 || xx >= fb->w) continue;
                uint32_t p = ip[j * sz + i], a = p >> 24, d = row[xx], o = 0;
                for (int sh = 0; sh <= 16; sh += 8) {
                    uint32_t c = ((p >> sh) & 0xff) + ((d >> sh) & 0xff) * (255 - a) / 255;
                    o |= (c > 255 ? 255 : c) << sh;
                }
                row[xx] = o;
            }
        }
        return;
    }
    static const uint32_t pal[6] = {0x002A6FDBu, 0x00C2410Cu, 0x00198754u, 0x007E3AF2u, 0x00B02A5Bu, 0x00117A8Bu};
    unsigned h = 2166136261u;
    for (const char *p = id; *p; p++) h = (h ^ (unsigned char) *p) * 16777619u;
    fill_rect(fb, x, y, sz, sz, pal[h % 6]);
    /* clang-format off */
    char t[2] = { name[0] >= 'a' && name[0] <= 'z' ? (char)(name[0] - 32) : name[0] ? name[0] : '?', 0 };
    /* clang-format on */
    c_text(fb, x, y, sz, sz, t, sz * 55 / 100, 0x00FFFFFFu);
}
static void cm_progress(const azmod_fb *fb, int x, int y, int w, int lh, int dpx) {   /* the status line + bar */
    if (!g_cs.state[0] || (!strcmp(g_cs.state, "done") && !g_cs.msg[0])) return;
    if (!strcmp(g_cs.state, "done") && (!strcmp(g_cs.op, "enable") || !strcmp(g_cs.op, "disable")) && g_cs.id[0]) {
        /* turned back to what loaded: "turns on after Reload" would announce a change that is not there */
        int ii = cm_installed_idx(g_cs.id);
        azcm_info_t m;
        if (ii >= 0 && !azcm_info(ii, &m) && (m.state == AZCM_LOADED) == !strcmp(g_cs.op, "enable")) return;
    }
    /* clang-format off */
    uint32_t col = !strcmp(g_cs.state, "error") ? CM_BAD : !strcmp(g_cs.state, "done") ? C_OK : C_AVAIL;
    /* clang-format on */
    int dl = !strcmp(g_cs.state, "downloading"), bw = dl && g_cs.total > 0 ? w * 2 / 5 : 0;
    char t[160];
    if (dl) snprintf(t, sizeof t, "Downloading %s", g_cs.msg);
    else snprintf(t, sizeof t, "%s", g_cs.msg[0] ? g_cs.msg : g_cs.state);
    cm_textw(fb, x, y, w - bw - (bw ? lh : 0), lh, t, dpx, col);
    if (bw) {
        int bx = x + w - bw, by = y + lh / 3, bh = lh / 3;
        draw_rect_outline(fb, bx, by, bw, bh, 2, C_CHECK_BOX);   /* the track: the checkbox outline reads in Day and dark */
        fill_rect(fb, bx, by, (int) (bw * (g_cs.done > g_cs.total ? g_cs.total : g_cs.done) / g_cs.total), bh, C_HI);
    }
}
/* text word-wrapped to a width, at most maxl lines (the last one cut with "..."); returns the
 * lines used. Same answer whether drawing or not, so the layout pass and the draw agree. */
static int cm_wrap(const azmod_fb *fb, int x, int y, int w, int lh, const char *s, int px, uint32_t c, int maxl, int draw) {
    int lines = 0;
    const char *p = s ? s : "";
    while (*p && lines < maxl) {
        while (*p == ' ') p++;
        char t[256];
        int best = 0, i = 0;
        for (; p[i] && i < (int) sizeof t - 1; i++) {
            if (p[i] == ' ' || !p[i + 1]) {
                int L = p[i] == ' ' ? i : i + 1;
                memcpy(t, p, L);
                t[L] = 0;
                if (cm_tw(t, px) <= w) best = L;
                else break;
            }
        }
        if (!best || lines == maxl - 1) best = (int) strlen(p) < (int) sizeof t - 1 ? (int) strlen(p) : (int) sizeof t - 1;   /* last line / one long word: cut */
        memcpy(t, p, best);
        t[best] = 0;
        if (draw) cm_textw(fb, x, y + lines * lh, w, lh, t, px, c);
        lines++;
        p += best;
    }
    return lines ? lines : 1;
}
/* cm_wrap without the last-line cut: draws lines [skip, skip+maxl) of the wrapped text at y and
 * returns how many lines the whole text wraps to (the catalog page's scrolling description) */
static int cm_wrap_from(const azmod_fb *fb, int x, int y, int w, int lh, const char *s, int px, uint32_t c, int skip, int maxl, int draw) {
    int lines = 0;
    const char *p = s ? s : "";
    while (*p && lines < 256) {
        while (*p == ' ') p++;
        if (!*p) break;
        char t[256];
        int best = 0, i = 0;
        for (; p[i] && i < (int) sizeof t - 1; i++) {
            if (p[i] == ' ' || !p[i + 1]) {
                int L = p[i] == ' ' ? i : i + 1;
                memcpy(t, p, L);
                t[L] = 0;
                if (cm_tw(t, px) <= w) best = L;
                else break;
            }
        }
        if (!best) best = (int) strlen(p) < (int) sizeof t - 1 ? (int) strlen(p) : (int) sizeof t - 1;   /* one long word: cut */
        memcpy(t, p, best);
        t[best] = 0;
        if (draw && lines >= skip && lines < skip + maxl) cm_textw(fb, x, y + (lines - skip) * lh, w, lh, t, px, c);
        lines++;
        p += best;
    }
    return lines;
}
/* ---- the keyboard (Add repository): owner/repo or a URL -------------------------- */
/* Full screen: the geometry of EP147's own search keyboard (key 0.066 of the width at a
 * 0.072 pitch, rows 0.098 of the height with a 0.012 gap; DEL right on row 1, 123 left and
 * SPACE right on row 3), its three rows at the bottom of the full-screen area. */
#define CMK_KW 0.066f
#define CMK_PITCH 0.072f
#define CMK_RH 0.098f
#define CMK_GAP 0.012f
static void cm_kbf_rect(const azmod_fb *fb, int row, int k, int nk, int *x, int *y, int *w, int *h) {
    int bx, by, bw, bh;
    cm_box(fb, &bx, &by, &bw, &bh);
    (void) bx;
    (void) bw;
    int kh = (int) (CMK_RH * fb->h), kg = (int) (CMK_GAP * fb->h);
    int y0 = by + bh - 3 * kh - 2 * kg;
    *y = y0 + row * (kh + kg);
    *h = kh;
    if (k < 0) {   /* the specials: -1 DEL (row 0, right), -2 123/abc (row 2, left), -3 SPACE (row 2, right) */
        if (k == -1) {
            *w = (int) (CMK_KW * fb->w);
            *x = (int) (0.960f * fb->w) - *w;
        } else if (k == -2) {
            *w = (int) (CMK_KW * fb->w);
            *x = (int) (0.035f * fb->w);
        } else {
            *w = (int) (0.104f * fb->w);
            *x = (int) (0.960f * fb->w) - *w;
        }
        return;
    }
    float span = nk * CMK_PITCH - (CMK_PITCH - CMK_KW), st = (1.0f - span) / 2.0f;
    *x = (int) ((st + k * CMK_PITCH) * fb->w);
    *w = (int) (CMK_KW * fb->w);
}
static const char *CM_KB[2][4] = {{"qwertyuiop", "asdfghjkl", "zxcvbnm", ""}, {"1234567890", "-_./:@", "~+=?&#", ""}};
static void cm_kb_rect(const azmod_fb *fb, int row, int k, int nk, int *x, int *y, int *w, int *h) {
    int bx, by, bw, bh;
    cm_box(fb, &bx, &by, &bw, &bh);
    int kw = bw / 11, gap = kw / 8, ky = by + bh - 4 * (kw + gap);
    int rowx = bx + (bw - nk * kw) / 2;
    *x = rowx + k * kw + gap / 2;
    *y = ky + row * (kw + gap);
    *w = kw - gap;
    *h = kw;
}
/* ---- the layout --------------------------------------------------------------------- */
static az_rect g_cm_area;   /* the mod screen's area, set by cm_layout; w = 0: no screen shown */
/* the catalog page's description: the box cm_layout gave it (h = 0: none, or it fits), its first
 * shown line, its line height and how far it can scroll; for the drag */
static struct {
    az_rect r;
    int skip, lh, max, cat;
} g_cm_desc = {.cat = -1};
static struct {
    bool on;
    int y0, s0;
} g_cm_dd;
/* What cm_layout's parts share: where to draw and where to record the controls, the page
 * geometry, and the running y position and control count. */
struct cm_lay {
    azmod_state *s;
    const azmod_fb *fb;
    cm_ctl *ctl;
    int draw, bx, by, bw, bh, rh, lh, px, dpx, pad, bh3, y, n;
};
#define CM_CTL(k, i, X, Y, W, H) \
    do {                         \
        if (n < CM_MAXCTL) {     \
            ctl[n].kind = (k);   \
            ctl[n].idx = (i);    \
            ctl[n].x = (X);      \
            ctl[n].y = (Y);      \
            ctl[n].w = (W);      \
            ctl[n].h = (H);      \
            n++;                 \
        }                        \
    } while (0)
#define CM_FOC (g_full && s->pane && n < CM_MAXCTL && ((g_cm_knob && g_cm_sel == n) || g_cm_press == n))

/* The notice shown when every enabled mod was held back this start. */
static void cm_home_held_back(struct cm_lay *lay) {
    azmod_state *s = lay->s;
    const azmod_fb *fb = lay->fb;
    cm_ctl *ctl = lay->ctl;
    int draw = lay->draw;
    int bx = lay->bx;
    int bw = lay->bw;
    int lh = lay->lh;
    int dpx = lay->dpx;
    int pad = lay->pad;
    int bh3 = lay->bh3;
    int y = lay->y;
    int n = lay->n;
    int bh2 = 3 * lh + (g_cm_resumed ? 0 : bh3 + pad) + pad;
    if (g_cm_resumed) bh2 = lh + pad;
    if (draw) {
        fill_rect(fb, bx, y, bw, bh2, 0x00343434u);
        draw_rect_outline(fb, bx, y, bw, bh2, 2, g_cm_resumed ? C_CHECK_BOX : CM_BAD);
        int ty = y + pad / 2;
        /* clang-format off */
        c_text(fb, bx + pad, ty, 0, lh, !g_cm_resumed ? "Community mods were held back this time."
                                        : cm_off_now() ? "Community mods are turned off below, so they stay off after a reload."
                                        : "Community mods will load again after a reload.", dpx, g_cm_resumed ? C_TEXT : CM_BAD);
        /* clang-format on */
        char why[112] = "The deck restarted twice while starting with them.";
        {
            azcm_info_t m0;
            if (azcm_count() > 0 && !azcm_info(0, &m0) && !strncmp(m0.reason, "Suspended: the deck ", 20))
                snprintf(why, sizeof why, "The deck %s.", m0.reason + 20);
        }   /* the reason the core gave */
        if (!g_cm_resumed) {
            c_text(fb, bx + pad, ty + lh, 0, lh, why, dpx, C_TEXT_DIM);
            char names[200] = "Held back: ";
            int nm = azcm_count();
            for (int i = 0; i < nm; i++) {
                azcm_info_t m;
                if (azcm_info(i, &m) || m.state != AZCM_SUSPENDED) continue;
                size_t L = strlen(names);
                snprintf(names + L, sizeof names - L, "%s%s", L > 11 ? ", " : "", cm_cat_name(m.id));
            }
            cm_textw(fb, bx + pad, ty + 2 * lh, bw - 2 * pad, lh, names, dpx, C_TEXT_DIM);
        }
    }
    if (!g_cm_resumed) {
        int bw2 = fb->w * 150 / 1280, gy = y + pad / 2 + 3 * lh + pad / 2;
        for (int b = 0; b < 2; b++) {
            int qx = bx + pad + b * (bw2 + pad);
            if (draw) cm_button(fb, qx, gy, bw2, bh3, b == 0 ? "Resume" : "Keep off", b == 0, 1, CM_FOC);
            CM_CTL(b == 0 ? CMC_RESUME : CMC_KEEPOFF, 0, qx, gy, bw2, bh3);
        }
    }
    y += bh2 + pad;
    lay->y = y;
    lay->n = n;
}

/* The Installed tab: what the core loaded this start, then what was installed since. */
static void cm_home_installed(struct cm_lay *lay) {
    azmod_state *s = lay->s;
    const azmod_fb *fb = lay->fb;
    cm_ctl *ctl = lay->ctl;
    int draw = lay->draw;
    int bx = lay->bx;
    int bw = lay->bw;
    int rh = lay->rh;
    int lh = lay->lh;
    int px = lay->px;
    int dpx = lay->dpx;
    int pad = lay->pad;
    int y = lay->y;
    int n = lay->n;
    int nm = azcm_count(), shown = nm;
    for (int i = 0; i < g_ncc; i++)
        if (g_cc[i].inst[0] && cm_installed_idx(g_cc[i].id) < 0) shown++;
    if (!shown && draw) c_text(fb, bx + pad, y, 0, rh, "No community mods are installed. Find some under Browse.", dpx, C_TEXT_DIM);
    for (int i = 0; i < nm; i++) {
        azcm_info_t m;
        if (azcm_info(i, &m)) continue;
        int h = rh + lh - fb->h * 6 / 800, foc = CM_FOC;
        if (draw) {
            fill_rect(fb, bx, y, bw, h, foc ? C_FOCUS_BAND : 0x00343434u);
            if (foc) c_focus_ring(fb, bx, y, bw, h);
            char t[128];
            int ci = -1;
            for (int k = 0; k < g_ncc; k++)
                if (!strcmp(g_cc[k].id, m.id) && g_cc[k].inst[0]) {
                    ci = k;
                    break;
                }
            snprintf(t, sizeof t, "%s  %s", strcmp(m.name, m.id) || ci < 0 ? m.name : g_cc[ci].name,
                     m.version[0] || ci < 0 ? m.version : g_cc[ci].inst);
            c_text(fb, bx + pad, y + fb->h * 4 / 800, 0, rh - fb->h * 8 / 800, t, px, C_TEXT);
            char st[160];
            uint32_t sc;
            const char *stt = cm_state_text(&m, st, sizeof st, &sc);
            char line[220];
            if (m.author[0]) snprintf(line, sizeof line, "by %s   ", m.author);
            else line[0] = 0;
            int aw = line[0] ? cm_tw(line, dpx) : 0;
            c_text(fb, bx + pad, y + rh - fb->h * 10 / 800, 0, lh, line, dpx, C_TEXT_DIM);
            c_text(fb, bx + pad + aw, y + rh - fb->h * 10 / 800, 0, lh, stt, dpx, sc);
            int chh = rh * 2 / 5, chw = chh * 16 / 28;
            draw_native_chevron(fb, bx + bw - fb->w / 40 - chw, y + (h - chh) / 2, chh, C_TEXT);
            if (cm_update_idx(m.id) >= 0 && !cm_removed(m.id))
                c_text(fb, bx + bw - fb->w / 40 - chw - pad - cm_tw("Update available", dpx), y, 0, h, "Update available", dpx, C_HI);
        }
        CM_CTL(CMC_MOD, i, bx, y, bw, h);
        y += h + fb->h * 6 / 800;
    }
    for (int i = 0; i < g_ncc; i++) {   /* installed since this start: on the stick, loads after Reload */
        cm_cmod *c = &g_cc[i];
        if (!c->inst[0] || cm_installed_idx(c->id) >= 0) continue;
        int h = rh + lh - fb->h * 6 / 800, foc = CM_FOC, pend = cm_pend_get(c->id);
        if (draw) {
            fill_rect(fb, bx, y, bw, h, foc ? C_FOCUS_BAND : 0x00343434u);
            if (foc) c_focus_ring(fb, bx, y, bw, h);
            char t[128];
            snprintf(t, sizeof t, "%s  %s", c->name, c->inst);
            c_text(fb, bx + pad, y + fb->h * 4 / 800, 0, rh - fb->h * 8 / 800, t, px, C_TEXT);
            char line[220];
            snprintf(line, sizeof line, "%s%s%s", c->author[0] ? "by " : "", c->author, c->author[0] ? "   " : "");
            int aw = line[0] ? cm_tw(line, dpx) : 0;
            c_text(fb, bx + pad, y + rh - fb->h * 10 / 800, 0, lh, line, dpx, C_TEXT_DIM);
            int alloff = cm_off_now();
            /* clang-format off */
            c_text(fb, bx + pad + aw, y + rh - fb->h * 10 / 800, 0, lh,
                   alloff ? "Installed. Community mods are off." : pend == 0 ? "Installed, turned off." : "Installed. Starts after Reload.",
                   dpx, alloff ? C_TEXT_DIM : C_AVAIL);
            /* clang-format on */
            int chh = rh * 2 / 5, chw = chh * 16 / 28;
            draw_native_chevron(fb, bx + bw - fb->w / 40 - chw, y + (h - chh) / 2, chh, C_TEXT);
            if (cm_update_idx(c->id) >= 0 && !cm_removed(c->id))
                c_text(fb, bx + bw - fb->w / 40 - chw - pad - cm_tw("Update available", dpx), y, 0, h, "Update available", dpx, C_HI);
        }
        CM_CTL(CMC_INSTCAT, i, bx, y, bw, h);
        y += h + fb->h * 6 / 800;
    }
    lay->y = y;
    lay->n = n;
}

/* The Browse and Repositories tabs: the status line with Refresh, then the tile grid or the repository list. */
static void cm_home_catalog(struct cm_lay *lay) {
    azmod_state *s = lay->s;
    const azmod_fb *fb = lay->fb;
    cm_ctl *ctl = lay->ctl;
    int draw = lay->draw;
    int bx = lay->bx;
    int bw = lay->bw;
    int rh = lay->rh;
    int lh = lay->lh;
    int px = lay->px;
    int dpx = lay->dpx;
    int pad = lay->pad;
    int bh3 = lay->bh3;
    int y = lay->y;
    int n = lay->n;
    int bw2 = fb->w * 140 / 1280;
    if (draw) {
        if (cm_status_shown()) cm_progress(fb, bx + pad, y, bw - bw2 - 2 * pad, bh3, dpx);
        else c_text(fb, bx + pad, y, 0, bh3, g_ncc || g_ncr ? "" : "Nothing loaded yet: press Refresh.", dpx, C_TEXT_DIM);
        cm_button(fb, bx + bw - bw2, y, bw2, bh3, cm_busy() ? "Working..." : "Refresh", 0, !cm_busy(), CM_FOC);
    }
    CM_CTL(CMC_REFRESH, 0, bx + bw - bw2, y, bw2, bh3);
    y += bh3 + pad;
    if (g_cm_tab == 1) {   /* the grid: 3 columns of tiles, 5 on the full screen */
        int cols = g_cm_full ? 5 : 3, gap = pad, tw = (bw - (cols - 1) * gap) / cols, ic = tw * 45 / 100, th = ic + 3 * lh + pad;
        if (!g_ncc && !cm_busy() && draw) {
            /* clang-format off */
            c_text(fb, bx + pad, y, 0, lh, g_ncr ? "No mods to show from your repositories yet. Press Refresh to check again."
                                                 : "No mod repositories yet. Add one under Repositories to see its mods here.", dpx, C_TEXT_DIM);
            /* clang-format on */
        }
        if (g_cc_more > 0) {   /* the catalog holds more than the menu shows: say so */
            if (draw) {
                char t[96];
                snprintf(t, sizeof t, "Showing the first %d of %d mods.", g_ncc, g_ncc + g_cc_more);
                c_text(fb, bx + pad, y, 0, lh, t, dpx, C_AVAIL);
            }
            y += lh + pad / 2;
        }
        int order[CM_NCAT], no = 0;   /* mods that can install here first, the rest after */
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < g_ncc; i++)
                if ((cm_works(&g_cc[i]) && cm_core_ok(&g_cc[i])) == !pass) order[no++] = i;
        for (int o = 0; o < no; o++) {
            int i = order[o];
            cm_cmod *m = &g_cc[i];
            int c = o % cols, tx = bx + c * (tw + gap), ty = y + (o / cols) * (th + gap), foc = CM_FOC;
            if (draw) {
                fill_rect(fb, tx, ty, tw, th, foc ? C_FOCUS_BAND : 0x00343434u);
                if (foc) c_focus_ring(fb, tx, ty, tw, th);
                if (!cm_repo_listed(m->repo)) {   /* from a repository the user added: a corner mark (the page says why) */
                    int ce = tw / 8;
                    for (int r = 0; r < ce; r++) fill_rect(fb, tx + tw - ce + r, ty + r, ce - r, 1, C_AVAIL);
                }
                cm_icon(fb, tx + (tw - ic) / 2, ty + pad / 2, ic, m->id, m->name, m->icon);
                int yy = ty + pad / 2 + ic + pad / 3;
                cm_textw(fb, tx + pad / 2, yy, tw - pad, lh, m->name, dpx * 11 / 10, C_TEXT);
                cm_textw(fb, tx + pad / 2, yy + lh, tw - pad, lh, m->author, dpx, C_TEXT_DIM);
                const char *badge = NULL;
                uint32_t bc = 0;
                int ii = cm_installed_idx(m->id), pend = cm_pend_get(m->id);
                azcm_info_t im;
                int toff = pend == 0 || (pend < 0 && ii >= 0 && !azcm_info(ii, &im) && im.state != AZCM_LOADED);
                if (m->inst[0] && cm_removed(m->id)) {
                    badge = "Removed";
                    bc = C_TEXT_DIM;
                } else if (m->inst[0] && azcm_ver_newer(m->ver, m->inst) && cm_works(m) && cm_core_ok(m)) {
                    badge = "Update available";
                    bc = C_HI;
                } else if (m->inst[0] && azcm_ver_newer(m->ver, m->inst) && !cm_core_ok(m)) {
                    badge = "Needs newer AZ-MODS";
                    bc = C_AVAIL;
                } else if (m->inst[0] && toff) {
                    badge = "Installed, turned off";
                    bc = C_TEXT_DIM;
                } else if (m->inst[0]) {
                    badge = "Installed";
                    bc = C_OK;
                } else if (m->held[0]) {
                    badge = "Its ID is in use";
                    bc = C_AVAIL;
                } else if (!cm_works(m)) {
                    badge = "Not for this deck";
                    bc = C_AVAIL;
                } else if (!cm_core_ok(m)) {
                    badge = "Needs newer AZ-MODS";
                    bc = C_AVAIL;
                }
                if (badge) cm_textw(fb, tx + pad / 2, yy + 2 * lh, tw - pad, lh, badge, dpx, bc);
            }
            CM_CTL(CMC_TILE, i, tx, ty, tw, th);
        }
        y += ((g_ncc + cols - 1) / cols) * (th + gap);
    } else {   /* repositories: Add first so the list growing never moves it */
        int abw = fb->w * 220 / 1280;
        if (draw) cm_button(fb, bx, y, abw, bh3, "Add repository", 1, !cm_busy(), CM_FOC);
        CM_CTL(CMC_ADDREPO, 0, bx, y, abw, bh3);
        if (draw) c_text(fb, bx + abw + 2 * pad, y, 0, bh3, "A repository is a list of mods someone publishes. Add one by its address.", dpx, C_TEXT_DIM);
        y += bh3 + pad;
        for (int i = 0; i < g_ncr; i++) {
            cm_crepo *r = &g_cr[i];
            int h = rh + lh - fb->h * 6 / 800, rbw = fb->w * 120 / 1280;
            if (draw) {
                fill_rect(fb, bx, y, bw, h, 0x00343434u);
                c_text(fb, bx + pad, y + fb->h * 4 / 800, 0, rh - fb->h * 8 / 800, r->name[0] ? r->name : r->repo, px, C_TEXT);
                /* clang-format off */
                char t[240]; snprintf(t, sizeof t, "%s   %s", r->repo, r->ok ? (r->listed ? "Mod Index" : "Added by you") : r->err[0] ? r->err : "Could not be read");
                /* clang-format on */
                cm_textw(fb, bx + pad, y + rh - fb->h * 10 / 800, bw - 3 * pad - (r->listed ? 0 : rbw), lh, t, dpx, r->ok ? C_TEXT_DIM : CM_BAD);
            }
            if (!r->listed) {
                if (draw) cm_button(fb, bx + bw - pad - rbw, y + (h - bh3) / 2, rbw, bh3, "Remove", 0, !cm_busy(), CM_FOC);
                CM_CTL(CMC_DELREPO, i, bx + bw - pad - rbw, y + (h - bh3) / 2, rbw, bh3);
            }
            y += h + fb->h * 6 / 800;
        }
        if (g_cr_more > 0) {
            if (draw) {
                char t[96];
                snprintf(t, sizeof t, "Showing the first %d of %d repositories.", g_ncr, g_ncr + g_cr_more);
                c_text(fb, bx + pad, y, 0, lh, t, dpx, C_AVAIL);
            }
            y += lh;
        }
        if (draw) {   /* for mod makers: what a hooking mod's fw_tags must list */
            char t[120];
            snprintf(t, sizeof t, "For mod makers: this deck's firmware tag is %s", azcm_fwtag()[0] ? azcm_fwtag() : "unknown");
            c_text(fb, bx + pad, y + pad / 2, 0, lh, t, dpx, C_TEXT_DIM);
        }
        y += lh + pad;
    }
    lay->y = y;
    lay->n = n;
}

/* The home page: the Installed, Browse and Repositories tabs. */
static void cm_layout_home(struct cm_lay *lay) {
    azmod_state *s = lay->s;
    const azmod_fb *fb = lay->fb;
    cm_ctl *ctl = lay->ctl;
    int draw = lay->draw;
    int bx = lay->bx;
    int bw = lay->bw;
    int rh = lay->rh;
    int lh = lay->lh;
    int px = lay->px;
    int dpx = lay->dpx;
    int pad = lay->pad;
    int bh3 = lay->bh3;
    int y = lay->y;
    int n = lay->n;
    if (azcm_suspended() && !g_cm_keptoff) {
        lay->y = y;
        lay->n = n;
        cm_home_held_back(lay);
        y = lay->y;
        n = lay->n;
    }
    {   /* a mod turned off on its own after a crash: say so first */
        int nm = azcm_count(), nc = 0;
        char who[48] = "";
        for (int i = 0; i < nm; i++) {
            azcm_info_t m;
            if (!azcm_info(i, &m) && cm_crashed(&m) && !(i < 16 && g_cm_unsusp[i]) && !cm_removed(m.id)) {
                if (!nc) snprintf(who, sizeof who, "%s", m.name);
                nc++;
            }
        }
        if (nc) {
            char t[200];
            if (nc == 1) snprintf(t, sizeof t, "%s stopped the deck, so it was turned off. Your other mods were not affected.", who);
            else snprintf(t, sizeof t, "%d mods stopped the deck, so they were turned off. Your other mods were not affected.", nc);
            if (draw) { cm_textw(fb, bx + pad, y, bw - 2 * pad, lh, t, dpx, CM_BAD); }
            y += lh;
            if (draw) c_text(fb, bx + pad, y, 0, lh, "Open it under Installed to try it again or remove it.", dpx, C_TEXT_DIM);
            y += lh + fb->h * 6 / 800;
        }
    }
    {   /* the master switch (also the Community Mods row's checkbox in the Mod Menu); a change applies at the next start */
        int off = cm_off_now(), was = azcm_all_off(), stick = off && azcm_off_by_stick() && off == was, foc = CM_FOC;
        int sh = rh * 4 / 5, sz = fb->h * 20 / 800;
        if (draw) {
            fill_rect(fb, bx, y, bw, sh, foc ? C_FOCUS_BAND : 0x00343434u);
            if (foc) c_focus_ring(fb, bx, y, bw, sh);
            c_text(fb, bx + pad, y, 0, sh, "Community mods", px, C_TEXT);
            const char *st = off != was ? (off ? "Turns off after Reload" : "Turns on after Reload")
                             : stick    ? "Off on this USB: turn them on in the AZ-MODS app"
                             : off      ? "Off: none of them load"
                                        : "On";
            int stw = cm_tw(st, dpx);
            c_text(fb, bx + bw - (stick ? pad : 2 * pad + sz) - stw - (stick ? 0 : pad), y, 0, sh, st, dpx, off != was ? C_AVAIL : C_TEXT_DIM);
            if (!stick) draw_checkbox(fb, bx + bw - pad - sz, y + (sh - sz) / 2, sz, !off, foc);
        }
        if (!stick) CM_CTL(CMC_MASTER, 0, bx + bw * 55 / 100, y, bw - bw * 55 / 100, sh);   /* the switch end of the row only */
        y += sh + pad;
    }
    if (draw) c_text(fb, bx + pad, y, 0, lh, "Community mods are made by other people and are not checked by AZ-MODS.", dpx, C_TEXT_DIM);
    y += lh + fb->h * 8 / 800;
    {
        static const char *TABS[3] = {"Installed", "Browse", "Repositories"};   /* the tabs */
        int tw = (bw - 2 * pad) / 3;
        for (int t = 0; t < 3; t++) {
            int qx = bx + t * (tw + pad);
            if (draw) cm_button(fb, qx, y, tw, bh3, TABS[t], g_cm_tab == t, 1, CM_FOC);
            CM_CTL(CMC_TAB, t, qx, y, tw, bh3);
        }
        y += bh3 + pad;
    }
    if (g_cm_tab == 0) {
        lay->y = y;
        lay->n = n;
        cm_home_installed(lay);
        y = lay->y;
        n = lay->n;
    } else {
        lay->y = y;
        lay->n = n;
        cm_home_catalog(lay);
        y = lay->y;
        n = lay->n;
    }
    lay->y = y;
    lay->n = n;
}

/* An installed mod's page. */
static int cm_layout_mod(struct cm_lay *lay) {
    azmod_state *s = lay->s;
    const azmod_fb *fb = lay->fb;
    cm_ctl *ctl = lay->ctl;
    int draw = lay->draw;
    int bx = lay->bx;
    int by = lay->by;
    int bw = lay->bw;
    int bh = lay->bh;
    int lh = lay->lh;
    int dpx = lay->dpx;
    int pad = lay->pad;
    int bh3 = lay->bh3;
    int y = lay->y;
    int n = lay->n;
    azcm_info_t m;
    if (azcm_info(g_cm_page, &m)) {
        g_cm_view = CMV_HOME;
        return 0;
    }
    char t[200];
    if (draw) {
        if (m.author[0]) snprintf(t, sizeof t, "v%s  by %s", m.version, m.author);
        else snprintf(t, sizeof t, "%s", m.version[0] ? m.version : m.id);
        c_text(fb, bx + pad, y, 0, lh, t, dpx, C_TEXT_DIM);
        char st[160];
        uint32_t sc;
        const char *stt = cm_state_text(&m, st, sizeof st, &sc);
        if (!(cm_crashed(&m) && cm_pend_get(m.id) != 1)) c_text(fb, bx + pad, y + lh, 0, lh, stt, dpx, sc);
        if (m.hooks) c_text(fb, bx + pad, y + 2 * lh, 0, lh, "Changes the deck software itself (uses hooks).", dpx, C_TEXT_DIM);
    }
    y += (m.hooks ? 3 : 2) * lh;
    y += cm_wrap(fb, bx + pad, y, bw - 2 * pad, lh, m.summary, dpx, C_TEXT, g_cm_full ? 2 : 1, draw) * lh + pad / 2;
    if (m.state == AZCM_NEW && !cm_removed(m.id)) {
        /* found turned on on the USB, but this deck has not approved this exact mod file: put
         * there by the AZ-MODS app, copied by hand, or a different build of a mod approved before */
        char t[320];
        if (m.from[0]) snprintf(t, sizeof t, "Turned on on this USB, from %s, but not yet allowed on this deck. It runs with full access to your deck: allow it only if you trust where it came from.", m.from);
        else snprintf(t, sizeof t, "Turned on on this USB, copied there by hand (no record of where it came from), but not yet allowed on this deck. It runs with full access to your deck: allow it only if you trust it.");
        y += cm_wrap(fb, bx + pad, y, bw - 2 * pad, lh, t, dpx, C_TEXT, 3, draw) * lh + pad / 2;
        int bw2 = fb->w * 200 / 1280, x = bx + pad, busy = cm_busy();
        if (!m.allowed) {
            if (draw) cm_button(fb, x, y, bw2, bh3, "Allow", 1, 1, CM_FOC);
            CM_CTL(CMC_ALLOW, g_cm_page, x, y, bw2, bh3);
            x += bw2 + pad;
        }
        if (draw) cm_button(fb, x, y, bw2, bh3, "Remove from USB", 0, !busy, CM_FOC);
        CM_CTL(CMC_MODACT, CMA_REMOVE, x, y, bw2, bh3);
        y += bh3 + pad;
        if (g_cm_allow_err == g_cm_page) {
            if (draw) c_text(fb, bx + pad, y, 0, lh, "Could not save that on the deck. Try again.", dpx, CM_BAD);
            y += lh + pad / 2;
        }
        if (draw && cm_status_shown() && g_cs.id[0] && !strcmp(g_cs.id, m.id)) cm_progress(fb, bx + pad, y, bw - 2 * pad, lh, dpx);
    } else {   /* the actions, on every installed page: Try again (after a crash) or Turn on/off, and Remove */
        int bw2 = fb->w * 200 / 1280, x = bx + pad, busy = cm_busy(), gone = cm_removed(m.id);
        int crashed = cm_crashed(&m), tried = (g_cm_page < 16 && g_cm_unsusp[g_cm_page]) || cm_pend_get(m.id) == 1;
        if (crashed && draw && !tried) {
            char t[200];
            snprintf(t, sizeof t, "%s %s, so it was turned off. Your other mods were not affected.", m.name,
                     strstr(m.reason, "while this mod was starting") ? "stopped the deck while it was starting" : "crashed the deck");
            cm_wrap(fb, bx + pad, y, bw - 2 * pad, lh, t, dpx, CM_BAD, 2, 1);
        }
        if (crashed && !tried) y += 2 * lh + pad / 2;
        if (!gone) {
            int up = cm_update_idx(m.id);
            if (up >= 0) {
                char ul[40];
                snprintf(ul, sizeof ul, "Update to %s", g_cc[up].ver);
                if (draw) cm_button(fb, x, y, bw2, bh3, ul, 1, !busy, CM_FOC);
                CM_CTL(CMC_MODUPD, up, x, y, bw2, bh3);
                x += bw2 + pad;
            }
            if (crashed && !tried) {
                if (draw) cm_button(fb, x, y, bw2, bh3, "Try again", 1, 1, CM_FOC);
                CM_CTL(CMC_UNSUSPEND, g_cm_page, x, y, bw2, bh3);
                x += bw2 + pad;
            } else if (crashed) {
                /* Try again pressed: the state line above says "Loads again after Reload." */
            } else if (!azcm_suspended()) {
                int pend = cm_pend_get(m.id), on = pend >= 0 ? pend : cm_turned_on(&m);
                if (draw) cm_button(fb, x, y, bw2, bh3, on ? "Turn off" : "Turn on", 0, !busy, CM_FOC);
                CM_CTL(CMC_MODACT, on ? CMA_DISABLE : CMA_ENABLE, x, y, bw2, bh3);
                x += bw2 + pad;
            }
            if (draw) cm_button(fb, x, y, bw2, bh3, "Remove from USB", 0, !busy, CM_FOC);
            CM_CTL(CMC_MODACT, CMA_REMOVE, x, y, bw2, bh3);
        } else if (draw) c_text(fb, x, y, 0, bh3, "Removed. Unloads after Reload.", dpx, C_AVAIL);
        y += bh3 + pad;
        if (draw && cm_status_shown() && g_cs.id[0] && !strcmp(g_cs.id, m.id)) cm_progress(fb, bx + pad, y, bw - 2 * pad, lh, dpx);
        if (cm_status_shown() && g_cs.id[0] && !strcmp(g_cs.id, m.id)) y += lh + pad / 2;
    }
    if (m.state == AZCM_LOADED && m.has_screen) {   /* the mod's own screen, under its info */
        if (draw) fill_rect(fb, bx, y, bw, 1, C_DIVIDER);
        y += pad / 2;
        az_rect a = {bx, y, bw, by + bh - y};
        g_cm_area = a;
        if (draw) {
            az_draw d;
            azcm_draw_init(&d, fb, a);
            azcm_screen_draw(g_cm_page, &d, a);
        }
    } else if (m.state == AZCM_LOADED && draw) c_text(fb, bx + pad, y, 0, lh, "This mod has no settings page.", dpx, C_TEXT_DIM);
    lay->y = y;
    lay->n = n;
    return 1;
}

/* A catalog mod's page. */
static int cm_layout_cat(struct cm_lay *lay) {
    azmod_state *s = lay->s;
    const azmod_fb *fb = lay->fb;
    cm_ctl *ctl = lay->ctl;
    int draw = lay->draw;
    int bx = lay->bx;
    int by = lay->by;
    int bw = lay->bw;
    int bh = lay->bh;
    int rh = lay->rh;
    int lh = lay->lh;
    int dpx = lay->dpx;
    int pad = lay->pad;
    int bh3 = lay->bh3;
    int y = lay->y;
    int n = lay->n;
    if (g_cm_cat < 0 || g_cm_cat >= g_ncc) {
        g_cm_view = CMV_HOME;
        return 0;
    }
    cm_cmod *m = &g_cc[g_cm_cat];
    int ic = g_cm_full ? rh * 3 : rh * 2;
    int works = cm_works(m), core = cm_core_ok(m), ii = cm_installed_idx(m->id);
    if (draw) {
        cm_icon(fb, bx + pad, y, ic, m->id, m->name, m->icon);
        int tx = bx + 2 * pad + ic;
        char t[240];
        snprintf(t, sizeof t, "v%s  by %s", m->ver, m->author);
        c_text(fb, tx, y, 0, lh, t, dpx, C_TEXT);
        snprintf(t, sizeof t, "from %s", m->repo);
        c_text(fb, tx, y + lh, 0, lh, t, dpx, C_TEXT_DIM);
        if (m->inst[0]) {
            snprintf(t, sizeof t, "Installed: v%s", m->inst);
            c_text(fb, tx, y + 2 * lh, 0, lh, t, dpx, C_OK);
        }
    }
    {   /* the summary (wrapped), then the facts: one per line */
        int ty = y + ic + pad, nl = cm_wrap(fb, bx + pad, ty, bw - 2 * pad, lh, m->summary, dpx, C_TEXT, g_cm_full ? 3 : 2, draw);
        ty += nl * lh;
        char t[240];
        int k = 0;
        if (draw) {
            char wt[80];
            if (!works) snprintf(wt, sizeof wt, "Not built for this deck's firmware.");
            else if (!core) snprintf(wt, sizeof wt, "Needs AZ-MODS %s or newer (this deck has %s).", m->mc, AZ_CORE_VERSION);
            else snprintf(wt, sizeof wt, "Works on this deck.");
            c_text(fb, bx + pad, ty + k * lh, 0, lh, wt, dpx, works && core ? C_OK : CM_BAD);
        }
        k++;
        if (!cm_repo_listed(m->repo)) {   /* a repository the user added: not reviewed for the Mod Index */
            if (draw) cm_textw(fb, bx + pad, ty + k * lh, bw - 2 * pad, lh, CM_UNLISTED ". Install only what you trust.", dpx, C_AVAIL);
            k++;
        }
        if (m->held[0]) {   /* another mod already has this id on the stick: it must go first */
            if (!strcmp(m->held, "-")) snprintf(t, sizeof t, "%s", CM_HELD_HAND);
            else snprintf(t, sizeof t, CM_HELD_FROM, m->held);
            if (draw) cm_textw(fb, bx + pad, ty + k * lh, bw - 2 * pad, lh, t, dpx, CM_BAD);
            k++;
        }
        if (m->nalso > 0 && m->also[0]) {   /* others offer the same id: only one can be installed */
            if (m->nalso > 1) snprintf(t, sizeof t, "%s and %d more also offer a mod with this ID. Only one can be installed.", m->also, m->nalso - 1);
            else snprintf(t, sizeof t, "%s also offers a mod with this ID. Only one can be installed.", m->also);
            if (draw) cm_textw(fb, bx + pad, ty + k * lh, bw - 2 * pad, lh, t, dpx, C_TEXT_DIM);
            k++;
        }
        if (g_cm_full) {
            snprintf(t, sizeof t, "Firmware: %s", m->fw[0] ? m->fw : "?");
            if (draw) cm_textw(fb, bx + pad, ty + k * lh, bw - 2 * pad, lh, t, dpx, C_TEXT_DIM);
            k++;
            if (draw) c_text(fb, bx + pad, ty + k * lh, 0, lh, m->hooks ? "Changes the deck software itself (uses hooks)" : "Uses only AZ-MODS features (no hooks)", dpx, C_TEXT_DIM);
            k++;
            if (m->cat[0]) {
                snprintf(t, sizeof t, "Category: %s", m->cat);
                if (draw) c_text(fb, bx + pad, ty + k * lh, 0, lh, t, dpx, C_TEXT_DIM);
                k++;
            }
        } else if (m->hooks) {
            if (draw) c_text(fb, bx + pad, ty + k * lh, 0, lh, "Changes the deck software itself (uses hooks).", dpx, C_TEXT_DIM);
            k++;
        }
        if (m->size > 0) {
            snprintf(t, sizeof t, "Download: %ld KB", (m->size + 1023) / 1024);
            if (draw) c_text(fb, bx + pad, ty + k * lh, 0, lh, t, dpx, C_TEXT_DIM);
            k++;
        }
        if (m->assets_kb > 0) {
            if (m->assets_kb >= 1024) snprintf(t, sizeof t, "Keeps %.1f MB of files on your USB stick.", m->assets_kb / 1024.0);
            else snprintf(t, sizeof t, "Keeps %d KB of files on your USB stick.", m->assets_kb);
            if (draw) c_text(fb, bx + pad, ty + k * lh, 0, lh, t, dpx, C_TEXT_DIM);
            k++;
        }
        y = ty + k * lh + pad;
    }
    int bw2 = fb->w * 140 / 1280, x = bx + pad, busy = cm_busy();
#define CM_BTN(lbl, act, prim, act_ok)                                                 \
    do {                                                                               \
        if (draw) cm_button(fb, x, y, bw2, bh3, lbl, prim, (act_ok) && !busy, CM_FOC); \
        CM_CTL(CMC_ACT, act, x, y, bw2, bh3);                                          \
        x += bw2 + pad;                                                                \
    } while (0)
    if (!m->inst[0]) CM_BTN("Install", CMA_INSTALL, 1, works && core && !m->held[0]);
    else {
        if (azcm_ver_newer(m->ver, m->inst)) CM_BTN("Update", CMA_UPDATE, 1, works && core);
        azcm_info_t im;
        int pend = cm_pend_get(m->id);
        int off = pend >= 0 ? !pend : ii >= 0 && !azcm_info(ii, &im) && im.state == AZCM_DISABLED;
        if (!cm_removed(m->id)) {
            CM_BTN(off ? "Turn on" : "Turn off", off ? CMA_ENABLE : CMA_DISABLE, 0, 1);
            CM_BTN("Remove", CMA_REMOVE, 0, 1);
        }
    }
#undef CM_BTN
    y += bh3 + pad;
    if (draw && cm_status_shown() && g_cs.id[0] && !strcmp(g_cs.id, m->id)) cm_progress(fb, bx + pad, y, bw - 2 * pad, lh, dpx);
    y += lh + pad;
    g_cm_desc.r.h = 0;
    if (g_cm_desc.cat != g_cm_cat) {
        g_cm_desc.cat = g_cm_cat;
        g_cm_desc.skip = 0;
    }
    if (m->desc[0]) {   /* the author's longer description, in the space that is left; drag to scroll */
        int vis = (by + bh - y) / lh, tw = bw - 2 * pad;
        if (vis > 0) {
            int n = cm_wrap_from(fb, 0, 0, tw, lh, m->desc, dpx, 0, 0, 0, 0), sbw = 0;
            if (n > vis) {   /* it does not fit: leave room for the scroll bar and wrap again */
                sbw = fb->w / 160;
                tw -= sbw + pad / 2;
                n = cm_wrap_from(fb, 0, 0, tw, lh, m->desc, dpx, 0, 0, 0, 0);
            }
            int max = n > vis ? n - vis : 0;
            if (g_cm_desc.skip > max) g_cm_desc.skip = max;
            if (g_cm_desc.skip < 0) g_cm_desc.skip = 0;
            cm_wrap_from(fb, bx + pad, y, tw, lh, m->desc, dpx, C_TEXT, g_cm_desc.skip, vis, draw);
            if (max) {
                az_rect r = {bx + pad, y, bw - 2 * pad, vis * lh};
                g_cm_desc.r = r;
                g_cm_desc.lh = lh;
                g_cm_desc.max = max;
                if (draw) {   /* the description's scroll bar: track and thumb */
                    int sx = bx + bw - pad - sbw, th = r.h * vis / n, ty = r.y + (r.h - th) * g_cm_desc.skip / max;
                    fill_rect(fb, sx, r.y, sbw, r.h, C_ROW_BG);
                    fill_rect(fb, sx, ty, sbw, th, C_CHECK_BOX);
                }
            }
        }
    }
    lay->y = y;
    lay->n = n;
    return 1;
}

/* The one-time trust question. */
static void cm_layout_trust(struct cm_lay *lay) {
    azmod_state *s = lay->s;
    const azmod_fb *fb = lay->fb;
    cm_ctl *ctl = lay->ctl;
    int draw = lay->draw;
    int bx = lay->bx;
    int lh = lay->lh;
    int dpx = lay->dpx;
    int pad = lay->pad;
    int bh3 = lay->bh3;
    int y = lay->y;
    int n = lay->n;
    static const char *L[] = {"", "",
                              "Community mods are made by other people, not by AZ-MODS.",
                              "AZ-MODS does not check them. They run inside the deck software",
                              "with full access to your deck, and a faulty one can stop the music.",
                              "", "Only install mods whose source you trust. You can turn every",
                              "community mod off at any time under Community Mods."};
    char first[120];
    snprintf(first, sizeof first, "Before you install %s, your first community mod:",
             g_cm_cat >= 0 && g_cm_cat < g_ncc ? g_cc[g_cm_cat].name : "it");
    for (int i = 0; i < (int) (sizeof L / sizeof *L); i++)
        if (draw) c_text(fb, bx + pad, y + i * lh, 0, lh, i ? L[i] : first, dpx, i ? C_TEXT : C_HI);
    y += 9 * lh;
    int bw2 = fb->w * 220 / 1280;
    if (draw) cm_button(fb, bx + pad, y, bw2, bh3, "I understand, install", 1, 1, CM_FOC);
    CM_CTL(CMC_TRUST_OK, 0, bx + pad, y, bw2, bh3);
    if (draw) cm_button(fb, bx + 2 * pad + bw2, y, bw2 / 2, bh3, "Cancel", 0, 1, CM_FOC);
    CM_CTL(CMC_TRUST_NO, 0, bx + 2 * pad + bw2, y, bw2 / 2, bh3);
    lay->y = y;
    lay->n = n;
}

/* The Reload question, asked only while a track is loaded (or that cannot be told). */
static void cm_layout_reload(struct cm_lay *lay) {
    azmod_state *s = lay->s;
    const azmod_fb *fb = lay->fb;
    cm_ctl *ctl = lay->ctl;
    int draw = lay->draw;
    int bx = lay->bx;
    int lh = lay->lh;
    int dpx = lay->dpx;
    int pad = lay->pad;
    int bh3 = lay->bh3;
    int y = lay->y;
    int n = lay->n;
    if (draw) {
        c_text(fb, bx + pad, y, 0, lh, "Reload now?", dpx * 6 / 5, C_HI);
        c_text(fb, bx + pad, y + 2 * lh, 0, lh, "Reload restarts the deck software. Music stops, and loaded tracks", dpx, C_TEXT);
        c_text(fb, bx + pad, y + 3 * lh, 0, lh, "are unloaded, for about 10 seconds.", dpx, C_TEXT);
    }
    y += 5 * lh;
    int bw2 = fb->w * 200 / 1280;
    if (draw) cm_button(fb, bx + pad, y, bw2, bh3, "Not now", 0, 1, CM_FOC);
    CM_CTL(CMC_RELOAD_NO, 0, bx + pad, y, bw2, bh3);
    if (draw) cm_button(fb, bx + 2 * pad + bw2, y, bw2, bh3, "Reload", 1, 1, CM_FOC);
    CM_CTL(CMC_RELOAD_OK, 0, bx + 2 * pad + bw2, y, bw2, bh3);
    lay->y = y;
    lay->n = n;
}

/* Add repository: the field and the keyboard. Full screen: a big field and the deck's keyboard. */
static void cm_layout_kbd_full(struct cm_lay *lay) {
    azmod_state *s = lay->s;
    const azmod_fb *fb = lay->fb;
    cm_ctl *ctl = lay->ctl;
    int draw = lay->draw;
    int bx = lay->bx;
    int bw = lay->bw;
    int rh = lay->rh;
    int lh = lay->lh;
    int dpx = lay->dpx;
    int pad = lay->pad;
    int bh3 = lay->bh3;
    int y = lay->y;
    int n = lay->n;
    int fh = rh * 13 / 10, fpx = rh * 52 / 100, bw2 = fb->w * 150 / 1280;
    if (draw) {
        c_text(fb, bx + pad, y, 0, lh, "Type owner/repo, a github.com address, or an https:// address ending in azmods-repo.json:", dpx, C_TEXT_DIM);
        int fw = bw - 2 * pad - 2 * (bw2 + pad);
        fill_rect(fb, bx + pad, y + lh + pad / 2, fw, fh, 0x00343434u);
        /* the end of what was typed stays in view: cut from the left */
        char t[110];
        snprintf(t, sizeof t, "%s_", g_cm_kb);
        const char *v = t;
        while (*v && cm_tw(v, fpx) > fw - 2 * pad) v++;
        c_text(fb, bx + 2 * pad, y + lh + pad / 2, 0, fh, v, fpx, C_TEXT);
    }
    int ky = y + lh + pad / 2 + (fh - bh3) / 2, ax = bx + bw - pad - 2 * bw2 - pad;
    if (draw) cm_button(fb, ax, ky, bw2, bh3, "Add", 1, g_cm_kb[0] != 0, CM_FOC);
    CM_CTL(CMC_KB_OK, 0, ax, ky, bw2, bh3);
    if (draw) cm_button(fb, ax + bw2 + pad, ky, bw2, bh3, "Cancel", 0, 1, CM_FOC);
    CM_CTL(CMC_KB_NO, 0, ax + bw2 + pad, ky, bw2, bh3);
    {   /* whole pieces of an address, one press each */
        static const char *SC[3] = {"https://", "github.com/", "/azmods-repo.json"};
        int sy = y + lh + pad / 2 + fh + pad, sx = bx + pad;
        for (int k = 0; k < 3; k++) {
            int sw = cm_tw(SC[k], dpx) + 3 * pad;
            if (draw) cm_button(fb, sx, sy, sw, bh3, SC[k], 0, 1, CM_FOC);
            CM_CTL(CMC_KEY, 0x110 + k, sx, sy, sw, bh3);
            sx += sw + pad;
        }
    }
    for (int r = 0; r < 3; r++) {
        const char *keys = CM_KB[g_cm_kbnum][r];
        int nk = (int) strlen(keys);
        for (int k = -3; k < nk; k++) {
            if (k < 0 && !((k == -1 && r == 0) || ((k == -2 || k == -3) && r == 2))) continue;
            int x, yy, w, h;
            cm_kbf_rect(fb, r, k, nk, &x, &yy, &w, &h);
            char lab[8];
            int code;
            if (k >= 0) {
                lab[0] = keys[k];
                lab[1] = 0;
                code = (unsigned char) keys[k];
            } else if (k == -1) {
                snprintf(lab, sizeof lab, "del");
                code = 0x102;
            } else if (k == -2) {
                snprintf(lab, sizeof lab, "%s", g_cm_kbnum ? "abc" : "123");
                code = 0x100;
            } else {
                snprintf(lab, sizeof lab, "space");
                code = 0x101;
            }
            if (draw) {
                int foc = CM_FOC;
                fill_rect(fb, x, yy, w, h, foc ? C_FOCUS_BAND : 0x00343434u);
                if (foc) c_focus_ring(fb, x, yy, w, h);
                c_text(fb, x, yy, w, h, lab, k >= 0 ? h * 42 / 100 : h * 30 / 100, C_TEXT);
            }
            CM_CTL(CMC_KEY, code, x, yy, w, h);
        }
    }
    lay->y = y;
    lay->n = n;
}

/* Add repository: the field and the keyboard. */
static void cm_layout_kbd(struct cm_lay *lay) {
    azmod_state *s = lay->s;
    const azmod_fb *fb = lay->fb;
    cm_ctl *ctl = lay->ctl;
    int draw = lay->draw;
    int bx = lay->bx;
    int bw = lay->bw;
    int rh = lay->rh;
    int lh = lay->lh;
    int px = lay->px;
    int dpx = lay->dpx;
    int pad = lay->pad;
    int bh3 = lay->bh3;
    int y = lay->y;
    int n = lay->n;
    if (draw) {
        c_text(fb, bx + pad, y, 0, lh, "Type owner/repo, or a github.com address:", dpx, C_TEXT_DIM);
        fill_rect(fb, bx + pad, y + lh, bw - 2 * pad, rh, 0x00343434u);
        char t[110];
        snprintf(t, sizeof t, "%s_", g_cm_kb);
        c_text(fb, bx + 2 * pad, y + lh, 0, rh, t, px, C_TEXT);
    }
    int bw2 = fb->w * 130 / 1280, ky = y + lh + rh + pad;
    if (draw) cm_button(fb, bx + pad, ky, bw2, bh3, "Add", 1, g_cm_kb[0] != 0, CM_FOC);
    CM_CTL(CMC_KB_OK, 0, bx + pad, ky, bw2, bh3);
    if (draw) cm_button(fb, bx + 2 * pad + bw2, ky, bw2, bh3, "Cancel", 0, 1, CM_FOC);
    CM_CTL(CMC_KB_NO, 0, bx + 2 * pad + bw2, ky, bw2, bh3);
    for (int r = 0; r < 4; r++) {   /* rows 0-2 keys; row 3: 123/abc, space, del */
        const char *keys = CM_KB[g_cm_kbnum][r];
        int nk = r < 3 ? (int) strlen(keys) : 3;
        for (int k = 0; k < nk; k++) {
            int x, yy, w, h;
            cm_kb_rect(fb, r, k, r < 3 ? nk : 3, &x, &yy, &w, &h);
            if (r == 3) {
                int kw = w;
                w = kw * 3;
                x = bx + (bw - 3 * (kw * 3 + kw / 4)) / 2 + k * (kw * 3 + kw / 4);
            }
            char lab[8];
            int code;
            if (r < 3) {
                lab[0] = keys[k];
                lab[1] = 0;
                code = (unsigned char) keys[k];
            } else {
                static const char *SP[3] = {"123", "space", "del"};
                snprintf(lab, sizeof lab, "%s", k == 0 && g_cm_kbnum ? "abc" : SP[k]);
                code = 0x100 + k;
            }
            if (draw) {
                int foc = CM_FOC;
                fill_rect(fb, x, yy, w, h, foc ? C_FOCUS_BAND : 0x00343434u);
                if (foc) c_focus_ring(fb, x, yy, w, h);
                c_text(fb, x, yy, w, h, lab, h * 45 / 100, C_TEXT);
            }
            CM_CTL(CMC_KEY, code, x, yy, w, h);
        }
    }
    lay->y = y;
    lay->n = n;
}

/* The full-screen header band: the back square, the section's icon, the page title and the Reload note. */
static void cm_layout_header(struct cm_lay *lay) {
    azmod_state *s = lay->s;
    const azmod_fb *fb = lay->fb;
    cm_ctl *ctl = lay->ctl;
    int draw = lay->draw;
    int dpx = lay->dpx;
    int pad = lay->pad;
    int bh3 = lay->bh3;
    int n = lay->n;
    int top = dock_y(fb), hh = menu_hdrh(fb), side = hh * 5 / 8, qx = fb->w / 40, qy = top + (hh - side) / 2;
    if (draw) {
        fill_rect(fb, 0, top, fb->w, hh, C_HEADER_BG);
        draw_rect_outline(fb, qx, qy, side, side, 2, CM_FOC ? C_HI : C_CHECK_BOX);
        int asz = side * 3 / 5;
        blit_icon(fb, qx + (side - asz) / 2, qy + (side - asz) / 2, asz, C_TEXT, IC_BACK);
        int isz = hh * 45 / 100, ix = qx + side + fb->w / 50;
        draw_item_icon(fb, ix, top + (hh - isz) / 2, isz, AZI_COMMUNITY, C_TEXT);
        const char *title = "Community Mods";
        azcm_info_t m;
        if (g_cm_view == CMV_MOD && !azcm_info(g_cm_page, &m)) title = m.name;
        else if (g_cm_view == CMV_CAT && g_cm_cat >= 0 && g_cm_cat < g_ncc) title = g_cc[g_cm_cat].name;
        else if (g_cm_view == CMV_KBD) title = "Add repository";
        else if (g_cm_view == CMV_RELOAD) title = "Reload";
        char t[120];
        if (g_cm_view == CMV_HOME || g_cm_view == CMV_TRUST) snprintf(t, sizeof t, "%s", title);
        else snprintf(t, sizeof t, "Community Mods  >  %s", title);
        int tx = ix + isz + fb->w / 60, tright = fb->w - fb->w / 40;
        if (cm_need_reload() && !cm_busy() && g_cm_view != CMV_RELOAD)   /* leave room for the Reload note + button */
            tright -= fb->w * 180 / 1280 + 2 * pad + cm_tw("Changes take effect after a reload", dpx);
        cm_textw(fb, tx, top, tright - tx - pad, hh, t, hh * 32 / 100, C_TEXT);
        fill_rect(fb, 0, top + hh - 2, fb->w, 2, C_DIVIDER);
    }
    if (cm_need_reload() && !cm_busy() && g_cm_view != CMV_RELOAD) {   /* something changed what loads: one fixed place */
        int rbw = fb->w * 180 / 1280, rx = fb->w - fb->w / 40 - rbw, ry = top + (hh - bh3) / 2;
        if (draw) {
            const char *a = "Changes take effect after a reload";
            c_text(fb, rx - pad - cm_tw(a, dpx), top, 0, hh, a, dpx, C_AVAIL);
            cm_button(fb, rx, ry, rbw, bh3, "Reload now", 1, 1, CM_FOC);
        }
        CM_CTL(CMC_APPLY, 0, rx, ry, rbw, bh3);
    }
    CM_CTL(CMC_BACK, 0, qx, qy, side, side);
    lay->n = n;
}

static int cm_layout(azmod_state *s, const azmod_fb *fb, cm_ctl *ctl, int draw) {
    int bx, by, bw, bh;
    cm_box(fb, &bx, &by, &bw, &bh);
    int rh = c_rrowh(fb), lh = fb->h * 26 / 800, px = rh * 44 / 100, dpx = fb->h * 17 / 800, pad = fb->w / 80;
    int y = by - (g_cm_view == CMV_HOME ? g_cm_scroll : 0), n = 0, bh3 = rh * 3 / 5;
    struct cm_lay lay = {s, fb, ctl, draw, bx, by, bw, bh, rh, lh, px, dpx, pad, bh3, y, n};
    if (g_cm_view == CMV_HOME) {
        cm_layout_home(&lay);
    } else if (g_cm_view == CMV_MOD) {
        if (!cm_layout_mod(&lay))
            return 0;
    } else if (g_cm_view == CMV_CAT) {
        if (!cm_layout_cat(&lay))
            return 0;
    } else if (g_cm_view == CMV_TRUST) {
        cm_layout_trust(&lay);
    } else if (g_cm_view == CMV_RELOAD) {
        cm_layout_reload(&lay);
    } else if (g_cm_view == CMV_KBD && g_cm_full) {
        cm_layout_kbd_full(&lay);
    } else if (g_cm_view == CMV_KBD) {
        cm_layout_kbd(&lay);
    }
    if (g_cm_full)
        cm_layout_header(&lay);
    return lay.n;
}
#undef CM_CTL
#undef CM_FOC
/* MOD MENU > Community Mods selected but not open: a summary card in the right pane */
static void cm_draw_card(azmod_state *s, const azmod_fb *fb) {
    cm_load();
    int x0 = dock_x(fb), top = dock_y(fb), hh = menu_hdrh(fb), w0 = dock_w(fb), pad = fb->w / 40;
    int lh = fb->h * 26 / 800, dpx = fb->h * 17 / 800, rh = c_rrowh(fb), px = rh * 44 / 100;
    fill_rect(fb, x0, top, w0, dock_h(fb), C_MENU_SCRIM);
    fill_rect(fb, x0, top, w0, hh, C_HEADER_BG);
    c_text(fb, x0 + pad, top, 0, hh, "Community Mods", hh * 34 / 100, C_TEXT);
    fill_rect(fb, x0, top + hh - 2, w0, 2, C_DIVIDER);
    int y = top + hh + fb->h * 14 / 800;
    c_text(fb, x0 + pad, y, 0, lh, "Mods made by other people: browse, install and manage them.", dpx, C_TEXT_DIM);
    c_text(fb, x0 + pad, y + lh, 0, lh, "They are not checked by AZ-MODS and run with full access to your deck.", dpx, C_TEXT_DIM);
    y += 2 * lh + fb->h * 14 / 800;
    int off = cm_off_now(), was = azcm_all_off(), nm = azcm_count(), live = 0;
    for (int i = 0; i < nm; i++) {
        azcm_info_t m;
        if (!azcm_info(i, &m) && m.state == AZCM_LOADED) live++;
    }
    c_text(fb, x0 + pad, y, 0, rh, off ? "Community mods are off." : "Community mods are on.", px, C_TEXT);
    if (off != was) {
        const char *a = "Changes after Reload";
        c_text(fb, x0 + w0 - pad - cm_tw(a, dpx), y, 0, rh, a, dpx, C_AVAIL);
    }
    y += rh + fb->h * 10 / 800;
    int inst = nm, bad = 0;
    for (int i = 0; i < g_ncc; i++)
        if (g_cc[i].inst[0] && cm_installed_idx(g_cc[i].id) < 0) inst++;
    for (int i = 0; i < nm; i++) {
        azcm_info_t m;
        if (!azcm_info(i, &m) && cm_crashed(&m)) bad++;
    }
    char t[96];
    snprintf(t, sizeof t, "%d installed, %d running", inst, live);
    c_text(fb, x0 + pad, y, 0, lh, t, dpx, C_TEXT);
    y += lh;
    if (bad) {
        snprintf(t, sizeof t, "%d turned off after a problem", bad);
        c_text(fb, x0 + pad, y, 0, lh, t, dpx, CM_BAD);
        y += lh;
    }
    {
        int nn = azcm_new_count();
        if (nn) {
            snprintf(t, sizeof t, nn == 1 ? "1 new mod found: open Community Mods to allow it." : "%d new mods found: open Community Mods to allow them.", nn);
            c_text(fb, x0 + pad, y, 0, lh, t, dpx, C_HI);
            y += lh;
        }
    }
    if (azcm_suspended() && !g_cm_resumed) {
        c_text(fb, x0 + pad, y, 0, lh, "Held back this time: open Community Mods.", dpx, CM_BAD);
        y += lh;
    }
    if (cm_need_reload()) {
        c_text(fb, x0 + pad, y, 0, lh, "Changes take effect after a reload.", dpx, C_AVAIL);
        y += lh;
    }
    y += fb->h * 14 / 800;
    c_text(fb, x0 + pad, y, 0, lh, s->pane ? "Press the knob to open." : "Tap Community Mods, or press the knob, to open.", dpx, C_TEXT_DIM);
}
static void cm_draw(azmod_state *s, const azmod_fb *fb) {
    cm_load();
    if (!g_cm_full) {
        cm_draw_card(s, fb);
        return;
    }
    /* the whole menu area: the content, then its header (drawn last by the layout) */
    fill_rect(fb, 0, dock_y(fb), fb->w, dock_h(fb), C_MENU_SCRIM);
    cm_ctl ctl[CM_MAXCTL];
    int n = cm_layout(s, fb, ctl, 0);
    if (g_cm_sel >= n) g_cm_sel = n ? n - 1 : 0;
    cm_layout(s, fb, ctl, 1);
}
/* ---- the slide: in from the right on open, out to the right on BACK ------
 * ~220 ms, ease-out, one step per overlay tick (azmod_tick asks for a redraw while it runs;
 * no sleep, no loop). The full view is rendered once into an offscreen copy when the slide
 * starts, so a frame costs one pass of the Mod Menu under it plus a row copy. A frame that
 * comes too late or draws too slowly ends the slide at once (the end state, never a stall).
 * Input is held only while it runs. settings/azmod-logs.on: each slide's frame times go to
 * /tmp/azmod-cmslide.log. */
#define CM_SLIDE_MS 220
#define CM_SLIDE_LATE 90      /* ms between two slide frames that ends it */
#define CM_SLIDE_SLOW 40      /* ms one slide frame may take to draw */
static long g_cm_t0, g_cm_last;
static int g_cm_dir;      /* +1 opening, -1 closing, 0 idle */
static uint8_t *g_cm_off;
static int g_cm_offsz;           /* the full view, rendered once */
static int g_cm_nfr, g_cm_cut;
static short g_cm_fr[32][2]; /* frame: ms since start, draw ms */
static bool cm_animating(void) { return g_cm_dir != 0; }
static void g_cm_dir_reset(void) { g_cm_dir = 0; }
static void cm_slide_start(azmod_state *s, int dir) {
    g_cm_dir = dir;
    g_cm_t0 = g_cm_last = cm_ms();
    g_cm_nfr = 0;
    g_cm_cut = 0;
    (void) s;
}
static void cm_full_open(azmod_state *s) {
    s->left_sel = AZI_COMMUNITY;
    s->mode = AZ_COMMUNITY;
    s->pane = 1;
    if (g_cm_full && !g_cm_dir) return;
    if (!g_cm_full) cm_reset();
    g_cm_full = 1;
    cm_slide_start(s, 1);
}
static void cm_full_close(azmod_state *s) {
    if (g_cm_full) cm_slide_start(s, -1);
}
static void cm_slide_end(azmod_state *s) {
    if (access("/home/root/settings/azmod-logs.on", F_OK) == 0) {
        FILE *f = azlog_fopen_capped("/tmp/azmod-cmslide.log", AZLOG_CAP_BYTES);
        if (f) {
            fprintf(f, "%s %d frames%s:", g_cm_dir > 0 ? "open" : "close", g_cm_nfr, g_cm_cut ? " (cut short: a late or slow frame)" : "");
            for (int i = 0; i < g_cm_nfr && i < 32; i++) fprintf(f, " %d/%dms", g_cm_fr[i][0], g_cm_fr[i][1]);
            fprintf(f, "\n");
            fclose(f);
        }
    }
    if (g_cm_dir < 0 && g_cm_row >= 0) {   /* back on the Mod Menu, on the SCREEN row that opened the page */
        g_cm_full = 0;
        c_select(s, s->left_sel);
        s->pane = g_cm_row_pane;
        s->menu_sel = g_cm_row_sel;
        g_cm_row = -1;
    } else if (g_cm_dir < 0) {
        g_cm_full = 0;
        s->pane = 0;
        s->mode = AZ_COMMUNITY;
    }   /* back on the Mod Menu, Community Mods row */
    g_cm_dir = 0;
}
/* one slide frame: the Mod Menu (left list + the card), then the full view shifted right by off */
static void cm_slide_draw(azmod_state *s, const azmod_fb *fb) {
    long now = cm_ms(), el = now - g_cm_t0;
    if (now - g_cm_last > CM_SLIDE_LATE && g_cm_nfr) g_cm_cut = 1;
    int need = fb->stride * fb->h;
    if (!g_cm_off || g_cm_offsz != need) {
        free(g_cm_off);
        g_cm_off = malloc(need);
        g_cm_offsz = g_cm_off ? need : 0;
        g_cm_nfr = 0;
    }
    if (!g_cm_off) g_cm_cut = 1;
    if (!g_cm_cut && g_cm_nfr == 0) {   /* render the full view once */
        azmod_fb o = {g_cm_off, fb->w, fb->h, fb->stride};
        cm_draw(s, &o);
    }
    float t = el >= CM_SLIDE_MS || g_cm_cut ? 1.f : (float) el / CM_SLIDE_MS, e = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
    float shown = g_cm_dir > 0 ? e : 1.f - e;           /* share of the full view on screen */
    int off = (int) ((1.f - shown) * fb->w);
    if (off > 0) {
        g_cm_full = 0;
        c_draw_left(s, fb);
        if (g_cm_row >= 0) c_draw_right(s, fb);
        else cm_draw_card(s, fb);
        g_cm_full = 1;
    }
    if (off < fb->w && g_cm_off)
        for (int y = dock_y(fb); y < dock_y(fb) + dock_h(fb) && y < fb->h; y++)
            memcpy(fb->px + (long) y * fb->stride + (long) off * 4, g_cm_off + (long) y * fb->stride, (size_t) (fb->w - off) * 4);
    long dt = cm_ms() - now;
    if (dt > CM_SLIDE_SLOW) g_cm_cut = 1;
    if (g_cm_nfr < 32) {
        g_cm_fr[g_cm_nfr][0] = (short) el;
        g_cm_fr[g_cm_nfr][1] = (short) dt;
    }
    g_cm_nfr++;
    g_cm_last = now;
    if (t >= 1.f) cm_slide_end(s);
}
/* the page's mod screen takes the input while it is open (when it has one) */
static int cm_page_screen(void) {
    azcm_info_t m;
    return g_cm_view == CMV_MOD && !azcm_info(g_cm_page, &m) && m.state == AZCM_LOADED && m.has_screen;
}
static void cm_open(int view) {
    if (view != CMV_MOD) azcm_page_screen(-1, NULL);   /* a row's own screen lasts for its page only */
    if (g_cm_view == CMV_HOME && view != CMV_HOME) {
        g_cm_home_sel = g_cm_sel;
        g_cm_home_scroll = g_cm_scroll;
    }
    if (view == CMV_HOME && g_cm_home_sel >= 0) {
        g_cm_sel = g_cm_home_sel;
        g_cm_scroll = g_cm_home_scroll;
        g_cm_home_sel = -1;
    } else g_cm_sel = 0;   /* the first control of a page is its main action, never the back square */
    g_cm_view = view;
}
static void cm_master_toggle(void) {
    int off = !cm_off_now();
    azcm_set_all_off(off);
    g_cm_pending_off = off;
}
static azmod_state *g_cm_s;   /* for CMC_BACK (cm_fire has no state); set by cm_input */
static void cm_fire(const cm_ctl *c) {
    /* clang-format off */
    switch (c->kind) {
    case CMC_RESUME:  azcm_resume(); g_cm_resumed = 1; g_cm_sel = 0; break;
    case CMC_KEEPOFF: azcm_set_all_off(1); g_cm_pending_off = 1; g_cm_keptoff = 1; g_cm_sel = 0; break;
    case CMC_MASTER:  cm_master_toggle(); break;
    case CMC_BACK:    if (g_cm_s && !cm_back()) cm_full_close(g_cm_s); break;
    case CMC_MOD:     if (!g_cm_row_opening) azcm_page_screen(-1, NULL);   /* from the list: the mod's own page */
                      g_cm_page = c->idx; cm_open(CMV_MOD); if (cm_page_screen()) azcm_screen_enter(g_cm_page); break;
    case CMC_UNSUSPEND: { azcm_info_t m; if (!azcm_info(c->idx, &m)) { azcm_unsuspend(m.id); cm_pend_set(m.id, 1); }
                          if (c->idx < 16) g_cm_unsusp[c->idx] = 1;
                          g_cm_sel = 0; } break;
    case CMC_TAB:     g_cm_tab = c->idx; g_cm_scroll = 0; if (c->idx && !g_ncc && !g_ncr && !cm_busy()) cm_req("refresh", 0, 0); break;
    case CMC_TILE:    g_cm_cat = c->idx; cm_open(CMV_CAT); break;
    case CMC_REFRESH: if (!cm_busy()) cm_req("refresh", 0, 0); break;
    case CMC_ADDREPO: if (!cm_busy()) { g_cm_kbnum = 0; cm_open(CMV_KBD); } break;   /* the draft is kept until a repository is added */
    case CMC_DELREPO:
        if (!cm_busy() && c->idx < g_ncr)
            cm_ask("Remove repository?", g_cr[c->idx].name[0] ? g_cr[c->idx].name : g_cr[c->idx].repo,
                   "delrepo %s", g_cr[c->idx].repo);
        break;
    case CMC_APPLY:   if (azcm_any_loaded()) { g_cm_reload_from = g_cm_view; cm_open(CMV_RELOAD); g_cm_sel = 0; break; }
                      /* fall through - nothing is playing, reload now */
    case CMC_RELOAD_OK:
        azcm_reload_requested(); cm_req("apply", 0, 0); g_cm_reload = 0; g_cm_resumed = 0;
        if (g_cm_view == CMV_RELOAD) cm_open(g_cm_reload_from);
        break;
    case CMC_RELOAD_NO: cm_open(g_cm_reload_from); break;
    case CMC_ALLOW:   g_cm_allow_err = azcm_allow(c->idx) == 0 ? -1 : c->idx; g_cm_sel = 0; break;
    case CMC_INSTCAT: g_cm_cat = c->idx; cm_open(CMV_CAT); break;
    case CMC_MODUPD: {   /* Update on an installed mod's page: the same install the catalog page sends */
        azcm_info_t m; int k;   /* looked up again: the catalog may have been re-read since the draw */
        if (cm_busy() || azcm_info(g_cm_page, &m) || (k = cm_update_idx(m.id)) < 0) break;
        cm_req("install %s %s", g_cc[k].repo, g_cc[k].id);
    } break;
    case CMC_MODACT: {
        azcm_info_t m; if (cm_busy() || azcm_info(g_cm_page, &m)) break;
        if (c->idx == CMA_REMOVE) { char t[96]; snprintf(t, sizeof t, "%s is deleted from this USB.", m.name[0] ? m.name : m.id);
                                    cm_ask("Remove mod?", t, "remove %s", m.id); }
        else cm_req(c->idx == CMA_ENABLE ? "enable %s" : "disable %s", m.id, 0);
    } break;
    case CMC_ACT: {
        if (cm_busy() || g_cm_cat < 0 || g_cm_cat >= g_ncc) break;
        cm_cmod *m = &g_cc[g_cm_cat];
        if ((c->idx == CMA_INSTALL || c->idx == CMA_UPDATE) && !(cm_works(m) && cm_core_ok(m))) break;
        if (c->idx == CMA_INSTALL && m->held[0]) break;   /* another mod holds the id: the page says so */
        if (c->idx == CMA_INSTALL && access(CM_TRUSTED, F_OK) != 0) { cm_open(CMV_TRUST); g_cm_sel = 1; break; }   /* focus on Cancel */
        if (c->idx == CMA_INSTALL || c->idx == CMA_UPDATE) cm_req("install %s %s", m->repo, m->id);
        else if (c->idx == CMA_REMOVE)  { char t[96]; snprintf(t, sizeof t, "%s is deleted from this USB.", m->name[0] ? m->name : m->id);
                                          cm_ask("Remove mod?", t, "remove %s", m->id); }
        else if (c->idx == CMA_ENABLE)  cm_req("enable %s", m->id, 0);
        else if (c->idx == CMA_DISABLE) cm_req("disable %s", m->id, 0);
    } break;
    case CMC_TRUST_OK: {
        mkdir("/home/root/settings/azmod-community", 0755);
        FILE *f = fopen(CM_TRUSTED, "we"); if (f) fclose(f);
        if (g_cm_cat >= 0 && g_cm_cat < g_ncc) { cm_req("install %s %s", g_cc[g_cm_cat].repo, g_cc[g_cm_cat].id); }
        cm_open(CMV_CAT);
    } break;
    case CMC_TRUST_NO: cm_open(CMV_CAT); break;
    case CMC_KEY: {
        size_t L = strlen(g_cm_kb);
        if (c->idx == 0x100) g_cm_kbnum = !g_cm_kbnum;
        else if (c->idx == 0x101) { if (L + 1 < sizeof g_cm_kb && L) { g_cm_kb[L] = ' '; g_cm_kb[L + 1] = 0; } }
        else if (c->idx == 0x102) { if (L) g_cm_kb[L - 1] = 0; }
        else if (c->idx >= 0x110 && c->idx <= 0x112) {
            static const char *SC[3] = { "https://", "github.com/", "/azmods-repo.json" };
            snprintf(g_cm_kb + L, sizeof g_cm_kb - L, "%s", SC[c->idx - 0x110]);
        }
        else if (L + 1 < sizeof g_cm_kb) { g_cm_kb[L] = (char)c->idx; g_cm_kb[L + 1] = 0; }
    } break;
    case CMC_KB_OK:   if (g_cm_kb[0]) { cm_req("addrepo %s", g_cm_kb, 0); g_cm_tab = 2; cm_open(CMV_HOME); } break;
    case CMC_KB_NO:   cm_open(CMV_HOME); break;
    }
    /* clang-format on */
}
/* A mod's SCREEN row in the Mod Menu's main list (register_row, AZ_ROW_SCREEN) opens that mod's
 * page through the same path as Community Mods > Installed > <mod>: the full screen
 * (cm_full_open) and cm_fire's CMC_MOD, so the page is the mod's registered screen with the same
 * header, input and BACK handling. BACK from it returns to the row (cm_back, cm_slide_end), not to
 * the Mod Index. false: not a registered SCREEN row, or its mod is not installed (the caller
 * then handles the row like any other). */
static bool cm_open_row(azmod_state *s, int it) {
    if (it < AZI_REG0 || it >= AZI_ACT0 || az_kind(it) != AZ_ROW_SCREEN)
        return false;
    int idx = cm_installed_idx(azf_owner(it));
    if (idx < 0)
        return false;
    const struct az_row *r = az_row(it);
    const az_screen_v1 *own = r ? r->d.screen : NULL;
    if (!azcm_row_target(azf_owner(it), own))
        return true;           /* nothing to open: the row is grayed and does nothing */
    int left = s->left_sel, pane = s->pane, sel = s->menu_sel;
    cm_full_open(s);           /* resets the Community view (and g_cm_row) when it opens */
    s->left_sel = left;        /* the left list keeps the row lit, under the page and in the slide */
    g_cm_row = it;
    g_cm_row_pane = pane;
    g_cm_row_sel = sel;
    azcm_page_screen(idx, own); /* the row's own screen; NULL = the mod's page */
    g_cm_row_opening = 1;
    cm_ctl c = {CMC_MOD, idx, 0, 0, 0, 0};
    cm_fire(&c);
    g_cm_row_opening = 0;
    return true;
}
static bool cm_back(void) {
    if (g_cm_view == CMV_HOME) return false;
    if (cm_page_screen()) {
        az_event ev = {AZ_EV_BACK, 0, 0, 0};
        if (azcm_screen_input(g_cm_page, &ev)) return true;
        azcm_flush_settings();
    }   /* leaving a mod's page: its settings are saved now */
    if (g_cm_view == CMV_TRUST) cm_open(CMV_CAT);
    else if (g_cm_view == CMV_RELOAD) cm_open(g_cm_reload_from);
    else if (g_cm_row >= 0 && g_cm_view == CMV_MOD) return false;   /* opened from its row: out to the row */
    else cm_open(CMV_HOME);
    return true;
}
static bool cm_input(azmod_state *s, const azmod_fb *fb, int tx, int ty, bool touched, int d, bool click) {
    cm_load();
    g_cm_s = s;
    if (cm_animating()) return true;                   /* held only while the slide runs (~220 ms) */
    if (!g_cm_full) {                                   /* the card: a tap or a press opens; a turn goes back to the list */
        if (touched || click) cm_full_open(s);
        else if (d) s->pane = 0;
        return true;
    }
    g_cm_area.w = 0;
    cm_ctl ctl[CM_MAXCTL];
    int n = cm_layout(s, fb, ctl, 0);
    if (cm_page_screen()) {   /* controls above the screen first, then the mod */
        az_event ev = {0, 0, 0, 0};
        if (touched) {
            for (int k = 0; k < n; k++)
                if (in_rect(tx, ty, ctl[k].x, ctl[k].y, ctl[k].w, ctl[k].h)) {
                    cm_fire(&ctl[k]);
                    return true;
                }
            /* the mod hears only taps inside the area it paints: not the header, its info or the buttons' gaps */
            if (g_cm_area.w > 0 && in_rect(tx, ty, g_cm_area.x, g_cm_area.y, g_cm_area.w, g_cm_area.h)) {
                ev.kind = AZ_EV_TAP;
                ev.x = tx;
                ev.y = ty;
            }
        } else if (d) {
            ev.kind = AZ_EV_KNOB;
            ev.dir = d > 0 ? 1 : -1;
        } else if (click) ev.kind = AZ_EV_CLICK;
        if (ev.kind) azcm_screen_input(g_cm_page, &ev);
        return true;
    }
    if (d && n && !g_cm_knob) { /* after a touch, the first detent shows the focus where it is */
        g_cm_knob = 1;
        d = 0;
    }
    if (click && n)
        g_cm_knob = 1;
    if (d && n && g_cm_view == CMV_CAT && g_cm_desc.r.h > 0) {   /* past the last button the knob scrolls the description */
        int nk = n;
        if (nk > 1 && ctl[nk - 1].kind == CMC_BACK) nk--;
        if (g_cm_sel == nk - 1 && (d > 0 || g_cm_desc.skip > 0)) {
            int ns = g_cm_desc.skip + d * 2;
            g_cm_desc.skip = ns < 0 ? 0 : ns > g_cm_desc.max ? g_cm_desc.max
                                                             : ns;
            return true;
        }
    }
    if (d && n) {
        int nk = n;   /* the back square is the last control and the knob skips it (the BACK button goes back) */
        if (nk > 1 && ctl[nk - 1].kind == CMC_BACK) nk--;
        g_cm_sel = ((g_cm_sel + d) % nk + nk) % nk;   /* every detent counts (a fast turn arrives as one delta) */
        if (g_cm_view == CMV_HOME) {   /* scroll only as far as needed to show the focused control */
            int bx, by, bw, bh;
            cm_box(fb, &bx, &by, &bw, &bh);
            (void) bx;
            (void) bw;
            const cm_ctl *c = &ctl[g_cm_sel];
            if (g_cm_sel == 0) g_cm_scroll = 0;
            else if (c->y + c->h > by + bh) g_cm_scroll += c->y + c->h - (by + bh);
            else if (c->y < by) g_cm_scroll -= by - c->y;
            if (g_cm_scroll < 0) g_cm_scroll = 0;
        }
    }
    if (click && n && g_cm_sel < n) cm_fire(&ctl[g_cm_sel]);
    if (touched && g_cm_view == CMV_CAT && g_cm_desc.r.h > 0 &&
        in_rect(tx, ty, g_cm_desc.r.x, g_cm_desc.r.y, g_cm_desc.r.w, g_cm_desc.r.h)) {   /* a drag starts here */
        g_cm_dd.on = true;
        g_cm_dd.y0 = ty;
        g_cm_dd.s0 = g_cm_desc.skip;
        return true;
    }
    if (touched) {
        int pad = g_cm_view == CMV_KBD ? 0 : fb->h * 8 / 800;
        for (int k = 0; k < n; k++)
            if (in_rect(tx, ty, ctl[k].x - pad, ctl[k].y - pad, ctl[k].w + 2 * pad, ctl[k].h + 2 * pad)) {
                int v = g_cm_view, pg = g_cm_page, tab = g_cm_tab;
                g_cm_sel = k;
                g_cm_knob = 0;
                g_cm_press = k;   /* lit until the finger lifts (azmod_pane_drag, phase 2) */
                cm_fire(&ctl[k]);
                if (g_cm_view != v || g_cm_page != pg || g_cm_tab != tab)
                    g_cm_press = -1; /* a new page: index k is some other control there */
                break;
            }
    }
    return true;
}
/* Community > a catalog mod's page: the description follows the finger in whole lines. Same
 * contract as azmod_changelog_drag (cm_input starts it; 1 = a step, 2 = the release). */
bool azmod_cm_drag(azmod_state *s, const azmod_fb *fb, int y, int phase) {
    (void) fb;
    if (!g_cm_dd.on) return false;
    if (s->mode != AZ_COMMUNITY || g_cm_view != CMV_CAT || g_cm_desc.r.h <= 0 || g_cm_desc.lh <= 0) {
        g_cm_dd.on = false;
        return false;
    }
    if (phase == 2) {
        g_cm_dd.on = false;
        return false;
    }
    int ns = g_cm_dd.s0 + (g_cm_dd.y0 - y) / g_cm_desc.lh;
    if (ns < 0) ns = 0;
    if (ns > g_cm_desc.max) ns = g_cm_desc.max;
    if (ns == g_cm_desc.skip) return false;
    g_cm_desc.skip = ns;
    return true;
}
/* while Community is open: redraw twice a second so the fetcher's status and catalog show up,
 * and at once when an icon finishes, so a tile's picture appears as soon as it is decoded */
static bool cm_tick(azmod_state *s) {
    static int t;
    static unsigned icons;
    if (s->mode != AZ_COMMUNITY) {
        g_cm_dir = 0;
        return false;
    }
    if (g_cm_dir) return true;                          /* the slide: a frame every tick */
    unsigned n = azcm_icons_done();
    if (n != icons) {
        icons = n;
        return true;
    }
    return ++t % 15 == 0;
}

/* ---- community mods: the draw helpers a mod's page paints through (azmod-mod.h) --------
 * A mod draws in absolute device pixels; the context is a framebuffer view whose origin is
 * the mod's area, so every write clips to the area for free (as the changelog box does) and
 * a mod can never paint outside the rectangle the core handed it. */
void azcm_draw_init(az_draw *d, const azmod_fb *fb, az_rect a) {
    if (a.x < 0) {
        a.w += a.x;
        a.x = 0;
    }
    if (a.y < 0) {
        a.h += a.y;
        a.y = 0;
    }
    if (a.x + a.w > fb->w) a.w = fb->w - a.x;
    if (a.y + a.h > fb->h) a.h = fb->h - a.y;
    if (a.w < 0) a.w = 0;
    if (a.h < 0) a.h = 0;
    d->fb = *fb;
    d->fb.px = fb->px + (long) a.y * fb->stride + (long) a.x * 4;
    d->fb.w = a.w;
    d->fb.h = a.h;
    d->ox = a.x;
    d->oy = a.y;
    d->sh = fb->h;
}
/* Values from a community mod: clamp before any arithmetic, so nothing can overflow. */
static int cm_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi
                                                                          : v; }
#define CM_XY(v) cm_clampi((v), -32768, 32767)
#define CM_WH(v) cm_clampi((v), 0, 32767)
void azcm_fill_rect(az_draw *d, int x, int y, int w, int h, uint32_t rgb) {
    if (d) fill_rect(&d->fb, CM_XY(x) - d->ox, CM_XY(y) - d->oy, CM_WH(w), CM_WH(h), rgb & 0x00FFFFFFu);
}
void azcm_draw_rect(az_draw *d, int x, int y, int w, int h, int t, uint32_t rgb) {
    if (!d || t <= 0) return;
    x = CM_XY(x);
    y = CM_XY(y);
    w = CM_WH(w);
    h = CM_WH(h);
    t = CM_WH(t);
    azcm_fill_rect(d, x, y, w, t, rgb);
    azcm_fill_rect(d, x, y + h - t, w, t, rgb);
    azcm_fill_rect(d, x, y, t, h, rgb);
    azcm_fill_rect(d, x + w - t, y, t, h, rgb);
}
void azcm_draw_text(az_draw *d, int x, int y, const char *s, int px, uint32_t rgb) {
    if (!d || !s) return;
    if (px < 8) px = 8;
    else if (px > 96) px = 96;
    x = CM_XY(x);
    y = CM_XY(y);
    int h = px * 3 / 2;
    if (azmod_ttf_menu && azmod_ttf_menu(&d->fb, x - d->ox, y - d->oy, 0, h, s, px, 0, 2, rgb & 0x00FFFFFFu)) return;
    draw_text(&d->fb, x - d->ox, y - d->oy + (h - 7) / 2, s, 1, rgb & 0x00FFFFFFu);
}
int azcm_text_width(const char *s, int px) {
    if (!s) return 0;
    if (px < 8) px = 8; /* as draw_text draws it: the face renders glyphs at this size */
    else if (px > 96) px = 96;
    int n = (int) strlen(s), w = azmod_ttf_menu_width ? azmod_ttf_menu_width(s, n, px, 2) : 0;
    return w > 0 ? w : n * px / 2;
}
void azcm_draw_header(az_draw *d, const char *title) {
    if (!d || !title) return;
    int rh = azcm_row_height(d), px = rh * 45 / 100;
    if (px < 12) px = 12;
    int pad = px / 2;
    if (azmod_ttf_menu) azmod_ttf_menu(&d->fb, pad, 0, 0, rh, title, px, 0, 1 | 2, C_HI);
    else draw_text(&d->fb, pad, (rh - 7) / 2, title, 1, C_HI);
    fill_rect(&d->fb, pad, rh - 2, d->fb.w - 2 * pad, 1, C_CHECK_BOX);
}
void azcm_draw_checkbox(az_draw *d, int x, int y, int size, int on, int hi) {
    if (d) draw_checkbox(&d->fb, CM_XY(x) - d->ox, CM_XY(y) - d->oy, cm_clampi(size, 1, 512), on != 0, hi != 0);
}
uint32_t azcm_color(int role) {
    /* clang-format off */
    switch (role) {
    case AZ_COL_TEXT:     return C_TEXT;
    case AZ_COL_TEXT_DIM: return C_TEXT_DIM;
    case AZ_COL_HI:       return C_HI;
    case AZ_COL_HI_TEXT:  return C_HI_TEXT;
    case AZ_COL_BG:       return C_ROW_BG;
    default:              return C_TEXT;
    }
    /* clang-format on */
}
/* the right pane's row height (c_rrowh), from the screen height: the clipped fb is only the
 * mod's area, far too small to size rows from */
int azcm_row_height(az_draw *d) { return d && d->sh > 0 ? d->sh * 50 / 800 : 50; }
