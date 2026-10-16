/* SPDX-License-Identifier: GPL-3.0-only */
/* A minimal export.pdb in rekordbox's layout for the pdb_sync.c suites (the layout
 * tools/fuzz/gen_corpus.py writes): every table 0..13 has an index page; tracks (0), the
 * name tables (1-5), playlist_tree (7), playlist_entries (8) and artwork (13) also get one
 * data page.
 * Include after pdb_sync.c (it uses ps_w16 / ps_w32). */
#ifndef PDBFIX_H
#define PDBFIX_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define FX_PL 4096
#define FX_MAXPAGES 64
struct fx_trk {
    const char *path;
    unsigned art;     /* artwork row id, 0 = none */
    const char *anlz; /* string 14 (analyze_path), 0 = "" */
};
struct fx {
    const struct fx_trk *trk;
    int ntrk;
    const char *const *art; /* artwork rows 1..nart */
    int nart;
};

static size_t fx_dstr(unsigned char *o, const char *s) {
    size_t L = strlen(s);
    o[0] = (unsigned char) ((L + 1) << 1 | 1);
    memcpy(o + 1, s, L);
    return L + 1;
}
static void fx_page(unsigned char *file, unsigned p, unsigned typ, unsigned nx, unsigned flags,
                    unsigned char rows[][600], const size_t *lens, int n) {
    unsigned char *b = file + (size_t) p * FX_PL;
    memset(b, 0, FX_PL);
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
    ps_w16(b + 0x1c, (unsigned) (FX_PL - 0x28 - heap - 0x24 * ((n + 15) / 16)));
    ps_w16(b + 0x1e, (unsigned) heap);
    ps_w16(b + 0x20, 0x1fff);
    ps_w16(b + 0x22, 0x1fff);
    for (int g = 0; g < (n + 15) / 16; g++) {
        unsigned base = FX_PL - g * 0x24, k = n - 16 * g < 16 ? n - 16 * g : 16;
        ps_w16(b + base - 4, (1u << k) - 1);
        ps_w16(b + base - 2, 0);
        for (unsigned i = 0; i < k; i++)
            ps_w16(b + base - 6 - 2 * i, ofs[16 * g + i]);
    }
    if (flags & 0x40)
        ps_w16(b + 0x24, 8);
}
static size_t fx_track_row(unsigned char *r, unsigned id, const struct fx_trk *t) {
    memset(r, 0, 0x88);
    ps_w32(r + 0x1c, t->art);
    ps_w32(r + 0x48, id);
    size_t o = 0x88;
    for (int k = 0; k < 21; k++) {
        char tt[32];
        snprintf(tt, sizeof tt, "Title %u", id);
        ps_w16(r + 0x5e + 2 * k, (unsigned) o);
        o += fx_dstr(r + o, k == 20 ? t->path : k == 17          ? tt
                                            : k == 14 && t->anlz ? t->anlz
                                                                 : "");
    }
    return o;
}
/* the file into out (FX_MAXPAGES pages); returns its length */
static size_t fx_build(unsigned char *out, const struct fx *f) {
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
    for (int t = 0; t < T; t++)
        ec[t] = e++;
    memset(out, 0, (size_t) FX_MAXPAGES * FX_PL);
    unsigned char (*rows)[600] = malloc(64 * 600); /* per call: the thread suite builds two at once */
    size_t lens[64];
    if (!rows)
        return 0;
    for (int t = 0; t < T; t++) {
        fx_page(out, idx[t], (unsigned) t, dp[t] != idx[t] ? dp[t] : ec[t], 0x64, rows, lens, 0);
        if (dp[t] == idx[t])
            continue;
        int n = 0;
        if (t == 0)
            for (int i = 0; i < f->ntrk; i++, n++)
                lens[n] = fx_track_row(rows[n], (unsigned) i + 1, &f->trk[i]);
        else if (t == 13)
            for (int i = 0; i < f->nart; i++, n++) {
                ps_w32(rows[n], (unsigned) i + 1);
                lens[n] = 4 + fx_dstr(rows[n] + 4, f->art[i]);
            }
        else if (t == 7) {
            memset(rows[0], 0, 20);
            ps_w32(rows[0] + 12, 9);
            lens[0] = 20 + fx_dstr(rows[0] + 20, "Old");
            n = 1;
        } else if (t == 8 && f->ntrk) {
            ps_w32(rows[0], 1);
            ps_w32(rows[0] + 4, 1);
            ps_w32(rows[0] + 8, 9);
            lens[0] = 12;
            n = 1;
        }
        fx_page(out, dp[t], (unsigned) t, ec[t], 0x24, rows, lens, n);
    }
    unsigned char *h = out;
    ps_w32(h + 4, FX_PL);
    ps_w32(h + 8, T);
    ps_w32(h + 0xc, e);
    ps_w32(h + 0x14, 1);
    for (int t = 0; t < T; t++) {
        ps_w32(h + 0x1c + 16 * t, (unsigned) t);
        ps_w32(h + 0x20 + 16 * t, ec[t]);
        ps_w32(h + 0x24 + 16 * t, idx[t]);
        ps_w32(h + 0x28 + 16 * t, dp[t]);
    }
    free(rows);
    return (size_t) nxt * FX_PL;
}
/* root/PIONEER/rekordbox/export.pdb from f; 0 = written */
static int fx_write(const char *root, const struct fx *f) {
    char p[800];
    snprintf(p, sizeof p, "%s", root);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/PIONEER", root);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/PIONEER/rekordbox", root);
    mkdir(p, 0755);
    unsigned char *file = malloc((size_t) FX_MAXPAGES * FX_PL);
    size_t n = file ? fx_build(file, f) : 0;
    snprintf(p, sizeof p, "%s/PIONEER/rekordbox/export.pdb", root);
    FILE *o = n ? fopen(p, "wb") : 0;
    int rc = o && fwrite(file, 1, n, o) == n ? 0 : -1;
    if (o && fclose(o))
        rc = -1;
    free(file);
    return rc;
}
static int fx_rmrf(const char *dir) {
    char cmd[900];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    return system(cmd);
}
#endif
