/* SPDX-License-Identifier: GPL-3.0-only */
/* The deck's stem state on its jog display (guest/azmod/jog_stems_logic.h and the hook bodies
 * of jog_display.c, extracted by run.sh):
 *   - the pad-mode record asks for the HOT CUE pad page (Pad Info Display stays On, so the page
 *     keeps its waveform preview) only for a deck whose pads are its stem toggles, with stems
 *     engaged, and only when the user's Pad Info Display is On; any other deck, and an unknown
 *     one, keeps EP147's record;
 *   - the pad table: pads 1-4 set in the stem colors while on and empty while off, pads 5-8 the
 *     solo; a table EP147 sends inside the deck's collector update is replaced while the deck
 *     shows stems, a changed state is sent once after the update, and EP147's own table (with
 *     its header) goes back when the deck stops showing stems;
 *   - identity: a wrong collector or queue vtable, a misaligned object, a deck outside 0..3
 *     change nothing, the deck is known only inside its own collector's update, and deck N's
 *     state never reaches deck M's jog;
 *   - the timing diagnostics: the deck-frame message slot, one artwork transfer counted once
 *     per sequence number, gaps counted;
 *   - the preview strip: four blocks in pad order, tall in the stem color when on, short and
 *     dim when off, EP147's column count and flags; EP147's strip kept and passed through, the
 *     stem strip in its place while stems show, sent once per stem-state edge, EP147's strip
 *     back when stems stop; an unknown queue untouched; deck by the queue its collector named;
 *   - concurrent publishers and hook threads (the sanitizer passes).
 * Fake objects stand in for EP147's. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "check.h"

#define AZMOD_STEM_DECKS 4
#include JOG_LOGIC_INC

static uintptr_t g_cue_vt[4], g_tiq_vt[16], g_other_vt[16];
static FILE *azlog_open(const char *path, const char *mode) {
    (void) path;
    return fopen("/dev/null", mode);
}
#define EP_218a6d8 ((uintptr_t) 0x218a6d8)
#define EP_1ff1250 ((uintptr_t) 0x1ff1250)
#define EP_1ffbc60 ((uintptr_t) 0x1ffbc60)
#define EP_218a890 ((uintptr_t) 0x218a890)
#define EP_2df7ff8 ((uintptr_t) g_cue_vt)
#define EP_2e509c8 ((uintptr_t) g_tiq_vt)
static unsigned g_jog_src[AZMOD_STEM_DECKS];
static int g_jog_page[AZMOD_STEM_DECKS];
#include JOG_HOOK_INC

/* ---- what reached the controller: the last table, per queue */
struct sent_rec {
    _Atomic int n;
    _Atomic long hdr;
    _Atomic uintptr_t vec; /* the vector pointer handed over */
    unsigned char set[JOG_PADS], color[JOG_PADS];
    _Atomic int deck_seen; /* t_jog_deck inside EP147's own sends */
};
#define NQ 16
static struct {
    _Alignas(16) uintptr_t vt;
} g_q[NQ];
static struct sent_rec g_rec[NQ];
static int qidx(long q) {
    for (int i = 0; i < NQ; i++)
        if ((long) &g_q[i] == q)
            return i;
    return NQ - 1;
}
static void fake_pads(long queue, const void *vec, long hdr) {
    struct sent_rec *r = &g_rec[qidx(queue)];
    const struct jog_cue_vec *v = vec;
    for (int i = 0; i < JOG_PADS; i++) {
        const struct jog_cue *c = v->begin + i < v->end ? v->begin + i : NULL;
        r->set[i] = c ? c->set : 0;
        r->color[i] = c ? c->color : 0;
    }
    atomic_store(&r->hdr, hdr);
    atomic_store(&r->vec, (uintptr_t) vec);
    atomic_fetch_add(&r->n, 1);
}

/* ---- the pad-mode record */
static _Thread_local unsigned char g_fill_d4 = 1, g_fill_d5 = 1;
static void fake_padinfo_fill(long pid, long info) {
    (void) pid;
    *(unsigned char *) (info + 0xd4) = g_fill_d4;
    *(unsigned char *) (info + 0xd5) = g_fill_d5;
}
struct pid {
    _Alignas(16) unsigned char b[0x360];
};
static void run_padinfo(int deck, unsigned char setting, unsigned char *d4, unsigned char *d5) {
    struct pid p;
    unsigned char info[0x100];
    memset(&p, 0, sizeof p);
    memset(info, 0xee, sizeof info);
    p.b[0x350] = (unsigned char) deck;
    g_fill_d4 = setting;
    g_fill_d5 = 1; /* BEAT LOOP */
    jog_padinfo_hook((long) &p, (long) info);
    *d4 = info[0xd4];
    *d5 = info[0xd5];
}
static void publish(int d, int engaged, int page, unsigned mask) {
    __atomic_store_n(&g_jog_src[d], jog_word(engaged, 1, mask), __ATOMIC_RELAXED);
    __atomic_store_n(&g_jog_page[d], page, __ATOMIC_RELAXED);
}

/* ---- the collector: its update may send EP147's own table, as the real one does */
struct coll {
    _Alignas(16) unsigned char b[0x900];
};
static _Thread_local int t_send_inside = 0; /* this update sends EP147's table */
static _Thread_local long t_inside_hdr = 0;
static _Atomic int g_deck_in_orig[NQ];
static void fake_cue_update(long self, long changed, long reset) {
    (void) changed;
    (void) reset;
    long q;
    memcpy(&q, (const void *) (self + 0x10), sizeof q); /* self may be the misaligned case */
    atomic_store(&g_deck_in_orig[qidx(q)], t_jog_deck);
    if (t_send_inside) /* EP147: the queue's slot +0x58, through the (patched) vtable */
        jog_cue_slot_hook(q, (const void *) (self + 0x8d0), t_inside_hdr);
}
static struct jog_cue g_ep147_cues[NQ][JOG_PADS]; /* EP147's own tables, one per collector */
static void mk_coll(struct coll *c, uintptr_t vt, unsigned deck, int qi, int good_q) {
    memset(c, 0, sizeof *c);
    *(uintptr_t *) c->b = vt;
    *(unsigned *) (c->b + 0x08) = deck;
    g_q[qi].vt = good_q ? (uintptr_t) g_tiq_vt : (uintptr_t) g_other_vt;
    *(long *) (c->b + 0x10) = (long) &g_q[qi];
    struct jog_cue *e = g_ep147_cues[qi];
    memset(e, 0, sizeof g_ep147_cues[qi]);
    e[0].set = 1;
    e[0].color = 33; /* EP147's own hot cue A */
    struct jog_cue_vec *v = (struct jog_cue_vec *) (c->b + 0x8d0);
    v->begin = e;
    v->end = v->cap = e + JOG_PADS;
}
static void reset_all(void) {
    for (int d = 0; d < AZMOD_STEM_DECKS; d++) {
        g_jog_src[d] = 0;
        g_jog_page[d] = 0;
        g_jog_active[d] = 0;
        g_jog_sent[d] = 0;
        g_jog_hdr[d] = 0;
        g_jog_busy[d] = 0;
    }
    memset(g_rec, 0, sizeof g_rec);
    for (int i = 0; i < NQ; i++)
        g_deck_in_orig[i] = -2;
}
static int set_mask(const struct sent_rec *r) {
    int m = 0;
    for (int i = 0; i < JOG_PADS; i++)
        if (r->set[i])
            m |= 1 << i;
    return m;
}

struct thr {
    int id;
    _Atomic int *stop;
};
static struct coll g_tc[4];
static void *publisher(void *arg) {
    struct thr *t = arg;
    unsigned x = 12345;
    while (!atomic_load(t->stop)) {
        x = x * 1103515245u + 12345u;
        publish((int) (x >> 8) & 3, (x >> 12) & 1, (x >> 13) & 1, (x >> 16) & 15);
    }
    return NULL;
}
static void *pad_thread(void *arg) {
    struct thr *t = arg;
    int i = 0;
    while (!atomic_load(t->stop)) {
        unsigned char d4, d5;
        run_padinfo(i++ & 3, 1, &d4, &d5);
    }
    return NULL;
}
static void *cue_thread(void *arg) {
    struct thr *t = arg;
    t_send_inside = t->id & 1;
    while (!atomic_load(t->stop))
        jog_cue_update_hook((long) &g_tc[t->id], 1, 0);
    return NULL;
}

/* ---- the preview strip: what the queue's slot +0x20 was handed */
struct prev_rec {
    _Atomic int n;
    int cols;
    long a, b, c;
    struct jog_prev_col last[JOG_PREV_MAX];
};
static struct prev_rec g_prev[NQ];
/* EP147's queue takes pushes from any thread (its own atomic queue); the fake locks. */
static pthread_mutex_t g_prev_mu = PTHREAD_MUTEX_INITIALIZER;
static void fake_prev(long q, const void *vec, long a, long b, long c) {
    pthread_mutex_lock(&g_prev_mu);
    struct prev_rec *r = &g_prev[qidx(q)];
    const struct jog_prev_vec *v = vec;
    r->cols = (int) (v->end - v->begin);
    memcpy(r->last, v->begin, sizeof(struct jog_prev_col) * (size_t) r->cols);
    r->a = a;
    r->b = b;
    r->c = c;
    atomic_fetch_add(&r->n, 1);
    pthread_mutex_unlock(&g_prev_mu);
}
static struct jog_prev_col g_ep_cols[JOG_PREV_MAX];
static void ep147_strip(long q, int n, unsigned char tag) {
    for (int i = 0; i < n; i++) {
        g_ep_cols[i].r = tag;
        g_ep_cols[i].h = 77;
    }
    struct jog_prev_vec v = {g_ep_cols, g_ep_cols + n, g_ep_cols + n};
    jog_prev_slot_hook(q, &v, 1, 0, 0);
}
/* the block a strip shows at block b's centre: 2 = tall stem color, 1 = short dim, 0 = none */
static int blk(const struct prev_rec *r, int b) {
    int x = (JOG_PREV_M + b * (JOG_PREV_W + JOG_PREV_G) + JOG_PREV_W / 2) * r->cols / 600;
    const struct jog_prev_col *c = &r->last[x];
    if (c->h == JOG_PREV_ON_H && c->r == JOG_PREV_RGB[b][0] && c->g == JOG_PREV_RGB[b][1])
        return 2;
    if (c->h == JOG_PREV_OFF_H && c->r == JOG_PREV_RGB[b][0] * 77 / 255)
        return 1;
    return 0;
}
static void *prev_ep_thread(void *arg) {
    struct thr *t = arg;
    unsigned char tag = 1;
    while (!atomic_load(t->stop))
        ep147_strip((long) &g_q[13], 1200, tag++);
    return NULL;
}
static void *prev_cue_thread(void *arg) {
    struct thr *t = arg;
    while (!atomic_load(t->stop))
        jog_cue_update_hook((long) &g_tc[t->id], 1, 0);
    return NULL;
}
static void preview_strip(void) {
    unsigned char d4, d5;
    g_orig_jog_prev = fake_prev;
    g_jog_hooks = 1;

    /* the column builder */
    struct jog_prev_col cols[JOG_PREV_MAX];
    CHECK("strip: a column is EP147's 5 bytes {R, G, B, height, highlight}",
          sizeof(struct jog_prev_col) == 5);
    CHECK("strip: 600 and 1200 columns build; 0 and 1201 are refused",
          jog_prev_build(JOG_ON | 5, cols, 600) == 600 && jog_prev_build(JOG_ON, cols, 1200) == 1200 &&
              !jog_prev_build(JOG_ON, cols, 0) && !jog_prev_build(JOG_ON, cols, 1201));
    jog_prev_build(JOG_ON | 0x5, cols, 600);
    struct prev_rec *r0 = &g_prev[0];
    r0->cols = 600;
    memcpy(r0->last, cols, sizeof(struct jog_prev_col) * 600);
    CHECK("strip: blocks in pad order; on = tall in the stem color (vocal, bass)",
          blk(r0, 0) == 2 && blk(r0, 2) == 2);
    CHECK("strip: off = short at 30 % (inst, drums)", blk(r0, 1) == 1 && blk(r0, 3) == 1);
    CHECK("strip: margins and gaps are empty columns",
          cols[0].h == 0 && cols[599].h == 0 && cols[JOG_PREV_M + JOG_PREV_W + 1].h == 0 &&
              cols[JOG_PREV_M + JOG_PREV_W + 1].r == 0);
    CHECK("strip: no highlight anywhere", cols[100].hl == 0 && cols[300].hl == 0);
    jog_prev_build(JOG_ON | 0x5, cols, 1200);
    r0->cols = 1200;
    memcpy(r0->last, cols, sizeof(struct jog_prev_col) * 1200);
    CHECK("strip: a 1200-column strip has the same blocks", blk(r0, 0) == 2 && blk(r0, 1) == 1);
    CHECK("strip: our column count: 600 for EP147's 1200, a shorter strip keeps its own",
          jog_prev_ncols(1200) == 600 && jog_prev_ncols(600) == 600 && jog_prev_ncols(300) == 300);
    CHECK("strip: block of column, edges", jog_prev_block(JOG_PREV_M, 600) == 0 &&
                                               jog_prev_block(JOG_PREV_M - 1, 600) == -1 &&
                                               jog_prev_block(JOG_PREV_M + JOG_PREV_W, 600) == -1 &&
                                               jog_prev_block(-1, 600) == -1 && jog_prev_block(5, 0) == -1);

    /* through the hooks */
    reset_all();
    memset(g_prev, 0, sizeof g_prev);
    memset(g_jp, 0, sizeof g_jp);
    memset(g_jog_q, 0, sizeof g_jog_q);
    g_jog_strip = 1;
    struct coll c;
    mk_coll(&c, (uintptr_t) g_cue_vt, 1, 0, 1);
    long q = (long) &g_q[0];
    ep147_strip(q, 1200, 9);
    CHECK("strip: before any collector update the queue's deck is unknown: EP147's, untouched",
          g_prev[0].n == 1 && g_prev[0].last[0].r == 9 && !g_jp[1].have);
    jog_cue_update_hook((long) &c, 1, 0);
    ep147_strip(q, 1200, 10);
    CHECK("strip: EP147's strip passes through and is kept for its deck",
          g_prev[0].n == 2 && g_prev[0].last[0].r == 10 && g_jp[1].have && g_jp[1].n == 1200 &&
              g_jp[1].ep[0].r == 10 && g_jp[1].a == 1);
    publish(1, 1, 1, 0x6);
    run_padinfo(1, 1, &d4, &d5);
    jog_cue_update_hook((long) &c, 1, 0);
    CHECK("strip: stems shown -> the stem strip, once, 600 columns for EP147's 1200, its flags",
          g_prev[0].n == 3 && g_prev[0].cols == 600 && g_prev[0].a == 1 && blk(&g_prev[0], 1) == 2 &&
              blk(&g_prev[0], 2) == 2 && blk(&g_prev[0], 0) == 1);
    jog_cue_update_hook((long) &c, 1, 0);
    CHECK("strip: no edge, nothing sent", g_prev[0].n == 3);
    publish(1, 1, 1, 0x7);
    run_padinfo(1, 1, &d4, &d5);
    jog_cue_update_hook((long) &c, 1, 0);
    CHECK("strip: a toggle sends the new strip", g_prev[0].n == 4 && blk(&g_prev[0], 0) == 2);
    ep147_strip(q, 1200, 11); /* a new track's strip from EP147 */
    CHECK("strip: EP147's new strip is kept, ours goes in its place",
          g_prev[0].n == 5 && g_jp[1].ep[0].r == 11 && blk(&g_prev[0], 0) == 2);
    publish(1, 0, 1, 0x7);
    run_padinfo(1, 1, &d4, &d5);
    jog_cue_update_hook((long) &c, 1, 0);
    CHECK("strip: stems stop -> EP147's latest strip back", g_prev[0].n == 6 &&
                                                                g_prev[0].last[0].r == 11 &&
                                                                g_prev[0].last[0].h == 77);
    jog_cue_update_hook((long) &c, 1, 0);
    CHECK("strip: and only once", g_prev[0].n == 6);

    /* identity */
    struct coll c3;
    mk_coll(&c3, (uintptr_t) g_cue_vt, 3, 3, 1);
    publish(3, 1, 1, 0xf);
    run_padinfo(3, 1, &d4, &d5);
    jog_cue_update_hook((long) &c3, 1, 0);
    CHECK("identity: a deck EP147 sent no strip yet gets none of ours", g_prev[3].n == 0);
    ep147_strip((long) &g_q[3], 600, 12);
    CHECK("identity: deck 4's own strip -> deck 4's stems, deck 2 untouched",
          g_prev[3].n == 1 && blk(&g_prev[3], 3) == 2 && g_prev[0].n == 6);
    ep147_strip((long) &g_q[7], 600, 13);
    CHECK("identity: a queue no collector named is passed through untouched",
          g_prev[7].n == 1 && g_prev[7].last[0].r == 13);
    g_jog_pbusy[1] = 1;
    ep147_strip(q, 1200, 14);
    CHECK("identity: a busy deck passes EP147's strip through and marks it", g_prev[0].last[0].r == 14 &&
                                                                                 g_jog_pstale[1] == 1);
    g_jog_pbusy[1] = 0;
    publish(1, 1, 1, 0x1);
    run_padinfo(1, 1, &d4, &d5);
    jog_cue_update_hook((long) &c, 1, 0);
    CHECK("identity: after it, the next update puts the stem strip back", blk(&g_prev[0], 0) == 2 &&
                                                                              g_jog_pstale[1] == 0);
    g_jog_strip = 0;
    unsigned before = (unsigned) g_prev[0].n;
    publish(1, 1, 1, 0x3);
    run_padinfo(1, 1, &d4, &d5);
    jog_cue_update_hook((long) &c, 1, 0);
    ep147_strip(q, 1200, 15);
    CHECK("identity: strip off (kill switch) -> EP147's strips only",
          g_prev[0].n == (int) before + 1 && g_prev[0].last[0].r == 15);
    g_jog_strip = 1;

    /* threads: EP147 sending strips while collector updates run */
    reset_all();
    memset(g_jp, 0, sizeof g_jp);
    memset(g_jog_pstale, 0, sizeof g_jog_pstale);
    mk_coll(&g_tc[0], (uintptr_t) g_cue_vt, 2, 13, 1);
    mk_coll(&g_tc[1], (uintptr_t) g_cue_vt, 2, 13, 1);
    jog_cue_update_hook((long) &g_tc[0], 1, 0);
    publish(2, 1, 1, 0x9);
    run_padinfo(2, 1, &d4, &d5);
    _Atomic int stop = 0;
    struct thr th[3] = {{0, &stop}, {0, &stop}, {1, &stop}};
    pthread_t pt[3];
    pthread_create(&pt[0], NULL, prev_ep_thread, &th[0]);
    pthread_create(&pt[1], NULL, prev_cue_thread, &th[1]);
    pthread_create(&pt[2], NULL, prev_cue_thread, &th[2]);
    struct timespec ts = {0, 200 * 1000 * 1000};
    nanosleep(&ts, NULL);
    atomic_store(&stop, 1);
    for (int i = 0; i < 3; i++)
        pthread_join(pt[i], NULL);
    jog_cue_update_hook((long) &g_tc[0], 1, 0);
    CHECK("threads: the deck ends on its stem strip, nothing left busy",
          g_jog_pbusy[2] == 0 && g_jp[2].shown == (JOG_ON | 9) && blk(&g_prev[13], 0) == 2 &&
              blk(&g_prev[13], 1) == 1);
    g_jog_strip = 0;
}

/* ---- the timing diagnostics */
static int ev_count(int kind) {
    int n = 0;
    unsigned head = g_jog_ev_head;
    for (unsigned k = head > JOG_EV_N ? head - JOG_EV_N : 0; k < head; k++)
        if (g_jog_ev[k % JOG_EV_N].seq == k + 1 && g_jog_ev[k % JOG_EV_N].kind == kind)
            n++;
    return n;
}

static void mk_frame(unsigned char *f, unsigned disp, unsigned id, unsigned seq, unsigned idx,
                     unsigned tot, unsigned len) {
    memset(f, 0, 152);
    f[0x4c] = 1;
    f[0x4e] = (unsigned char) seq;
    f[0x4f] = (unsigned char) (seq >> 8);
    f[0x50] = (unsigned char) disp;
    f[0x51] = (unsigned char) id;
    f[0x52] = (unsigned char) idx;
    f[0x53] = (unsigned char) (idx >> 8);
    f[0x54] = (unsigned char) tot;
    f[0x55] = (unsigned char) (tot >> 8);
    f[0x56] = (unsigned char) len;
    f[0x57] = (unsigned char) (len >> 8);
    f[0x58] = (unsigned char) (len >> 16);
}

static void tx(int bus, const unsigned char *f, uint32_t size) {
    unsigned char desc[16] = {0};
    uint64_t ptr = (uint64_t) (uintptr_t) f;
    memcpy(desc + 4, &size, 4);
    memcpy(desc + 8, &ptr, 8);
    jog_tx_note(bus, desc);
}

static void *tx_thread(void *arg) {
    struct thr *t = arg;
    unsigned char f[152];
    unsigned seq = 0;
    while (!atomic_load(t->stop)) {
        for (unsigned i = 1; i <= 3; i++) {
            mk_frame(f, 0x10, JOG_MSG_ART, ++seq, i, 3, 100);
            tx(t->id, f, 152);
        }
    }
    return NULL;
}

static void timing_diag(void) {
    g_jog_timing = 1;
    g_jog_hooks = 1;
    /* the timing diagnostics */
    unsigned char f[152];
    struct jog_msg m;
    mk_frame(f, 0x10, JOG_MSG_ART, 7, 1, 3, 3000);
    CHECK("timing: the message slot parses (display, id, piece, pieces, JPEG length)",
          jog_msg_parse(f, 152, &m) && m.display == 0x10 && m.id == 0x33 && m.index == 1 &&
              m.total == 3 && m.len == 3000 && m.seq == 7);
    CHECK("timing: a frame too short for the slot is not read", !jog_msg_parse(f, 0x56 + 57, &m));
    f[0x4c] = 0;
    CHECK("timing: no message present -> nothing", !jog_msg_parse(f, 152, &m));
    mk_frame(f, 0x30, JOG_MSG_ART, 7, 1, 3, 3000);
    CHECK("timing: an unknown display -> nothing", !jog_msg_parse(f, 152, &m));
    struct jog_xfer x;
    memset(&x, 0, sizeof x);
    int done = 0;
    unsigned seq = 100;
    for (unsigned i = 1; i <= 3; i++) {
        mk_frame(f, 0x20, JOG_MSG_ART, seq++, i, 3, i == 1 ? 120 : 0);
        jog_msg_parse(f, 152, &m);
        done += jog_xfer_feed(&x, &m, 1000 * i);
        done += jog_xfer_feed(&x, &m, 1000 * i + 1); /* the frame repeats until the next message */
    }
    CHECK("timing: one artwork = one transfer, each piece once, first to last",
          done == 1 && x.pieces == 3 && x.gaps == 0 && x.len == 120 && x.first == 1000 &&
              x.last == 3000);
    memset(&x, 0, sizeof x);
    done = 0;
    for (unsigned i = 1; i <= 4; i++) {
        if (i == 3)
            continue;
        mk_frame(f, 0x20, JOG_MSG_ART, seq++, i, 4, 0);
        jog_msg_parse(f, 152, &m);
        done += jog_xfer_feed(&x, &m, i);
    }
    CHECK("timing: a missing piece is counted as a gap", done == 1 && x.pieces == 3 && x.gaps == 1);
    memset(&x, 0, sizeof x);
    mk_frame(f, 0x20, 0x2d, seq++, 0, 0, 0);
    jog_msg_parse(f, 152, &m);
    mk_frame(f, 0x20, JOG_MSG_ART, seq++, 2, 3, 0);
    jog_msg_parse(f, 152, &m);
    CHECK("timing: other messages and a transfer joined midway count nothing",
          !jog_xfer_feed(&x, &m, 5) && x.pieces == 0);
    unsigned before = (unsigned) ev_count(JN_XFER);
    for (unsigned i = 1; i <= 2; i++) {
        mk_frame(f, 0x10, JOG_MSG_ART, seq++, i, 2, 50);
        tx(1, f, 152);
    }
    CHECK("timing: through the ioctl descriptor, a transfer is logged", ev_count(JN_XFER) == (int) before + 1);
    g_jog_timing = 0;
    for (unsigned i = 1; i <= 2; i++) {
        mk_frame(f, 0x10, JOG_MSG_ART, seq++, i, 2, 50);
        tx(1, f, 152);
    }
    CHECK("timing: diagnostics off, nothing is timed", ev_count(JN_XFER) == (int) before + 1);
    g_jog_timing = 1;
    tx(5, f, 152);
    tx(1, f, 300);
    CHECK("timing: an unknown bus or an oversized frame is ignored", ev_count(JN_XFER) == (int) before + 1);

    /* the bus threads, both buses at once */
    _Atomic int stop = 0;
    struct thr th[2] = {{0, &stop}, {1, &stop}};
    pthread_t pt[2];
    pthread_create(&pt[0], NULL, tx_thread, &th[0]);
    pthread_create(&pt[1], NULL, tx_thread, &th[1]);
    struct timespec ts = {0, 100 * 1000 * 1000};
    nanosleep(&ts, NULL);
    atomic_store(&stop, 1);
    for (int i = 0; i < 2; i++)
        pthread_join(pt[i], NULL);
    for (int i = 0; i < 5; i++)
        jogstems_tick(); /* the log drains on the message thread */
    CHECK("timing: after the bus threads, no bus left busy", g_jog_x_busy[0] == 0 && g_jog_x_busy[1] == 0);
    g_jog_timing = 0;
}

int main(void) {
    ut_suite = "jog stems";
    g_orig_jog_padinfo = fake_padinfo_fill;
    g_orig_jog_cue_update = fake_cue_update;
    g_orig_jog_pads = fake_pads;

    /* ---- the word and the table */
    CHECK("word: engaged on its stems page carries the mask", jog_word(1, 1, 0x5) == (JOG_ON | 5));
    CHECK("word: not on the stems page is nothing", jog_word(1, 0, 0xf) == 0);
    CHECK("word: no stems engaged is nothing", jog_word(0, 1, 0xf) == 0);
    struct jog_cue t[JOG_PADS];
    jog_cues_build(JOG_ON | 0x5, t);
    CHECK("table: stems on are set in their colors (vocal green 21, bass magenta 50)",
          t[0].set && t[0].color == 21 && t[2].set && t[2].color == 50);
    CHECK("table: stems off are empty pads", !t[1].set && !t[3].set && !t[1].color);
    CHECK("table: two stems on, no solo pad lit", !t[4].set && !t[5].set && !t[6].set && !t[7].set);
    jog_cues_build(JOG_ON | 0x8, t);
    CHECK("table: one stem on = its solo pad white (drums -> pad 8)",
          t[3].set && t[3].color == 4 && t[7].set && t[7].color == JOG_CUE_WHITE && !t[4].set);
    jog_cues_build(JOG_ON, t);
    int none = 1;
    for (int i = 0; i < JOG_PADS; i++)
        if (t[i].set)
            none = 0;
    CHECK("table: all stems off = eight empty pads", none);
    jog_cues_build(JOG_ON | 0xf, t);
    CHECK("table: every entry has no position and is not a loop",
          t[0].pos == INT64_MAX && t[7].pos == INT64_MAX && !t[0].is_loop);
    CHECK("table: entry layout is EP147's (0x18 bytes, set +0x15, color +0x16)",
          sizeof(struct jog_cue) == 0x18 && __builtin_offsetof(struct jog_cue, set) == 0x15 &&
              __builtin_offsetof(struct jog_cue, color) == 0x16);

    /* ---- the pad-mode record */
    unsigned char d4, d5;
    reset_all();
    publish(1, 1, 1, 0x9);
    run_padinfo(1, 1, &d4, &d5);
    CHECK("padinfo: hooks not in, EP147's record stays", d4 == 1 && d5 == 1 && g_jog_active[1] == 0);
    g_jog_hooks = 1;
    run_padinfo(1, 1, &d4, &d5);
    CHECK("padinfo: stems page + Pad Info Display On -> HOT CUE page, Pad Info stays On",
          d4 == 1 && d5 == 0);
    CHECK("padinfo: and publishes the deck's word", g_jog_active[1] == (JOG_ON | 9));
    CHECK("padinfo: other decks publish nothing",
          !g_jog_active[0] && !g_jog_active[2] && !g_jog_active[3]);
    run_padinfo(1, 0, &d4, &d5);
    CHECK("padinfo: Pad Info Display Off -> EP147's record as made", d4 == 0 && d5 == 1 &&
                                                                         g_jog_active[1] == 0);
    run_padinfo(0, 1, &d4, &d5);
    CHECK("padinfo: a deck without stems keeps its pad page", d4 == 1 && d5 == 1 && !g_jog_active[0]);
    publish(2, 1, 0, 0xf);
    run_padinfo(2, 1, &d4, &d5);
    CHECK("padinfo: engaged but on another pad page keeps its page", d4 == 1 && d5 == 1);
    for (int d = 0; d < 4; d++)
        publish(d, 1, 1, 0xf);
    memset(g_jog_active, 0, sizeof g_jog_active);
    run_padinfo(7, 1, &d4, &d5);
    CHECK("padinfo: an unknown deck keeps EP147's record and publishes nothing",
          d4 == 1 && d5 == 1 && !g_jog_active[0] && !g_jog_active[1] && !g_jog_active[2] &&
              !g_jog_active[3]);
    reset_all();
    publish(2, 1, 1, 0x3);
    run_padinfo(0, 1, &d4, &d5);
    CHECK("padinfo: deck 3's stems never reach deck 1's record", d5 == 1 && !g_jog_active[0]);
    run_padinfo(2, 1, &d4, &d5);
    CHECK("padinfo: deck 3 itself gets them", d5 == 0 && g_jog_active[2] == (JOG_ON | 3));

    /* ---- the table, through the collector update */
    reset_all();
    struct coll c2;
    mk_coll(&c2, (uintptr_t) g_cue_vt, 2, 0, 1);
    struct sent_rec *r = &g_rec[0];
    jog_cue_update_hook((long) &c2, 1, 0);
    CHECK("table: the deck is known inside its own collector update", g_deck_in_orig[0] == 2);
    CHECK("table: and only there", t_jog_deck == -1);
    CHECK("table: no stems, nothing of ours sent", r->n == 0);
    t_send_inside = 1;
    t_inside_hdr = 0x1234;
    jog_cue_update_hook((long) &c2, 1, 0);
    CHECK("table: no stems, EP147's own table goes through",
          r->n == 1 && r->vec == (uintptr_t) (c2.b + 0x8d0) && r->color[0] == 33);
    t_send_inside = 0;
    g_jog_active[2] = JOG_ON | 0x5;
    memset(r, 0, sizeof *r);
    jog_cue_update_hook((long) &c2, 0, 0);
    CHECK("table: stems on -> the stem table, once, with EP147's header",
          r->n == 1 && set_mask(r) == 0x5 && r->color[0] == 21 && r->color[2] == 50 &&
              r->hdr == 0x1234);
    jog_cue_update_hook((long) &c2, 0, 0);
    jog_cue_update_hook((long) &c2, 0, 0);
    CHECK("table: unchanged -> sent once only", r->n == 1);
    g_jog_active[2] = JOG_ON | 0x7;
    jog_cue_update_hook((long) &c2, 0, 0);
    CHECK("table: a stem toggled -> one new table, that pad set",
          r->n == 2 && set_mask(r) == 0x7 && r->color[1] == 42);
    t_send_inside = 1;
    t_inside_hdr = 0x99;
    jog_cue_update_hook((long) &c2, 1, 0);
    CHECK("table: EP147's own send while stems show is replaced, nothing after it",
          r->n == 3 && set_mask(r) == 0x7 && r->hdr == 0x99);
    t_send_inside = 0;
    g_jog_active[2] = 0;
    jog_cue_update_hook((long) &c2, 0, 0);
    CHECK("table: stems off -> EP147's own table back, with its last header",
          r->n == 4 && r->vec == (uintptr_t) (c2.b + 0x8d0) && r->color[0] == 33 && r->hdr == 0x99);
    jog_cue_update_hook((long) &c2, 0, 0);
    CHECK("table: and only once", r->n == 4);

    /* ---- identity */
    reset_all();
    for (int d = 0; d < AZMOD_STEM_DECKS; d++)
        g_jog_active[d] = JOG_ON | 1;
    static uintptr_t other_vt[4];
    struct coll cx;
    mk_coll(&cx, (uintptr_t) other_vt, 1, 1, 1);
    t_send_inside = 1;
    jog_cue_update_hook((long) &cx, 1, 0);
    CHECK("identity: not a CurrentHotCueCollect -> EP147's table untouched, deck unknown",
          g_rec[1].n == 1 && g_rec[1].color[0] == 33 && g_deck_in_orig[1] == -1);
    mk_coll(&cx, (uintptr_t) g_cue_vt, 4, 2, 1);
    jog_cue_update_hook((long) &cx, 1, 0);
    CHECK("identity: a deck outside 0..3 -> untouched", g_rec[2].n == 1 && g_rec[2].color[0] == 33);
    t_send_inside = 0;
    mk_coll(&cx, (uintptr_t) g_cue_vt, 1, 3, 0);
    jog_cue_update_hook((long) &cx, 0, 0);
    CHECK("identity: a queue that is not a TrackInfoQueue -> nothing sent", g_rec[3].n == 0);
    static _Alignas(16) unsigned char mis[0x920];
    mk_coll((struct coll *) mis, (uintptr_t) g_cue_vt, 1, 4, 1);
    memmove(mis + 4, mis, 0x900);
    jog_cue_update_hook((long) (mis + 4), 0, 0);
    CHECK("identity: a misaligned object -> nothing", g_rec[4].n == 0);
    reset_all();
    g_jog_active[0] = JOG_ON | 0xf;
    struct coll c3;
    mk_coll(&c3, (uintptr_t) g_cue_vt, 3, 5, 1);
    jog_cue_update_hook((long) &c3, 0, 0);
    CHECK("identity: deck 1's stems never reach deck 4's jog", g_rec[5].n == 0);
    g_jog_active[3] = JOG_ON | 0x2;
    jog_cue_update_hook((long) &c3, 0, 0);
    CHECK("identity: deck 4's own reach it (inst alone: pad 2 and its solo pad 6)",
          g_rec[5].n == 1 && set_mask(&g_rec[5]) == 0x22);
    jog_cue_slot_hook((long) &g_q[5], (const void *) (c3.b + 0x8d0), 5);
    CHECK("identity: a table sent outside a collector update is EP147's, untouched",
          g_rec[5].n == 2 && g_rec[5].color[0] == 33);
    g_jog_hooks = 0;
    g_jog_active[3] = JOG_ON | 0x4;
    jog_cue_update_hook((long) &c3, 0, 0);
    CHECK("identity: hooks not all in -> nothing", g_rec[5].n == 2);
    g_jog_hooks = 1;

    /* ---- threads: publisher, pad-mode records, and collector updates (two on one deck) */
    reset_all();
    _Atomic int stop = 0;
    mk_coll(&g_tc[0], (uintptr_t) g_cue_vt, 1, 8, 1);
    mk_coll(&g_tc[1], (uintptr_t) g_cue_vt, 1, 9, 1);
    mk_coll(&g_tc[2], (uintptr_t) g_cue_vt, 0, 10, 1);
    mk_coll(&g_tc[3], (uintptr_t) g_cue_vt, 3, 11, 1);
    struct thr th[7] = {{0, &stop}, {0, &stop}, {1, &stop}, {0, &stop}, {1, &stop}, {2, &stop}, {3, &stop}};
    pthread_t pt[7];
    pthread_create(&pt[0], NULL, publisher, &th[0]);
    pthread_create(&pt[1], NULL, pad_thread, &th[1]);
    pthread_create(&pt[2], NULL, pad_thread, &th[2]);
    for (int i = 3; i < 7; i++)
        pthread_create(&pt[i], NULL, cue_thread, &th[i]);
    struct timespec ts = {0, 300 * 1000 * 1000};
    nanosleep(&ts, NULL);
    atomic_store(&stop, 1);
    for (int i = 0; i < 7; i++)
        pthread_join(pt[i], NULL);
    publish(1, 1, 1, 0x6);
    run_padinfo(1, 1, &d4, &d5);
    t_send_inside = 0;
    jog_cue_update_hook((long) &g_tc[0], 0, 0);
    jog_cue_update_hook((long) &g_tc[0], 0, 0);
    CHECK("threads: after the race the deck settles on its last stem state",
          g_jog_active[1] == (JOG_ON | 6) && g_jog_busy[1] == 0 && g_jog_sent[1] == (JOG_ON | 6));

    timing_diag();
    preview_strip();
    return ut_summary();
}
