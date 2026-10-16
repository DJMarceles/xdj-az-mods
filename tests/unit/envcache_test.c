/* SPDX-License-Identifier: GPL-3.0-only */
/* The stems' envelope cache, <stems folder>/azstem.env (env_cache_save / env_cache_load,
 * extracted out of guest/azstem/stem_envelopes.c by run.sh). It sits on the stick, next to the
 * stems, and is written while the deck plays, so:
 *   - saved and loaded back: the same envelope and frame count, published;
 *   - a cut file (any length short of whole), a wrong magic or shape, no frames, or a frame
 *     count no track can have: rejected, nothing published;
 *   - a save that stops short (a full or pulled stick) leaves the previous cache whole and no
 *     temporary file behind.
 * argv[1] = a scratch directory. */
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"

#define NDECK 4
#define NSTEM 4
#define AZW_N 65536
static unsigned char g_env[NDECK][NSTEM][AZW_N];
static long g_env_frames[NDECK];
static atomic_int g_env_final[NDECK], g_env_ready[NDECK], g_env_gen[NDECK];
static void logln(const char *fmt, ...) { (void) fmt; }
#include ENVCACHE_INC

static char g_path[1200];
static long fsize(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 ? (long) st.st_size : -1;
}
static void fill(int deck, unsigned seed) {
    for (int k = 0; k < NSTEM; k++)
        for (int i = 0; i < AZW_N; i++)
            g_env[deck][k][i] = (unsigned char) (seed + k * 7 + i * 13);
}
/* the cache file cut to n bytes */
static void cut(const char *whole, long n) {
    FILE *in = fopen(whole, "rb"), *out = fopen(g_path, "wb");
    char b[4096];
    long left = n;
    while (left > 0) {
        size_t r = fread(b, 1, left < (long) sizeof b ? (size_t) left : sizeof b, in);
        if (!r)
            break;
        fwrite(b, 1, r, out);
        left -= (long) r;
    }
    fclose(in);
    fclose(out);
}
/* a cache file whose frame count is fr */
static void with_frames(const char *whole, long fr) {
    cut(whole, fsize(whole));
    FILE *f = fopen(g_path, "r+b");
    fseek(f, (long) (sizeof ENV_MAGIC - 1) + 3 * (long) sizeof(unsigned), SEEK_SET);
    fwrite(&fr, sizeof fr, 1, f);
    fclose(f);
}
static int load_fresh(int deck) {
    memset(g_env[deck], 0, sizeof g_env[deck]);
    g_env_frames[deck] = 0;
    atomic_store(&g_env_ready[deck], 0);
    return env_cache_load(deck);
}

int main(int argc, char **argv) {
    ut_suite = "envelope cache";
    if (argc < 2)
        return 2;
    mkdir(argv[1], 0755);
    snprintf(g_env_dir[1], sizeof g_env_dir[1], "%s", argv[1]);
    env_cache_path(1, g_path, sizeof g_path);
    char whole[1300], tmp[1300];
    snprintf(whole, sizeof whole, "%s/whole.env", argv[1]);
    snprintf(tmp, sizeof tmp, "%s.tmp", g_path);

    fill(1, 3);
    g_env_frames[1] = 44100L * 300;
    env_cache_save(1);
    long size = fsize(g_path);
    CHECK("saved: the file holds the header, the frame count and the whole envelope",
          size == (long) (sizeof ENV_MAGIC - 1 + 3 * sizeof(unsigned) + sizeof(long) + sizeof g_env[1]));
    CHECK("no temporary file is left", access(tmp, F_OK) != 0);
    unsigned char want[NSTEM][AZW_N];
    memcpy(want, g_env[1], sizeof want);
    rename(g_path, whole); /* the good file, kept; each case below writes its own copy */
    cut(whole, size);
    int gen = atomic_load(&g_env_gen[1]);
    CHECK("loaded back: 1", load_fresh(1) == 1);
    CHECK("... the same envelope and frame count", !memcmp(want, g_env[1], sizeof want) && g_env_frames[1] == 44100L * 300);
    CHECK("... published (ready, final, a new generation)",
          atomic_load(&g_env_ready[1]) && atomic_load(&g_env_final[1]) && atomic_load(&g_env_gen[1]) == gen + 1);

    /* a cut file: every length short of whole is rejected */
    long cuts[] = {0, 1, 6, 7, 10, 19, 27, 28, 1000, size / 2, size - 1};
    int bad = 0;
    for (unsigned i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
        cut(whole, cuts[i]);
        bad += load_fresh(1) != 0 || atomic_load(&g_env_ready[1]);
    }
    CHECK("a file cut at any length short of whole is rejected, nothing published", bad == 0);
    with_frames(whole, 0);
    CHECK("no frames: rejected", load_fresh(1) == 0);
    with_frames(whole, -5);
    CHECK("a negative frame count: rejected", load_fresh(1) == 0);
    with_frames(whole, 0x7fffffffffffffffL);
    CHECK("a frame count no track can have: rejected", load_fresh(1) == 0);
    with_frames(whole, 44100L * 3600);
    CHECK("an hour-long track's frame count is taken (CONTROL for the bound)", load_fresh(1) == 1);
    cut(whole, size);
    {
        FILE *f = fopen(g_path, "r+b");
        fputc('X', f);
        fclose(f);
    }
    CHECK("a wrong magic: rejected", load_fresh(1) == 0);
    cut(whole, size);
    {
        FILE *f = fopen(g_path, "r+b");
        unsigned four = 5;
        fseek(f, (long) (sizeof ENV_MAGIC - 1), SEEK_SET);
        fwrite(&four, sizeof four, 1, f);
        fclose(f);
    }
    CHECK("another stem count: rejected", load_fresh(1) == 0);

    /* a save that stops short keeps the previous cache */
    cut(whole, size);
    fill(1, 99);
    g_env_frames[1] = 44100L * 10;
    signal(SIGXFSZ, SIG_IGN);
    struct rlimit old, lim;
    getrlimit(RLIMIT_FSIZE, &old);
    lim = old;
    lim.rlim_cur = 4096;
    setrlimit(RLIMIT_FSIZE, &lim);
    env_cache_save(1);
    setrlimit(RLIMIT_FSIZE, &old);
    CHECK("a save that stops short leaves the previous cache whole", fsize(g_path) == size && load_fresh(1) == 1 &&
                                                                         !memcmp(want, g_env[1], sizeof want));
    CHECK("... and no temporary file", access(tmp, F_OK) != 0);
    g_env_dir[2][0] = 0;
    CHECK("a deck with no stems folder: nothing loaded", env_cache_load(2) == 0);
    return ut_summary();
}
