/* SPDX-License-Identifier: GPL-3.0-only */
/* Drum Roll HOLD (pad 7), extracted by run.sh: erp_pads_drum_roll and mixer_xpad_drum
 * (azmod/bus_interpose.c) with drum_hold_frame, drum_hold_clear, drum_disarm_all and
 * drum_disarm_others (azmod/drum_roll_display.c), driven frame by frame. azstem's control
 * surface is a model with its real rules: a roll sounds while gate && touch, and the gate
 * forgets the finger (azstem_drum_gate).
 *
 * Without HOLD a roll follows the finger, as before. With HOLD a release is not passed on,
 * so the roll goes on at the last zone touched; pad 7 again, any disarm (pad 8, arming
 * another deck, the FX controls, the mod off) ends it, and so does leaving the deck's
 * drum-roll pads (drum_hold_surface: another pad mode, or the pad set on another deck); a HOLD
 * turned off on the pad bus while the x-pad's frame is passing it stays off. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#define AZMOD_STEM_DECKS 4
#define AZMOD_PAD_DECKS 2
enum {
    DRUM_HOLD_OFF = 0,
    DRUM_HOLD_ON = 1,
    DRUM_HOLD_RUN = 2
};
enum {
    DRUM_HOLD_KEEP = 0,
    DRUM_HOLD_PASS = 1
};
static int g_drum_held[AZMOD_STEM_DECKS], g_drum_hold[AZMOD_STEM_DECKS];
static int g_drum_hold_zone[AZMOD_STEM_DECKS];
static int g_drum_arm = 1, g_pad_mode_slip[2];
static unsigned g_caption_repaint;
static volatile unsigned g_drum_sel_led[AZMOD_STEM_DECKS];
static int g_xpad_raw, g_fxrow_zone = -1;
static FILE *azlog_open(const char *p, const char *m) {
    (void) p;
    (void) m;
    return NULL;
}
static void frame_crc_stamp(uint8_t *b, int n) {
    (void) b;
    (void) n;
}
/* The x-pad layout the bus config hands out: position byte 0, zone bitmap byte 1, 125 steps */
struct bus_xpad {
    int pos, touch, mask, span, zone;
};
static void bus_cfg_xpad(uint64_t c, struct bus_xpad *x) {
    (void) c;
    x->pos = 0;
    x->touch = -1;
    x->mask = 0;
    x->span = 125;
    x->zone = 1;
}
static void bus_capture_xpad_fx(uint8_t *b, ssize_t r, const struct bus_xpad *xp, int touched,
                                int have_zone) {
    (void) b, (void) r, (void) xp, (void) touched, (void) have_zone;
}
/* azstem, as it behaves */
static int st_gate[AZMOD_STEM_DECKS], st_touch[AZMOD_STEM_DECKS], st_rate[AZMOD_STEM_DECKS];
static int st_sel[AZMOD_STEM_DECKS], st_touch_calls;
static int race_pad7_off = -1; /* a deck: its pad 7 turns HOLD off during the next touch call */
static void st_gate_fn(int d, int on) {
    st_touch[d] = 0;
    st_gate[d] = on;
}
static void st_select_fn(int d, int s) { st_sel[d] = s; }
static void st_rate_fn(int d, int i) { st_rate[d] = i; }
static void st_touch_fn(int d, int on);
static void (*g_drum_gate_fn)(int, int) = st_gate_fn;
static void (*g_drum_select_fn)(int, int) = st_select_fn;
static void (*g_drum_rate_fn)(int, int) = st_rate_fn;
static void (*g_drum_touch_fn)(int, int) = st_touch_fn;
static int drum_hold_frame(int hold, int touched);
static void drum_hold_clear(int ld);
static void drum_hold_surface(int d, int ld, int drum_side);
static void drum_disarm_all(const char *why);
static void drum_disarm_others(int keep, const char *why);
/* single-threaded here; clang refuses __atomic_* on an _Atomic object (the log counter) */
#define _Atomic
#include HOLD_INC
#undef _Atomic
#include "check.h"

static void st_touch_fn(int d, int on) {
    st_touch_calls++;
    st_touch[d] = on;
    if (race_pad7_off == d) { /* the pad bus, between this frame's load and its update */
        race_pad7_off = -1;
        drum_hold_clear(d);
    }
}
static int sounding(int d) { return st_gate[d] && st_touch[d]; }

/* One pad frame for side 0 / deck d: the pads held now (bits 0-7 = pads 1-8) */
static unsigned last_pads;
static void pads(int d, unsigned now) {
    uint8_t eb[32] = {0};
    eb[14] = (uint8_t) now;
    unsigned rise = now & ~last_pads;
    last_pads = now;
    erp_pads_drum_roll(eb, 0, d, 14, 13, now, rise, 1);
}
static void tap(int d, unsigned pad) { /* press and release one pad (1-8) */
    pads(d, 1u << (pad - 1));
    pads(d, 0);
}
/* One x-pad frame: zone 0-5 under the finger, or -1 for no finger */
static void xpad(int zone) {
    uint8_t b[8] = {0};
    if (zone >= 0) {
        b[0] = (uint8_t) (zone * 125 / 6 + 5);
        b[1] = 1;
    }
    mixer_xpad_drum(b, sizeof b, 0);
}
static void reset(void) {
    memset(g_drum_held, 0, sizeof g_drum_held);
    memset(g_drum_hold, 0, sizeof g_drum_hold);
    memset(g_drum_hold_zone, 0, sizeof g_drum_hold_zone);
    memset(st_gate, 0, sizeof st_gate);
    memset(st_touch, 0, sizeof st_touch);
    memset(st_rate, 0, sizeof st_rate);
    memset(st_sel, 0, sizeof st_sel);
    last_pads = 0;
    g_fxrow_zone = -1;
    race_pad7_off = -1;
    drum_hold_surface(0, 0, 0); /* neither pad set on the drum-roll pads (HOLD off: no effect) */
    drum_hold_surface(1, 0, 0);
}

int main(void) {
    ut_suite = "drum roll HOLD";

    reset(); /* without HOLD: the roll follows the finger, as before */
    tap(0, 8);
    CHECK("pad 8 arms deck 1, HOLD off", g_drum_held[0] && st_gate[0] && g_drum_hold[0] == DRUM_HOLD_OFF);
    xpad(3);
    CHECK("no HOLD: a finger on zone 4 rolls at that rate", sounding(0) && st_rate[0] == 3);
    xpad(-1);
    CHECK("no HOLD: the release stops the roll", !sounding(0));
    CHECK("no HOLD: the row art goes idle on release", g_fxrow_zone == -1);

    reset(); /* HOLD: the release is not passed on */
    tap(0, 8);
    tap(0, 7);
    CHECK("pad 7 turns HOLD on, waiting for a touch", g_drum_hold[0] == DRUM_HOLD_ON);
    xpad(-1);
    CHECK("HOLD on, untouched: nothing rolls yet", !sounding(0));
    xpad(2);
    CHECK("HOLD: a touch rolls at its zone", sounding(0) && st_rate[0] == 2 && g_drum_hold[0] == DRUM_HOLD_RUN);
    xpad(-1);
    xpad(-1);
    CHECK("HOLD: after the release the roll goes on at the same rate", sounding(0) && st_rate[0] == 2);
    CHECK("HOLD: the row art keeps the held zone", g_fxrow_zone == 2);
    xpad(5);
    xpad(-1);
    CHECK("HOLD: another zone changes the held rate", sounding(0) && st_rate[0] == 5);
    tap(0, 2);
    CHECK("HOLD: pad 2 changes the sample, the roll goes on", st_sel[0] == 1 && sounding(0));
    tap(0, 7);
    CHECK("pad 7 again: HOLD off and the roll stops", g_drum_hold[0] == DRUM_HOLD_OFF && !sounding(0));
    xpad(1);
    xpad(-1);
    CHECK("after HOLD off the roll follows the finger again", !sounding(0));

    reset(); /* every disarm ends HOLD */
    tap(0, 8);
    tap(0, 7);
    xpad(4);
    xpad(-1);
    tap(0, 8);
    CHECK("pad 8 disarms: the held roll stops, HOLD off", !sounding(0) && g_drum_hold[0] == DRUM_HOLD_OFF);
    tap(0, 8);
    xpad(-1);
    CHECK("re-armed: no HOLD left over, nothing rolls", g_drum_hold[0] == DRUM_HOLD_OFF && !sounding(0));

    reset();
    tap(0, 8);
    tap(0, 7);
    xpad(4);
    xpad(-1);
    tap(1, 8);
    CHECK("arming deck 2 disarms deck 1: its held roll stops, HOLD off",
          !sounding(0) && g_drum_hold[0] == DRUM_HOLD_OFF && g_drum_held[1]);

    reset();
    tap(0, 8);
    tap(0, 7);
    xpad(4);
    xpad(-1);
    drum_disarm_all("fx on/off");
    CHECK("the FX controls disarm: the held roll stops, HOLD off", !sounding(0) && g_drum_hold[0] == DRUM_HOLD_OFF);

    reset(); /* leaving the drum-roll pads ends HOLD, where pad 7 could no longer stop it */
    drum_hold_surface(0, 0, 1);
    tap(0, 8);
    tap(0, 7);
    xpad(2);
    xpad(-1);
    drum_hold_surface(0, 0, 1);
    CHECK("staying on the drum-roll pads keeps the held roll", sounding(0) && g_drum_hold[0] == DRUM_HOLD_RUN);
    drum_hold_surface(1, 1, 0);
    CHECK("the other pad set leaving its pads does not touch it", sounding(0) && g_drum_hold[0] == DRUM_HOLD_RUN);
    drum_hold_surface(0, 0, 0);
    CHECK("another pad mode: the held roll stops, HOLD off, still armed",
          !sounding(0) && g_drum_hold[0] == DRUM_HOLD_OFF && g_drum_held[0] && st_gate[0]);

    reset();
    drum_hold_surface(0, 0, 1);
    tap(0, 8);
    tap(0, 7);
    xpad(4);
    xpad(-1);
    drum_hold_surface(0, 2, 1);
    CHECK("deck select to deck 3: deck 1's held roll stops, HOLD off", !sounding(0) && g_drum_hold[0] == DRUM_HOLD_OFF);

    reset(); /* pad 7 on an unarmed deck is SLIP LOOP's, not HOLD */
    tap(0, 7);
    CHECK("pad 7 unarmed: no HOLD", g_drum_hold[0] == DRUM_HOLD_OFF);

    reset(); /* HOLD turned off on the pad bus while the x-pad frame is passing it */
    tap(0, 8);
    tap(0, 7);
    race_pad7_off = 0;
    xpad(3);
    CHECK("race: HOLD turned off mid-frame stays off", g_drum_hold[0] == DRUM_HOLD_OFF);
    xpad(-1);
    CHECK("race: the next release stops the roll", !sounding(0));

    reset(); /* drum_hold_frame itself */
    CHECK("frame: no HOLD passes a touch", drum_hold_frame(DRUM_HOLD_OFF, 1) == DRUM_HOLD_PASS);
    CHECK("frame: no HOLD passes a release", drum_hold_frame(DRUM_HOLD_OFF, 0) == DRUM_HOLD_PASS);
    CHECK("frame: HOLD passes a touch", drum_hold_frame(DRUM_HOLD_RUN, 1) == DRUM_HOLD_PASS);
    CHECK("frame: HOLD keeps a release", drum_hold_frame(DRUM_HOLD_RUN, 0) == DRUM_HOLD_KEEP &&
                                             drum_hold_frame(DRUM_HOLD_ON, 0) == DRUM_HOLD_KEEP);
    return ut_summary();
}
