/* SPDX-License-Identifier: GPL-3.0-only */
/* azmod.conf (guest/azmod/azconf.c, compiled from the tree as its own object).
 *
 * Input byte 0 picks the path, the rest is the file content:
 *   even: azconf_load of the bytes as azmod.conf. A valid load must save, and the saved file
 *         must load again as valid and save to the same bytes (a load + save is a fixed point
 *         after the first rewrite). The getters and setters run over the loaded entries.
 *   odd:  azconf_migrate_legacy over 1.2.2's files: the rest is split at 0x00 into azmod.cfg,
 *         azmod-theme, azmod-vinyl.v and azmod-btchannel (a missing part = an absent file),
 *         byte 1 picks the flag files and the slot count. A migration's result must save and
 *         load back as valid. */
#include "azconf.h"
#include "fuzz_common.h"

static int same_file(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    int same = fa && fb;
    while (same) {
        int x = fgetc(fa), y = fgetc(fb);
        if (x != y) same = 0;
        if (x == EOF || y == EOF) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return same;
}

static void exercise(struct azconf *c) {
    static const char *const keys[] = {"version", "theme", "padcolor.0", "pinned", "a", "drumroll.bank", "x:y"};
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        const char *v = azconf_get(c, keys[i]);
        (void) azconf_get_bool(c, keys[i], 1);
        (void) azconf_get_int(c, keys[i], 7, -5, 1000000);
        if (v) FZ_ASSERT(strlen(v) <= 511);
    }
    FZ_ASSERT(azconf_set(c, "version", "2") != 0); /* never settable */
    FZ_ASSERT(azconf_set(c, "Bad Key", "x") != 0);
    (void) azconf_set_bool(c, "fz.bool", 1);
    (void) azconf_set_int(c, "fz.int", -42);
    FZ_ASSERT(azconf_get_int(c, "fz.int", 0, -100, 100) == -42);
}

static void load_path(const uint8_t *d, size_t n) {
    const char *p = fz_file("azmod.conf", d, n);
    char path[512], p2[512];
    snprintf(path, sizeof path, "%s", p);
    snprintf(p2, sizeof p2, "%s/second.conf", fz_dir());
    struct azconf *c = azconf_new();
    if (!c) return;
    int r = azconf_load(c, path);
    FZ_ASSERT(r == 0 || r == -2 || r == -3);
    if (r == 0) {
        /* heal: what load kept always saves, except a file at the 64 KB cap with no final newline
         * (the rewrite adds one: 64 KB + 1, which save refuses as load would) */
        if (azconf_save(c, path) != 0) {
            FZ_ASSERT(n >= 64 * 1024 && d[n - 1] != '\n');
            azconf_free(c);
            return;
        }
        struct azconf *c2 = azconf_new();
        FZ_ASSERT(c2 && azconf_load(c2, path) == 0);
        FZ_ASSERT(!azconf_dirty(c2)); /* the healed file drops nothing more */
        FZ_ASSERT(azconf_save(c2, p2) == 0 && same_file(path, p2));
        azconf_free(c2);
        exercise(c);
        if (azconf_save(c, path) == 0) {
            c2 = azconf_new();
            FZ_ASSERT(c2 && azconf_load(c2, path) == 0);
            FZ_ASSERT(azconf_get_int(c2, "fz.int", 0, -100, 100) == -42);
            azconf_free(c2);
        }
    } else {
        FZ_ASSERT(azconf_get(c, "version") && !strcmp(azconf_get(c, "version"), "1"));
    }
    azconf_free(c);
}

static void migrate_path(const uint8_t *d, size_t n) {
    if (n < 1) return;
    uint8_t ctl = d[0];
    d++;
    n--;
    const uint8_t *part[4];
    size_t len[4];
    int k = fz_split(d, n, 0, part, len, 4);
    static const char *const files[] = {"azmod.cfg", "azmod-theme", "azmod-vinyl.v", "azmod-btchannel"};
    for (int i = 0; i < 4; i++) {
        if (i < k) fz_file(files[i], part[i], len[i]);
        else fz_remove(files[i]);
    }
    static const char *const flags[] = {"azmod-plreorder.on", "azmod-rowcache.on", "azmod-touchaccum.on"};
    for (int i = 0; i < 3; i++) {
        if (ctl >> i & 1) fz_file(flags[i], "", 0);
        else fz_remove(flags[i]);
    }
    /* the slot layout of the shipping build's shape: 30-odd slots, a radio set of four, two
     * retired slots, an int slot; the count varies so the width rules see short and long files */
    int nslots = 20 + (ctl >> 3) % 45; /* 20..64 */
    static char names[64][16];
    const char *slot_key[64];
    for (int i = 0; i < nslots; i++) {
        snprintf(names[i], sizeof names[i], "slot.%d", i);
        slot_key[i] = (i == 12 || i == 23) ? NULL : names[i];
    }
    static const int dflt_on[] = {19, 21}, force_on[] = {6}, force_off[] = {12, 23, 26};
    static const int bank[] = {7, 8, 9, 10}, ints[] = {15};
    static const struct azconf_radio radio[] = {{"drumroll.bank", bank, 4}};
    static const char *const themes[] = {"dark", "light", "day"};
    const struct azconf_legacy_defaults def = {
        .default_on = dflt_on, .n_default_on = 2, .force_on = force_on, .n_force_on = 1, .force_off = force_off, .n_force_off = 3, .slot_fps = 4, .slot_overlays = 13, .slot_pledit = 14, .slot_remotestems = 19, .slot_ui60ui = 27, .palette_count = 28, .themes = themes, .n_themes = 3, .radio = radio, .n_radio = 1, .int_slots = ints, .n_int_slots = 1};
    struct azconf *c = azconf_new();
    if (!c) return;
    int r = azconf_migrate_legacy(c, fz_dir(), slot_key, nslots, &def);
    FZ_ASSERT(r == 0 || (r == -1 && k < 1));
    if (r == 0) {
        char out[512];
        snprintf(out, sizeof out, "%s/migrated.conf", fz_dir());
        FZ_ASSERT(azconf_save(c, out) == 0);
        struct azconf *c2 = azconf_new();
        FZ_ASSERT(c2 && azconf_load(c2, out) == 0 && !azconf_dirty(c2));
        azconf_free(c2);
    }
    azconf_free(c);
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (n < 1) return 0;
    if (d[0] & 1) migrate_path(d + 1, n - 1);
    else load_path(d + 1, n - 1);
    return 0;
}
