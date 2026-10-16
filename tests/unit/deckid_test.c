/* SPDX-License-Identifier: GPL-3.0-only */
/* Deck identity for the pad tiles and pad containers (guest/azmod/stems_display.c
 * deck_for_ds, deck_for_modeobj), extracted and built against a stand-in per-deck table.
 *
 * The rule under test: a lookup that cannot answer returns -1 and never a guessed deck.
 * Before the per-deck table is filled every pointer is unknown; once it is, only a pointer
 * a deck's block holds at the right offset maps to that deck, and any other pointer stays
 * unknown. run.sh also builds this from mutated copies that fall back to arrival order or
 * to a default deck; each must fail. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define AZMOD_STEM_DECKS 4
#define PERDECK_MODEOBJ 0x10b0L
#define PERDECK_TILEDS 0x1160L

/* The per-deck table as tileowner_hook fills it: one block per deck, or 0 before it runs. */
static long g_tile_ds[AZMOD_STEM_DECKS], g_mode_obj[AZMOD_STEM_DECKS];
static int g_table_filled;
static int deck_for_perdeck_field(long p, long off) {
    if (!g_table_filled || p <= 0x10000)
        return -1;
    for (int d = 0; d < AZMOD_STEM_DECKS; d++)
        if ((off == PERDECK_TILEDS ? g_tile_ds[d] : g_mode_obj[d]) == p)
            return d;
    return -1;
}

#include DECKID_INC
#include "check.h"

int main(void) {
    ut_suite = "deck identity";
    /* four decks' tile deckstates and container mode objects, as distinct pointers */
    for (int d = 0; d < AZMOD_STEM_DECKS; d++) {
        g_tile_ds[d] = 0x7f0000100000L + d * 0x780L;
        g_mode_obj[d] = 0x7f0000200000L + d * 0x40L;
    }

    /* before the table is filled: tiles arrive in 4-DECK's row order (DECK 3,1,2,4) */
    int order[4] = {2, 0, 1, 3}, unknown_ds = 1, unknown_mo = 1;
    for (int i = 0; i < 4; i++) {
        unknown_ds &= deck_for_ds(g_tile_ds[order[i]]) == -1;
        unknown_mo &= deck_for_modeobj(g_mode_obj[order[i]]) == -1;
    }
    CHECK("before the table: every tile deckstate is unknown (-1), never an arrival slot", unknown_ds);
    CHECK("before the table: every container mode object is unknown (-1), never a default", unknown_mo);
    CHECK("before the table: asking twice still answers -1", deck_for_ds(g_tile_ds[2]) == -1);

    /* once the table is filled, each pointer maps to its own deck, whatever was asked earlier */
    g_table_filled = 1;
    int right_ds = 1, right_mo = 1;
    for (int d = 0; d < AZMOD_STEM_DECKS; d++) {
        right_ds &= deck_for_ds(g_tile_ds[d]) == d;
        right_mo &= deck_for_modeobj(g_mode_obj[d]) == d;
    }
    CHECK("table filled: each tile deckstate maps to its own deck", right_ds);
    CHECK("table filled: each container mode object maps to its own deck", right_mo);
    CHECK("table filled: a pointer no deck holds is unknown", deck_for_ds(0x7f0000300000L) == -1 &&
                                                                  deck_for_modeobj(0x7f0000300000L) == -1);
    CHECK("table filled: a container object is not a tile deckstate (offsets kept apart)",
          deck_for_ds(g_mode_obj[1]) == -1 && deck_for_modeobj(g_tile_ds[1]) == -1);
    CHECK("a null or small pointer is unknown", deck_for_ds(0) == -1 && deck_for_modeobj(8) == -1);
    return ut_summary();
}
