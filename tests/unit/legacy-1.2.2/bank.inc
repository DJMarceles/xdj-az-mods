/* azmod 1.2.2: guest/azmod/mod_menu_overlay.c, frozen as a test oracle; do not edit */
    int bank = 0;
    for (int b = 0; b < AZMOD_DRUM_BANKS; b++)
        if (g_state.mod_enabled[AZMOD_ACTION_DRUMB1 + b]) {
            bank = b + 1;
            break;
        }
    if (!bank) {
        bank = 1;
        g_state.mod_enabled[AZMOD_ACTION_DRUMB1] = 1;
        g_state.config_dirty = true;
    }
