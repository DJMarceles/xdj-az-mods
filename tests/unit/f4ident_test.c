/* SPDX-License-Identifier: GPL-3.0-only */
/* Cached EP147 pointers revalidated at use (extracted by run.sh):
 *   perdeck   stems_display.c perdeck_capture / perdeck_block / deck_for_perdeck_field: a
 *             per-player block of gui::PlayerInfoState, pinned by a weak reference, answers
 *             only while the state's use count is above zero, its control block still has the
 *             expected vtable and the block still names its deck; anything else is unknown
 *             (-1), never a default deck.
 *   wwb       waveform.c wwb_widget_live: a WWB color-mode widget, through its wref slot
 *             (cleared by juce::Component's destructor), on the message thread, vtable checked
 *             at use; a failed check clears the slot.
 * Fake objects stand in for EP147's; nothing is freed. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ep_mem.h"
#include "check.h"

#define AZMOD_STEM_DECKS 4
#define PERDECK_STRIDE 0x1400L
#define PERDECK_TILEDS 0x1160L
#define PERDECK_MODEOBJ 0x10b0L
#define EP_2defe68 ((uintptr_t) 0x31a3270)   /* the control block's vtable (any value) */
#define WIDGET_COMPONENT_OFF 0x86A0L
static struct {
    unsigned long vt;
    long deckoff;
    long wwboff;
} WAVEW_MODES[3] = {{0x2b66d98, 0, 0x90}, {0x2b67408, 0, 0x87F8}, {0x2b67a78, 0, 0x10F60}};
#include F4_INC

/* ---- a fake make_shared PlayerInfoState: control block (vt, use, weak), then 4 blocks */
static uintptr_t mk_state(uintptr_t vt) {
    size_t n = 0x10 + 4 * PERDECK_STRIDE;
    uintptr_t c = (uintptr_t) aligned_alloc(16, n);
    memset((void *) c, 0, n);
    *(uintptr_t *) c = vt;
    *(int *) (c + 8) = 31; /* use */
    *(int *) (c + 12) = 1; /* weak */
    uintptr_t s = c + 0x10;
    for (int d = 0; d < 4; d++) {
        uintptr_t b = s + d * PERDECK_STRIDE;
        *(uintptr_t *) b = (uintptr_t) d;
        *(uintptr_t *) (b + PERDECK_TILEDS) = 0x7000000 + 0x100 * d;  /* the deck's tile ds */
        *(uintptr_t *) (b + PERDECK_MODEOBJ) = 0x7100000 + 0x100 * d; /* its mode object */
    }
    return s;
}
static void reset(void) {
    for (int d = 0; d < 4; d++)
        g_perdeck[d] = 0;
    g_pis = 0;
    g_pis_pins = 0;
}
static void file_all(uintptr_t s) {
    for (int d = 0; d < 4; d++)
        perdeck_capture((long) (s + d * PERDECK_STRIDE), d);
}
static int wref_count(uintptr_t s) { return *(int *) (s - 0x10 + 12); }

static void *off_thread(void *a) {
    *(uintptr_t *) a = wwb_widget_live(0, 1);
    return NULL;
}

int main(void) {
    ut_suite = "f4 identity";
    msg_thread_note(); /* main is the message thread */

    /* ---------------- perdeck */
    uintptr_t s = mk_state(EP_2defe68);
    reset();
    file_all(s);
    CHECK("perdeck: four blocks of a known state are filed", perdeck_block(0) && perdeck_block(3));
    CHECK("perdeck: the state is pinned once (one weak reference for four blocks)", wref_count(s) == 2);
    CHECK("perdeck: tile ds of deck 3 -> deck 3", deck_for_perdeck_field(0x7000300, PERDECK_TILEDS) == 3);
    CHECK("perdeck: mode object of deck 1 -> deck 1", deck_for_perdeck_field(0x7100100, PERDECK_MODEOBJ) == 1);
    CHECK("perdeck: a value no block holds -> unknown, not deck 0",
          deck_for_perdeck_field(0x7999999, PERDECK_TILEDS) == -1);

    /* block filed under the wrong deck: word 0 is the player index */
    reset();
    perdeck_capture((long) (s + 1 * PERDECK_STRIDE), 2);
    CHECK("perdeck: block 1 offered as deck 2 is not filed", !perdeck_block(2));

    /* not a PlayerInfoState: the control block's vtable differs */
    uintptr_t x = mk_state(0x1234560);
    reset();
    file_all(x);
    /* clang-format off */
    CHECK("perdeck: a block whose control block is another type is not filed", g_perdeck[0] == 0 &&
          deck_for_perdeck_field(0x7000000, PERDECK_TILEDS) == -1);
    /* clang-format on */
    CHECK("perdeck: ...and no weak reference is taken on it", wref_count(x) == 1);

    /* disposed: use count 0 -> unknown, and unfiled */
    reset();
    file_all(s);
    *(int *) (s - 0x10 + 8) = 0;
    CHECK("perdeck: state disposed (use count 0) -> unknown", deck_for_perdeck_field(0x7000200, PERDECK_TILEDS) == -1);
    CHECK("perdeck: ...and the block is unfiled", g_perdeck[2] == 0);
    *(int *) (s - 0x10 + 8) = 31;

    /* the block no longer names its deck */
    reset();
    file_all(s);
    *(uintptr_t *) (s + 2 * PERDECK_STRIDE) = 7;
    CHECK("perdeck: block 2 whose word 0 changed -> unknown", deck_for_perdeck_field(0x7000200, PERDECK_TILEDS) == -1);
    *(uintptr_t *) (s + 2 * PERDECK_STRIDE) = 2;

    /* the control block's vtable changed under the pin */
    reset();
    file_all(s);
    *(uintptr_t *) (s - 0x10) = 0x5a5a5a5a;
    CHECK("perdeck: control block vtable changed -> unknown", deck_for_perdeck_field(0x7000000, PERDECK_TILEDS) == -1);
    *(uintptr_t *) (s - 0x10) = EP_2defe68;

    /* a filed pointer that is not block d of the pinned state */
    reset();
    file_all(s);
    g_perdeck[1] = (long) (s + 3 * PERDECK_STRIDE);
    CHECK("perdeck: entry 1 pointing at block 3 -> unknown", deck_for_perdeck_field(0x7000300, PERDECK_TILEDS) != 1);

    /* ---------------- wwb */
    g_compdtor_ok = 1;
    CHECK("wwb: slots tracked", wref_track(&g_wwb_comp[0][0], 8 * 3, 0));
    uintptr_t wwb = (uintptr_t) aligned_alloc(16, 0x1A000);
    memset((void *) wwb, 0, 0x1A000);
    for (int m = 0; m < 3; m++) {
        *(uintptr_t *) (wwb + WAVEW_MODES[m].wwboff) = WAVEW_MODES[m].vt;
        wref_set(&g_wwb_comp[0][m], wwb + WAVEW_MODES[m].wwboff + WIDGET_COMPONENT_OFF);
    }
    CHECK("wwb: a live widget of the right class is returned", wwb_widget_live(0, 1) == wwb + 0x87F8);
    uintptr_t got = 1;
    pthread_t t;
    pthread_create(&t, NULL, off_thread, &got);
    pthread_join(t, NULL);
    CHECK("wwb: off the message thread -> unknown", got == 0);
    wref_on_destroy(wwb + 0x90 + WIDGET_COMPONENT_OFF); /* juce::Component's destructor */
    CHECK("wwb: destroyed widget (slot cleared) -> unknown", wwb_widget_live(0, 0) == 0);
    *(uintptr_t *) (wwb + 0x10F60) = 0x4242;
    CHECK("wwb: widget whose vtable changed -> unknown", wwb_widget_live(0, 2) == 0);
    CHECK("wwb: ...and its slot is cleared", wref_get(&g_wwb_comp[0][2]) == 0);
    unsigned long keep = WAVEW_MODES[1].vt;
    WAVEW_MODES[1].vt = 0;
    CHECK("wwb: mode vtable not resolved yet -> unknown", wwb_widget_live(0, 1) == 0);
    WAVEW_MODES[1].vt = keep;
    CHECK("wwb: an untouched slot still answers", wwb_widget_live(0, 1) == wwb + 0x87F8);
    return ut_summary();
}
