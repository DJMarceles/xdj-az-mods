/* SPDX-License-Identifier: GPL-3.0-only */
/* A track's hot cues from its analysis file (guest/azmod/hc_anlz.h), over files built here in
 * the layout rekordbox exports: the .EXT's colored PCO2 list wins over its PCOB lists, a .DAT's
 * hot cues may be split over two PCOB lists, a color sits after the comment, memory cue lists
 * and slots outside A..H are left out, and every length that runs past its section or the
 * file makes the file malformed. Also where a cue lands in a preview of a given width.
 * HCANLZ_HDR = the header under test. */
#include "check.h"
#include <stdlib.h>
#include <string.h>
#include HCANLZ_HDR

static uint8_t buf[4096];
static size_t blen;
static void put32(size_t o, uint32_t v) {
    buf[o] = (uint8_t) (v >> 24);
    buf[o + 1] = (uint8_t) (v >> 16);
    buf[o + 2] = (uint8_t) (v >> 8);
    buf[o + 3] = (uint8_t) v;
}
static void put16(size_t o, uint32_t v) {
    buf[o] = (uint8_t) (v >> 8);
    buf[o + 1] = (uint8_t) v;
}
static void file_start(void) {
    memset(buf, 0, sizeof buf);
    memcpy(buf, "PMAI", 4);
    put32(4, 28);
    blen = 28;
}
static void file_end(void) { put32(8, (uint32_t) blen); }
static size_t sec_start(const char *tag, uint32_t h, uint32_t type) {
    size_t o = blen;
    memcpy(buf + o, tag, 4);
    put32(o + 4, h);
    put32(o + 12, type);
    blen += h;
    return o;
}
static void sec_end(size_t o) { put32(o + 8, (uint32_t) (blen - o)); }
/* a PCOB list entry (PCPT, 0x38 bytes) */
static void pcpt(uint32_t slot, uint32_t ms) {
    size_t e = blen;
    memcpy(buf + e, "PCPT", 4);
    put32(e + 4, 0x1c);
    put32(e + 8, 0x38);
    put32(e + 0x0c, slot);
    buf[e + 0x1c] = 1;
    put32(e + 0x20, ms);
    blen += 0x38;
}
/* a PCO2 list entry (PCP2) with a comment of `lc` bytes and its color after it */
static void pcp2(uint32_t slot, uint32_t ms, uint32_t lc, uint32_t rgb) {
    size_t e = blen, en = 0x2c + lc + 4 + 0x10;
    memcpy(buf + e, "PCP2", 4);
    put32(e + 4, 0x10);
    put32(e + 8, (uint32_t) en);
    put32(e + 0x0c, slot);
    buf[e + 0x10] = 1;
    put32(e + 0x14, ms);
    put32(e + 0x28, lc);
    memset(buf + e + 0x2c, 0x41, lc);
    buf[e + 0x2c + lc] = 0x26;
    buf[e + 0x2c + lc + 1] = (uint8_t) (rgb >> 16);
    buf[e + 0x2c + lc + 2] = (uint8_t) (rgb >> 8);
    buf[e + 0x2c + lc + 3] = (uint8_t) rgb;
    blen += en;
}
static size_t pcob(uint32_t type, int n) {
    size_t o = sec_start("PCOB", 24, type);
    put16(o + 0x12, (uint32_t) n);
    return o;
}
static size_t pco2(uint32_t type, int n) {
    size_t o = sec_start("PCO2", 20, type);
    put16(o + 0x10, (uint32_t) n);
    return o;
}
static struct hc_cue c[HC_MAX];

/* hc_anlz_pick over buf: a reader that counts what it read */
static size_t g_read;
static int mem_read(void *ctx, uint8_t *b, size_t n, uint64_t off) {
    size_t len = *(size_t *) ctx;
    if (off > len || n > len - off)
        return 0;
    memcpy(b, buf + off, n);
    g_read += n;
    return 1;
}
/* parsing the picked sections gives what parsing the whole file gives */
static int pick_agrees(size_t len) {
    struct hc_cue a[HC_MAX], p[HC_MAX];
    memset(a, 0, sizeof a);
    memset(p, 0, sizeof p);
    int na = hc_anlz_hot_cues(buf, len, a), np = -1;
    size_t pn = 0;
    g_read = 0;
    uint8_t *pb = hc_anlz_pick(mem_read, &len, len, sizeof buf, &pn);
    if (pb) {
        np = hc_anlz_hot_cues(pb, pn, p);
        free(pb);
    }
    if (na != np)
        return 0;
    for (int i = 0; i < na; i++) /* field by field: the struct has padding */
        if (a[i].slot != p[i].slot || a[i].ms != p[i].ms || a[i].rgb != p[i].rgb)
            return 0;
    return 1;
}

/* the .EXT of a track with six hot cues: PCOB D..F, a memory list, PCO2 A..F with colors */
static void ext_six(int with_co2) {
    file_start();
    size_t o = pcob(1, 3);
    pcpt(6, 236112);
    pcpt(5, 177035);
    pcpt(4, 147497);
    sec_end(o);
    o = pcob(0, 1);
    pcpt(0, 1000);
    sec_end(o);
    if (with_co2) {
        o = pco2(1, 6);
        pcp2(6, 236112, 0, 0xff5e00);
        pcp2(5, 177035, 0, 0x00ff30);
        pcp2(4, 147497, 0, 0x4d00ff);
        pcp2(3, 88420, 0, 0x33ff00);
        pcp2(2, 29343, 0, 0x00c4ff);
        pcp2(1, 25399, 0, 0x1aff00);
        sec_end(o);
    }
    file_end();
}

int main(void) {
    ut_suite = "hot cues from analysis files";

    ext_six(1);
    int n = hc_anlz_hot_cues(buf, blen, c);
    CHECK(".EXT: the colored PCO2 list, all six, in slot order",
          n == 6 && c[0].slot == 1 && c[5].slot == 6 && c[0].ms == 25399 && c[5].ms == 236112);
    CHECK(".EXT: each cue's color", c[0].rgb == 0x1aff00 && c[3].rgb == 0x4d00ff && c[5].rgb == 0xff5e00);

    ext_six(0);
    n = hc_anlz_hot_cues(buf, blen, c);
    CHECK("no PCO2: the hot cue PCOB list, without colors", n == 3 && c[0].slot == 4 && c[0].rgb == 0);

    file_start(); /* a .DAT: A..C in one PCOB list, D..E in another */
    size_t o = pcob(1, 3);
    pcpt(3, 300);
    pcpt(1, 100);
    pcpt(2, 200);
    sec_end(o);
    o = pcob(1, 2);
    pcpt(5, 500);
    pcpt(4, 400);
    sec_end(o);
    file_end();
    n = hc_anlz_hot_cues(buf, blen, c);
    CHECK(".DAT: hot cues split over two PCOB lists, merged in slot order",
          n == 5 && c[0].slot == 1 && c[0].ms == 100 && c[4].slot == 5 && c[4].ms == 500);

    file_start(); /* a memory-cue PCO2 does not count as the colored hot cue list */
    o = pco2(0, 1);
    pcp2(0, 50, 0, 0x123456);
    sec_end(o);
    o = pcob(1, 1);
    pcpt(2, 2000);
    sec_end(o);
    file_end();
    n = hc_anlz_hot_cues(buf, blen, c);
    CHECK("a memory cue PCO2 list is not the hot cues", n == 1 && c[0].slot == 2 && c[0].ms == 2000);

    file_start(); /* a comment before the color */
    o = pco2(1, 2);
    pcp2(1, 10, 22, 0xabcdef);
    pcp2(2, 20, 7, 0x010203);
    sec_end(o);
    file_end();
    n = hc_anlz_hot_cues(buf, blen, c);
    CHECK("the color is read after the comment", n == 2 && c[0].rgb == 0xabcdef && c[1].rgb == 0x010203);

    file_start(); /* slots outside A..H, and a slot given twice */
    o = pcob(1, 4);
    pcpt(0, 1);
    pcpt(9, 2);
    pcpt(3, 30);
    pcpt(3, 33);
    sec_end(o);
    file_end();
    n = hc_anlz_hot_cues(buf, blen, c);
    CHECK("slots outside A..H are left out; a repeated slot keeps its last time",
          n == 1 && c[0].slot == 3 && c[0].ms == 33);

    file_start(); /* a count larger than the entries there */
    o = pcob(1, 3);
    pcpt(1, 10);
    sec_end(o);
    file_end();
    CHECK("a list counting more entries than it holds is malformed", hc_anlz_hot_cues(buf, blen, c) == -1);

    ext_six(1);
    CHECK("a file cut short is malformed", hc_anlz_hot_cues(buf, blen - 9, c) == -1);
    put32(28 + 8, 0x7fffffff);
    CHECK("a section longer than the file is malformed", hc_anlz_hot_cues(buf, blen, c) == -1);

    file_start();
    o = pco2(1, 1);
    pcp2(1, 10, 0, 0x112233);
    sec_end(o);
    file_end();
    put32(28 + 20 + 8, 0x400); /* the entry claims more than its section */
    CHECK("an entry longer than its section is malformed", hc_anlz_hot_cues(buf, blen, c) == -1);
    put32(28 + 20 + 8, 0x2c + 4 + 0x10);
    put32(28 + 20 + 0x28, 0x7ffffff0); /* a comment longer than the entry */
    n = hc_anlz_hot_cues(buf, blen, c);
    CHECK("a comment longer than its entry: the cue, without a color", n == 1 && c[0].rgb == 0);

    memcpy(buf, "PMAX", 4);
    CHECK("not an analysis file", hc_anlz_hot_cues(buf, blen, c) == -1);
    CHECK("nothing", hc_anlz_hot_cues(buf, 0, c) == -1 && hc_anlz_hot_cues(0, 100, c) == -1);
    file_start();
    file_end();
    CHECK("an analysis file without cue lists has no hot cues", hc_anlz_hot_cues(buf, blen, c) == 0);

    /* reading only the cue sections */
    ext_six(1);
    {
        size_t w = sec_start("PWV5", 24, 0); /* a detail waveform: most of a real .EXT */
        blen += 2000;
        sec_end(w);
        file_end();
    }
    size_t whole = blen;
    CHECK("cue sections only: the same hot cues as the whole file", pick_agrees(blen));
    CHECK("cue sections only: the waveform is not read (only its 12-byte header)", g_read + 2024 - 12 <= whole + 12 * 6);
    ext_six(0);
    CHECK("cue sections only: PCOB without PCO2, the same", pick_agrees(blen));
    ext_six(1);
    CHECK("cue sections only: a file cut short, malformed both ways", pick_agrees(blen - 9));
    put32(28 + 8, 0x7fffffff);
    CHECK("cue sections only: a section longer than the file, malformed both ways", pick_agrees(blen));
    file_start();
    o = pcob(1, 3);
    pcpt(1, 10);
    sec_end(o);
    file_end();
    CHECK("cue sections only: a short list, malformed both ways", pick_agrees(blen));
    memcpy(buf, "PMAX", 4);
    CHECK("cue sections only: not an analysis file", pick_agrees(blen));
    file_start();
    file_end();
    CHECK("cue sections only: no cue lists, none both ways", pick_agrees(blen));

    CHECK("the start of the track is the left edge", hc_x(0, 200000, 150) == 0);
    CHECK("the middle", hc_x(100000, 200000, 150) == 75);
    CHECK("just before the end is the last pixel", hc_x(199999, 200000, 150) == 149);
    CHECK("at or past the end is not drawn", hc_x(200000, 200000, 150) == -1 && hc_x(300000, 200000, 150) == -1);
    CHECK("no length or no width is not drawn", hc_x(10, 0, 150) == -1 && hc_x(10, 100, 0) == -1);
    CHECK("long tracks do not overflow", hc_x(4000000000u, 4200000000u, 65536) == 62415);

    return ut_summary();
}
