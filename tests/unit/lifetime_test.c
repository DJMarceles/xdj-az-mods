/* SPDX-License-Identifier: GPL-3.0-only */
/* EP147 object lifetime in azstem (extracted by run.sh from azstem/stem_playback.c and
 * deck_track_map.c): what azstem keeps of gui::LoadState, gui::LoadedTrackInfo and
 * dj_player::DjPlayerUnit, and how it reads them.
 *   - A LoadState is published only after its ctor returned and the object proved itself
 *     (vtable, magic, deck), and unpublished by a destructor hook before the original runs.
 *   - A reader on another thread loads LoadState fields only between ls_enter and ls_leave;
 *     the destructor hook waits for such a reader before the original destructor runs.
 *   - The deck's track (TrackID, title, artist) is copied inside EP147's own LoadedTrackInfo
 *     update into a sequence-locked snapshot, served only while its LoadedTrackInfo is still
 *     the deck's (lti_read). No EP147 memory is read outside EP147's calls.
 *   - The DjPlayerUnit's KeyControlFGPR is read inside the unit's ctor call, chain checked.
 * The fake EP147 objects are ordinary heap memory with the layouts the code expects. */
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define NDECK 4
#define MAX_DECKS 8
static const int STEM_SLOT[NDECK] = {0, 1, 2, 3};
#define logln(...) ((void) 0)
#define EP_2ddb878 ((uintptr_t) 0x2ddb878) /* LoadState vtable */
#define EP_2dd9288 ((uintptr_t) 0x2dd9288) /* LoadedTrackInfo vtable */
#define EP_1f91210 ((uintptr_t) 0x1f91210)
#define EP_14a2560 ((uintptr_t) 0x14a2560)
#define EP_14a2270 ((uintptr_t) 0x14a2270)
#define EP_1f9c498 ((uintptr_t) 0x1f9c498)
#define EP_1fa16d0 ((uintptr_t) 0x1fa16d0)
#define AK_VT_PBU ((uintptr_t) 0x289f798)
#define AK_VT_MGR ((uintptr_t) 0x27b8a50)
#define FGPR_VTABLE ((uintptr_t) 0x27ba108)
#define AK_SUB 0xe0UL
#define IDMAP_PBU_OFF 0x180UL
#define IDMAP_MGR_OFF 0x10UL
#define IDMAP_SUB_OFF 0x48UL
static _Atomic(void *) g_deck_self[MAX_DECKS];
static _Atomic unsigned g_ctor_calls[MAX_DECKS], g_ap_rebind, g_ap_ctor;
#include LIFE_INC
#include "check.h"

/* a fake gui::LoadedTrackInfo (vtable +0, TrackID +0x08, title +0x38, artist +0x40) and a fake
 * gui::LoadState (vtable +0, LoadedTrackInfo* +0x150, deck +0x47c, magic +0x4a8) */
static void *mk_lti(void) {
    unsigned char *p = aligned_alloc(16, 0x80);
    memset(p, 0, 0x80);
    uintptr_t vt = LTI_VT;
    static const char empty[] = "";
    const char *e = empty;
    memcpy(p, &vt, 8);
    memcpy(p + LTI_TITLE_OFF, &e, 8);
    memcpy(p + LTI_ARTIST_OFF, &e, 8);
    return p;
}
static void *mk_ls(int deck) {
    unsigned char *p = aligned_alloc(16, 0x4b0);
    memset(p, 0, 0x4b0);
    uintptr_t vt = EP_2ddb878, lti = (uintptr_t) mk_lti();
    unsigned d = (unsigned) deck, m = LS_MAGIC;
    memcpy(p, &vt, 8);
    memcpy(p + LS_LTI_OFF, &lti, 8);
    memcpy(p + LS_DECK_OFF, &d, 4);
    memcpy(p + LS_MAGIC_OFF, &m, 4);
    return p;
}
static uintptr_t lti_of(void *ls) {
    uintptr_t v;
    memcpy(&v, (char *) ls + LS_LTI_OFF, 8);
    return v;
}
/* What EP147's update writes: the TrackID (16 bytes of k), the title, the artist. */
static void lti_set(uintptr_t lti, int k, const char *ti, const char *ar) {
    memset((void *) (lti + LTI_ID_OFF), k, 16);
    memcpy((void *) (lti + LTI_TITLE_OFF), &ti, 8);
    memcpy((void *) (lti + LTI_ARTIST_OFF), &ar, 8);
}
static int upd_k;
static const char *upd_ti, *upd_ar;
static long fake_update(void *s, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7) {
    (void) a1;
    (void) a2;
    (void) a3;
    (void) a4;
    (void) a5;
    (void) a6;
    (void) a7;
    lti_set((uintptr_t) s, upd_k, upd_ti, upd_ar);
    return 0;
}
static int ls_orig_calls, dtor_saw_slot, djctor_saw_slot, djdtor_saw_slot;
static _Atomic int dtor_orig_calls;
static void *watch_self;
static int watch_slot;
static void fake_lsctor(void *s, void *a1, void *a2, void *a3, long deck, void *a5, void *a6, void *a7) {
    (void) s;
    (void) a1;
    (void) a2;
    (void) a3;
    (void) deck;
    (void) a5;
    (void) a6;
    (void) a7;
    ls_orig_calls++;
}
static void fake_lsdtor(void *s) {
    atomic_fetch_add(&dtor_orig_calls, 1);
    for (int d = 0; d < NDECK; d++)
        if (atomic_load(&g_ls_self[d]) == s)
            dtor_saw_slot = 1; /* still published while being torn down */
}
static void fake_djctor(void *s, const void *id, void *ctx, int cap) {
    (void) id;
    (void) ctx;
    (void) cap;
    if (atomic_load(&g_deck_self[watch_slot]) == s)
        djctor_saw_slot = 1; /* published before the ctor finished */
}
static void fake_djdtor(void *s) {
    if (atomic_load(&g_deck_self[watch_slot]) == s)
        djdtor_saw_slot = 1;
}
static void *dtor_thread(void *ls) {
    lsdtor_hook(ls);
    return NULL;
}
static void sleep_ms(int ms) {
    struct timespec ts = {0, ms * 1000000L};
    nanosleep(&ts, NULL);
}
static uintptr_t fake_obj(uintptr_t vt) {
    uintptr_t *p = aligned_alloc(16, 0x200);
    memset(p, 0, 0x200);
    p[0] = vt;
    return (uintptr_t) p;
}

int main(void) {
    ut_suite = "lifetime";
    g_orig_lsctor = fake_lsctor;
    g_orig_lsdtor = fake_lsdtor;
    g_orig_lti_update = fake_update;
    g_orig_djpuctor = fake_djctor;
    g_orig_djpudtor = fake_djdtor;
    unsigned char id[16];
    char ti[64], ar[64];

    /* ---- gui::LoadState publication */
    void *a = mk_ls(1);
    lsctor_hook(a, 0, 0, 0, 1, 0, 0, 0);
    CHECK("LoadState ctor: a valid object for deck 1 is published",
          atomic_load(&g_ls_self[1]) == a && ls_orig_calls == 1);
    CHECK("LoadState ctor: its LoadedTrackInfo becomes deck 1's", atomic_load(&g_lti_tok[1]) == lti_of(a));
    CHECK("lti_read: the state the ctor left (an empty deck) is known",
          lti_read(1, id, ti, sizeof ti, ar, sizeof ar) == 1 && id[0] == 0 && !ti[0]);
    void *wv = mk_ls(2);
    uintptr_t other_vt = 0x2ddb8c8; /* a LoadState secondary vtable: not the object's primary */
    memcpy(wv, &other_vt, 8);
    lsctor_hook(wv, 0, 0, 0, 2, 0, 0, 0);
    CHECK("LoadState ctor: an object without the LoadState vtable is not published",
          atomic_load(&g_ls_self[2]) == NULL && atomic_load(&g_lti_tok[2]) == 0);
    void *wd = mk_ls(3);
    lsctor_hook(wd, 0, 0, 0, 2, 0, 0, 0);
    CHECK("LoadState ctor: deck argument and the object's deck must agree",
          atomic_load(&g_ls_self[2]) == NULL && atomic_load(&g_ls_self[3]) == NULL);

    /* ---- the track, copied inside EP147's update */
    upd_k = 0x5a, upd_ti = "Alone - Mixed", upd_ar = "Conducta";
    lti_update_hook((void *) lti_of(a), 0, 0, 0, 0, 0, 0, 0);
    int r = lti_read(1, id, ti, sizeof ti, ar, sizeof ar);
    CHECK("update hook: TrackID, title and artist copied together",
          r == 1 && id[0] == 0x5a && id[15] == 0x5a && !strcmp(ti, "Alone - Mixed") && !strcmp(ar, "Conducta"));
    upd_k = 0x11, upd_ti = "Other", upd_ar = "Someone";
    lti_update_hook((void *) lti_of(wd), 0, 0, 0, 0, 0, 0, 0); /* deck 3's object was never published */
    r = lti_read(1, id, ti, sizeof ti, ar, sizeof ar);
    CHECK("update hook: an update of an unpublished LoadedTrackInfo changes no deck",
          r == 1 && id[0] == 0x5a && atomic_load(&g_lti_orphans) == 1 &&
              lti_read(3, id, ti, sizeof ti, ar, sizeof ar) == 0);
    /* EP147 zeroes an empty deck in place through the same update */
    upd_k = 0, upd_ti = "", upd_ar = "";
    lti_update_hook((void *) lti_of(a), 0, 0, 0, 0, 0, 0, 0);
    r = lti_read(1, id, ti, sizeof ti, ar, sizeof ar);
    CHECK("update hook: an eject is copied too (zero TrackID, empty strings)", r == 1 && id[0] == 0 && !ti[0]);
    upd_k = 0x5a, upd_ti = "Alone - Mixed", upd_ar = "Conducta";
    lti_update_hook((void *) lti_of(a), 0, 0, 0, 0, 0, 0, 0);

    /* a copy is served only while it belongs to the deck's current LoadedTrackInfo: a newer
     * LoadState for the deck has published its LoadedTrackInfo but not copied it yet */
    uintptr_t tok = atomic_load(&g_lti_tok[1]);
    atomic_store(&g_lti_tok[1], (uintptr_t) mk_lti());
    CHECK("lti_read: a copy of a LoadedTrackInfo that is no longer the deck's is not served",
          lti_read(1, id, ti, sizeof ti, ar, sizeof ar) == 0 && !ti[0] && id[0] == 0);
    atomic_store(&g_lti_tok[1], tok);

    /* ---- ls_enter / ls_leave */
    uintptr_t t = ls_enter(1);
    CHECK("ls_enter: the live LoadState, validated, reader counted in",
          t == (uintptr_t) a && atomic_load(&g_ls_readers[1]) == 1);
    ls_leave(1);
    CHECK("ls_leave: reader counted out", atomic_load(&g_ls_readers[1]) == 0);

    /* the destructor waits for a reader inside the bracket. Not a fixed sleep: the bracket is
     * held until the destructor thread has reached one of its two possible ends: the
     * original destructor ran (a failure), or ls_wait_readers counted a reader inside
     * (g_ls_dtor_waits moved: it is waiting, as it must). So the outcome is decided before the
     * reader leaves, whatever the scheduling; the 10 s bound only turns a hang into a FAIL. */
    t = ls_enter(1);
    unsigned waits0 = atomic_load(&g_ls_dtor_waits);
    pthread_t th;
    pthread_create(&th, NULL, dtor_thread, a);
    for (int ms = 0; ms < 10000 && !atomic_load(&dtor_orig_calls) &&
                     atomic_load(&g_ls_dtor_waits) == waits0;
         ms++)
        sleep_ms(1);
    int ran_early = atomic_load(&dtor_orig_calls);
    CHECK("LoadState dtor: reached its wait or its original within 10 s",
          ran_early || atomic_load(&g_ls_dtor_waits) != waits0);
    CHECK("LoadState dtor: unpublished at once, though a reader is inside",
          atomic_load(&g_ls_self[1]) == NULL && atomic_load(&g_lti_tok[1]) == 0);
    CHECK("LoadState dtor: the original does not run while a reader is inside the bracket", ran_early == 0);
    ls_leave(1);
    pthread_join(th, NULL);
    CHECK("LoadState dtor: runs once the reader left, and was unpublished before it ran",
          atomic_load(&dtor_orig_calls) == 1 && !dtor_saw_slot);
    CHECK("ls_enter: after the destructor, deck 1 is unknown", (t = ls_enter(1)) == 0);
    ls_leave(1);
    CHECK("lti_read: after the destructor, deck 1's track is unknown",
          lti_read(1, id, ti, sizeof ti, ar, sizeof ar) == 0 && !ti[0]);
    /* an update of the destroyed LoadState's LoadedTrackInfo that comes later */
    upd_k = 0x77, upd_ti = "Late", upd_ar = "Late";
    lti_update_hook((void *) lti_of(a), 0, 0, 0, 0, 0, 0, 0);
    CHECK("update hook: an update after the destructor does not bring the deck back",
          lti_read(1, id, ti, sizeof ti, ar, sizeof ar) == 0);

    /* a published object whose memory no longer holds a LoadState (freed and reused) */
    void *b = mk_ls(0);
    lsctor_hook(b, 0, 0, 0, 0, 0, 0, 0);
    unsigned zero = 0;
    memcpy((char *) b + LS_MAGIC_OFF, &zero, 4);
    CHECK("ls_enter: magic gone at use -> unknown", ls_enter(0) == 0);
    ls_leave(0);
    unsigned m = LS_MAGIC, d9 = 9;
    memcpy((char *) b + LS_MAGIC_OFF, &m, 4);
    memcpy((char *) b + LS_DECK_OFF, &d9, 4);
    CHECK("ls_enter: deck field changed at use -> unknown", ls_enter(0) == 0);
    ls_leave(0);
    unsigned d0 = 0;
    memcpy((char *) b + LS_DECK_OFF, &d0, 4);
    memcpy(b, &other_vt, 8);
    CHECK("ls_enter: vtable changed at use -> unknown", ls_enter(0) == 0);
    ls_leave(0);
    uintptr_t vt = EP_2ddb878;
    memcpy(b, &vt, 8);
    CHECK("ls_enter: every call balanced by ls_leave", atomic_load(&g_ls_readers[0]) == 0);

    /* a newer LoadState for the same deck */
    void *c = mk_ls(0);
    lsctor_hook(c, 0, 0, 0, 0, 0, 0, 0);
    CHECK("LoadState dtor of an unpublished object leaves the deck's current one alone",
          (lsdtor_hook(b), atomic_load(&g_ls_self[0]) == c && atomic_load(&g_lti_tok[0]) == lti_of(c)));

    /* ---- dj_player::DjPlayerUnit */
    struct {
        unsigned char no;
        unsigned char pad[7];
        unsigned no32;
    } idu = {2, {0}, 2};
    uintptr_t unit = fake_obj(0x289f878), pbu = fake_obj(AK_VT_PBU), mgr = fake_obj(AK_VT_MGR),
              fg = fake_obj(FGPR_VTABLE);
    ((uintptr_t *) unit)[IDMAP_PBU_OFF / 8] = pbu;
    ((uintptr_t *) pbu)[IDMAP_MGR_OFF / 8] = mgr;
    ((uintptr_t *) mgr)[IDMAP_SUB_OFF / 8] = fg + AK_SUB;
    watch_self = (void *) unit;
    watch_slot = STEM_SLOT[2];
    djctor_saw_slot = 0;
    djpuctor_hook((void *) unit, &idu, 0, 0);
    CHECK("DjPlayerUnit ctor: published only after the ctor returned",
          atomic_load(&g_deck_self[STEM_SLOT[2]]) == (void *) unit && !djctor_saw_slot);
    CHECK("DjPlayerUnit ctor: its KeyControlFGPR read inside the ctor call",
          atomic_load(&g_djpu_fgpr[2]) == fg);
    struct {
        unsigned char no;
        unsigned char pad[7];
        unsigned no32;
    } id3 = {3, {0}, 3};
    uintptr_t unit3 = fake_obj(0x289f878), pbu3 = fake_obj(AK_VT_PBU), mgr3 = fake_obj(AK_VT_MGR),
              notfg = fake_obj(0x1234560);
    ((uintptr_t *) unit3)[IDMAP_PBU_OFF / 8] = pbu3;
    ((uintptr_t *) pbu3)[IDMAP_MGR_OFF / 8] = mgr3;
    ((uintptr_t *) mgr3)[IDMAP_SUB_OFF / 8] = notfg + AK_SUB;
    djpuctor_hook((void *) unit3, &id3, 0, 0);
    CHECK("DjPlayerUnit ctor: a chain that does not end at a KeyControlFGPR gives no FGPR",
          atomic_load(&g_djpu_fgpr[3]) == 0);
    struct {
        unsigned char no;
        unsigned char pad[7];
        unsigned no32;
    } bad = {1, {0}, 3};
    static char unit2[0x1a8];
    djpuctor_hook(unit2, &bad, 0, 0);
    CHECK("DjPlayerUnit ctor: disagreeing deck copies publish nothing",
          atomic_load(&g_deck_self[1]) == NULL && atomic_load(&g_djpu_fgpr[1]) == 0);
    static char unit0[0x1a8];
    struct {
        unsigned char no;
        unsigned char pad[7];
        unsigned no32;
    } id0 = {0, {0}, 0};
    djpuctor_hook(unit0, &id0, 0, 0);
    djdtor_saw_slot = 0;
    djpudtor_hook((void *) unit);
    CHECK("DjPlayerUnit dtor: its slot and FGPR cleared before the original destructor ran",
          atomic_load(&g_deck_self[STEM_SLOT[2]]) == NULL && atomic_load(&g_djpu_fgpr[2]) == 0 &&
              !djdtor_saw_slot);
    CHECK("DjPlayerUnit dtor: other decks' slots untouched", atomic_load(&g_deck_self[STEM_SLOT[0]]) == unit0);
    return ut_summary();
}
