/* SPDX-License-Identifier: GPL-3.0-only */
/* Deck screen evidence from the pad-mode getter (guest/azmod/stems_display.c
 * modeget_from_deck_screen), extracted and built against EP147's real addresses.
 *
 * The rule under test: a getter call marks the deck screen as up only when one of the deck
 * screen's own widgets made it (the pad-mode dispatcher or the pad title). azmod's own reads of
 * the mode run while EP147 builds its views at startup, before any screen is shown; counting
 * them maps the deck-view overlays over a black screen for a frame or a few. run.sh also builds
 * this from mutated copies that count every caller, or drop the title's window; each must fail. */
#include <stdint.h>
#include <stdio.h>

#define WAVE_SWITCH 0x1df6010UL
#define TITLE_UPDATE 0x1df3ae0UL
#define TITLE_UPDATE_END 0x1df4908UL

#include DECKSCREEN_INC
#include "check.h"

int main(void) {
    ut_suite = "deck screen evidence";
    /* return addresses measured on the emulator: the dispatcher's call and the title's */
    CHECK("the pad-mode dispatcher's call (0x1df6034) is the deck screen",
          modeget_from_deck_screen(0x1df6034UL));
    CHECK("the pad title's call (0x1df3b04) is the deck screen", modeget_from_deck_screen(0x1df3b04UL));
    /* azmod's own read from waveswitch_hook, at startup with no screen shown */
    CHECK("a call from azmod.so's own code is not the deck screen",
          !modeget_from_deck_screen(0xffffb7eb45e8UL));
    CHECK("a call from elsewhere in EP147 is not the deck screen", !modeget_from_deck_screen(0x1deb250UL));
    CHECK("no return address (0) is not the deck screen", !modeget_from_deck_screen(0));
    /* the windows' edges */
    CHECK("the dispatcher's first byte counts", modeget_from_deck_screen(WAVE_SWITCH));
    CHECK("one byte before the dispatcher does not", !modeget_from_deck_screen(WAVE_SWITCH - 1));
    CHECK("the dispatcher's last byte counts", modeget_from_deck_screen(WAVE_SWITCH + 0x22f));
    CHECK("the byte past the dispatcher does not", !modeget_from_deck_screen(WAVE_SWITCH + 0x230));
    CHECK("the title's first byte counts", modeget_from_deck_screen(TITLE_UPDATE));
    CHECK("the title's end does not", !modeget_from_deck_screen(TITLE_UPDATE_END));
    return ut_summary();
}
