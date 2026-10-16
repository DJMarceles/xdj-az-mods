/* SPDX-License-Identifier: GPL-3.0-only */
/* Names copied into a fixed buffer are cut at a character boundary (guest/azmod/utf8_cut.h):
 * a byte-wise cut can end inside a UTF-8 sequence, and a playlist created or renamed with that
 * name is ill-formed UTF-8 in the stick's library. Covered: utf8_copy (paste, rename, create,
 * the add target), sq_text (a name read from the library, extracted from library_db.c by
 * run.sh) and own_str (a name read from EP147's memory, ep_mem.h, extracted likewise).
 * Control (run.sh): utf8_cut made a no-op, which is the byte-wise cut of the old code. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include UTF8_INC
#include OWNSTR_INC /* own_str, extracted from ep_mem.h (the rest of it is Linux-only) */

#define SQLITE_ROW 100
static const char *g_row;
static int f_prepare(void *c, const char *sql, int n, void **st, const char **tail) {
    (void) c, (void) sql, (void) n, (void) tail;
    *st = (void *) 1;
    return 0;
}
static int f_bind(void *st, int i, long long v) { return (void) st, (void) i, (void) v, 0; }
static int f_step(void *st) { return (void) st, SQLITE_ROW; }
static const unsigned char *f_text(void *st, int c) { return (void) st, (void) c, (const unsigned char *) g_row; }
static int f_finalize(void *st) { return (void) st, 0; }
static struct {
    int (*prepare)(void *, const char *, int, void **, const char **);
    int (*bind_int64)(void *, int, long long);
    int (*step)(void *);
    const unsigned char *(*column_text)(void *, int);
    int (*finalize)(void *);
} g_sq = {f_prepare, f_bind, f_step, f_text, f_finalize};
#include SQTEXT_INC

/* 1 = well-formed UTF-8 */
static int wf(const char *s) {
    const unsigned char *u = (const unsigned char *) s;
    while (*u) {
        int n = *u < 0x80 ? 1 : (*u & 0xe0) == 0xc0 ? 2
                            : (*u & 0xf0) == 0xe0   ? 3
                            : (*u & 0xf8) == 0xf0   ? 4
                                                    : 0;
        if (!n)
            return 0;
        for (int i = 1; i < n; i++)
            if ((u[i] & 0xc0) != 0x80)
                return 0;
        u += n;
    }
    return 1;
}

int main(void) {
    ut_suite = "utf8 cut";
    char cjk[200] = "", emoji[200] = "", out[96], want[96];
    for (int i = 0; i < 40; i++)
        strcat(cjk, "\xe6\x97\xa5"); /* 120 bytes of 日: a 96-byte buffer cuts inside one */
    strcpy(emoji, "Set ");
    for (int i = 0; i < 30; i++)
        strcat(emoji, "\xf0\x9f\x94\xa5"); /* 4-byte characters */
    utf8_copy(out, sizeof out, cjk);
    CHECK("utf8_copy: a CJK name cut at its last whole character (93 bytes)", strlen(out) == 93 && wf(out));
    utf8_copy(out, sizeof out, emoji);
    CHECK("utf8_copy: 4-byte characters cut whole", wf(out) && strlen(out) == 4 + 4 * 22);
    utf8_copy(out, sizeof out, "Plain name");
    CHECK("utf8_copy: a name that fits is copied as is", !strcmp(out, "Plain name"));
    snprintf(want, sizeof want, "%.93s", cjk);
    utf8_copy(out, 94, cjk); /* ends exactly on a boundary */
    CHECK("utf8_copy: a cut that falls on a boundary keeps every character", !strcmp(out, want));
    g_row = cjk;
    CHECK("sq_text: a library name longer than the buffer is cut at a character boundary",
          sq_text(0, "SELECT name", 1, out, sizeof out) && strlen(out) == 93 && wf(out));
    g_row = "Short";
    CHECK("sq_text: a short name is read whole", sq_text(0, "SELECT name", 1, out, sizeof out) && !strcmp(out, "Short"));
    char *heap = strdup(cjk);
    CHECK("own_str: a name read from memory is cut at a character boundary",
          own_str((uintptr_t) heap, out, (int) sizeof out) && strlen(out) == 93 && wf(out));
    free(heap);
    return ut_summary();
}
