/* SPDX-License-Identifier: GPL-3.0-only */
/* A track's hot cues from its analysis file on the stick (guest/azmod/hc_anlz.h), read by the
 * Preview Hot Cues worker from any ANLZ file a library row points at.
 *
 * The input is the file. The result is -1 or a count up to HC_MAX, in strictly ascending
 * slots 1..8, and every cue maps to a pixel inside a preview (or to none) for any width and
 * any track length taken from the input's first bytes. Reading only the cue sections
 * (hc_anlz_pick) gives the same result as parsing the whole input. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fuzz_common.h"
#include HCANLZ_HDR

struct fz_mem {
    const uint8_t *d;
    size_t n;
};
static int fz_read(void *ctx, uint8_t *b, size_t n, uint64_t off) {
    const struct fz_mem *m = (const struct fz_mem *) ctx;
    if (off > m->n || n > m->n - off)
        return 0;
    memcpy(b, m->d + off, n);
    return 1;
}
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    struct hc_cue c[HC_MAX];
    memset(c, 0xa5, sizeof c);
    int k = hc_anlz_hot_cues(d, n, c);
    FZ_ASSERT(k >= -1 && k <= HC_MAX);
    for (int i = 0; i < k; i++) {
        FZ_ASSERT(c[i].slot >= 1 && c[i].slot <= HC_MAX);
        FZ_ASSERT(i == 0 || c[i].slot > c[i - 1].slot);
        FZ_ASSERT(c[i].rgb <= 0xffffffu);
    }
    {
        struct fz_mem m = {d, n};
        struct hc_cue p[HC_MAX];
        size_t pn = 0;
        uint8_t *pb = hc_anlz_pick(fz_read, &m, n, 1 << 24, &pn);
        int kp = -1;
        if (pb) {
            FZ_ASSERT(pn <= n);
            kp = hc_anlz_hot_cues(pb, pn, p);
            free(pb);
        }
        FZ_ASSERT(kp == k);
        for (int i = 0; i < k; i++)
            FZ_ASSERT(p[i].slot == c[i].slot && p[i].ms == c[i].ms && p[i].rgb == c[i].rgb);
    }
    uint32_t len = n >= 4 ? (uint32_t) d[0] << 24 | (uint32_t) d[1] << 16 | (uint32_t) d[2] << 8 | d[3] : 0;
    int w = n >= 6 ? (int) (d[4] << 8 | d[5]) - 16 : 0;
    for (int i = 0; i < k; i++) {
        int x = hc_x(c[i].ms, len, w);
        FZ_ASSERT(x == -1 || (x >= 0 && x < w));
    }
    return 0;
}
