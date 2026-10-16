/* SPDX-License-Identifier: GPL-3.0-only */
/* menudump_test.c's adapters for the feature-table code (-DMENUDUMP_NEW="menudump_new.h"):
 * the same dump, read through the table instead of 1.2.2's mod_enabled[]. */
static int slot_value(const azmod_state *s, int i) {
    (void) s;
    return az_cfg_char(i) == '1';
}
static unsigned long long pins_of(const azmod_state *s) {
    (void) s;
    return az_pins_legacy();
}
/* the drum bank is one CHOICE: a load always yields a bank (what 1.2.2's first overlay tick
 * did), so there is nothing left for the tick to do */
static void overlay_first_tick(azmod_state *s) { (void) s; }
#define LEFT_RESET() (g_last_left = -1, g_rscroll = 0)
#define START_VISIBILITY() ((void) 0) /* Phase Meter and Themes are rows of every build now */
static const char *act_name(int a) {
    return a < 0 ? "none" : a == AZI_RELOAD ? "reload"
                        : a == AZI_SYSINFO  ? "sysinfo"
                        : a == AZI_UPDATE   ? "update"
                        : a == AZ_ACT_STEMS ? "stems"
                                            : "row";
}
static void bank_rect(int b, int *x, int *y, int *w, int *h) { item_bank_rect(&g_fb, AZF_DRUM_BANK, b, x, y, w, h); }
/* every load starts from 1.2.2's files alone: no azmod.conf yet (the first 1.2.3 boot) */
static void before_load(void) { unlink(CONF_PATH); }
