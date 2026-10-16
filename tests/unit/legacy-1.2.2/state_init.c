/* SPDX-License-Identifier: GPL-3.0-only */
/* azmod 1.2.2: guest/azmod/menu.c, frozen as a test oracle; do not edit */
#include "menu.h"
#include <string.h>
void azmod_state_init(azmod_state *s) {
    memset(s, 0, sizeof(*s));
    s->pressed_item = -1;
    s->last_action = -1;
    s->update_pct = -1;
    /* STEMS (mod 0) is the master switch for jog forge + center grid + pad
     * routing (azmod arm_stems). Default OFF so the deck boots stock and the
     * whole stems surface is opt-in; toggling STEMS on arms all three layers. */
    for (int i = 0; i < AZMOD_MOD_COUNT; i++)
        s->mod_enabled[i] = false;
    s->mod_enabled[AZMOD_ACTION_PLSHIFT] =
        false; /* default: a plain knob hold grabs (SHIFT is the option) */
    s->mod_enabled[AZMOD_ACTION_BJ2FIRST] =
        false; /* default OFF: the Extras start off */
    s->mod_enabled[AZMOD_ACTION_REMOTESTEMS] =
        true; /* default ON: MAKE STEMS shows once Stems is on */
    s->mod_enabled[AZMOD_ACTION_VINYLFINE] =
        false; /* default OFF: the Extras start off */
    s->mod_enabled[AZMOD_ACTION_STEMSMARK] =
        true; /* default ON: the stem marks in the track lists */
    /* AZMOD_ACTION_STEMEQ: default OFF (menu.h) */
    s->cpu_temp_c10 = -1; /* no reading yet: the readouts show --, not 0 */
    s->cpu_use_pct = -1;
    s->cpu_clk_mhz = -1;
    s->ram_use_pct = -1;
    s->mod_enabled[AZMOD_ACTION_DRUMSYNC] = true; /* Beat Sync forced ON (hidden row);
                                                   * mod 12 stays false = forced OFF.
                                                   * load_config re-pins both after parse. */
    for (int d = 0; d < AZMOD_STEM_DECKS; d++) /* all stems on */
        for (int i = 0; i < AZMOD_STEM_COUNT; i++)
            s->stem_on[d][i] = true;
    for (int j = 0; j < AZMOD_PADMODE_COUNT; j++)
        s->padmode_color[j] = s->padmode_color2[j] = -1; /* native */
    s->pad_colors_on = true; /* master enable; individual buttons still default to native */
    s->color_sel = 0;
}
