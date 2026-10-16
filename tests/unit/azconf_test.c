/* SPDX-License-Identifier: GPL-3.0-only */
/* azmod.conf (guest/azmod/azconf.c): parse, rewrite, atomic save, and the one-time
 * migration from 1.2.2's azmod.cfg.
 *
 * The migration's equivalence ORACLE is 1.2.2's own code, frozen in legacy-1.2.2/ (taken
 * from the revision before the feature table): load_config / save_config /
 * settings_write from azmod.c (cfg.inc), the drum-bank radio resolution from
 * mod_menu_overlay.c (bank.inc) and azmod_state_init with 1.2.2's menu.h (state_init.c), all
 * built as the release (AZMOD_RELEASE). For every fixture the test loads the legacy files
 * through that code, migrates the same files with azconf_migrate_legacy, saves the conf, loads
 * it back from disk, and requires identical on/off for every keyed slot, the identical live
 * drum bank, pad colors and pins.
 *
 * Built with: -DCFG_INC (the frozen load_config), -DBANK_INC (the frozen bank block),
 * -DAZCONF_C (azconf.c or a control variant). argv[1]: a scratch directory. */
#include "menu.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include AZCONF_C

/* ---- the oracle: 1.2.2's load_config and the bank resolution */
static char tdir[900];
static char cfg_path[1024];
#define CONFIG_PATH cfg_path
static int g_disp60_armed;
static void dbg(const char *fmt, ...) { (void) fmt; }
static int ui60_clock_wanted(const azmod_state *s) {
    (void) s;
    return 0;
}
static void pledit_apply(const azmod_state *s) { (void) s; }
static void real_save_config(const azmod_state *s);
/* load_config's self-heal / migration rewrite must not touch the fixture: the oracle only
 * reads. (The azmod.cfg suite tests the rewrite itself.) */
static void save_config(const azmod_state *s) { (void) s; }
/* load_config's flag files live under /home/root/settings: map them into the fixture dir */
static int t_access(const char *p, int m) {
    const char *pre = "/home/root/settings/";
    char q[1200];
    if (!strncmp(p, pre, strlen(pre))) {
        snprintf(q, sizeof q, "%s/%s", tdir, p + strlen(pre));
        p = q;
    }
    return access(p, m);
}
#define access(p, m) t_access(p, m)
#include CFG_INC
#undef access
static azmod_state g_state;
static int ref_bank(void) { /* 1-based, as the overlay hands it to azstem */
#include BANK_INC
    return bank;
}

/* ---- fixtures */
static const char *const THEMES[] = {"day", "azmods", "midnight", "ember", "forest"};
static const int DEF_ON[] = {AZMOD_ACTION_REMOTESTEMS, AZMOD_ACTION_STEMSMARK};
static const int F_ON[] = {AZMOD_ACTION_DRUMSYNC};
static const int F_OFF[] = {AZMOD_ACTION_RETIRED12, AZMOD_ACTION_RTSTEMS, AZMOD_ACTION_RETIRED26};
static const int BANKS[] = {AZMOD_ACTION_DRUMB1, AZMOD_ACTION_DRUMB2, AZMOD_ACTION_DRUMB3,
                            AZMOD_ACTION_DRUMB4};
static const struct azconf_radio RADIO[] = {{"drumroll.bank", BANKS, 4}};
static const struct azconf_legacy_defaults DEFS = {
    DEF_ON, 2, F_ON, 1, F_OFF, 3,
    AZMOD_ACTION_FPS, AZMOD_ACTION_OVERLAYS, AZMOD_ACTION_PLEDIT, AZMOD_ACTION_REMOTESTEMS,
    AZMOD_ACTION_UI60UI, AZMOD_PAD_PALETTE_COUNT, THEMES, 5, RADIO, 1};
static char keybuf[AZMOD_MOD_COUNT][16];
static const char *slot_key[AZMOD_MOD_COUNT];
static int is_bank(int i) { return AZMOD_ACTION_IS_DRUMBANK(i); }
static void keys_init(void) {
    for (int i = 0; i < AZMOD_MOD_COUNT; i++) {
        snprintf(keybuf[i], sizeof keybuf[i], "mod.%d", i);
        slot_key[i] = keybuf[i];
    }
    slot_key[AZMOD_ACTION_RETIRED12] = NULL; /* retired slots: no key */
    slot_key[AZMOD_ACTION_RETIRED20] = NULL;
    slot_key[AZMOD_ACTION_BLSWAP] = NULL;
    slot_key[AZMOD_ACTION_RTSTEMS] = NULL;
    slot_key[AZMOD_ACTION_RETIRED26] = NULL;
    /* the bank slots keep their keys here: migration must skip them on its own */
}

static char logbuf[16384];
static void tlog(const char *fmt, ...) {
    size_t n = strlen(logbuf);
    va_list ap;
    va_start(ap, fmt);
    if (n < sizeof logbuf - 1)
        vsnprintf(logbuf + n, sizeof logbuf - n, fmt, ap);
    va_end(ap);
}

static void path_of(char *out, size_t cap, const char *name) { snprintf(out, cap, "%s/%s", tdir, name); }
static void put_n(const char *name, const void *data, size_t n) {
    char p[1200];
    path_of(p, sizeof p, name);
    FILE *f = fopen(p, "wb");
    if (!f) {
        perror(p);
        exit(2);
    }
    fwrite(data, 1, n, f);
    fclose(f);
}
static void put(const char *name, const char *s) { put_n(name, s, strlen(s)); }
static void wipe(void) { /* every regular file in the scratch dir */
    DIR *d = opendir(tdir);
    struct dirent *e;
    char p[1200];
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        path_of(p, sizeof p, e->d_name);
        unlink(p);
    }
    if (d) closedir(d);
}
static size_t slurp(const char *name, char *buf, size_t cap) {
    char p[1200];
    path_of(p, sizeof p, name);
    FILE *f = fopen(p, "rb");
    size_t n = f ? fread(buf, 1, cap - 1, f) : 0;
    if (f) fclose(f);
    buf[n] = 0;
    return n;
}
/* A fingerprint of every file in the dir except azmod.conf*: name + bytes + mtime. */
static unsigned long long dir_sig(void) {
    unsigned long long h = 1469598103934665603ull;
    DIR *d = opendir(tdir);
    struct dirent *e;
    char p[1200], buf[8192];
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.' || !strncmp(e->d_name, "azmod.conf", 10)) continue;
        struct stat st;
        path_of(p, sizeof p, e->d_name);
        stat(p, &st);
        size_t n = slurp(e->d_name, buf, sizeof buf);
        for (const char *q = e->d_name; *q; q++) h = (h ^ (unsigned char) *q) * 1099511628211ull;
        for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char) buf[i]) * 1099511628211ull;
        h = (h ^ (unsigned long long) st.st_mtime) * 1099511628211ull;
        h = (h ^ (unsigned long long) st.st_size) * 1099511628211ull;
    }
    if (d) closedir(d);
    return h;
}

/* What an azmod.conf means, decoded the way the integration will: per slot, the bank, the
 * pad colors, the pins (as slot bits; a radio key pins DRUMB1). -1 = key absent. */
struct decoded {
    int on[AZMOD_MOD_COUNT], bank, colors_on, color[4], color2[4];
    uint64_t pins;
};
static int decode_color(const struct azconf *c, const char *k) {
    const char *v = azconf_get(c, k);
    if (!v) return -2;
    if (!strcmp(v, "native")) return -1;
    return (int) azconf_get_int(c, k, -2, 0, AZMOD_PAD_PALETTE_COUNT - 1);
}
static void decode(const struct azconf *c, struct decoded *o) {
    for (int i = 0; i < AZMOD_MOD_COUNT; i++)
        o->on[i] = slot_key[i] && azconf_get(c, slot_key[i]) ? azconf_get_bool(c, slot_key[i], -1) : -1;
    o->bank = (int) azconf_get_int(c, "drumroll.bank", -1, 0, 3);
    o->colors_on = azconf_get_bool(c, "padcolor.on", -1);
    for (int j = 0; j < 4; j++) {
        char k[32];
        snprintf(k, sizeof k, "padcolor.%d", j);
        o->color[j] = decode_color(c, k);
        snprintf(k, sizeof k, "padcolor2.%d", j);
        o->color2[j] = decode_color(c, k);
    }
    o->pins = 0;
    const char *pl = azconf_get(c, "pinned");
    if (pl) {
        char tmp[600];
        snprintf(tmp, sizeof tmp, "%s", pl);
        for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) {
            if (!strcmp(t, "drumroll.bank")) o->pins |= 1ull << AZMOD_ACTION_DRUMB1;
            for (int i = 0; i < AZMOD_MOD_COUNT; i++)
                if (slot_key[i] && !strcmp(t, slot_key[i])) o->pins |= 1ull << i;
        }
    }
}

/* One fixture: oracle vs migrated-saved-reloaded conf. Returns the conf text in out. */
static void equiv(const char *what, char *out, size_t cap) {
    char name[256];
    path_of(cfg_path, sizeof cfg_path, "azmod.cfg");
    azmod_state s;
    azmod_state_init(&s);
    load_config(&s);
    g_state = s;
    int bank = ref_bank();

    unsigned long long before = dir_sig();
    struct azconf *c = azconf_new();
    int rc = azconf_migrate_legacy(c, tdir, slot_key, AZMOD_MOD_COUNT, &DEFS);
    snprintf(name, sizeof name, "%s: migration ran", what);
    CHECK(name, rc == 0 && azconf_dirty(c));
    snprintf(name, sizeof name, "%s: azmod.cfg and every flag file unchanged (bytes + mtime)", what);
    CHECK(name, dir_sig() == before);
    char p[1200];
    path_of(p, sizeof p, "azmod.conf");
    snprintf(name, sizeof name, "%s: saved", what);
    CHECK(name, azconf_save(c, p) == 0 && !azconf_dirty(c));
    azconf_free(c);
    c = azconf_new();
    snprintf(name, sizeof name, "%s: the saved conf loads as valid v1", what);
    CHECK(name, azconf_load(c, p) == 0 && !azconf_dirty(c));
    struct decoded d;
    decode(c, &d);

    int compared = 0, same = 1, first_bad = -1;
    for (int i = 0; i < AZMOD_MOD_COUNT; i++) {
        if (!slot_key[i] || is_bank(i)) continue;
        compared++;
        if (d.on[i] != (int) s.mod_enabled[i]) {
            same = 0;
            if (first_bad < 0) first_bad = i;
        }
    }
    snprintf(name, sizeof name, "%s: %d keyed slots identical on/off%s", what, compared,
             same ? "" : " (MISMATCH)");
    if (!same) printf("      first mismatch: slot %d conf=%d today=%d\n", first_bad, d.on[first_bad],
                      s.mod_enabled[first_bad]);
    CHECK(name, same && compared == AZMOD_MOD_COUNT - 5 - AZMOD_DRUM_BANKS);
    int bad_bank_key = 0;
    for (int i = 0; i < AZMOD_MOD_COUNT; i++)
        if (is_bank(i) && azconf_get(c, slot_key[i])) bad_bank_key = 1;
    snprintf(name, sizeof name, "%s: live drum bank identical (today %d, conf index %d), no per-bank keys",
             what, bank, d.bank);
    CHECK(name, d.bank == bank - 1 && !bad_bank_key);
    int col = d.colors_on == (int) s.pad_colors_on;
    for (int j = 0; j < 4; j++)
        col &= d.color[j] == s.padmode_color[j] && d.color2[j] == s.padmode_color2[j];
    snprintf(name, sizeof name, "%s: pad colors + master identical", what);
    CHECK(name, col);
    /* pins: compared on keyed slots + the bank row; a retired slot's pin has nowhere to go */
    uint64_t keyed = 0;
    for (int i = 0; i < AZMOD_MOD_COUNT; i++)
        if (slot_key[i] && (!is_bank(i) || i == AZMOD_ACTION_DRUMB1)) keyed |= 1ull << i;
    uint64_t want = s.pinned_mask & keyed;
    for (int i = AZMOD_ACTION_DRUMB1; i <= AZMOD_ACTION_DRUMB4; i++) /* any bank pinned = the row */
        if (s.pinned_mask >> i & 1) want |= 1ull << AZMOD_ACTION_DRUMB1;
    snprintf(name, sizeof name, "%s: pins identical (mask %llu)", what, (unsigned long long) want);
    CHECK(name, d.pins == want);
    if (out) slurp("azmod.conf", out, cap);
    azconf_free(c);
}

static const char DECK_LINE[] = "100011110001011001110000100011000111111";
static char *line_with_banks(char *buf, const char *b4) { /* a deck's own line, banks replaced */
    strcpy(buf, DECK_LINE);
    memcpy(buf + AZMOD_ACTION_DRUMB1, b4, 4);
    return buf;
}

int main(int argc, char **argv) {
    ut_suite = "azmod.conf";
    if (argc != 2) return 2;
    snprintf(tdir, sizeof tdir, "%s", argv[1]);
    mkdir(tdir, 0755);
    keys_init();
    azconf_log = tlog;
    _Static_assert(sizeof DECK_LINE - 1 == 39, "the deck line is width 39");
    char txt[8192], f[2048], line[256];

    /* ============ 1. real old configs: 1.2.2's load_config is the oracle */
    /* 1.0.x: width 13, a 5-field line 2, FPS on (-> OVERLAYS), plreorder + rowcache flags */
    wipe();
    put("azmod.cfg", "1001011000101\n2 -1 -1 5 0\n");
    put("azmod-plreorder.on", "");
    put("azmod-rowcache.on", "");
    equiv("width 13 (1.0.x), 5-field line 2, flags", txt, sizeof txt);
    CHECK("width 13: FPS on in an old file switched OVERLAYS on (rule exercised)",
          strstr(txt, "mod.14 = on") != NULL);
    CHECK("width 13: plreorder.on -> PLEDIT on, rowcache.on -> UI60UI on",
          strstr(txt, "mod.18 = on") && strstr(txt, "mod.19 = on"));
    wipe();
    put("azmod.cfg", "1001010000101\n");
    equiv("width 13 (1.0.x), line 1 only, no flags", txt, sizeof txt);
    CHECK("width 13 without flags: PLEDIT / UI60UI stay off, REMOTESTEMS default on",
          strstr(txt, "mod.18 = off") && strstr(txt, "mod.19 = off") && strstr(txt, "mod.24 = on"));
    /* 1.2.0 / 1.2.1: width 38 */
    wipe();
    memcpy(line, DECK_LINE, 38);
    line[38] = 0;
    snprintf(f, sizeof f, "%s\n4 -1 12 -1 1 0 -1 -1 27\n3\n", line);
    put("azmod.cfg", f);
    put("azmod-plreorder.on", ""); /* wider than PLEDIT: must be ignored */
    equiv("width 38 (1.2.0/1.2.1), 9-field line 2, pins 3", txt, sizeof txt);
    /* 1.2.2: a real deck's settings */
    wipe();
    snprintf(f, sizeof f, "%s\n-1 3 -1 -1 1 -1 -1 -1 -1\n0\n", DECK_LINE);
    put("azmod.cfg", f);
    put("azmod-theme", "midnight\n");
    put("azmod-vinyl.v", "0.35\n");
    put("azmod-btchannel", "4\n");
    equiv("width 39 (1.2.2), a real deck", txt, sizeof txt);
    CHECK("a real deck: theme / vinyl.brake / bluetooth.channel carried over",
          strstr(txt, "\ntheme = midnight\n") && strstr(txt, "\nvinyl.brake = 0.35\n") &&
              strstr(txt, "\nbluetooth.channel = 4\n"));
    CHECK("a real deck: no pinned key when nothing is pinned", !strstr(txt, "pinned"));
    CHECK("a real deck: starts with the header and version 1",
          !strncmp(txt, "# AZ-MODS settings\nversion = 1\n", 31));
    wipe();
    put("azmod.cfg", DECK_LINE); /* line 1 only, no newline at EOF */
    equiv("width 39, line 1 only (no line end)", NULL, 0);
    /* a newer build: width 45, pins on a keyed slot, a retired slot and a bank */
    wipe();
    snprintf(f, sizeof f, "%s101101\n1 2 3 4 1 5 6 7 8\n%llu\n", DECK_LINE,
             (1ull << 0) | (1ull << AZMOD_ACTION_RETIRED20) | (1ull << AZMOD_ACTION_DRUMB3) | (1ull << 40));
    put("azmod.cfg", f);
    equiv("width 45 (newer build), pins incl. retired + bank", txt, sizeof txt);
    CHECK("width 45: pinned lists names (mod.0, drumroll.bank), not bits",
          strstr(txt, "\npinned = mod.0,drumroll.bank\n") != NULL);
    CHECK("width 45: the retired slot's pin was logged as dropped", strstr(logbuf, "retired slot 20") != NULL);
    /* the drum banks: each one, none, two, all */
    /* clang-format off */
    const char *bk[][2] = {{"1000", "bank 1 set"}, {"0100", "bank 2 set"}, {"0010", "bank 3 set"},
                           {"0001", "bank 4 set"}, {"0000", "no bank set"}, {"0101", "banks 2 and 4 set"},
                           {"1111", "all banks set"}};
    /* clang-format on */
    for (unsigned b = 0; b < sizeof bk / sizeof bk[0]; b++) {
        wipe();
        snprintf(f, sizeof f, "%s\n", line_with_banks(line, bk[b][0]));
        put("azmod.cfg", f);
        char what[64];
        snprintf(what, sizeof what, "drum banks: %s", bk[b][1]);
        equiv(what, NULL, 0);
    }
    /* an invalid line 1: defaults only (lines 2-3 not trusted) plus the small files */
    wipe();
    put("azmod.cfg", "1\n3 3 3 3 0\n7\n");
    put("azmod-theme", "ember\n");
    equiv("lone \"1\" (corrupt line 1)", txt, sizeof txt);
    CHECK("lone \"1\": theme still migrated, pad colors default",
          strstr(txt, "theme = ember") && strstr(txt, "padcolor.0 = native") && strstr(txt, "padcolor.on = on"));
    wipe();
    put_n("azmod.cfg", "\x01\xff\x00garbage\n1 2 3\n", 17);
    equiv("binary azmod.cfg", NULL, 0);
    wipe();
    put("azmod.cfg", "");
    equiv("empty azmod.cfg", NULL, 0);
    /* wrong values in the small files: key skipped, logged */
    wipe();
    logbuf[0] = 0;
    snprintf(f, sizeof f, "%s\n", DECK_LINE);
    put("azmod.cfg", f);
    put("azmod-theme", "purple\n");
    put("azmod-vinyl.v", "1.5\n");
    put("azmod-btchannel", "5\n");
    equiv("bad theme / vinyl / bt files", txt, sizeof txt);
    CHECK("bad small files: no theme / vinyl.brake / bluetooth.channel key",
          !strstr(txt, "theme") && !strstr(txt, "vinyl") && !strstr(txt, "bluetooth"));
    CHECK("bad small files: each problem logged",
          strstr(logbuf, "azmod-theme") && strstr(logbuf, "azmod-vinyl.v") && strstr(logbuf, "azmod-btchannel"));
    const char *badv[] = {"nan", "-0.1", "0x0.8", "0.5abc", ".", "1e-1"};
    int vrej = 1;
    for (unsigned i = 0; i < sizeof badv / sizeof *badv; i++) {
        put("azmod-vinyl.v", badv[i]);
        struct azconf *c = azconf_new();
        azconf_migrate_legacy(c, tdir, slot_key, AZMOD_MOD_COUNT, &DEFS);
        vrej &= azconf_get(c, "vinyl.brake") == NULL;
        azconf_free(c);
    }
    CHECK("vinyl.brake: nan, negative, hex, trailing junk, a lone dot, exponent all refused", vrej);
    /* no azmod.cfg: nothing to migrate */
    wipe();
    put("azmod-theme", "day\n");
    {
        struct azconf *c = azconf_new();
        CHECK("no azmod.cfg: migrate returns -1", azconf_migrate_legacy(c, tdir, slot_key, AZMOD_MOD_COUNT, &DEFS) == -1);
        azconf_free(c);
    }

    /* ============ 6. idempotence: migrating twice gives the same bytes */
    wipe();
    snprintf(f, sizeof f, "%s\n-1 3 -1 -1 1 -1 -1 -1 -1\n5\n", DECK_LINE);
    put("azmod.cfg", f);
    put("azmod-theme", "forest\n");
    {
        char a[4096], b[4096], p[1200];
        path_of(p, sizeof p, "azmod.conf");
        struct azconf *c = azconf_new();
        azconf_migrate_legacy(c, tdir, slot_key, AZMOD_MOD_COUNT, &DEFS);
        azconf_save(c, p);
        slurp("azmod.conf", a, sizeof a);
        azconf_set(c, "junk.key", "x"); /* a second migration into a used conf starts clean */
        azconf_migrate_legacy(c, tdir, slot_key, AZMOD_MOD_COUNT, &DEFS);
        azconf_save(c, p);
        slurp("azmod.conf", b, sizeof b);
        CHECK("idempotence: migrating twice writes identical bytes", !strcmp(a, b) && a[0]);
        azconf_free(c);
    }

    /* ============ 2. garbage azmod.conf files */
    struct {
        const char *what;
        const char *data;
        size_t n;
        int want;
    } g[] = {
        {"empty file", "", 0, -2},
        {"lone 1", "1", 1, -2},
        {"binary bytes", "\x00\x01\xfe\xff\x80version = 1\x00", 17, -2},
        {"no version line", "# hi\nmod.0 = on\n", 16, -2},
        {"version = 99", "version = 99\nmod.0 = on\n", 24, -3},
        {"version = 2", "version = 2\n", 12, -3},
        {"version = 0", "version = 0\n", 12, -2},
        {"version = abc", "version = abc\n", 14, -2},
        {"version = 1x", "version = 1x\n", 13, -2},
        {"= with no key", "= 1\n", 4, -2},
    };
    for (unsigned i = 0; i < sizeof g / sizeof *g; i++) {
        char nm[160], p[1200];
        wipe();
        put_n("azmod.conf", g[i].data, g[i].n);
        path_of(p, sizeof p, "azmod.conf");
        struct azconf *c = azconf_new();
        int rc = azconf_load(c, p);
        snprintf(nm, sizeof nm, "garbage (%s): load returns %d (got %d), conf reset to fresh", g[i].what, g[i].want, rc);
        CHECK(nm, rc == g[i].want && !azconf_get(c, "mod.0") && !strcmp(azconf_get(c, "version"), "1") &&
                      azconf_get_bool(c, "mod.0", 1) == 1);
        azconf_free(c);
    }
    {
        char p[1200];
        path_of(p, sizeof p, "azmod.conf");
        struct azconf *c = azconf_new();
        wipe();
        CHECK("absent file: load returns -1", azconf_load(c, p) == -1);
        /* version = 99: the caller must not save; the file is never touched by load */
        put("azmod.conf", "version = 99\nfuture.key = 1\n");
        char before[256], after[256];
        slurp("azmod.conf", before, sizeof before);
        CHECK("version 99: -3", azconf_load(c, p) == -3);
        slurp("azmod.conf", after, sizeof after);
        CHECK("version 99: file left byte-identical by load", !strcmp(before, after));
        /* CRLF, truncated last line, per-key defaults */
        put("azmod.conf", "# c\r\nversion = 1\r\nmod.0 = on\r\nmod.1 = maybe\r\nn.a = 12x\r\nn.b = 99\r\n"
                          "n.c = -5\r\nn.d = 99999999999999999999999\r\nmod.2 = of");
        int rc = azconf_load(c, p);
        CHECK("CRLF + truncated last line: valid v1", rc == 0);
        CHECK("CRLF: values carry no \\r", !strcmp(azconf_get(c, "mod.0"), "on") && azconf_get_bool(c, "mod.0", 0) == 1);
        CHECK("truncated last line: `mod.2 = of` -> per-key default",
              azconf_get_bool(c, "mod.2", 1) == 1 && azconf_get_bool(c, "mod.2", 0) == 0);
        CHECK("non-bool value -> default", azconf_get_bool(c, "mod.1", 1) == 1 && azconf_get_bool(c, "mod.1", 0) == 0);
        CHECK("non-numeric int -> default", azconf_get_int(c, "n.a", 7, 0, 100) == 7);
        CHECK("out-of-range int -> default", azconf_get_int(c, "n.b", 7, 0, 50) == 7 && azconf_get_int(c, "n.c", 7, 0, 50) == 7);
        CHECK("overflowing int -> default", azconf_get_int(c, "n.d", 7, 0, 100) == 7);
        CHECK("in-range int read", azconf_get_int(c, "n.b", 7, 0, 100) == 99 && azconf_get_int(c, "n.c", 0, -10, 10) == -5);
        CHECK("CRLF file is dirty (the save normalizes it)", azconf_dirty(c));
        /* duplicates, a key with spaces, = with no key, a 10 KB line, control bytes, bad UTF-8 */
        static char big[16384];
        int n = snprintf(big, sizeof big, "version = 1\nmod.0 = on\nmy key = 1\n= 2\nmod.0 = off\nmod.3 = ");
        memset(big + n, 'x', 10240);
        n += 10240;
        n += snprintf(big + n, sizeof big - n, "\nmod.4 = a\x01"
                                               "b\nmod.5 = \xc3\x28\nmod.6 = \xe2\x82\xac\n"
                                               "mod.7 = \xed\xa0\x80\nmod.8 = \xc0\xaf\nnoequals\nmod.9=on\n");
        put_n("azmod.conf", big, (size_t) n);
        logbuf[0] = 0;
        rc = azconf_load(c, p);
        CHECK("junk lines: still valid v1", rc == 0);
        CHECK("duplicate key: the last one wins", azconf_get_bool(c, "mod.0", 1) == 0);
        CHECK("key with spaces ignored", !azconf_get(c, "my key") && !azconf_get(c, "my"));
        CHECK("10 KB line ignored", !azconf_get(c, "mod.3"));
        CHECK("control byte in a value ignored", !azconf_get(c, "mod.4"));
        CHECK("invalid UTF-8 / surrogate / overlong ignored",
              !azconf_get(c, "mod.5") && !azconf_get(c, "mod.7") && !azconf_get(c, "mod.8"));
        CHECK("valid UTF-8 value kept", azconf_get(c, "mod.6") && !strcmp(azconf_get(c, "mod.6"), "\xe2\x82\xac"));
        CHECK("no spaces around = accepted", azconf_get_bool(c, "mod.9", 0) == 1);
        CHECK("each dropped line logged", strstr(logbuf, "longer than") && strstr(logbuf, "bad key") &&
                                              strstr(logbuf, "control bytes") && strstr(logbuf, "duplicate key") &&
                                              strstr(logbuf, "not `key = value`"));
        azconf_save(c, p);
        slurp("azmod.conf", txt, sizeof txt);
        CHECK("rewrite keeps only the duplicate's winner and drops junk",
              !strstr(txt, "mod.0 = on") && strstr(txt, "mod.0 = off") && !strstr(txt, "xxxx") && !strstr(txt, "my key"));
        /* 100 KB file */
        static char huge[100 * 1024];
        memset(huge, '#', sizeof huge);
        memcpy(huge, "version = 1\n", 12);
        for (size_t i = 100; i < sizeof huge; i += 100) huge[i] = '\n';
        put_n("azmod.conf", huge, sizeof huge);
        CHECK("100 KB file: invalid (-2)", azconf_load(c, p) == -2);
        /* exactly at the cap with no final newline: loads, saves with the newline, reloads equal */
        static char at[64 * 1024 + 2], back[64 * 1024 + 8];
        put_n("azmod.conf", huge, 64 * 1024);
        CHECK("64 KB file, no final newline: loads", huge[64 * 1024 - 1] != '\n' && azconf_load(c, p) == 0);
        CHECK("64 KB file, no final newline: saves", azconf_save(c, p) == 0);
        memcpy(at, huge, 64 * 1024);
        at[64 * 1024] = '\n';
        size_t nb = slurp("azmod.conf", back, sizeof back);
        CHECK("64 KB file: saved byte for byte, plus the final newline",
              nb == 64 * 1024 + 1 && !memcmp(back, at, nb));
        CHECK("64 KB file: the saved file reloads", azconf_load(c, p) == 0);
        /* one byte over the cap with no final newline: refused at load */
        put_n("azmod.conf", huge, 64 * 1024 + 1);
        CHECK("64 KB + 1, no final newline: invalid (-2)", huge[64 * 1024] != '\n' && azconf_load(c, p) == -2);
        /* a directory where the file should be */
        wipe();
        mkdir(p, 0755);
        CHECK("a directory at the path: not valid, no crash", azconf_load(c, p) < 0);
        rmdir(p);
        /* setter validation */
        azconf_load(c, p);
        CHECK("set: bad keys refused",
              azconf_set(c, "Bad", "1") == -1 && azconf_set(c, "a b", "1") == -1 && azconf_set(c, "", "1") == -1 &&
                  azconf_set(c, "a=b", "1") == -1 && azconf_set(c, "version", "2") == -1);
        CHECK("set: bad values refused",
              azconf_set(c, "k", "a\nb") == -1 && azconf_set(c, "k", " x") == -1 && azconf_set(c, "k", "\xff") == -1);
        CHECK("set: community key with ':' accepted", azconf_set(c, "mymod:speed", "3") == 0);
        azconf_free(c);
    }

    /* ============ 3. round trip */
    {
        char p[1200], a[4096], b[4096];
        path_of(p, sizeof p, "azmod.conf");
        wipe();
        const char *src = "# azmod.conf: AZ-MODS settings\n# hand note\n\nversion = 1\nmod.0 = on\n"
                          "future.key = something new   \n\t# indented comment\nother:mod = 7\nmod.1=off\n";
        put("azmod.conf", src);
        struct azconf *c = azconf_new();
        CHECK("round trip: loads", azconf_load(c, p) == 0 && !azconf_dirty(c));
        CHECK("unknown key readable", !strcmp(azconf_get(c, "future.key"), "something new"));
        azconf_save(c, p);
        slurp("azmod.conf", a, sizeof a);
        CHECK("save with nothing changed: identical bytes (comments, blank, unknown keys, spacing)", !strcmp(a, src));
        CHECK("set to the same value: not dirty", azconf_set(c, "mod.0", "on") == 0 && !azconf_dirty(c));
        CHECK("set_bool: dirty", azconf_set_bool(c, "mod.0", 0) == 0 && azconf_dirty(c));
        azconf_set_int(c, "new.int", -42);
        CHECK("save clears dirty", azconf_save(c, p) == 0 && !azconf_dirty(c));
        slurp("azmod.conf", a, sizeof a);
        CHECK("changed key rewritten in place, new key appended, unknown keys + comments survive",
              strstr(a, "\nversion = 1\nmod.0 = off\nfuture.key = something new   \n\t# indented comment\n") &&
                  strstr(a, "other:mod = 7\nmod.1=off\nnew.int = -42\n") && strstr(a, "# hand note\n\n"));
        struct azconf *d = azconf_new();
        azconf_load(d, p);
        azconf_save(d, p);
        slurp("azmod.conf", b, sizeof b);
        CHECK("load -> save -> load is byte-stable", !strcmp(a, b) && azconf_get_int(d, "new.int", 0, -100, 0) == -42);
        azconf_free(d);
        /* the fresh conf */
        d = azconf_new();
        azconf_save(d, p);
        slurp("azmod.conf", b, sizeof b);
        CHECK("a fresh conf is the header + version 1", !strcmp(b, "# AZ-MODS settings\nversion = 1\n"));
        azconf_free(d);

        /* ============ 4. atomic / power loss */
        char nw[1300], sub[1200];
        snprintf(nw, sizeof nw, "%s.new", p);
        struct stat s1, s2;
        stat(p, &s1);
        azconf_set(c, "mod.0", "on");
        azconf_save(c, p);
        stat(p, &s2);
        CHECK("save replaces the file (new inode), never rewrites in place", s1.st_ino != s2.st_ino);
        CHECK("save leaves no .new", access(nw, F_OK) != 0);
        /* crash between write and rename: a stale .new beside a good file */
        slurp("azmod.conf", a, sizeof a);
        put("azmod.conf.new", "version = 1\nmod.0 = garbage-from-a-crash\n");
        struct azconf *e = azconf_new();
        CHECK("stale .new: load reads the good file", azconf_load(e, p) == 0 && !strcmp(azconf_get(e, "mod.0"), "on"));
        azconf_set(e, "mod.0", "off");
        CHECK("stale .new: the next save succeeds and consumes it",
              azconf_save(e, p) == 0 && access(nw, F_OK) != 0 && azconf_load(e, p) == 0 &&
                  !strcmp(azconf_get(e, "mod.0"), "off"));
        put_n("azmod.conf.new", "\x00\xff\xfe junk", 8);
        CHECK("garbage .new does not affect load", azconf_load(e, p) == 0 && !strcmp(azconf_get(e, "mod.0"), "off"));
        unlink(nw);
        /* the temp path taken by a directory: save fails, file intact */
        slurp("azmod.conf", a, sizeof a);
        mkdir(nw, 0755);
        azconf_set(e, "mod.0", "on");
        CHECK("save that cannot open .new returns -1", azconf_save(e, p) == -1 && azconf_dirty(e));
        slurp("azmod.conf", b, sizeof b);
        CHECK("... and the old file is intact", !strcmp(a, b));
        rmdir(nw);
        /* the rename fails (target is a non-empty directory): .new written, then removed */
        path_of(sub, sizeof sub, "blocked.conf");
        char subf[1300], subnew[1300];
        mkdir(sub, 0755);
        snprintf(subf, sizeof subf, "%s/x", sub);
        snprintf(subnew, sizeof subnew, "%s.new", sub);
        FILE *ff = fopen(subf, "w");
        fclose(ff);
        CHECK("rename failure: -1, no .new left, target untouched",
              azconf_save(e, sub) == -1 && access(subnew, F_OK) != 0 && access(subf, F_OK) == 0);
        unlink(subf);
        rmdir(sub);
        /* a full partition (the settings partition is small): the write stops short of the whole file */
        {
            slurp("azmod.conf", a, sizeof a);
            for (int k = 0; k < 60; k++) {
                char key[32];
                snprintf(key, sizeof key, "fill.%02d", k);
                azconf_set(e, key, "a value long enough to pass the limit");
            }
            signal(SIGXFSZ, SIG_IGN);
            struct rlimit old, lim;
            getrlimit(RLIMIT_FSIZE, &old);
            lim = old;
            lim.rlim_cur = 512;
            setrlimit(RLIMIT_FSIZE, &lim);
            int rc = azconf_save(e, p);
            setrlimit(RLIMIT_FSIZE, &old);
            slurp("azmod.conf", b, sizeof b);
            CHECK("a write that stops short (full partition): -1, the old file intact, no .new, still dirty",
                  rc == -1 && !strcmp(a, b) && access(nw, F_OK) != 0 && azconf_dirty(e));
            CHECK("... and the same save once there is room succeeds (CONTROL)",
                  azconf_save(e, p) == 0 && azconf_load(e, p) == 0 && azconf_get(e, "fill.59") != NULL);
        }
        /* a read-only directory (root ignores mode bits: then reported, not counted) */
        char ro[1200], rof[1300], ronew[1400];
        path_of(ro, sizeof ro, "ro");
        mkdir(ro, 0755);
        snprintf(rof, sizeof rof, "%s/azmod.conf", ro);
        snprintf(ronew, sizeof ronew, "%s.new", rof);
        azconf_save(e, rof);
        slurp("ro/azmod.conf", a, sizeof a);
        chmod(ro, 0555);
        char probe[1400];
        snprintf(probe, sizeof probe, "%s/probe", ro);
        int pfd = open(probe, O_WRONLY | O_CREAT, 0644);
        if (pfd >= 0) {
            close(pfd);
            unlink(probe);
            printf("SKIP  azmod.conf: read-only directory (running as root: mode bits do not stop writes)\n");
        } else {
            azconf_set(e, "mod.0", e && !strcmp(azconf_get(e, "mod.0"), "on") ? "off" : "on");
            int rc = azconf_save(e, rof);
            slurp("ro/azmod.conf", b, sizeof b);
            CHECK("read-only dir: save returns -1, old file intact, no .new",
                  rc == -1 && !strcmp(a, b) && access(ronew, F_OK) != 0);
        }
        chmod(ro, 0755);
        unlink(rof);
        rmdir(ro);
        azconf_free(e);
        azconf_free(c);
    }
    CHECK("NULLs are safe", azconf_get(NULL, "x") == NULL && azconf_dirty(NULL) == 0 &&
                                azconf_save(NULL, "x") == -1 && azconf_set(NULL, "a", "b") == -1);
    azconf_free(NULL);
    wipe();
    {
        /* azconf_entry walks the entries in file order, without "version"; azconf_unset removes one */
        struct azconf *c = azconf_new();
        const char *k, *v;
        azconf_set(c, "a", "1");
        azconf_set(c, "m:cfg.b", "\"x\"");
        int ok = azconf_entry(c, 0, &k, &v) && !strcmp(k, "a") && azconf_entry(c, 1, &k, &v) &&
                 !strcmp(v, "\"x\"") && !azconf_entry(c, 2, &k, &v);
        CHECK("azconf_entry: entries in order, version skipped, 0 past the end", ok);
        char up[1024];
        snprintf(up, sizeof up, "%s/unset.conf", tdir);
        ok = azconf_save(c, up) == 0 && !azconf_dirty(c);
        CHECK("azconf_unset precondition: saved, clean", ok);
        ok = azconf_unset(c, "a") == 0 && azconf_dirty(c) && !azconf_get(c, "a") && azconf_get(c, "m:cfg.b") &&
             azconf_unset(c, "a") == -1 && azconf_unset(c, "version") == -1 && azconf_get(c, "version");
        CHECK("azconf_unset: removes the key and dirties; absent or version: -1", ok);
        azconf_free(c);
    }
    return ut_summary();
}
