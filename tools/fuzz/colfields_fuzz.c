/* SPDX-License-Identifier: GPL-3.0-only */
/* Column Edit's saved fields parser (guest/azmod/browse_fields.h b2_fields_parse):
 * settings/azmod-colfields on the deck, so any bytes as far as the parser is concerned. The same
 * bytes, as a library text, go through b2_clean_text and b2_format.
 *
 * A rejected file sets nothing. An accepted one sets only ids 1..B2_MAXID, each to a field other
 * than DEFAULT and TRACK, as many as it reports; writing it out and reading it back gives the
 * same fields. A cleaned text is valid UTF-8 without control characters and fits its buffer. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fuzz_common.h"
#include "browse_fields.h"

static int fz_clean_ok(const char *s, size_t cap) {
    size_t n = strlen(s);
    if (n >= cap || n >= B2_TEXT_MAX)
        return 0;
    for (size_t i = 0; i < n;) {
        size_t k = b2_utf8_len((const unsigned char *) s + i, n - i);
        if (!k || (k == 1 && ((unsigned char) s[i] < 0x20 || s[i] == 0x7f)))
            return 0;
        i += k;
    }
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    unsigned char f[B2_MAXID + 1], r[B2_MAXID + 1];
    int rc = b2_fields_parse((const char *) d, n, f);
    int set = 0;
    for (int i = 0; i <= B2_MAXID; i++) {
        if (rc < 0)
            FZ_ASSERT(f[i] == B2_NONE);
        else if (f[i] != B2_NONE) {
            FZ_ASSERT(i >= 1 && f[i] > B2_TRACK && f[i] < B2_N);
            set++;
        }
    }
    if (rc >= 0) {
        FZ_ASSERT(rc == set);
        char buf[B2_FILE_MAX];
        int len = b2_fields_format(f, buf, sizeof buf);
        FZ_ASSERT(len >= 0);
        FZ_ASSERT(b2_fields_parse(buf, (size_t) len, r) == rc && !memcmp(r, f, sizeof f));
    }
    char *s = malloc(n + 1);
    if (!s)
        return 0;
    memcpy(s, d, n);
    s[n] = 0;
    char out[B2_TEXT_MAX + 8];
    size_t cap = n % (sizeof out) + 1;
    b2_clean_text(s, out, cap);
    FZ_ASSERT(fz_clean_ok(out, cap));
    int kind = 0;
    for (int fld = B2_TRACK; fld < B2_N; fld++) {
        FZ_ASSERT(b2_format(fld, n & 1, (long long) n * 37 - 500, s, out, sizeof out, &kind) == 1);
        FZ_ASSERT(fz_clean_ok(out, sizeof out));
        FZ_ASSERT(kind == 0 || (kind > B2_KIND_COLOR0 && kind <= B2_KIND_COLOR0 + B2_COLOR_MAX));
    }
    free(s);
    return 0;
}
