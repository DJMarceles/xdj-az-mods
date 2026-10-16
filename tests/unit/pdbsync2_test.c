/* SPDX-License-Identifier: GPL-3.0-only */
/* export.pdb sync (pdb_sync.c, #included whole as PDBS_INC) on generated sticks (pdbfix.h):
 *   anlz    a track row copied from the source stick carries the analysis path this paste
 *           wrote (OneLibrary's), not the source's folder, which on the target holds another
 *           track's waveform, beat grid and cues; its other strings are kept byte for byte;
 *   utf8    a playlist name that is ill-formed UTF-8 (a name cut byte-wise) is written whole
 *           characters only: the cut-off sequence dropped, any other bad byte as U+FFFD;
 *   long    a name too long for its row is cut at a character boundary and the sync goes on;
 *   torn    the sync interrupted after every write, fsync and rename (a pulled stick), the
 *           write itself cut in half too: export.pdb is always the complete old file or the
 *           complete new one, byte for byte; a stale export.pdb.aznew is removed when it is
 *           another file, kept when it may share export.pdb's clusters; a missing export.pdb
 *           comes back from a complete export.pdb.aznew;
 *   shrink  playlists deleted, so a table has fewer rows than pages: the rows are packed from
 *           its first page and every surplus page becomes a one-slot tombstone (never a 0-slot
 *           page, which sends rekordbox 7 into a runaway allocation), the index page's free
 *           list names exactly the pages with deleted slots, and the table grows back after.
 * Controls (run.sh): each protection removed in turn. */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#endif
#include "check.h"

/* ---- a pulled stick: every write-side call counted; at call g_crash_at the stick is gone
 * (that call does nothing, or half of a pwrite lands) and so is every call after it */
static int g_ops, g_crash_at = -1, g_half, g_dead;
static int t_dead(void) {
    if (g_dead)
        return 1;
    if (g_crash_at >= 0 && g_ops++ == g_crash_at) {
        g_dead = 1;
        return 1;
    }
    return 0;
}
static int t_open(const char *p, int fl, ...) {
    va_list ap;
    va_start(ap, fl);
    int mode = (fl & O_CREAT) ? va_arg(ap, int) : 0;
    va_end(ap);
    if ((fl & (O_WRONLY | O_RDWR | O_CREAT)) && t_dead())
        return errno = EIO, -1;
    if (g_dead)
        return errno = EIO, -1;
    return open(p, fl, mode);
}
static ssize_t t_pwrite(int fd, const void *b, size_t n, off_t at) {
    if (!g_dead && g_crash_at >= 0 && g_ops == g_crash_at && g_half && n > 1) {
        ssize_t w = pwrite(fd, b, n / 2, at); /* the first half of this write lands */
        (void) w;
    }
    if (t_dead())
        return errno = EIO, -1;
    return pwrite(fd, b, n, at);
}
static int t_fsync(int fd) { return t_dead() ? (errno = EIO, -1) : fsync(fd); }
static int t_rename(const char *a, const char *b) { return t_dead() ? (errno = EIO, -1) : rename(a, b); }
static int t_unlink(const char *a) { return t_dead() ? (errno = EIO, -1) : unlink(a); }
static int t_ftruncate(int fd, off_t n) { return t_dead() ? (errno = EIO, -1) : ftruncate(fd, n); }
/* FIBMAP as the media would answer it: 1 = both files start in the same block (shared
 * clusters), 2 = in different blocks; 0 = the real call */
static int g_fibmap;
static int t_ioctl(int fd, unsigned long req, ...) {
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
#ifdef FIBMAP
    if (req == FIBMAP && g_fibmap) {
        *(int *) arg = g_fibmap == 1 ? 777 : 1000 + fd;
        return 0;
    }
#endif
    return ioctl(fd, req, arg);
}
#define ioctl t_ioctl
#define open(...) t_open(__VA_ARGS__)
#define pwrite t_pwrite
#define fsync t_fsync
#define rename t_rename
#define unlink t_unlink
#define ftruncate t_ftruncate
#include PDBS_INC
#undef ioctl
#undef open
#undef pwrite
#undef fsync
#undef rename
#undef unlink
#undef ftruncate
#include "pdbfix.h"

static char g_tmp[600];
static unsigned char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long L = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(L > 0 ? (size_t) L : 1);
    *n = fread(b, 1, (size_t) L, f);
    fclose(f);
    return b;
}
static void pdb_path(const char *root, char *out, size_t n) { snprintf(out, n, "%s/PIONEER/rekordbox/export.pdb", root); }
/* string k of the track row for path (UTF-8), "" when there is none */
static void trk_str(const char *root, const char *path, int k, char *out, size_t cap) {
    char p[700];
    ps_buf d;
    struct ps_map loc = {0};
    unsigned at;
    out[0] = 0;
    pdb_path(root, p, sizeof p);
    if (ps_load(p, &d))
        return;
    if (!ps_tracks(&d, 0, &loc) && ps_map_get(&loc, path, &at)) {
        const unsigned char *r = ps_page(&d, at >> 16) + (at & 0xffff);
        unsigned o = ps_u16(r + 0x5e + 2 * k);
        ps_str(r + o, d.pl - (at & 0xffff) - o, out, cap);
    }
    ps_map_free(&loc);
    free(d.b);
}
/* playlist_tree name of id (UTF-8, up to cap); 0 = found */
static int tree_name(const char *root, unsigned id, char *out, size_t cap) {
    char p[700];
    ps_buf d;
    static unsigned pg[PS_MAXPAGES], ro[PS_MAXSLOTS];
    int found = -1;
    out[0] = 0;
    pdb_path(root, p, sizeof p);
    if (ps_load(p, &d))
        return -1;
    int np = ps_pages(&d, PT_TREE, pg, PS_MAXPAGES);
    for (int i = 0; i < np && found; i++) {
        unsigned char *x = ps_page(&d, pg[i]);
        int nr = ps_rows(&d, x, ro);
        for (int r = 0; r < nr; r++)
            if (ps_u32(x + ro[r] + 12) == id) {
                ps_str(x + ro[r] + 20, d.pl - ro[r] - 20, out, cap);
                found = 0;
            }
    }
    free(d.b);
    return found;
}
static int valid_utf8(const char *s) {
    for (size_t i = 0, L = strlen(s); i < L;) {
        unsigned k;
        if (ps_u8((const unsigned char *) s + i, L - i, &k) < 0)
            return 0;
        i += k;
    }
    return 1;
}
static int walk_all(const char *root) {
    char p[700];
    ps_buf d;
    pdb_path(root, p, sizeof p);
    if (ps_load(p, &d))
        return -1;
    int bad = 0;
    for (size_t q = 1; q < d.n / d.pl; q++) {
        const unsigned char *x = d.b + q * d.pl;
        if (ps_u32(x + 4) == q && !(x[0x1b] & 0x40) && !ps_walk_ok(&d, x))
            bad++;
    }
    bad += ps_open_ok(&d, 0) != 0;
    free(d.b);
    return bad;
}
static void sub(const char *name, char *out, size_t n) {
    snprintf(out, n, "%s/%s", g_tmp, name);
    fx_rmrf(out);
}

static const struct fx_trk T_DST[] = {{"/Contents/b.mp3", 0, "/PIONEER/USBANLZ/P002/00000002/ANLZ0000.DAT"}};
static const struct fx_trk T_SRC[] = {{"/Contents/a.mp3", 0, "/PIONEER/USBANLZ/P010/00008F30/ANLZ0000.DAT"},
                                      {"/Contents/b.mp3", 0, "/PIONEER/USBANLZ/P002/00000002/ANLZ0000.DAT"},
                                      {"/Contents/c.mp3", 0, "/PIONEER/USBANLZ/P003/00000003/ANLZ0000.DAT"}};

static void t_anlz(void) {
    char dst[700], src[700], s[400];
    sub("anlz-dst", dst, sizeof dst);
    sub("anlz-src", src, sizeof src);
    struct fx fd = {T_DST, 1, 0, 0}, fs = {T_SRC, 3, 0, 0};
    if (fx_write(dst, &fd) || fx_write(src, &fs))
        exit(2);
    /* a's source folder was another track's here: the paste moved a's files (cp_copy_track) */
    const struct pdbs_track tr[] = {{"/Contents/a.mp3", 0, "/PIONEER/USBANLZ/P010/33632095/ANLZ0000.DAT"},
                                    {"/Contents/b.mp3", 0, "/PIONEER/USBANLZ/P002/00000002/ANLZ0000.DAT"},
                                    {"/Contents/c.mp3", "", "/PIONEER/USBANLZ/P003/00000003/ANLZ0000.DAT"}};
    const struct pdbs_pl pl[] = {{9, 0, 0, 0, "Pasted"}};
    const struct pdbs_item it[] = {{9, "/Contents/a.mp3"}, {9, "/Contents/c.mp3"}};
    CHECK("anlz: the paste syncs", pdbs_sync(dst, src, pl, 1, it, 2, tr, 3, 0) == 0);
    trk_str(dst, "/Contents/a.mp3", PS_TRK_ANLZ, s, sizeof s);
    CHECK("anlz: a moved track's row names the folder the paste wrote", !strcmp(s, "/PIONEER/USBANLZ/P010/33632095/ANLZ0000.DAT"));
    trk_str(dst, "/Contents/a.mp3", 17, s, sizeof s);
    CHECK("anlz: its other strings are the source's (title)", !strcmp(s, "Title 1"));
    trk_str(dst, "/Contents/a.mp3", 20, s, sizeof s);
    CHECK("anlz: its other strings are the source's (path)", !strcmp(s, "/Contents/a.mp3"));
    trk_str(dst, "/Contents/c.mp3", PS_TRK_ANLZ, s, sizeof s);
    CHECK("anlz: a track whose folder was free keeps the source's path", !strcmp(s, "/PIONEER/USBANLZ/P003/00000003/ANLZ0000.DAT"));
    CHECK("anlz: every page passes the open checks and the dirty walk", walk_all(dst) == 0);
}

static void t_names(void) {
    char dst[700], s[2400];
    sub("names", dst, sizeof dst);
    struct fx fd = {T_DST, 1, 0, 0};
    if (fx_write(dst, &fd))
        exit(2);
    char cut[200] = "", longn[2200] = "";
    for (int i = 0; i < 31; i++)
        strcat(cut, "\xe6\x97\xa5"); /* 93 bytes of 日 */
    strcat(cut, "\xe6\x97");         /* + 2 bytes of a third-byte-less 日: cut byte-wise at 95 */
    for (int i = 0; i < 700; i++)
        strcat(longn, "\xe6\x97\xa5"); /* 700 characters: 1400 bytes as UTF-16 */
    const struct pdbs_pl pl[] = {{1, 0, 0, 0, cut}, {2, 0, 1, 0, "a\xff"
                                                                 "b"},
                                 {3, 0, 2, 0, longn},
                                 {4, 0, 3, 0, "Plain"}};
    const struct pdbs_track tr[] = {{"/Contents/b.mp3", 0, 0}};
    const struct pdbs_item it[] = {{4, "/Contents/b.mp3"}};
    CHECK("names: the sync is not stopped by any name", pdbs_sync(dst, 0, pl, 4, it, 1, tr, 1, 0) == 0);
    char want[200];
    snprintf(want, sizeof want, "%.93s", cut);
    CHECK("utf8: a name cut inside a character is written without the cut-off bytes",
          !tree_name(dst, 1, s, sizeof s) && !strcmp(s, want));
    CHECK("utf8: another ill-formed byte is written as U+FFFD", !tree_name(dst, 2, s, sizeof s) && !strcmp(s, "a\xef\xbf\xbd"
                                                                                                              "b"));
    CHECK("long: a name too long for its row is cut at a character boundary",
          !tree_name(dst, 3, s, sizeof s) && valid_utf8(s) && strlen(s) >= 3 * 500 && !strncmp(s, longn, strlen(s)));
    CHECK("long: the other playlists are mirrored", !tree_name(dst, 4, s, sizeof s) && !strcmp(s, "Plain"));
    CHECK("names: every page passes the open checks and the dirty walk", walk_all(dst) == 0);
}

/* the old file, then a sync whose new file differs on many pages and grows by a few */
static struct pdbs_pl g_pl[300];
static char g_nm[300][40];
static int torn_round(const char *root, int crash_at, int half, const unsigned char *old, size_t on,
                      unsigned char **newb, size_t *nn) {
    struct fx fd = {T_DST, 1, 0, 0};
    fx_rmrf(root);
    if (fx_write(root, &fd))
        exit(2);
    for (int i = 0; i < 300; i++) {
        snprintf(g_nm[i], sizeof g_nm[i], "Playlist number %03d", i);
        g_pl[i] = (struct pdbs_pl) {(unsigned) i + 1, 0, (unsigned) i, 0, g_nm[i]};
    }
    const struct pdbs_track tr[] = {{"/Contents/b.mp3", 0, 0}};
    const struct pdbs_item it[] = {{1, "/Contents/b.mp3"}};
    g_ops = 0, g_crash_at = crash_at, g_half = half, g_dead = 0;
    int rc = pdbs_sync(root, 0, g_pl, 300, it, 1, tr, 1, 0);
    int n_ops = g_ops;
    g_crash_at = -1, g_dead = 0;
    char p[700];
    pdb_path(root, p, sizeof p);
    size_t n = 0;
    unsigned char *b = slurp(p, &n);
    int ok;
    if (!old)
        ok = rc == 0; /* the reference run */
    else
        ok = b && ((n == on && !memcmp(b, old, on)) || (n == *nn && !memcmp(b, *newb, n)));
    if (!old) {
        *newb = b;
        *nn = n;
        return n_ops;
    }
    free(b);
    return ok ? 0 : -1;
}
static void t_torn(void) {
    char root[700], p[700], q[700];
    sub("torn", root, sizeof root);
    struct fx fd = {T_DST, 1, 0, 0};
    fx_rmrf(root);
    if (fx_write(root, &fd))
        exit(2);
    pdb_path(root, p, sizeof p);
    size_t on = 0, nn = 0;
    unsigned char *old = slurp(p, &on), *newb = 0;
    g_ops = 0, g_crash_at = 1 << 30, g_dead = 0;
    torn_round(root, 1 << 30, 0, 0, 0, &newb, &nn);
    int total = g_ops;
    CHECK("torn: the uninterrupted sync writes a different, larger file", newb && nn > on && memcmp(newb, old, on));
    int bad = 0, rounds = 0;
    for (int k = 0; k <= total; k++)
        for (int half = 0; half < 2; half++, rounds++)
            if (torn_round(root, k, half, old, on, &newb, &nn)) {
                if (bad < 3)
                    printf("      pulled at write-side call %d%s: export.pdb is neither file\n", k, half ? " (half written)" : "");
                bad++;
            }
    printf("      %d interruption points (%d write-side calls, each also half-written)\n", rounds, total);
    CHECK("torn: pulled at any point, export.pdb is the complete old or the complete new file", bad == 0 && total > 4);
    /* a stale temp that is another file: removed, and the sync goes on */
    fx_rmrf(root);
    fx_write(root, &fd);
    snprintf(q, sizeof q, "%s.aznew", p);
    FILE *f = fopen(q, "wb");
    fputs("half a file", f);
    fclose(f);
    const struct pdbs_track tr[] = {{"/Contents/b.mp3", 0, 0}};
    const struct pdbs_item it[] = {{1, "/Contents/b.mp3"}};
    struct stat st;
    CHECK("torn: a stale export.pdb.aznew that is another file is replaced",
          pdbs_sync(root, 0, g_pl, 300, it, 1, tr, 1, 0) == 0 && stat(q, &st) != 0);
    /* a stale temp identical to export.pdb (a FAT rename cut short may share its clusters):
     * kept unless the media shows the two files apart */
    for (int mode = 1; mode <= 2; mode++) {
        size_t n = 0;
        unsigned char *cur = slurp(p, &n);
        f = fopen(q, "wb");
        fwrite(cur, 1, n, f);
        fclose(f);
        free(cur);
        g_pl[0].name = mode == 1 ? "Renamed" : "Renamed again";
        g_fibmap = mode;
        int rc = pdbs_sync(root, 0, g_pl, 300, it, 1, tr, 1, 0);
        g_fibmap = 0;
        if (mode == 1)
            CHECK("torn: a stale identical export.pdb.aznew that may share the file's clusters is never removed",
                  rc < 0 && stat(q, &st) == 0);
#ifdef FIBMAP
        else
            CHECK("torn: a stale identical export.pdb.aznew in clusters of its own is replaced", rc == 0 && stat(q, &st) != 0);
#endif
        g_pl[0].name = g_nm[0];
        unlink(q);
#ifndef FIBMAP
        break; /* no FIBMAP here: nothing can show the two apart */
#endif
    }
    /* export.pdb missing, the complete new file beside it: put back */
    rename(p, q);
    CHECK("torn: a missing export.pdb comes back from a complete export.pdb.aznew",
          pdbs_sync(root, 0, g_pl, 300, it, 1, tr, 1, 0) >= 0 && stat(p, &st) == 0 && stat(q, &st) != 0);
    free(old);
    free(newb);
}

/* a table's pages: count, tombstones (one deleted slot, nothing present), and whether the
 * index page's free list names exactly the pages with deleted slots, each once */
static int table_shape(const char *root, unsigned typ, int *pages, int *tombs) {
    char p[700];
    ps_buf d;
    static unsigned pg[PS_MAXPAGES];
    pdb_path(root, p, sizeof p);
    if (ps_load(p, &d))
        return -1;
    int np = ps_pages(&d, typ, pg, PS_MAXPAGES), holes = 0, bad = 0;
    *pages = np, *tombs = 0;
    unsigned char *t = ps_tab(&d, typ), *ip = t ? ps_page(&d, ps_u32(t + 8)) : NULL;
    if (np <= 0 || !ip) {
        free(d.b);
        return -1;
    }
    unsigned nl = ps_u16(ip + 0x38);
    for (int i = 0; i < np; i++) {
        unsigned char *x = ps_page(&d, pg[i]);
        unsigned present = (x[0x19] >> 5) | x[0x1a] << 3, slots = ps_slots(x);
        bad += slots == 0; /* a 0-slot page: never */
        *tombs += slots == 1 && present == 0;
        if (slots != present) {
            holes++;
            int listed = 0;
            for (unsigned k = 0; k < nl; k++)
                listed += ps_u32(ip + 0x3c + 4 * k) == pg[i] << 3;
            bad += listed != 1;
        }
    }
    bad += nl != (unsigned) holes || ps_u16(ip + 0x26) != nl;
    free(d.b);
    return bad;
}
static void t_shrink(void) {
    char root[700], s[64];
    sub("shrink", root, sizeof root);
    struct fx fd = {T_DST, 1, 0, 0};
    if (fx_write(root, &fd))
        exit(2);
    for (int i = 0; i < 300; i++) {
        snprintf(g_nm[i], sizeof g_nm[i], "Playlist number %03d", i);
        g_pl[i] = (struct pdbs_pl) {(unsigned) i + 1, 0, (unsigned) i, 0, g_nm[i]};
    }
    const struct pdbs_track tr[] = {{"/Contents/b.mp3", 0, 0}};
    const struct pdbs_item it[] = {{1, "/Contents/b.mp3"}};
    int big = 0, tombs = 0, pages = 0;
    CHECK("shrink: 300 playlists synced", pdbs_sync(root, 0, g_pl, 300, it, 1, tr, 1, 0) == 0);
    CHECK("shrink: they take several pages of the playlist table",
          table_shape(root, PT_TREE, &big, &tombs) == 0 && big > 2 && tombs == 0);
    CHECK("shrink: 2 playlists left: synced", pdbs_sync(root, 0, g_pl, 2, it, 1, tr, 1, 0) == 0);
    int bad = table_shape(root, PT_TREE, &pages, &tombs);
    CHECK("shrink: every surplus page is a one-slot tombstone, none has 0 slots", bad >= 0 && pages == big && tombs == big - 1);
    CHECK("shrink: the free list names exactly the pages with deleted slots, each once", bad == 0);
    CHECK("shrink: the 2 playlists are there, the deleted ones are not",
          !tree_name(root, 1, s, sizeof s) && !tree_name(root, 2, s, sizeof s) && tree_name(root, 3, s, sizeof s) &&
              tree_name(root, 300, s, sizeof s));
    CHECK("shrink: every page passes the open checks and the dirty walk", walk_all(root) == 0);
    CHECK("shrink: back to 300 playlists: synced", pdbs_sync(root, 0, g_pl, 300, it, 1, tr, 1, 0) == 0);
    CHECK("shrink: grown back, no tombstone left, the free list still exact",
          table_shape(root, PT_TREE, &pages, &tombs) == 0 && tombs == 0 && !tree_name(root, 300, s, sizeof s) &&
              walk_all(root) == 0);
}

int main(void) {
    ut_suite = "export.pdb sync";
    const char *t = getenv("TMPDIR");
    snprintf(g_tmp, sizeof g_tmp, "%s/pdbsync2.XXXXXX", t && t[0] ? t : "/tmp");
    if (!mkdtemp(g_tmp))
        return 2;
    t_anlz();
    t_names();
    t_torn();
    t_shrink();
    fx_rmrf(g_tmp);
    return ut_summary();
}
