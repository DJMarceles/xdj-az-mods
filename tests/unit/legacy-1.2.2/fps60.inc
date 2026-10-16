/* azmod 1.2.2: guest/azmod/menu.c, frozen as a test oracle; do not edit */
static void c_toggle_fps(azmod_state *s) {
    bool *m = s->mod_enabled;
    if (m[AZMOD_ACTION_UI60] || m[AZMOD_ACTION_UI60UI]) {
        m[AZMOD_ACTION_FPSMEM_WAVE] = m[AZMOD_ACTION_UI60];
        m[AZMOD_ACTION_FPSMEM_UI] = m[AZMOD_ACTION_UI60UI];
        m[AZMOD_ACTION_UI60] = m[AZMOD_ACTION_UI60UI] = false;
    } else {
        bool saved = m[AZMOD_ACTION_FPSMEM_WAVE] || m[AZMOD_ACTION_FPSMEM_UI];
        m[AZMOD_ACTION_UI60] = saved ? m[AZMOD_ACTION_FPSMEM_WAVE] : true;
        m[AZMOD_ACTION_UI60UI] = saved ? m[AZMOD_ACTION_FPSMEM_UI] : true;
        m[AZMOD_ACTION_FPSMEM_WAVE] = m[AZMOD_ACTION_FPSMEM_UI] = false;
    }
    s->config_dirty = true;
}
void azmod_mod_toggle(azmod_state *s, int i) {
    if (i < 0 || i >= AZMOD_MOD_COUNT)
        return;
    s->mod_enabled[i] = !s->mod_enabled[i];
    /* A 60 FPS member chosen by hand replaces whatever the group switch saved. */
    if (i == AZMOD_ACTION_UI60 || i == AZMOD_ACTION_UI60UI)
        s->mod_enabled[AZMOD_ACTION_FPSMEM_WAVE] = s->mod_enabled[AZMOD_ACTION_FPSMEM_UI] = false;
}
