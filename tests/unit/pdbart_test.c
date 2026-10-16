/* SPDX-License-Identifier: GPL-3.0-only */
/* export.pdb artwork after a paste onto a stick that already lists the tracks (pdb_sync.c,
 * #included whole as PDBS_INC: it is libc only). The fixture is a minimal export.pdb in
 * rekordbox's layout (the same one tools/fuzz/gen_corpus.py writes) with five tracks:
 *   a  own art file gone, OneLibrary names a paste's copy that is there -> the copy
 *   b  own art file there, OneLibrary names a paste's copy               -> its own art kept
 *   c  own art file gone, the paste's copy is not on the stick either    -> unchanged
 *   d  own art file gone, OneLibrary art is rekordbox's own (b<n>.jpg)   -> unchanged (export.pdb
 *      names its own art files apart from OneLibrary's: never mirrored in general)
 *   e  no art, OneLibrary names a paste's copy                           -> the copy
 *   f  not in this pdb; the source stick's row has no art                -> copied, with the copy
 * a's path is on two OneLibrary rows. The copy gets one artwork row, a second sync changes nothing, and every data page passes
 * rekordbox's dirty-bits walk. Controls: the pass removed (the old sync), and a row's own art
 * replaced although its file is on the stick. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "check.h"
#include PDBS_INC

#define PL 4096
static unsigned char g_file[32 * PL];
static size_t g_len;

static size_t dstr(unsigned char *o, const char *s) {
    size_t L = strlen(s);
    o[0] = (unsigned char) ((L + 1) << 1 | 1);
    memcpy(o + 1, s, L);
    return L + 1;
}
/* one page: rows packed from 0x28, the row index down from the page end */
static void page(unsigned p, unsigned typ, unsigned nx, unsigned flags, unsigned char rows[][600], const size_t *lens,
                 int n) {
    unsigned char *b = g_file + (size_t) p * PL;
    memset(b, 0, PL);
    ps_w32(b + 4, p);
    ps_w32(b + 8, typ);
    ps_w32(b + 0xc, nx);
    size_t heap = 0;
    unsigned ofs[64];
    for (int i = 0; i < n; i++) {
        ofs[i] = (unsigned) heap;
        memcpy(b + 0x28 + heap, rows[i], lens[i]);
        heap += (lens[i] + 3) & ~(size_t) 3;
    }
    b[0x18] = (unsigned char) (n & 0xff);
    b[0x19] = (unsigned char) (((n & 7) << 5) | (n >> 8 & 0x1f));
    b[0x1a] = (unsigned char) (n >> 3);
    b[0x1b] = (unsigned char) flags;
    ps_w16(b + 0x1c, (unsigned) (PL - 0x28 - heap - 0x24 * ((n + 15) / 16)));
    ps_w16(b + 0x1e, (unsigned) heap);
    ps_w16(b + 0x20, 0x1fff);
    ps_w16(b + 0x22, 0x1fff);
    for (int g = 0; g < (n + 15) / 16; g++) {
        unsigned base = PL - g * 0x24, k = n - 16 * g < 16 ? n - 16 * g : 16;
        ps_w16(b + base - 4, (1u << k) - 1);
        ps_w16(b + base - 2, 0);
        for (unsigned i = 0; i < k; i++) ps_w16(b + base - 6 - 2 * i, ofs[16 * g + i]);
    }
    if (flags & 0x40) ps_w16(b + 0x24, 8);
}
static size_t track_row(unsigned char *r, unsigned id, unsigned art, const char *path) {
    memset(r, 0, 0x88);
    ps_w32(r + 0x1c, art);
    ps_w32(r + 0x48, id);
    size_t o = 0x88;
    for (int k = 0; k < 21; k++) {
        char t[32];
        snprintf(t, sizeof t, "Title %u", id);
        ps_w16(r + 0x5e + 2 * k, (unsigned) o);
        o += dstr(r + o, k == 20 ? path : k == 17 ? t
                                                  : "");
    }
    return o;
}
static const char *TRK[6] = {"/Contents/a.mp3", "/Contents/b.mp3", "/Contents/c.mp3",
                             "/Contents/d.mp3", "/Contents/e.mp3", "/Contents/f.mp3"};
static const unsigned TRK_ART[6] = {1, 2, 3, 4, 0, 0};
static const char *ART[4] = {"/PIONEER/Artwork/00001/a1.jpg", "/PIONEER/Artwork/00001/a2.jpg",
                             "/PIONEER/Artwork/00001/a3.jpg", "/PIONEER/Artwork/00001/a4.jpg"};
static void build(int ntrk) { /* the first ntrk tracks */
    enum {
        T = 14
    };
    static const int data[T] = {1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 1};
    unsigned idx[T], dp[T], ec[T], nxt = 1;
    for (int t = 0; t < T; t++) {
        idx[t] = nxt++;
        dp[t] = data[t] ? nxt++ : idx[t];
    }
    unsigned e = nxt;
    for (int t = 0; t < T; t++) ec[t] = e++;
    memset(g_file, 0, sizeof g_file);
    static unsigned char rows[64][600];
    size_t lens[64];
    for (int t = 0; t < T; t++) {
        page(idx[t], (unsigned) t, dp[t] != idx[t] ? dp[t] : ec[t], 0x64, rows, lens, 0);
        if (dp[t] == idx[t]) continue;
        int n = 0;
        if (t == PT_TRACK)
            for (int i = 0; i < ntrk; i++, n++) lens[n] = track_row(rows[n], (unsigned) i + 1, TRK_ART[i], TRK[i]);
        else if (t == PT_ART)
            for (int i = 0; i < 4; i++, n++) {
                ps_w32(rows[n], (unsigned) i + 1);
                lens[n] = 4 + dstr(rows[n] + 4, ART[i]);
            }
        else if (t == PT_TREE) {
            memset(rows[0], 0, 20);
            ps_w32(rows[0] + 12, 9);
            lens[0] = 20 + dstr(rows[0] + 20, "Old");
            n = 1;
        } else if (t == PT_ENTRY) {
            ps_w32(rows[0], 1);
            ps_w32(rows[0] + 4, 1);
            ps_w32(rows[0] + 8, 9);
            lens[0] = 12;
            n = 1;
        }
        page(dp[t], (unsigned) t, ec[t], 0x24, rows, lens, n);
    }
    unsigned char *h = g_file;
    ps_w32(h + 4, PL);
    ps_w32(h + 8, T);
    ps_w32(h + 0xc, e);
    ps_w32(h + 0x14, 1);
    for (int t = 0; t < T; t++) {
        ps_w32(h + 0x1c + 16 * t, (unsigned) t);
        ps_w32(h + 0x20 + 16 * t, ec[t]);
        ps_w32(h + 0x24 + 16 * t, idx[t]);
        ps_w32(h + 0x28 + 16 * t, dp[t]);
    }
    g_len = (size_t) nxt * PL;
}
static char g_root[600];
static void mk(const char *rel, int file) {
    char p[800];
    snprintf(p, sizeof p, "%s%s", g_root, rel);
    if (!file) {
        mkdir(p, 0755);
        return;
    }
    FILE *f = fopen(p, "wb");
    if (!f || fwrite(file == 2 ? g_file : (const unsigned char *) "jpg", 1, file == 2 ? g_len : 3, f) == 0 || fclose(f))
        exit(2);
}
static unsigned art_id(ps_buf *d, int track) { /* artwork_id of track id `track` */
    struct ps_map loc = {0};
    unsigned at = 0, r = 0xffffffff;
    if (!ps_tracks(d, 0, &loc) && ps_map_get(&loc, TRK[track - 1], &at)) r = ps_u32(ps_page(d, at >> 16) + (at & 0xffff) + 0x1c);
    ps_map_free(&loc);
    return r;
}
static int art_rows(ps_buf *d, const char *path, unsigned *id) { /* rows naming path */
    struct ps_named *v = 0;
    int n = ps_names(d, PT_ART, &v), k = 0;
    for (int i = 0; i < n; i++)
        if (!strcmp(v[i].name, path)) {
            k++;
            *id = v[i].id;
        }
    free(v);
    return k;
}

int main(void) {
    ut_suite = "export.pdb: a paste's artwork";
    const char *tmp = getenv("TMPDIR");
    snprintf(g_root, sizeof g_root, "%s/pdbart.XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
    if (!mkdtemp(g_root)) return 2;
    const char *dirs[] = {"/PIONEER", "/PIONEER/rekordbox", "/PIONEER/Artwork", "/PIONEER/Artwork/00001",
                          "/PIONEER/Artwork/00002", "/PIONEER/Artwork/azcp", "/src", "/src/PIONEER",
                          "/src/PIONEER/rekordbox"};
    for (unsigned i = 0; i < sizeof dirs / sizeof *dirs; i++) mk(dirs[i], 0);
    build(6); /* the source stick also has f, with no art in its own export.pdb */
    mk("/src/PIONEER/rekordbox/export.pdb", 2);
    build(5);
    mk("/PIONEER/rekordbox/export.pdb", 2);
    mk("/PIONEER/Artwork/00001/a2.jpg", 1);           /* b's own art is there */
    mk("/PIONEER/Artwork/00002/b4.jpg", 1);           /* d's OneLibrary art (rekordbox's own) */
    mk("/PIONEER/Artwork/azcp/0102_b1.jpg", 1);       /* the paste's copies; 0102_b3 was not written */
    mk("/PIONEER/Artwork/azcp/0102_b2.jpg", 1);
    mk("/PIONEER/Artwork/azcp/0102_b5.jpg", 1);
    mk("/PIONEER/Artwork/azcp/0102_b6.jpg", 1);
    const struct pdbs_track tr[] = {{TRK[0], "/PIONEER/Artwork/azcp/0102_b1.jpg"},
                                    {TRK[1], "/PIONEER/Artwork/azcp/0102_b2.jpg"},
                                    {TRK[2], "/PIONEER/Artwork/azcp/0102_b3.jpg"},
                                    {TRK[3], "/PIONEER/Artwork/00002/b4.jpg"},
                                    {TRK[4], "/PIONEER/Artwork/azcp/0102_b5.jpg"},
                                    {TRK[5], "/PIONEER/Artwork/azcp/0102_b6.jpg"},
                                    {"/Contents/x.mp3", 0},
                                    {TRK[0], "/PIONEER/Artwork/azcp/0102_b1.jpg"}}; /* a's 2nd row */
    const struct pdbs_pl pl[] = {{9, 0, 0, 0, "Party"}};
    const struct pdbs_item it[] = {{9, TRK[0]}, {9, TRK[1]}, {9, TRK[2]}, {9, TRK[3]}, {9, TRK[4]}, {9, TRK[5]}};
    char path[700], src[700];
    snprintf(src, sizeof src, "%s/src", g_root);
    snprintf(path, sizeof path, "%s/PIONEER/rekordbox/export.pdb", g_root);
    ps_buf d0;
    CHECK("the fixture loads", ps_load(path, &d0) == 0 && art_id(&d0, 1) == 1 && art_id(&d0, 5) == 0);
    free(d0.b);
    int rc = pdbs_sync(g_root, src, pl, 1, it, 6, tr, 8, stdout);
    CHECK("the sync writes export.pdb", rc == 0);
    ps_buf d;
    if (ps_load(path, &d)) return 2;
    unsigned a1 = 0, a5 = 0;
    CHECK("the copy of a's art has one artwork row", art_rows(&d, "/PIONEER/Artwork/azcp/0102_b1.jpg", &a1) == 1);
    CHECK("the copy of e's art has one artwork row", art_rows(&d, "/PIONEER/Artwork/azcp/0102_b5.jpg", &a5) == 1);
    CHECK("a (own art file gone) shows the paste's copy", art_id(&d, 1) == a1 && a1 > 4);
    CHECK("b (own art file there) keeps its own art", art_id(&d, 2) == 2);
    CHECK("c (the copy is not on the stick) is unchanged", art_id(&d, 3) == 3);
    CHECK("d (OneLibrary's own art, not a paste's) is unchanged", art_id(&d, 4) == 4);
    CHECK("e (no art) shows the paste's copy", art_id(&d, 5) == a5 && a5 > 4 && a5 != a1);
    unsigned a6 = 0;
    CHECK("f (new here; its source row has no art) is copied and shows the paste's copy",
          art_rows(&d, "/PIONEER/Artwork/azcp/0102_b6.jpg", &a6) == 1 && art_id(&d, 6) == a6 && a6 > 4);
    unsigned dummy;
    CHECK("no row for art that is not on the stick", art_rows(&d, "/PIONEER/Artwork/azcp/0102_b3.jpg", &dummy) == 0);
    CHECK("no row for b's unused copy", art_rows(&d, "/PIONEER/Artwork/azcp/0102_b2.jpg", &dummy) == 0);
    int bad = 0;
    for (size_t p = 1; p < d.n / d.pl; p++) {
        const unsigned char *x = d.b + p * d.pl;
        if (ps_u32(x + 4) == p && !(x[0x1b] & 0x40) && !ps_walk_ok(&d, x)) bad++;
    }
    CHECK("every data page passes rekordbox's dirty-bits walk", bad == 0);
    free(d.b);
    CHECK("a second sync changes nothing", pdbs_sync(g_root, src, pl, 1, it, 6, tr, 8, stdout) == 1);
    char cmd[700];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_root);
    if (system(cmd)) return 2;
    return ut_summary();
}
