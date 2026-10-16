/* SPDX-License-Identifier: GPL-3.0-only */
/* The 60 FPS group as 1.2.2 has it (legacy-1.2.2/fps60.inc: menu.c c_toggle_fps and
 * azmod_mod_toggle in 1.2.2), built against 1.2.2's own menu.h, as fps60_test.c's oracle.
 * st[4] = {60 FPS Waveforms, 60 FPS UI, memory waveforms, memory UI} = slots 4, 19, 30, 31. */
#include "menu.h"
#include <string.h>
#include "fps60.inc"
static void put(azmod_state *s, const int st[4]) {
    memset(s, 0, sizeof *s);
    s->mod_enabled[AZMOD_ACTION_UI60] = st[0];
    s->mod_enabled[AZMOD_ACTION_UI60UI] = st[1];
    s->mod_enabled[AZMOD_ACTION_FPSMEM_WAVE] = st[2];
    s->mod_enabled[AZMOD_ACTION_FPSMEM_UI] = st[3];
}
static void get(const azmod_state *s, int st[4]) {
    st[0] = s->mod_enabled[AZMOD_ACTION_UI60];
    st[1] = s->mod_enabled[AZMOD_ACTION_UI60UI];
    st[2] = s->mod_enabled[AZMOD_ACTION_FPSMEM_WAVE];
    st[3] = s->mod_enabled[AZMOD_ACTION_FPSMEM_UI];
}
void oracle_group_tap(int st[4]) { /* the 60 FPS left checkbox */
    azmod_state s;
    put(&s, st);
    c_toggle_fps(&s);
    get(&s, st);
}
void oracle_hand_toggle(int st[4], int member) { /* member 0 = Waveforms, 1 = UI */
    azmod_state s;
    put(&s, st);
    azmod_mod_toggle(&s, member ? AZMOD_ACTION_UI60UI : AZMOD_ACTION_UI60);
    get(&s, st);
}
