/* SPDX-License-Identifier: GPL-3.0-only */
/* The BEAT LOOP page flip (guest/azmod/bl_flip.h and bus_interpose.c erp_beat_loop_page): with
 * stems on BEAT LOOP's first page, a second BEAT LOOP press flips azmod's page only on a deck
 * whose stems are engaged. On a deck without stems the press reaches EP147, as stock, so its
 * second BEAT LOOP page stays reachable. Each logical deck answers for itself. BLF_SRC = the
 * extracted erp_beat_loop_page under test. */
#define _Atomic /* single-threaded harness: the builtins need plain integers on clang */
#include "check.h"
#include <stdint.h>
#include <string.h>

#define AZMOD_PAD_DECKS 2
#define AZMOD_STEM_DECKS 4
#include "bl_flip.h"

static int g_swap = 1; /* "Stems on BEAT LOOP 2" off */
static int bl_swap(void) { return g_swap; }
static volatile int g_wave_forcestems = 1;
static volatile int g_erp_active[AZMOD_STEM_DECKS];
static _Atomic int g_bl_vpage2[AZMOD_STEM_DECKS];
static int g_mode[AZMOD_STEM_DECKS]; /* pad_mode_fresh's answer per deck */
static int pad_mode_fresh(int side, int ld) {
    (void) side;
    return g_mode[ld];
}
static int g_stamps;
static void frame_crc_stamp(uint8_t *b, int n) {
    (void) b;
    (void) n;
    g_stamps++;
}

#include BLF_SRC

enum {
    MODE_OFF = 2
};
/* One BEAT LOOP press and release on side d driving logical deck ld. 1 = EP147 saw the bit. */
static int press(int d, int ld) {
    uint8_t f[32];
    memset(f, 0, sizeof f);
    f[MODE_OFF] = 0x02;
    erp_beat_loop_page(f, d, ld, MODE_OFF, 0x02u, 1);
    int seen = (f[MODE_OFF] & 0x02u) != 0;
    memset(f, 0, sizeof f); /* release */
    erp_beat_loop_page(f, d, ld, MODE_OFF, 0, 1);
    return seen;
}
static int vp(int ld) { return __atomic_load_n(&g_bl_vpage2[ld], __ATOMIC_RELAXED); }

int main(void) {
    ut_suite = "beat loop flip";

    CHECK("rule: swap, stems on, deck stems, mode 3 = flip", bl_flip_swallow(1, 1, 1, 4));
    CHECK("rule: a deck without stems = EP147's", !bl_flip_swallow(1, 1, 0, 4));
    CHECK("rule: Stems on BEAT LOOP 2 = EP147's", !bl_flip_swallow(0, 1, 1, 4));
    CHECK("rule: STEMS master off = EP147's", !bl_flip_swallow(1, 0, 1, 4));
    CHECK("rule: EP147 on its page 2 (mode 4) = EP147's", !bl_flip_swallow(1, 1, 1, 5));
    CHECK("rule: mode not known (0 / stale) = EP147's",
          !bl_flip_swallow(1, 1, 1, 0) && !bl_flip_swallow(1, 1, 1, -1));

    /* #101: deck 1 has no stems, stems on page 1, EP147 in BEAT LOOP mode 3 */
    g_mode[0] = 4;
    g_erp_active[0] = 0;
    CHECK("no stems: the press reaches EP147 (its page 2 is reachable)", press(0, 0) == 1);
    CHECK("no stems: azmod's page is not flipped", vp(0) == 0);
    CHECK("no stems: a second press reaches EP147 too", press(0, 0) == 1 && vp(0) == 0);

    /* CONTROL, same round: deck 2 has stems, so its press is azmod's page flip */
    g_mode[1] = 4;
    g_erp_active[1] = 1;
    CHECK("CONTROL: stems deck: the press is swallowed", press(1, 1) == 0);
    CHECK("CONTROL: stems deck: azmod's page flips to 2", vp(1) == 1);
    CHECK("CONTROL: stems deck: again flips back to 1", press(1, 1) == 0 && vp(1) == 0);
    CHECK("deck 1 untouched by deck 2's flips", vp(0) == 0);

    /* decks 3/4 answer for themselves on the same physical sides */
    g_mode[2] = 4;
    g_erp_active[2] = 0;
    g_mode[3] = 4;
    g_erp_active[3] = 1;
    CHECK("deck 3 without stems (side A): EP147's", press(0, 2) == 1 && vp(2) == 0);
    CHECK("deck 4 with stems (side B): azmod's flip", press(1, 3) == 0 && vp(3) == 1);

    /* a stems track on azmod's page 2 is ejected, a track without stems loaded */
    g_erp_active[3] = 0;
    CHECK("after the eject: the press reaches EP147", press(1, 3) == 1);
    CHECK("after the eject: azmod's page resets (EP147 owns the pages)", vp(3) == 0);
    /* EP147 is now on its page 2 (mode 4); a stems track is loaded there */
    g_mode[3] = 5;
    g_erp_active[3] = 1;
    CHECK("stems loaded on EP147's page 2: the press reaches EP147 (back to page 1 = stems)",
          press(1, 3) == 1 && vp(3) == 0);

    /* Stems on BEAT LOOP 2: no swap at all, even with stems */
    g_swap = 0;
    g_mode[1] = 4;
    g_erp_active[1] = 1;
    CHECK("Stems on BEAT LOOP 2: stems deck press reaches EP147", press(1, 1) == 1 && vp(1) == 0);
    g_swap = 1;

    /* an unknown logical deck never flips anything */
    CHECK("unknown deck: EP147's", press(0, -1) == 1);
    return ut_summary();
}
