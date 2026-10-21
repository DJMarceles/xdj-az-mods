/* SPDX-License-Identifier: GPL-3.0-only */
/* The community icon PNG gate (cmi_png_ok, guest/azmod/community_icons.c, extracted by
 * tools/fuzz/lib.sh as the png unit suite extracts it): what may reach EP147's PNG decoder from
 * a repository's icon, compacted in place.
 *
 * Input byte 0: bit 0 set = every chunk's CRC is recomputed before the gate (so the fuzzer
 * reaches the chunk rules instead of stopping at the CRC); the rest is the file. An accepted
 * PNG must fit where it was, start with the signature and IHDR, end with IEND, and pass the
 * gate again unchanged (the compaction is a fixed point). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fuzz_common.h"
#include PNG_INC

static void fix_crcs(unsigned char *b, size_t n) {
    size_t off = 8;
    while (off + 12 <= n) {
        uint32_t len = cmi_be32(b + off);
        if (len > n - off - 12) return;
        uint32_t c = cmi_crc(b + off + 4, len + 4);
        unsigned char *p = b + off + 8 + len;
        p[0] = (unsigned char) (c >> 24), p[1] = (unsigned char) (c >> 16), p[2] = (unsigned char) (c >> 8), p[3] = (unsigned char) c;
        off += 12 + len;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (n < 1 || n > (1 << 20)) return 0;
    int fix = d[0] & 1;
    d++;
    n--;
    unsigned char *b = malloc(n ? n : 1);
    memcpy(b, d, n);
    if (fix) fix_crcs(b, n);
    /* the screenshot limits (1280 x 800) on a copy: never past n, and every icon it takes passes them too */
    unsigned char *shot = malloc(n ? n : 1);
    memcpy(shot, b, n);
    int rs = cmi_png_fit(shot, (int) n, 1280, 800);
    FZ_ASSERT(rs >= 0 && (size_t) rs <= n);
    int r = cmi_png_ok(b, (int) n);
    FZ_ASSERT(!r || (rs == r && !memcmp(shot, b, (size_t) r)));
    free(shot);
    FZ_ASSERT(r >= 0 && (size_t) r <= n);
    if (r) {
        FZ_ASSERT(r >= 8 + 25 + 12);
        FZ_ASSERT(!memcmp(b + 12, "IHDR", 4) && !memcmp(b + r - 8, "IEND", 4));
        unsigned char *again = malloc((size_t) r);
        memcpy(again, b, (size_t) r);
        FZ_ASSERT(cmi_png_ok(again, r) == r && !memcmp(again, b, (size_t) r));
        free(again);
    }
    free(b);
    return 0;
}
