/* SPDX-License-Identifier: GPL-3.0-only */
/* ls_dup and ls_pdb_sync_root (extracted from library_edits.c by run.sh): the
 * OneLibrary rows that feed the export.pdb sync after a cross-USB paste.
 *
 * Every name and path is a strdup'd copy. pdbs_sync hashes, compares and encodes those
 * strings without a NULL check (ps_hash, ps_map_get, ps_enc in pdb_sync.c), so a copy that
 * failed must stop the sync: rc -1, pdbs_sync not called, export.pdb untouched, the
 * "library read failed" line logged, and every copy that did succeed freed.
 *
 * The library is a fake g_sq returning three small result sets; strdup fails on a chosen
 * call; every allocation is counted so a leak shows as a nonzero live count. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"

#define SQLITE_ROW 100
#define PL_ATTR_FOLDER 1

#include PDBS_STRUCTS_INC

/* ---- allocation accounting and failure injection */
static int g_dup_calls, g_dup_fail_at, g_live;
static char *t_strdup(const char *s) {
    if (++g_dup_calls == g_dup_fail_at)
        return 0;
    char *r = strdup(s);
    if (r)
        g_live++;
    return r;
}
static void *t_realloc(void *p, size_t n) {
    void *r = realloc(p, n);
    if (r && !p)
        g_live++;
    return r;
}
static void t_free(void *p) {
    if (p)
        g_live--;
    free(p);
}

/* ---- fake sqlite: one result set per query */
struct row {
    long long i[4];
    const char *t[5];
};
static const struct row PL_ROWS[] = {{{1, 0, 1, 0}, {0, 0, 0, 0, "House"}},
                                     {{2, 0, 2, PL_ATTR_FOLDER}, {0, 0, 0, 0, "Sets"}},
                                     {{3, 2, 1, 0}, {0, 0, 0, 0, 0}}};
static const struct row IT_ROWS[] = {{{1}, {0, "/Contents/a.mp3"}},
                                     {{1}, {0, "/Contents/b.mp3"}},
                                     {{3}, {0, "/Contents/a.mp3"}}};
static const struct row TR_ROWS[] = {
    {{0}, {"/Contents/a.mp3", "/Artwork/a.jpg", "/PIONEER/USBANLZ/P001/0000ABCD/ANLZ0000.DAT"}},
    {{0}, {"/Contents/b.mp3", 0, 0}}};
#define NDUPS (3 + 3 + 2 * 3) /* strdup calls one full read makes */
struct stmt {
    const struct row *r;
    int n, at;
};
static struct stmt g_stmt;
static int g_open_stmts;
static int f_prepare(void *c, const char *sql, int n, void **st, const char **tail) {
    (void) c, (void) n, (void) tail;
    if (strstr(sql, "FROM playlist_content"))
        g_stmt = (struct stmt) {IT_ROWS, 3, -1};
    else if (strstr(sql, "FROM playlist"))
        g_stmt = (struct stmt) {PL_ROWS, 3, -1};
    else if (strstr(sql, "FROM content"))
        g_stmt = (struct stmt) {TR_ROWS, 2, -1};
    else
        return 1;
    g_open_stmts++;
    *st = &g_stmt;
    return 0;
}
static int f_step(void *st) {
    struct stmt *s = st;
    return ++s->at < s->n ? SQLITE_ROW : 101;
}
static long long f_int(void *st, int c) { return ((struct stmt *) st)->r[((struct stmt *) st)->at].i[c]; }
static const unsigned char *f_text(void *st, int c) {
    return (const unsigned char *) ((struct stmt *) st)->r[((struct stmt *) st)->at].t[c];
}
static int f_finalize(void *st) {
    (void) st;
    g_open_stmts--;
    return 0;
}
static struct {
    int (*prepare)(void *, const char *, int, void **, const char **);
    int (*step)(void *);
    long long (*column_int64)(void *, int);
    const unsigned char *(*column_text)(void *, int);
    int (*finalize)(void *);
} g_sq = {f_prepare, f_step, f_int, f_text, f_finalize};

/* ---- pdbs_sync stub: records what it was handed */
static int g_sync_calls, g_sync_nulls, g_sync_shape_ok;
static int pdbs_sync(const char *root, const char *src, const struct pdbs_pl *pl, int npl,
                     const struct pdbs_item *it, int nit, const struct pdbs_track *trk, int ntrk,
                     FILE *lg) {
    (void) root, (void) src, (void) lg;
    g_sync_calls++;
    for (int i = 0; i < npl; i++)
        g_sync_nulls += !pl[i].name;
    for (int i = 0; i < nit; i++)
        g_sync_nulls += !it[i].path;
    for (int i = 0; i < ntrk; i++)
        g_sync_nulls += !trk[i].path + !trk[i].art + !trk[i].anlz;
    if (g_sync_nulls)
        return 0; /* recorded; the checks in main report it */
    g_sync_shape_ok =
        npl == 3 && nit == 3 && ntrk == 2 && pl[0].id == 1 && !strcmp(pl[0].name, "House") &&
        pl[1].folder == 1 && !strcmp(pl[1].name, "Sets") && pl[2].parent == 2 &&
        !strcmp(pl[2].name, "") && it[1].pl == 1 && !strcmp(it[1].path, "/Contents/b.mp3") &&
        it[2].pl == 3 && !strcmp(trk[0].art, "/Artwork/a.jpg") &&
        !strcmp(trk[1].path, "/Contents/b.mp3") && !strcmp(trk[1].art, "") &&
        !strcmp(trk[0].anlz, "/PIONEER/USBANLZ/P001/0000ABCD/ANLZ0000.DAT") &&
        !strcmp(trk[1].anlz, "");
    return 0;
}

#define strdup t_strdup
#define realloc t_realloc
#define free t_free
#include LSDUP_INC
#include LSSYNC_INC
#undef strdup
#undef realloc
#undef free

static int run(int fail_at, char **log) {
    size_t n;
    FILE *lg = open_memstream(log, &n);
    g_dup_calls = 0, g_dup_fail_at = fail_at, g_live = 0, g_open_stmts = 0;
    g_sync_calls = 0, g_sync_nulls = 0, g_sync_shape_ok = 0;
    int rc = ls_pdb_sync_root((void *) 1, "/media/usb/sdb", "/media/usb/sda", "azcp-1", lg);
    fclose(lg);
    return rc;
}

int main(void) {
    ut_suite = "ls_pdb_sync_root";
    char *log = 0;
    int rc = run(0, &log);
    CHECK("a full read syncs once", rc == 0 && g_sync_calls == 1);
    CHECK("the rows reach pdbs_sync as read (NULL text read as \"\")", g_sync_shape_ok);
    CHECK("every strdup counted", g_dup_calls == NDUPS);
    CHECK("a full read frees every copy", g_live == 0 && g_open_stmts == 0);
    CHECK("a full read does not log a failure", !strstr(log, "library read failed"));
    free(log);
    int called = 0, nulls = 0, notfail = 0, leaks = 0, nolog = 0, stmts = 0;
    for (int k = 1; k <= NDUPS; k++) {
        log = 0;
        rc = run(k, &log);
        called += g_sync_calls != 0;
        nulls += g_sync_nulls;
        notfail += rc != -1;
        leaks += g_live != 0;
        nolog += !strstr(log, "library read failed: export.pdb untouched");
        stmts += g_open_stmts != 0;
        if (g_sync_calls || rc != -1 || g_live)
            printf("      strdup #%d failing: rc %d, pdbs_sync calls %d (NULL fields %d), live %d\n",
                   k, rc, g_sync_calls, g_sync_nulls, g_live);
        free(log);
    }
    CHECK("a failed copy never reaches pdbs_sync (each of the copies failing in turn)",
          !called && !nulls);
    CHECK("a failed copy returns -1", !notfail);
    CHECK("a failed copy logs \"library read failed\"", !nolog);
    CHECK("a failed copy frees every copy that succeeded", !leaks);
    CHECK("a failed copy finalizes its statement", !stmts);
    return ut_summary();
}
