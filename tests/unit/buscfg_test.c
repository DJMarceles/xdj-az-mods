/* SPDX-License-Identifier: GPL-3.0-only */
/* What the overlay thread hands the panel-bus threads (guest/azmod/bus_cfg.h, and
 * bus_cfg_publish from mod_menu_overlay.c, extracted by run.sh into PUBLISH_INC):
 *   1. bus_cfg_pack / bus_cfg_xpad round-trip every field, and the defaults word decodes to
 *      the measured x-pad bytes;
 *   2. a writer thread rewrites the Mod Menu inputs field by field (as xpad_load_config and the
 *      menu do) and publishes after each full config; a reader taking bus_cfg() never sees a
 *      combination that was not published (A or B, never a mix);
 *   3. the same reader reading the source globals directly (what read() did before) does see
 *      mixes: the in-round control that proves step 2's probe can see the other answer;
 *   4. a 32-byte LED block through seqblk is always one whole published block: first pinned,
 *      single-threaded (a whole seqblk_write lands between the reader's loads of words 1 and 2),
 *      then a writer thread against a reader (the block is published once before the reader
 *      starts, so "torn" means a mix of two blocks).
 * Each concurrent case runs for RUN_MS, then longer only until it has what it needs (the word
 * seen changing MIN_CHANGES times; the direct reader's first mix), up to CAP_MS. The reader
 * sees a change only when the two threads interleave, which on a loaded machine (time-sliced
 * on one core) can be a few dozen times in RUN_MS: the run waits for them instead of failing. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* Test hook, harness only: every __atomic_load_n below (bus_cfg.h's included) first calls
 * ut_before_load, which can run one injected call just before the load of one chosen address.
 * The pinned seqblk case uses it to put the writer between two of the reader's loads. Inside
 * the macro the name is not expanded again, so it is the compiler's builtin. Production is
 * unchanged; with nothing armed it is a plain load. */
static const volatile void *ut_inject_at;
static void (*ut_inject)(void);
static inline void ut_before_load(const volatile void *p) {
    if (ut_inject && p == ut_inject_at) {
        void (*f)(void) = ut_inject;
        ut_inject = 0;
        f();
    }
}
#define __atomic_load_n(p, o) (ut_before_load(p), __atomic_load_n((p), (o)))
#include "bus_cfg.h"

/* the inputs bus_cfg_publish reads, as azmod has them (azfeatures.h: one int per row, one
 * relaxed atomic load each) */
enum {
    AZF_PLAYLIST_EDITING = 3,
    AZF_STEM_PADS_PAGE = 7,
    AZF_TEST_COUNT = 40
};
static int az_val[AZF_TEST_COUNT];
static inline bool az_on(int id) { return __atomic_load_n(&az_val[id], __ATOMIC_RELAXED) != 0; }
static int g_xpad_pos_off = XPAD_DEFAULT_POS, g_xpad_touch_off = -1, g_xpad_span = XPAD_DEFAULT_SPAN,
           g_xpad_zone_off = XPAD_DEFAULT_ZONE;
static unsigned g_xpad_touch_msk = 0xFF;
static int g_reorder_hooks, g_reorder_forced_off;
static uint64_t g_bus_cfg = BC_DEFAULT;
static inline uint64_t bus_cfg(void) { return __atomic_load_n(&g_bus_cfg, __ATOMIC_ACQUIRE); }
#include PUBLISH_INC

#define MIN_CHANGES 100
#define CAP_MS 60000
#ifndef RUN_MS
#define RUN_MS 1500
#endif
static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}
static int pass, fail;
static void check(const char *what, int ok) {
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) pass++;
    else fail++;
}

/* two full configurations that differ in every field */
struct cfg {
    int pledit, bl2, hooks, off;
    struct bus_xpad x;
};
static const struct cfg A = {1, 0, 1, 0, {0x1d, -1, 0xff, 128, 0x1e}};
static const struct cfg B = {0, 1, 0, 1, {0x40, 0x22, 0x0f, 64, -1}};
static int same(const struct cfg *c, int pledit, int bl2, int hooks, int off, const struct bus_xpad *x) {
    return c->pledit == pledit && c->bl2 == bl2 && c->hooks == hooks && c->off == off && c->x.pos == x->pos &&
           c->x.touch == x->touch && c->x.mask == x->mask && c->x.span == x->span && c->x.zone == x->zone;
}
static _Atomic int stop; /* the writers poll it: atomic, so the stop is a synchronized store */
static void set_fields(const struct cfg *c) { /* one store at a time, like the real writers */
    __atomic_store_n(&az_val[AZF_PLAYLIST_EDITING], c->pledit, __ATOMIC_RELAXED);
    __atomic_store_n(&g_xpad_pos_off, c->x.pos, __ATOMIC_RELAXED);
    __atomic_store_n(&az_val[AZF_STEM_PADS_PAGE], c->bl2, __ATOMIC_RELAXED);
    __atomic_store_n(&g_xpad_touch_off, c->x.touch, __ATOMIC_RELAXED);
    __atomic_store_n(&g_reorder_hooks, c->hooks, __ATOMIC_RELAXED);
    __atomic_store_n(&g_xpad_touch_msk, (unsigned) c->x.mask, __ATOMIC_RELAXED);
    __atomic_store_n(&g_reorder_forced_off, c->off, __ATOMIC_RELAXED);
    __atomic_store_n(&g_xpad_span, c->x.span, __ATOMIC_RELAXED);
    __atomic_store_n(&g_xpad_zone_off, c->x.zone, __ATOMIC_RELAXED);
}
static void *writer(void *a) {
    (void) a;
    for (unsigned long i = 0; !stop; i++) {
        set_fields(i & 1 ? &B : &A);
        bus_cfg_publish();
    }
    return 0;
}
static long reads, mixes, changes; /* changes: the word moved under the reader */
static void *reader_word(void *a) {
    (void) a;
    long t0 = now_ms();
    uint64_t prev = 0;
    while (now_ms() - t0 < CAP_MS && (now_ms() - t0 < RUN_MS || changes < MIN_CHANGES)) {
        for (int k = 0; k < 1000; k++) {
            uint64_t c = bus_cfg();
            changes += c != prev;
            prev = c;
            struct bus_xpad x;
            bus_cfg_xpad(c, &x);
            int pl = !!(c & BC_PLEDIT), b2 = !!(c & BC_STEMSBL2), h = !!(c & BC_REORDER_HOOKS),
                o = !!(c & BC_REORDER_OFF);
            if (c != BC_DEFAULT && !same(&A, pl, b2, h, o, &x) && !same(&B, pl, b2, h, o, &x))
                mixes++;
            reads++;
        }
    }
    return 0;
}
static void *reader_direct(void *a) { /* the old read(): the source globals, one by one */
    (void) a;
    long t_end = now_ms() + CAP_MS; /* ends at the first mix */
    while (now_ms() < t_end && !mixes) {
        for (int k = 0; k < 1000; k++) {
            struct bus_xpad x;
            int pl = __atomic_load_n(&az_val[AZF_PLAYLIST_EDITING], __ATOMIC_RELAXED);
            x.pos = __atomic_load_n(&g_xpad_pos_off, __ATOMIC_RELAXED);
            int b2 = __atomic_load_n(&az_val[AZF_STEM_PADS_PAGE], __ATOMIC_RELAXED);
            x.touch = __atomic_load_n(&g_xpad_touch_off, __ATOMIC_RELAXED);
            int h = __atomic_load_n(&g_reorder_hooks, __ATOMIC_RELAXED);
            x.mask = (int) __atomic_load_n(&g_xpad_touch_msk, __ATOMIC_RELAXED);
            int o = __atomic_load_n(&g_reorder_forced_off, __ATOMIC_RELAXED);
            x.span = __atomic_load_n(&g_xpad_span, __ATOMIC_RELAXED);
            x.zone = __atomic_load_n(&g_xpad_zone_off, __ATOMIC_RELAXED);
            if (!same(&A, pl, b2, h, o, &x) && !same(&B, pl, b2, h, o, &x))
                mixes++;
            reads++;
        }
    }
    return 0;
}
static long race(void *(*rd)(void *) ) {
    pthread_t w, r;
    stop = 0;
    reads = 0;
    mixes = 0;
    changes = 0;
    set_fields(&A);
    bus_cfg_publish();
    pthread_create(&w, 0, writer, 0);
    pthread_create(&r, 0, rd, 0);
    pthread_join(r, 0);
    stop = 1;
    pthread_join(w, 0);
    return mixes;
}

static struct seqblk blk;
static long bl_reads, bl_ok, bl_torn;
static void write_22(void) { /* the overlay thread's whole write, injected */
    unsigned char b[32];
    memset(b, 0x22, sizeof b);
    seqblk_write(&blk, b);
}
static void *blk_writer(void *a) {
    (void) a;
    unsigned char b[32];
    for (unsigned long i = 0; !stop; i++) {
        memset(b, i & 1 ? 0x22 : 0x11, sizeof b);
        seqblk_write(&blk, b);
    }
    return 0;
}
static void *blk_reader(void *a) {
    (void) a;
    long t_end = now_ms() + RUN_MS;
    while (now_ms() < t_end) {
        for (int k = 0; k < 1000; k++) {
            unsigned char d[32];
            memset(d, 0, sizeof d);
            bl_reads++;
            if (!seqblk_read(&blk, d))
                continue;
            bl_ok++;
            for (int i = 1; i < 32; i++)
                if (d[i] != d[0] || (d[0] != 0x11 && d[0] != 0x22)) {
                    bl_torn++;
                    break;
                }
        }
    }
    return 0;
}

int main(void) {
    char m[200];
    /* 1. round trip */
    int rt = 1;
    for (int pos = 0; pos < 128; pos += 7)
        for (int t = -1; t < 128; t += 19)
            for (int span = 2; span <= 256; span += 37)
                for (int z = -1; z < 128; z += 23) {
                    struct bus_xpad x = {pos, t, 0x5a, span, z}, y;
                    uint64_t c = bus_cfg_pack(pos & 1, t & 1, z & 1, span & 1, &x);
                    bus_cfg_xpad(c, &y);
                    if (memcmp(&x, &y, sizeof x) || !!(c & BC_PLEDIT) != (pos & 1) ||
                        !!(c & BC_STEMSBL2) != (t & 1) || !(c & BC_VALID))
                        rt = 0;
                }
    check("pack/unpack: every x-pad field and mod bit round-trips", rt);
    struct bus_xpad d;
    bus_cfg_xpad(BC_DEFAULT, &d);
    check("the default word decodes to the measured x-pad bytes (0x1d, none, 0xff, 128, 0x1e)",
          d.pos == 0x1d && d.touch == -1 && d.mask == 0xff && d.span == 128 && d.zone == 0x1e &&
              !(BC_DEFAULT & (BC_PLEDIT | BC_STEMSBL2)));
    check("reorder live only with the mod on, its hooks in and none declined",
          bus_cfg_reorder(BC_PLEDIT | BC_REORDER_HOOKS) && !bus_cfg_reorder(BC_PLEDIT) &&
              !bus_cfg_reorder(BC_REORDER_HOOKS) &&
              !bus_cfg_reorder(BC_PLEDIT | BC_REORDER_HOOKS | BC_REORDER_OFF));
    check("a pad color word: set bit and 24-bit color, 0 when unset",
          bus_rgb_pack(1, 0x123456) == (BC_RGB_SET | 0x123456) && bus_rgb_pack(0, 0x123456) == 0);
    /* 2. + 3. */
    long mx = race(reader_word);
    snprintf(m, sizeof m, "bus_cfg(): %ld reads, the word changed %ld times under them, %ld mixed combinations",
             reads, changes, mx);
    check(m, reads > 100000 && changes >= MIN_CHANGES && mx == 0);
    mx = race(reader_direct);
    snprintf(m, sizeof m, "control in the same run: reading the globals directly (the old read()) sees mixes (%ld in %ld reads)", mx, reads);
    check(m, mx > 0);
    /* 4. pinned: the reader has words 0 and 1 of block 0x11 when a whole 0x22 block is written */
    unsigned char b11[32], got[32];
    memset(b11, 0x11, sizeof b11);
    seqblk_write(&blk, b11);
    memset(got, 0, sizeof got);
    ut_inject_at = &blk.w[2];
    ut_inject = write_22;
    int rr = seqblk_read(&blk, got);
    int whole = got[0] == 0x11 || got[0] == 0x22;
    for (int i = 1; i < 32; i++)
        whole &= got[i] == got[0];
    check("seqblk pinned: the writer came in between the reader's words 1 and 2", !ut_inject);
    snprintf(m, sizeof m, "seqblk pinned: a block rewritten under the reader is not served torn (read %d, bytes 0x%02x..0x%02x)",
             rr, got[0], got[31]);
    check(m, rr == 0 || whole);
    ut_inject = 0;
    /* Publish one whole block first, as race() publishes A: the zero-filled block that has
     * never been written is not one of the two the check accepts, and a reader scheduled before
     * the writer's first store would count it as torn. */
    pthread_t w, r;
    unsigned char b0[32];
    memset(b0, 0x11, sizeof b0);
    seqblk_write(&blk, b0);
    stop = 0;
    pthread_create(&w, 0, blk_writer, 0);
    pthread_create(&r, 0, blk_reader, 0);
    pthread_join(r, 0);
    stop = 1;
    pthread_join(w, 0);
    snprintf(m, sizeof m, "seqblk: %ld reads, %ld whole, %ld torn", bl_reads, bl_ok, bl_torn);
    check(m, bl_ok > 1000 && bl_torn == 0);
    printf("passed %d failed %d\n", pass, fail);
    return fail != 0;
}
