/* SPDX-License-Identifier: GPL-3.0-only */
/* The community SDK's library services (guest/azmod/sdk_lib.c, #included by
 * tools/unit-tests/sdklib_test.c, which this harness reuses for its stand-ins): what they make of
 * the rows the stick's exportLibrary.db returns, and of the files they parse.
 *
 * The SQLite API is a fake driven by the input, so a row can hold anything the real one can
 * return: NULL text, text of any length (no NUL inside a column, as SQLite gives it), any
 * integer, a step that fails part way, and more rows than SDK_LIB_ROWS.
 * Input byte 0 picks the path:
 *   0: playlists, 1: tracks (of every track, or one playlist): a stream of rows;
 *   2: sdk_port_of_sysfs over a sysfs path;
 *   3: a /proc/mounts file for sdk_stick_mount;
 *   4: a deck's published source file for sdk_deck_source. */
#define main sdklib_test_main
#include "sdklib_test.c"
#undef main
#include "fuzz_common.h"

static const uint8_t *g_in;
static size_t g_in_n, g_in_off;
static unsigned g_repeat, g_rows_left, g_end_code;
static unsigned char g_txt[8][1100];
static int g_txt_null[8];
static long long g_int[8];
static uint8_t take(void) { return g_in_off < g_in_n ? g_in[g_in_off++] : 0; }
static int f_open(const char *u, void **h, int fl, const char *v) {
    (void) u, (void) fl, (void) v;
    *h = (void *) &g_in;
    return 0;
}
static int f_key(void *h, const void *k, int n) {
    (void) h, (void) k, (void) n;
    return 0;
}
static int f_close(void *h) {
    (void) h;
    return 0;
}
static int f_prepare(void *h, const char *q, int n, void **st, const char **tail) {
    (void) h, (void) q, (void) n, (void) tail;
    *st = (void *) &g_in_off;
    return 0;
}
static int f_bind(void *st, int i, long long v) {
    (void) st, (void) i, (void) v;
    return 0;
}
static void next_row(void) { /* one row: per column a kind byte, then its data */
    for (int c = 0; c < 8; c++) {
        uint8_t kind = take();
        g_txt_null[c] = kind % 3 == 0;
        size_t len = kind % 3 == 2 ? (size_t) take() * 4 + take() : take() % 32;
        if (len > sizeof g_txt[c] - 1) len = sizeof g_txt[c] - 1;
        size_t k = 0;
        for (; k < len; k++) {
            uint8_t ch = take();
            g_txt[c][k] = ch ? ch : 'x';
        }
        g_txt[c][k] = 0;
        g_int[c] = (long long) ((uint64_t) take() << 56 | (uint64_t) take() << 8 | take());
    }
}
static int f_step(void *st) {
    (void) st;
    if (g_rows_left) {
        g_rows_left--;
        return 100;
    }
    if (g_in_off < g_in_n && take() & 1) {
        next_row();
        g_rows_left = g_repeat; /* the same row again, to reach the row cap cheaply */
        return 100;
    }
    return (int) g_end_code;
}
static long long f_int(void *st, int c) {
    (void) st;
    return c >= 0 && c < 8 ? g_int[c] : 0;
}
static const unsigned char *f_text(void *st, int c) {
    (void) st;
    return c >= 0 && c < 8 && !g_txt_null[c] ? g_txt[c] : NULL;
}
static int f_finalize(void *st) {
    (void) st;
    return 0;
}

static char *dupz(const uint8_t *d, size_t n) {
    char *s = malloc(n + 1);
    memcpy(s, d, n);
    s[n] = 0;
    return s;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (n < 3) return 0;
    uint8_t mode = d[0] % 5;
    if (mode <= 1) {
        g_sdk_sq.open_v2 = f_open, g_sdk_sq.key = f_key, g_sdk_sq.close = f_close;
        g_sdk_sq.prepare = f_prepare, g_sdk_sq.bind_int64 = f_bind, g_sdk_sq.step = f_step;
        g_sdk_sq.col_int64 = f_int, g_sdk_sq.col_text = f_text, g_sdk_sq.finalize = f_finalize;
        g_sdk_sq.ok = 1;
        g_repeat = (unsigned) d[1] * 40; /* up to ~10000 copies of a row */
        g_end_code = d[2] & 1 ? 101 /* SQLITE_DONE */ : 1 /* SQLITE_ERROR */;
        g_in = d + 3, g_in_n = n - 3, g_in_off = 0, g_rows_left = 0;
        memset(g_txt_null, 0, sizeof g_txt_null);
        int k = -1, rc;
        if (mode == 0) {
            struct sdk_pl *v = NULL;
            rc = sdk_read_playlists("/fz/exportLibrary.db", &v, &k);
            if (rc >= 0)
                for (int i = 0; i < k; i++) FZ_ASSERT(strlen(v[i].name) < SDK_LIB_STR);
            free(v);
        } else {
            struct sdk_tr *v = NULL;
            rc = sdk_read_tracks("/fz/exportLibrary.db", d[1] & 1 ? 7 : 0, &v, &k);
            if (rc >= 0)
                for (int i = 0; i < k; i++)
                    FZ_ASSERT(strlen(v[i].title) < SDK_LIB_STR && strlen(v[i].path) < SDK_LIB_STR &&
                              strlen(v[i].key) < sizeof v[i].key);
            free(v);
        }
        FZ_ASSERT(rc == -1 || (k >= 0 && k <= SDK_LIB_ROWS && (rc != 1 || k == SDK_LIB_ROWS))); /* cut only at the cap */
        return 0;
    }
    d++, n--;
    if (mode == 2) {
        char *s = dupz(d, n);
        int p = sdk_port_of_sysfs(s);
        FZ_ASSERT(p >= 0 && p <= 2);
        free(s);
    } else if (mode == 3) {
        snprintf(g_mounts, sizeof g_mounts, "%s", fz_file("mounts", d, n));
        snprintf(g_media, sizeof g_media, "/media");
        snprintf(g_sysblock, sizeof g_sysblock, "%s/nosys", fz_dir());
        char mount[PATH_MAX];
        (void) sdk_stick_mount(d[0] & 1, mount, sizeof mount);
    } else {
        snprintf(g_cursrc, sizeof g_cursrc, "%s", fz_dir());
        fz_file("azstem.cursrc2", d, n);
        char out[256];
        sdk_deck_source(1, out, sizeof out);
        FZ_ASSERT(strlen(out) < sizeof out && !strchr(out, '\n'));
    }
    return 0;
}
