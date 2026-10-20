/* SPDX-License-Identifier: GPL-3.0-only */
/* The order the stem marks worker serves its requests in (guest/azmod/stem_marks.c sm_pop,
 * sm_serve_tracks, sm_work_one) and the Preview Hot Cues queue (guest/azmod/preview_hotcues.c
 * ph_ent, ph_get, ph_serve_one), compiled from the real functions with the library work stubbed:
 * a track row's stems, then the hot cues of the rows on screen, and only then a playlist row;
 * a playlist scan's steps serve both; the newest hot cue request first, and a full queue makes
 * room by dropping the oldest, whose row asks again. Two threads (a paint asking while the
 * worker serves) for the sanitizer passes.
 * SM_SRC / PH_SRC = the extracted functions under test. */
#include "check.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SM_Q 32
#define SM_PLF 0x80000000u
#define PH_CACHE 512
#define PH_Q 32
#define HC_MAX 8

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_sm_cv = PTHREAD_COND_INITIALIZER;
static void sm_lock(void) { pthread_mutex_lock(&g_mu); }
static void sm_unlock(void) { pthread_mutex_unlock(&g_mu); }
static long long g_now;
static long long drag_now_ms(void) { return __atomic_add_fetch(&g_now, 1, __ATOMIC_RELAXED); }
static unsigned sm_hash(const char *t) {
    unsigned h = 2166136261u;
    for (; *t; t++)
        h = (h ^ (unsigned char) *t) * 16777619u;
    return h;
}
static int g_sm_dirty;

static struct {
    unsigned cid, h, media;
    long long at;
    char title[160];
} g_smq[SM_Q];
static int g_smq_n;
static long long g_sm_pop_at;

static struct ph_ent {
    unsigned cid, h;
    unsigned char st, n, stale;
    long long used;
    uint16_t frac[HC_MAX];
    uint32_t rgb[HC_MAX];
} g_phc[PH_CACHE];
static struct {
    unsigned cid, h;
    long long at;
    char title[160];
} g_phq[PH_Q];
static int g_phq_n;
static int g_ph_budget = 0;

/* what was served, in order: 't' track stems, 'p' playlist, 'h' hot cues; with the cid */
static char g_kind[256];
static unsigned g_cid[256];
static int g_nserved;
static void record(char k, unsigned cid) {
    if (g_nserved < 256) {
        g_kind[g_nserved] = k;
        g_cid[g_nserved] = cid;
    }
    g_nserved++;
}
static int ph_resolve(unsigned cid, const char *title, unsigned *n_out, uint16_t *frac,
                      uint32_t *rgb, FILE *lg) {
    (void) title, (void) frac, (void) rgb, (void) lg;
    record('h', cid);
    *n_out = 1;
    return 1;
}
static void sm_serve(unsigned cid, unsigned h, unsigned media, const char *title, FILE *lg) {
    (void) h, (void) media, (void) title, (void) lg;
    record(cid & SM_PLF ? 'p' : 't', cid & ~SM_PLF);
}
static void sm_queue(unsigned cid, const char *title) { /* as sm_status_mb queues a row */
    sm_lock();
    g_smq[g_smq_n].cid = cid;
    g_smq[g_smq_n].h = sm_hash(title);
    g_smq[g_smq_n].media = 0;
    g_smq[g_smq_n].at = drag_now_ms();
    snprintf(g_smq[g_smq_n].title, sizeof g_smq[0].title, "%s", title);
    g_smq_n++;
    sm_unlock();
}
static int ph_serve_one(FILE *lg);
#include SM_SRC
#include PH_SRC

static void reset(void) {
    memset(g_phc, 0, sizeof g_phc);
    g_phq_n = g_smq_n = g_nserved = 0;
}
static int ask(unsigned cid) {
    char t[32];
    uint16_t f[HC_MAX];
    uint32_t c[HC_MAX];
    snprintf(t, sizeof t, "track %u", cid);
    return ph_get(cid, t, f, c);
}
static int served_before(char a, char b) { /* every a before the first b */
    int last_a = -1, first_b = 1 << 30;
    for (int i = 0; i < g_nserved && i < 256; i++) {
        if (g_kind[i] == a)
            last_a = i;
        if (g_kind[i] == b && i < first_b)
            first_b = i;
    }
    return last_a >= 0 && last_a < first_b;
}

static int g_stop, g_asked;
static void *painter(void *arg) {
    (void) arg;
    unsigned k = 1;
    while (!__atomic_load_n(&g_stop, __ATOMIC_ACQUIRE)) {
        ask(1000 + k % 97);
        __atomic_add_fetch(&g_asked, 1, __ATOMIC_RELEASE);
        k = k * 1103515245u + 12345u;
    }
    return 0;
}

int main(void) {
    ut_suite = "stem marks worker order";

    reset(); /* the playlist list just left: its rows queued; then a playlist's tracks open */
    sm_queue(SM_PLF | 11, "Party Playlist 1");
    sm_queue(SM_PLF | 12, "Party Playlist 2");
    sm_queue(SM_PLF | 13, "Disclosure");
    ask(501);
    ask(502);
    while (sm_work_one(0));
    CHECK("the hot cues of the rows on screen before any playlist row", served_before('h', 'p'));
    CHECK("everything queued is served", g_nserved == 5);

    reset();
    ask(601);
    sm_queue(701, "a track");
    sm_queue(SM_PLF | 14, "a playlist");
    while (sm_work_one(0));
    CHECK("a track row's stems first, then hot cues, then the playlist",
          g_nserved == 3 && g_kind[0] == 't' && g_kind[1] == 'h' && g_kind[2] == 'p');

    reset(); /* a playlist scan's step: the rows on screen, never a second playlist */
    sm_queue(SM_PLF | 15, "another playlist");
    sm_queue(702, "a track");
    ask(602);
    ask(603);
    sm_serve_tracks(0);
    CHECK("a playlist scan's step serves track rows and their hot cues",
          g_nserved == 3 && g_kind[0] == 't' && g_kind[1] == 'h' && g_kind[2] == 'h');
    CHECK("a playlist scan's step never starts another playlist", g_smq_n == 1);

    reset(); /* a scroll: the rows on screen now are the newest requests */
    ask(801);
    ask(802);
    ask(803);
    while (ph_serve_one(0));
    CHECK("the newest hot cue request first",
          g_nserved == 3 && g_cid[0] == 803 && g_cid[1] == 802 && g_cid[2] == 801);
    CHECK("a served row has its marks", ask(803) == 1 && ask(801) == 1);

    reset(); /* a long scroll fills the queue */
    for (unsigned i = 0; i < PH_Q + 5; i++)
        ask(900 + i);
    CHECK("a full queue keeps the newest rows", g_phq_n == PH_Q && g_phq[PH_Q - 1].cid == 900 + PH_Q + 4);
    int was = g_phq_n;
    ask(900); /* the row dropped first paints again */
    CHECK("a dropped row asks again when it paints", g_phq_n == was && g_phq[PH_Q - 1].cid == 900);
    while (ph_serve_one(0));
    CHECK("every row on screen gets its answer", ask(900) == 1 && ask(900 + PH_Q + 4) == 1);

    reset(); /* a paint asking while the worker serves */
    pthread_t t;
    pthread_create(&t, 0, painter, 0);
    /* serve while the painter asks: until both have done real work (the thread may start
     * late), bounded so a broken queue fails the check instead of hanging */
    int rounds = 0;
    for (long i = 0; i < 200000000L && (rounds < 50 || __atomic_load_n(&g_asked, __ATOMIC_ACQUIRE) < 2000); i++)
        rounds += sm_work_one(0);
    __atomic_store_n(&g_stop, 1, __ATOMIC_RELEASE);
    pthread_join(t, 0);
    while (sm_work_one(0));
    int pending = 0;
    sm_lock();
    for (int i = 0; i < PH_CACHE; i++)
        pending += g_phc[i].st == 1;
    int qn = g_phq_n;
    sm_unlock();
    CHECK("two threads: the worker served while the paint asked", rounds >= 50);
    CHECK("two threads: the queue drains, no row is left waiting", qn == 0 && pending == 0);

    return ut_summary();
}
