/* SPDX-License-Identifier: GPL-3.0-only */
/* export.pdb (guest/azmod/pdb_sync.c, #included whole: it is libc-only by design). Both the
 * destination stick's export.pdb and the paste's source stick's are read, so both are any bytes.
 *
 * Input: byte 0 picks the page size (512 / 1024 / 4096) and whether a source stick exists;
 * bytes 1-2 (LE) are the destination file's length, then the destination file, then the source
 * file. Each file is zero-padded to at least 4096 bytes and a whole number of pages, and its
 * page-size field is set to the chosen size, so the fuzzer works on the pages instead of on
 * ps_load's size checks (plain comparisons).
 * Invariant: a failed sync (< 0) leaves the destination file byte-identical to what it was. */
#define main pdbsync_unused_main
#include "pdb_sync.c"
#undef main
#include "fuzz_common.h"
#include <sys/stat.h>

static char g_root[512], g_src[512];
static void put_pdb(const char *root, const uint8_t *d, size_t n, unsigned pl) {
    char p[600];
    snprintf(p, sizeof p, "%s/PIONEER", root);
    mkdir(root, 0755);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/PIONEER/rekordbox", root);
    mkdir(p, 0755);
    size_t len = n < 4096 ? 4096 : n;
    len = (len + pl - 1) / pl * pl;
    unsigned char *b = calloc(1, len);
    memcpy(b, d, n);
    ps_w32(b + 4, pl);
    snprintf(p, sizeof p, "%s/PIONEER/rekordbox/export.pdb", root);
    FILE *f = fopen(p, "wb");
    if (!f || fwrite(b, 1, len, f) != len || fclose(f)) abort();
    free(b);
    snprintf(p, sizeof p, "%s/PIONEER/rekordbox/export.pdb.azbak", root);
    unlink(p);
}
static unsigned char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = (size_t) ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n ? *n : 1);
    if (fread(b, 1, *n, f) != *n) abort();
    fclose(f);
    return b;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (n < 3 || n > (1 << 18)) return 0;
    static const unsigned pls[] = {512, 1024, 4096, 512};
    unsigned pl = pls[d[0] & 3];
    int with_src = !(d[0] & 4);
    size_t dl = d[1] | (size_t) d[2] << 8;
    d += 3;
    n -= 3;
    if (dl > n) dl = n;
    if (!g_root[0]) {
        snprintf(g_root, sizeof g_root, "%s/dst", fz_dir());
        snprintf(g_src, sizeof g_src, "%s/src", fz_dir());
    }
    put_pdb(g_root, d, dl, pl);
    put_pdb(g_src, d + dl, n - dl, pl);
    static const struct pdbs_pl lists[] = {{1, 0, 0, 0, "Warmup"}, {2, 0, 1, 1, "Folder"}, {3, 2, 0, 0, "Peak \xc3\xa9"}};
    static const struct pdbs_item items[] = {{1, "/Contents/a.mp3"}, {1, "/Contents/b.mp3"}, {3, "/Contents/b.mp3"}, {3, "/Contents/c.mp3"}};
    static const struct pdbs_track tracks[] = {{"/Contents/a.mp3", "/PIONEER/Artwork/00001/a1.jpg"}, {"/Contents/b.mp3", NULL}, {"/Contents/c.mp3", ""}};
    char path[600];
    snprintf(path, sizeof path, "%s/PIONEER/rekordbox/export.pdb", g_root);
    size_t on = 0, an = 0;
    unsigned char *before = slurp(path, &on);
    FILE *lg = getenv("FZ_PDB_LOG") ? stderr : NULL;
    int rc = pdbs_sync(g_root, with_src ? g_src : NULL, lists, 3, items, 4, tracks, 3, lg);
    unsigned char *after = slurp(path, &an);
    if (rc != 0) FZ_ASSERT(after && an == on && !memcmp(before, after, on)); /* untouched / put back */
    if (getenv("FZ_PDB_LOG")) fprintf(stderr, "pdbs_sync rc=%d\n", rc);
    free(before);
    free(after);
    return 0;
}
