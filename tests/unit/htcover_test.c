/* SPDX-License-Identifier: GPL-3.0-only */
/* Hide Track Title: a press counts for a deck's title bar only when the bar gets it
 * (guest/azmod/ht_cover.h), against a model of the component tree the emulator showed under a
 * press (DefaultMainWindow > WaveformView > PlayerInfo widgets > TrackInfo > Label) and of
 * EP147's full-screen popup layer (gui::OkCancelPopupWidget, the EJECT question):
 *   - a press on deck 1's title text, on its bar's own surface (the eye), and on deck 2's bar
 *     each count for that deck only;
 *   - the same points with the popup up count for no deck (the press goes to the popup);
 *   - a press elsewhere, a deck whose widget is unknown, no top-level component, no hit, and
 *     a parent chain that loops all count for no deck.
 * HTCOVER_HDR = the header under test. */
#include "check.h"
#include <stdint.h>
#include <stdio.h>
#include HTCOVER_HDR

/* the tree: each node's parent */
enum {
    ROOT = 1,
    WAVEVIEW,
    FIELD,
    PI1,
    TI1,
    LABEL1,
    PI2,
    TI2,
    POPUP,
    LOOP_A,
    LOOP_B,
    N_NODES
};
static uintptr_t g_parent[N_NODES] = {
    [WAVEVIEW] = ROOT,
    [FIELD] = WAVEVIEW,
    [PI1] = FIELD,
    [TI1] = PI1,
    [LABEL1] = TI1,
    [PI2] = FIELD,
    [TI2] = PI2,
    [POPUP] = WAVEVIEW,
    [LOOP_A] = LOOP_B,
    [LOOP_B] = LOOP_A,
};
static uintptr_t parent(uintptr_t c) { return c < N_NODES ? g_parent[c] : 0; }
/* the hit test: the popup, when up, covers the whole window and takes every press */
static int g_popup;
static uintptr_t g_force; /* a hit to return instead, 0 = the scene */
enum {
    P_TITLE1 = 1,
    P_EYE1,
    P_BAR2,
    P_WAVE
};
static uintptr_t at(uintptr_t root, int x, int y) {
    (void) root;
    (void) y;
    if (g_force)
        return g_force == (uintptr_t) -1 ? 0 : g_force;
    if (g_popup)
        return POPUP;
    switch (x) {
    case P_TITLE1: return LABEL1; /* the title's label, a child of deck 1's TrackInfo */
    case P_EYE1: return PI1;      /* the bar's own surface, out to the eye */
    case P_BAR2: return TI2;
    default: return WAVEVIEW;
    }
}

int main(void) {
    ut_suite = "hide title cover";
    uintptr_t owner[HT_COVER_DECKS] = {PI1, PI2, 0, 0};
    CHECK("deck 1's title text: deck 1's", ht_cover_mask(owner, ROOT, P_TITLE1, 0, at, parent) == 1u);
    CHECK("deck 1's bar out to the eye: deck 1's", ht_cover_mask(owner, ROOT, P_EYE1, 0, at, parent) == 1u);
    CHECK("deck 2's bar: deck 2's only", ht_cover_mask(owner, ROOT, P_BAR2, 0, at, parent) == 2u);
    CHECK("the waveform: no deck's", ht_cover_mask(owner, ROOT, P_WAVE, 0, at, parent) == 0u);
    g_popup = 1;
    CHECK("a popup over deck 1's title text: no deck's", ht_cover_mask(owner, ROOT, P_TITLE1, 0, at, parent) == 0u);
    CHECK("a popup over deck 1's eye: no deck's", ht_cover_mask(owner, ROOT, P_EYE1, 0, at, parent) == 0u);
    CHECK("a popup over deck 2's bar: no deck's", ht_cover_mask(owner, ROOT, P_BAR2, 0, at, parent) == 0u);
    g_popup = 0;
    uintptr_t unknown[HT_COVER_DECKS] = {0, PI2, 0, 0};
    CHECK("deck 1's widget unknown: its bar counts for nothing", ht_cover_mask(unknown, ROOT, P_TITLE1, 0, at, parent) == 0u);
    CHECK("no top-level component: nothing", ht_cover_mask(owner, 0, P_TITLE1, 0, at, parent) == 0u);
    g_force = (uintptr_t) -1;
    CHECK("no hit: nothing", ht_cover_mask(owner, ROOT, P_TITLE1, 0, at, parent) == 0u);
    g_force = LOOP_A;
    CHECK("a parent chain that loops: nothing, and it ends", ht_cover_mask(owner, ROOT, P_TITLE1, 0, at, parent) == 0u);
    g_force = 0;
    CHECK("ht_hit_in: the owner itself", ht_hit_in(PI2, PI2, parent));
    CHECK("ht_hit_in: a sibling is not inside", !ht_hit_in(PI1, PI2, parent));
    return ut_summary();
}
