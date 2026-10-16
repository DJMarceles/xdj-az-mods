/* azmod 1.2.2: guest/azmod/menu.h, frozen as a test oracle; do not edit */
/* SPDX-License-Identifier: GPL-3.0-only */
/* azmod menu: pure logic (no syscalls). Hooks in azmod.c call into this.
 * Testable natively: build with -DAZMOD_SELFTEST and run. */
#ifndef AZMOD_MENU_H
#define AZMOD_MENU_H
#include <stdint.h>
#include <stdbool.h>

/* Themes (mod 37: Day Mode and the dark themes) are in every build. Their hooks install at
 * every start and are inert while Themes is off (azmod.c daymode_wanted; kill switch
 * settings/azmod-themes.off). */
#define AZMOD_HAVE_DAYMODE 1

/* Framebuffer we composite into (EP147's mapped dumb buffer). XRGB8888. */
typedef struct {
    uint8_t *px; /* base pointer */
    int w, h;    /* pixels */
    int stride;  /* bytes per row */
} azmod_fb;

typedef enum {
    AZ_CLOSED = 0,
    AZ_OPEN = 1,
    AZ_SETTINGS = 2 /* unused */
    ,
    AZ_INFO = 3,
    /* 4: retired; 5: reserved */
    AZ_COLORS = 6,     /* pad-mode button colors: master toggle + one row per button */
    AZ_COLOR_PICK = 7, /* swatch grid for the one button chosen on AZ_COLORS (color_sel) */
    AZ_UPDATE = 8,     /* Update Mods: the on-deck mods updater's status + one-click update */
    AZ_OVERLAYS = 9,   /* the deck-screen readouts: master toggle + one row per readout */
    AZ_COMMUNITY = 11  /* Community Mods: banner, master switch, installed list, a mod's page */
                       /* 10: retired */
} azmod_mode;

/* Deck count (bus/LED geometry). */
#define AZMOD_DECK_COUNT 4

/* STEM TOGGLES: a 4-button strip (VOCAL/INST/BASS/DRUM) over a deck's waveform while a deck
 * screen is up. Tapping toggles a stem; azmod.c writes the per-deck mask (azstem.stems /
 * azstem.stems2: bit7 engaged, bits0-3 enabled). Deck 1's strip sits at the top of its
 * waveform, deck 2's at the bottom. Gated by the STEMS mod (mod_enabled[0]). */
/* Stem palette, 0xRRGGBB, in azstem's stem order (VOCAL, INST, BASS, DRUMS). The pad pills,
 * mini lanes, layered waveform and per-column recoloring all read it from here. */
#define AZMOD_STEM_PALETTE {0x22E022u, 0xFF3030u, 0xFF33CCu, 0x4060FFu}
#define AZMOD_STEM_COUNT 4
#define AZMOD_STEM_DECKS 4 /* stem-capable decks: Deck 1..4 (azstem engine is NDECK=4) */
#include <unistd.h>
/* DEBUG PROBES: a probe written as AZ_PROBE_ON(flag) is the constant 0 in a normal build,
 * so the compiler removes it and its flag file does nothing; build with -DAZMOD_PROBES to
 * make those flags live. azmod's diagnostic logs go through azlog_open(), which writes
 * nothing unless /home/root/settings/azmod-logs.on existed at start. Not everything is
 * behind those two gates: a few plain access()/open() test hooks under /tmp remain in
 * release builds (fake temperature and CPU-frequency inputs, touch-simulation and
 * Themes test triggers), and azstem writes small status files to /tmp unconditionally.
 * Feature flags and ".off" kill switches are plain access() by design. */
#ifdef AZMOD_PROBES
#define AZ_PROBE_ON(p) (access((p), F_OK) == 0)
#else
#define AZ_PROBE_ON(p) 0
#endif

/* Physical PAD hardware decks. The ErP LED frame (128 B) and EP147's native HuiLed array
 * hold only two physical pad sets; decks 3/4 share those pads through the hardware
 * deck-select. Loops that index native per-deck memory (HuiLed arg0+0x110+d*0x108, LED frame
 * f+off+d*0x38, ErP read byte14+d*8) must bound to this, not AZMOD_STEM_DECKS: 4 there reads
 * and writes past the native 2-deck structures and crashes EP147. */
#define AZMOD_PAD_DECKS 2

/* Which logical deck a physical side is driving. side 0 -> deck 1 or 3, side 1
 * -> deck 2 or 4, indexing the 4-wide per-deck stem arrays. sel is that side's
 * deck-select state: 0 = primary, 1 = secondary (azmod.c tracks it off the ErP
 * deck-select bits; see g_deck_sel). */
static inline int azmod_logical_deck(int side, int sel) {
    return side + (sel ? AZMOD_PAD_DECKS : 0);
}

/* ---- PAD-MODE BUTTON COLOR PICKER (AZ_COLORS) ----------------------------
 * The 4 physical pad-mode buttons per deck (j=0..3; azmod.c maps j to the ERP mode mask).
 * The picker assigns each button a color; azmod.c applies them to the ERP LED frame
 * (frame[0x0e + 4*j], +0x38 deck stride).
 *
 * Encoding: the three LED bytes are linear light, not sRGB. Stock EP147 does linear
 * arithmetic on them (a button's unselected state is its hue scaled by a flat ~4.7% in the
 * byte domain: HOT CUE 98ff9a -> 070c07, BEAT LOOP d16300 -> 0a0400), and decoding the stock
 * triples through the sRGB EOTF gives the colors the buttons visibly are (d16300 -> #EAA700
 * orange, 98ff9a -> #CBFFCC pale mint). An sRGB value sent as-is lifts the non-dominant
 * channels ~10x and washes the color out (#FF2020 emits as #FF6363). So each palette entry
 * carries both: `swatch`, what the picker draws (sRGB), and `led`, the same color encoded
 * sRGB->linear, which goes in the frame. The 2-bit LedLuminance codes at 0x09..0x0c (LUT
 * 0x2e47820 is the identity {0,1,2}) stay constant across a pad-mode sweep and do not gate
 * the colors.
 *
 * /home/root/settings/azmod-ledraw makes azmod.c write `swatch` verbatim instead, for an A/B
 * on hardware without a rebuild. */
typedef struct {
    uint32_t swatch;
    uint32_t led;
} azmod_pad_color;
#define AZMOD_PADMODE_COUNT 4
/* Pad Colors rows: each button's PRIMARY page and its SECONDARY page (HOT CUE 2,
 * BEAT LOOP 2, SLIP LOOP 2, BEAT JUMP 2). Pick index
 * sel = 2*j + page; page 0 -> padmode_color[j], page 1 -> padmode_color2[j]. */
#define AZMOD_PADPICK_COUNT (2 * AZMOD_PADMODE_COUNT)
#define AZMOD_PAD_PALETTE_COUNT 28
/* {on-screen sRGB, LED linear}. INDEX-STABLE: azmod.cfg line 2 stores these indices, so
 * entries are only ever APPENDED; the on-screen order is AZMOD_PAD_GRID_ORDER below, never
 * this list. `led` = sRGB->linear of `swatch` (((v+.055)/1.055)^2.4 for v>.04045, else
 * v/12.92); use the same formula for additions. */
#define AZMOD_PAD_PALETTE                                                                          \
    {{0xFF0000u, 0xFF0000u}, /*  0 RED        */                                                   \
     {0xFF6000u, 0xFF1E00u}, /*  1 ORANGE     */                                                   \
     {0xFF9E00u, 0xFF5700u}, /*  2 AMBER      */                                                   \
     {0xFFE000u, 0xFFBE00u}, /*  3 YELLOW     */                                                   \
     {0xA8FF00u, 0x64FF00u}, /*  4 LIME       */                                                   \
     {0x00FF00u, 0x00FF00u}, /*  5 GREEN      */                                                   \
     {0x00FF8Au, 0x00FF41u}, /*  6 MINT       */                                                   \
     {0x00F0FFu, 0x00DEFFu}, /*  7 CYAN       */                                                   \
     {0x00A8FFu, 0x0064FFu}, /*  8 SKY        */                                                   \
     {0x0038FFu, 0x000AFFu}, /*  9 BLUE       */                                                   \
     {0x5000FFu, 0x1400FFu}, /* 10 INDIGO     */                                                   \
     {0xA000FFu, 0x5A00FFu}, /* 11 VIOLET     */                                                   \
     {0xFF00D0u, 0xFF00A1u}, /* 12 MAGENTA    */                                                   \
     {0xFF4080u, 0xFF0D37u}, /* 13 PINK       */                                                   \
     {0xFFFFFFu, 0xFFFFFFu}, /* 14 WHITE      */                                                   \
     {0xFFC000u, 0xFF8600u}, /* 15 GOLD       */                                                   \
     {0x60FF00u, 0x1EFF00u}, /* 16 CHARTREUSE */                                                   \
     {0x00FF40u, 0x00FF0Du}, /* 17 SPRING     */                                                   \
     {0x00FFC8u, 0x00FF93u}, /* 18 TEAL       */                                                   \
     {0x0080FFu, 0x0037FFu}, /* 19 AZURE      */                                                   \
     {0x2020FFu, 0x0404FFu}, /* 20 ROYAL      */                                                   \
     {0x7800FFu, 0x3000FFu}, /* 21 PURPLE     */                                                   \
     {0xD040FFu, 0xA10DFFu}, /* 22 ORCHID     */                                                   \
     {0xFF00A0u, 0xFF005Au}, /* 23 FUCHSIA    */                                                   \
     {0xFF2060u, 0xFF041Eu}, /* 24 ROSE       */                                                   \
     {0xFFB080u, 0xFF6F37u}, /* 25 PEACH      */                                                   \
     {0xCBFFCCu, 0x98FF9Au}, /* 26 PALE MINT  (EP147's own HOT CUE tone) */                        \
     {0xB0A0FFu, 0x6F5AFFu}} /* 27 LAVENDER   */
/* Picker grid (AZ_COLOR_PICK): 6 wide. Cell k -> palette index: four rows of the saturated
 * wheel in hue order, then the pales and white, then one trailing native cell (leave EP147's
 * own color alone). */
#define AZMOD_PAD_GRID_COLS 6
#define AZMOD_PAD_GRID_ORDER                                                                       \
    {0, 1,  2,  15, 3,  4,  16, 5,  17, 6,  18, 7,  8,  19,                                        \
     9, 20, 10, 21, 11, 22, 12, 23, 13, 24, 14, 25, 26, 27}
/* Swatch-grid cells = the palette plus one trailing native cell. */
#define AZMOD_PAD_SWATCH_CELLS (AZMOD_PAD_PALETTE_COUNT + 1)
/* Labels for the 4 pad-mode buttons in j order (ERP mode mask byte13 0x01/0x02/0x04/0x08),
 * matching the panel silkscreen. */
#define AZMOD_PADMODE_LABELS {"Hot Cue", "Beat Loop", "Slip Loop", "Beat Jump"}
#define AZMOD_ACTION_STEMS 100 /* last_action signal (not a menu row): write per-deck masks */
/* per-deck stem strip rect + per-button rect (fb pixels). Defined in menu.c. */
void azmod_stem_strip_rect(const azmod_fb *fb, int deck, int *x, int *y, int *w, int *h);
/* Black plate + "STEMS" that replaces the native "BEAT LOOP" pad-mode title, centered over
 * the deck's pad area. */
void azmod_stem_title_rect(const azmod_fb *fb, int deck, int *x, int *y, int *w, int *h);
/* FPS readout: the empty black band top-right of the deck screen (x 1090-1278, y 48-88 on
 * 1280x800). */
void azmod_fps_rect(const azmod_fb *fb, int *x, int *y, int *w, int *h);

/* Union of the title plate and the button strip: the overlay window a deck's
 * stem UI needs (azmod.c sizes win/win2 to this). */
void azmod_stem_win_rect(const azmod_fb *fb, int deck, int *x, int *y, int *w, int *h);
void azmod_stem_lanes_win_rect(const azmod_fb *fb, int deck, int *x, int *y, int *w, int *h);
/* Bitmap-font text for callers outside menu.c (the RESTARTING screen), which must not
 * depend on the TTF. */
void azmod_draw_text_at(const azmod_fb *fb, int x, int y, const char *s, int scale, uint32_t c);
/* Draw `s` centered in (x,y,w,h) at `px` pixel size in EP147's own UI TTF (text.c). Returns 0
 * if the font is not available; callers then fall back to the bitmap font. Weak so menu.c
 * links without text.c (selftest). */
int azmod_ttf_center(const azmod_fb *fb, int x, int y, int w, int h, const char *s, int px,
                     int track, int bold, uint32_t c) __attribute__((weak));
/* Same, in the face EP147 draws its SOURCE list in (TsukuGoCustomPro-B): the Mod Menu's
 * text. w <= 0 left-aligns at x. */
int azmod_ttf_menu(const azmod_fb *fb, int x, int y, int w, int h, const char *s, int px, int track,
                   int bold, uint32_t c) __attribute__((weak));
/* Advance width in px of the first n bytes of s in the menu face (0 = no font). */
int azmod_ttf_menu_width(const char *s, int n, int px, int bold) __attribute__((weak));
/* azmod_ttf_menu with a color switch at byte `split` (glyphs from there on in c2) */
int azmod_ttf_menu2(const azmod_fb *fb, int x, int y, int w, int h, const char *s, int px,
                    int track, int bold, uint32_t c, int split, uint32_t c2) __attribute__((weak));
void azmod_stem_btn_rect(const azmod_fb *fb, int deck, int i, int *x, int *y, int *w, int *h);
/* azstem.so's per-stem waveform getter, dlsym'd by azmod.c at init (both .so are preloaded
 * into EP147: an in-process call). NULL = no azstem; the strip's second row stays blank. */
extern int (*azmod_stem_wave)(int deck, const unsigned char **env, int *n, int *nstem, float *pos);
/* azstem.so's per-deck envelope identity, dlsym'd alongside azstem_wave. Bumped whenever
 * azstem replaces that deck's envelope; the mini-waveform peak cache rebuilds on a change.
 * NULL (an older azstem.so) = callers fall back to the have-edge latch. */
extern unsigned (*azmod_stem_env_gen)(int deck);
extern int (*azmod_stem_env_final)(int deck); /* NULL on an older azstem: no envelope available */
/* Second pad row, slot i: that stem's waveform lane. */
void azmod_stem_lane_rect(const azmod_fb *fb, int deck, int i, int *x, int *y, int *w, int *h);
/* 4-DECK layout: no room for the strip or the lanes, so STEM LANES shows four small stem
 * dots over the deck's source cell in the left column instead (1280x800: deck 1 y 202-240,
 * deck 2 y 324-362, x 0-168). AZMOD_STEMDOTS / AZMOD_STEMDOTS2 ("x,y,w,h" ratios) override. */
void azmod_stem_dots_rect(const azmod_fb *fb, int deck, int *x, int *y, int *w, int *h);
/* The deck's main waveform band, where STEM LANES paints its four lanes.
 * AZMOD_WAVEBAND / AZMOD_WAVEBAND2 ("x,y,w,h" ratios) override at runtime. */
void azmod_waveband_rect(const azmod_fb *fb, int deck, int *x, int *y, int *w, int *h);
/* Waveform zoom geometry, learned by azmod.c from the data hook: track points
 * per column, where the playhead sits (in columns) and how many columns the
 * band has. Returns 0 until a full sweep has been seen. */
extern int azmod_wave_zoom(int deck, double *ppc, double *phcol, double *ncol, double *total);

/* Menu items: the mods, then the utility rows.
 *   0..MOD_COUNT-1         : mods; tapping one toggles it in place
 *   MOD_COUNT..ITEM_COUNT-1: utilities and groups (AZMOD_ACTION_SYSINFO onward)
 * STEMS (mod 0) is the master switch for the whole stems feature (azmod.c mirrors it to
 * azstem.enable). */
#define AZMOD_MOD_COUNT 39
/* Every mod's bit in pinned_mask (a u64: 1u << 32 is undefined in C). */
#define AZMOD_MOD_MASK (AZMOD_MOD_COUNT >= 64 ? ~0ull : (1ull << (AZMOD_MOD_COUNT & 63)) - 1)
_Static_assert(AZMOD_MOD_COUNT <= 64, "pinned_mask is a u64: widen it before a 65th mod");
#define AZMOD_UTIL_COUNT 8
#define AZMOD_ITEM_COUNT (AZMOD_MOD_COUNT + AZMOD_UTIL_COUNT)
#define AZMOD_CHANGELOG_MAX 65536   /* bytes of CHANGELOG.txt kept (the rest is dropped) */
#define AZMOD_ACTION_STEMS_TOGGLE 0 /* mod 0: stems master toggle */
#define AZMOD_ACTION_STEMTINT 1     /* mod 1: tint EP147's own waveform by stem */
#define AZMOD_ACTION_TINTBLEND 2    /* mod 2: blend the tint vs loudest-wins */
#define AZMOD_ACTION_STEMWAVES 3    /* mod 3: draw our own stem waveforms over EP147's */
#define AZMOD_ACTION_UI60 4         /* mod 4: 60Hz UI repaint */
#define AZMOD_ACTION_FPS 5          /* mod 5: FPS readout overlay */
/* Drum-roll sampler. Mod 6 arms it; mods 7-10 are its bank rows, a RADIO group (one tap
 * selects that bank and clears the others), drawn with the usual checkboxes. Mod 11 is BEAT
 * SYNC, an independent switch beside the group (AZMOD_ACTION_IS_DRUMBANK excludes it):
 * roll hits lock to the track's beat grid, or free-run from the pad press; azmod.c publishes
 * it to azstem via azstem_drum_sync(). All are children of DRUM ROLL, so they indent and
 * gray out when it is off. */
#define AZMOD_ACTION_DRUM 6 /* mod 6: drum-roll master */
#define AZMOD_ACTION_DRUMB1 7
#define AZMOD_ACTION_DRUMB2 8
#define AZMOD_ACTION_DRUMB3 9
#define AZMOD_ACTION_DRUMB4 10
#define AZMOD_ACTION_DRUMSYNC 11 /* mod 11: beat-synced rolls */
/* Mod 12: RETIRED, never reuse. Hidden, forced OFF and read by nothing. */
#define AZMOD_ACTION_RETIRED12 12
/* Mod 13: CPU temperature readout beside the FPS readout, read 1/s from the soc-thermal
 * zone. */
#define AZMOD_ACTION_TEMP 13
/* Mods 14-17: the OVERLAYS group. Mod 14 is its master; FPS (5), CPU Temp (13) and these
 * three are its children. A readout shows only while OVERLAYS and its own toggle are both on
 * (azmod_mod_active). */
#define AZMOD_ACTION_OVERLAYS 14 /* master for the deck-screen readouts */
#define AZMOD_ACTION_CPUUSE 15   /* CPU usage %, all cores */
#define AZMOD_ACTION_CPUCLK 16   /* CPU clock, fastest core, GHz */
#define AZMOD_ACTION_RAM 17      /* RAM in use % */
/* Mod 18: playlist editing on the deck (knob-drag reorder, the MENU popup's remove/add).
 * Mirrors /home/root/settings/azmod-plreorder.on; its hooks install at EP147 start, so
 * switching it ON takes one Reload. */
#define AZMOD_ACTION_PLEDIT 18
/* Mod 19: 60 FPS UI, a child of 60 FPS Waveforms (it needs that mod's 60 Hz frame clock).
 * Smooth list scrolling: the post-paint repaint re-arm, the due-deadline keep, the
 * paint-start stamp and 17 ms fling coasting (all live), plus the row cache and slow-drag
 * accumulate (installed at EP147 start: "Reload to apply"). Migrates the hand-made flags
 * azmod-rowcache.on / azmod-touchaccum.on once. */
#define AZMOD_ACTION_UI60UI 19
/* Mod 20: RETIRED, never reuse. Existing azmod.cfg files may carry a '1' at char 20, and a
 * new mod here would inherit it and switch itself on. Hidden, read by nothing, its char
 * passed through untouched. */
#define AZMOD_ACTION_RETIRED20 20
/* Mod 21: "Stems on BEAT LOOP 2", a child of Stems. OFF (default) = stems on BEAT LOOP page
 * 1 and BEAT LOOP's own loop pads moved to BEAT LOOP 2 (azmod.c bl_swap); ON = stems on BEAT
 * LOOP 2 (EP147 mode 4) and page 1 stays native. Read live by azmod.c stems_bl_mode(). */
#define AZMOD_ACTION_STEMSBL2 21
/* Mod 22: RETIRED, never reuse (its page swap is what mod 21 OFF does). Hidden; its bit is
 * read by nothing. */
#define AZMOD_ACTION_BLSWAP 22
/* Mod 23: RETIRED, never reuse. Hidden, forced OFF and read by nothing. */
#define AZMOD_ACTION_RTSTEMS 23
/* Mod 24: Remote Stems, a child of Stems. MENU > MAKE STEMS sends a track or a whole
 * playlist to the AZ-MODS app on the PC/Mac, which makes the stems and writes them back to
 * the USB. Default ON; the row shows under Stems. */
#define AZMOD_ACTION_REMOTESTEMS 24
/* Mod 25: "SHIFT + Hold to Move", a child of Playlist Editing, shown as "Grab with:
 * [HOLD][SHIFT + HOLD]". Default OFF. ON: a playlist / folder / track MOVE starts only with
 * SHIFT held while the knob is pressed and held; a plain knob hold stays EP147's own quick
 * move. OFF: a plain hold starts the move. Read live by azmod.c's knob handler. */
#define AZMOD_ACTION_PLSHIFT 25
/* Mod 26: RETIRED, never reuse. Hidden, forced OFF and read by nothing. */
#define AZMOD_ACTION_RETIRED26 26
/* Mod 27: "BEAT JUMP 2 First", default OFF (azmod_state_init, so a narrower cfg gets it).
 * ON: entering BEAT JUMP lands on EP147's own BEAT JUMP 2 (8/16/32/64 beats) instead of page
 * 1 (1/2..4): azmod feeds EP147 one more BEAT JUMP press once EP147 reports page 1, so the
 * page, tiles, caption and jumps are all EP147's own. A further press goes to page 1 as
 * usual. OFF = stock. Read live by azmod.c's ERP hook. */
#define AZMOD_ACTION_BJ2FIRST 27
/* Mod 28: "Fine Vinyl Speed Adjust", default OFF (azmod_state_init). ON: the SHORTCUT
 * screen's VINYL SPEED ADJUST steps become a 1% slider (vinyl_slider.c). OFF = the stock five
 * steps and EP147's own values. Read live. */
#define AZMOD_ACTION_VINYLFINE 28
/* Mod 29: "Stem Marks", a child of Stems, default ON (azmod_state_init): the stem mark on
 * track and playlist rows that have stems. OFF hides those marks; a MAKE STEMS job still
 * animates its rows (its only progress display). Read live. */
#define AZMOD_ACTION_STEMSMARK 29
/* Mods 30-31: hidden, no row: the 60 FPS group switch's memory. The group switch is a bypass
 * (menu.c c_toggle_fps): turning it off saves here which members were on and switches them
 * off; turning it on restores exactly those. Both '0' = nothing saved (the next "on" switches
 * both on). Toggling either member by hand clears them (azmod_mod_toggle). Persisted in
 * azmod.cfg so a bypass survives a restart. The effects read mods 4 and 19 only. */
#define AZMOD_ACTION_FPSMEM_WAVE 30
#define AZMOD_ACTION_FPSMEM_UI 31
/* Mod 32: "Stem Volume on CH 3/4 EQ", a child of Stems.
 * ON = in 2-deck use the idle channels' EQ knobs set stem volumes:
 * channel 3 for deck 1, channel 4 for deck 2; HI = vocals, MID = inst and bass,
 * LOW = drums; Drum Roll volume's curve (12 o'clock = unchanged, left = silent,
 * right = +6 dB). Default OFF: knobs left down from an earlier set would silence
 * stems the moment it came on. Read live (azmod.c stemeq). */
#define AZMOD_ACTION_STEMEQ 32
/* 1 = azmod.c's channel 3/4 EQ byte offsets (stemeq CH[] table) are known; at 0 the row is
 * hidden, since a switch that cannot do anything must not show. */
#define AZMOD_STEMEQ_MEASURED 1
/* Mods 33-34: Bluetooth, under Extras. Stock never connects to the paired device by itself (not at boot, not
 * when Bluetooth is switched on) and resets channel 3/4 to DECK on every start.
 *   33 "Bluetooth Auto-Connect" (default OFF): when EP147's Bluetooth stack finishes
 *      starting, connect to the paired device.
 *   34 "Remember Selected Channel" (default OFF): the channel the DJ last set to
 *      Bluetooth in Utility > Mixer is remembered (settings/azmod-btchannel) and set to
 *      Bluetooth again whenever a device connects; back to DECK when it disconnects
 *      (only if still on Bluetooth). Setting that channel off Bluetooth by hand forgets it.
 * Independent of each other. azmod.c bt_mods.  */
#define AZMOD_ACTION_BTAUTO 33
#define AZMOD_ACTION_BTINPUT 34
/* Mod 35 (modeled on the CDJ-3000X): "Hide Track Title", under
 * Extras, default OFF. With it on, each deck's track-info bar shows an eye; a tap on the
 * bar hides that deck's title (the eye struck through) on WAVEFORM and on BROWSE's deck
 * strips, a second tap shows it again. Off = every title shown. azmod.c hide-title.
 */
#define AZMOD_ACTION_HIDETITLE 35
/* Mod 36 (modeled on the CDJ-3000X): "Phase Meter", under Extras,
 * default OFF. 2 DECK layout: two bar rulers between deck 1's and deck 2's waveforms (deck 1
 * on top), the MASTER deck's orange, a tick per beat, red on the downbeat; in phase when the
 * ticks line up. Data = EP147's own usecase::phase_meter. azmod.c "PHASE METER". */
#define AZMOD_ACTION_PHASEMETER 36
/* Mod 37: Themes ("Day Mode" and the dark themes), under Extras, default OFF. Repaints the
 * whole deck screen natively and switches live, with no EP147 restart (day_mode.c). */
#define AZMOD_ACTION_DAYMODE 37
/* Mod 38: "Style" of the stem marks, a child of Stem Marks (mod 29), shown as a two-button
 * choice. OFF (default) = Color: the four stem-colored tiles. ON = Mono: the same mark in one
 * ink, white on the dark themes and with Themes off, black on a light theme (Day). Read live
 * (azmod.c smk_draw). */
#define AZMOD_ACTION_SMSTYLE 38
/* The narrowest azmod.cfg line a shipped build ever wrote (13 mods). load_config migrates any
 * width from here up to AZMOD_MOD_COUNT. */
#define AZMOD_CFG_MIN_WIDTH 13
#define AZMOD_DRUM_BANKS 4
#define AZMOD_ACTION_IS_DRUMBANK(a) ((a) >= AZMOD_ACTION_DRUMB1 && (a) <= AZMOD_ACTION_DRUMB4)

/* azmod.cfg is positional (one char per mod, in this order), so mods are only ever APPENDED.
 * load_config reads any narrower line down to AZMOD_CFG_MIN_WIDTH (char i still means mod i;
 * newer mods load at their defaults; the file is rewritten at the new width) and keeps the
 * first AZMOD_MOD_COUNT chars of a wider one. */
/* Utility rows (ITEM_LABEL in menu.c must match). There is no Back row: the native SOURCE
 * "MOD MENU" row closes the menu (as does the compact-mode header X). */
#define AZMOD_ACTION_SYSINFO (AZMOD_MOD_COUNT + 0)
/* Reserved: never a row, never activates.
 * The slot is kept so the rows after it keep their numbers. */
#define AZMOD_ACTION_RESERVED (AZMOD_MOD_COUNT + 1)
#define AZMOD_ACTION_COLORS (AZMOD_MOD_COUNT + 2) /* opens the AZ_COLORS pad-color picker */
#define AZMOD_ACTION_RELOAD (AZMOD_MOD_COUNT + 3) /* restarts EP147 */
#define AZMOD_ACTION_UPDATE                                                                        \
    (AZMOD_MOD_COUNT + 4) /* opens AZ_UPDATE: update the mods from the deck */
/* GROUPS: a left-list row with no switch of its own; its members
 * (azmod_mod_parent) list in the right pane, each with a description. A group
 * parent is not a mod: azmod_mod_active skips it. */
#define AZMOD_ACTION_GRP_FPS (AZMOD_MOD_COUNT + 5) /* 60 FPS Waveforms, 60 FPS UI */
#define AZMOD_ACTION_GRP_EXTRAS                                                                    \
    (AZMOD_MOD_COUNT + 6) /* Extras: the small standalone mods (azmod_mod_parent) */
#define AZMOD_ACTION_IS_GROUP(a) ((a) == AZMOD_ACTION_GRP_FPS || (a) == AZMOD_ACTION_GRP_EXTRAS)
/* COMMUNITY MODS: a top-level list row opening Mod Menu > Community Mods (AZ_COMMUNITY):
 * the community mods' banner, master switch, installed list and each mod's page. */
#define AZMOD_ACTION_COMMUNITY (AZMOD_MOD_COUNT + 7)
/* Unused: the menu shows the live set's version (azmod_live_version) or none at all, never a
 * compiled-in number that may not be the one running. */
#define AZMOD_VERSION_STR                                                                          \
    "1.0.4"
/* The version the deck is actually running: the mods set the loader staged this boot
 * (/tmp/az-deck/version = manifest.json's version), re-read every ~2 s so a Mod Menu update
 * shows the new number without a reload. "" when no set is staged. */
const char *azmod_live_version(void);
/* ---- community mods (community.c in azmod.c; the Mod Menu's Community pages read these) */
#include "azmod-mod.h"
/* Shared by azmod.c and menu.c only: hidden, so none of these lands in EP147's symbol
 * namespace (community mods reach the host through az_core_v1, never by name). */
#pragma GCC visibility push(hidden)
/* AZCM_NEW: enabled and would load, but this exact .so is not on the deck's approved list */
enum { AZCM_LOADED = 1, AZCM_DISABLED, AZCM_REFUSED, AZCM_FAILED, AZCM_SUSPENDED, AZCM_NEW };
/* from: where the installed copy came from (the owner/repo of its .origin), "" = copied by hand;
 * allowed: an AZCM_NEW mod the DJ allowed during this start (it loads after the reload) */
typedef struct { char id[33], name[48], version[24], author[48], summary[120], reason[112], from[96];
                 int state, has_screen, hooks, allowed; } azcm_info_t;
int  azcm_count(void);
int  azcm_info(int i, azcm_info_t *out);
int  azcm_suspended(void);
int  azcm_all_off(void);
int  azcm_off_by_stick(void);   /* the stick's MODS/community-mods.off (the app's switch) */
const char *azcm_fwtag(void);
void azcm_screen_draw(int i, az_draw *d, az_rect area);
int  azcm_screen_input(int i, const az_event *ev);
void azcm_screen_enter(int i);
void azcm_resume(void);
void azcm_flush_settings(void);   /* pending mod settings to p7 now (reload, leaving a mod's page) */
void azcm_reload_requested(void); /* a Mod Menu reload: flush settings; this start counts as healthy */
void azcm_unsuspend(const char *id);   /* a mod that crashed the deck: load it again next start */
void azcm_set_all_off(int off);
int  azcm_allow(int i);   /* an AZCM_NEW mod: approve the .so this start hashed; 0 = written */
int  azcm_new_count(void); /* AZCM_NEW mods this start, not yet allowed */
/* the paint context a mod's on_draw gets: a framebuffer view clipped to its area (menu.c) */
struct az_draw { azmod_fb fb; int ox, oy, sh; };   /* sh: the SCREEN height, for sizes (fb is clipped) */
void     azcm_draw_header(az_draw *d, const char *title);
void     azcm_draw_text(az_draw *d, int x, int y, const char *s, int px, uint32_t rgb);
void     azcm_fill_rect(az_draw *d, int x, int y, int w, int h, uint32_t rgb);
void     azcm_draw_rect(az_draw *d, int x, int y, int w, int h, int width, uint32_t rgb);
void     azcm_draw_checkbox(az_draw *d, int x, int y, int size, int on, int highlighted);
uint32_t azcm_color(int role);
int      azcm_row_height(az_draw *d);
int      azcm_text_width(const char *s, int px);
/* make a paint context for area (device px) of fb: what menu.c hands a mod's on_draw */
void     azcm_draw_init(az_draw *d, const azmod_fb *fb, az_rect area);
/* a Community tile icon scaled to sz x sz (premultiplied ARGB), or NULL until ready / on failure:
 * then draw the placeholder (community_icons.c; decoded on EP147's message thread) */
const uint32_t *azcm_icon(const char *path, int sz);
/* 1 = a mod needing min_core ("" = none) runs on this core (AZ_CORE_VERSION, community.c) */
int azcm_min_core_ok(const char *min_core);
int azcm_ver_newer(const char *a, const char *b);
int azcm_any_loaded(void);   /* a track on any deck, or unknown */
#pragma GCC visibility pop

#define AZMOD_INFO_LINES 5 /* uptime, mem free, mem total, AZ-MODS version, IP + SSH */
#define AZMOD_INFO_LINE_LEN                                                                        \
    40 /* "Installed vX.Y.ZZ. Reloading..." is 31 chars; room for 3-digit versions */
#define AZMOD_UPDATE_LINES 5 /* AZ_UPDATE text: installed / channel / progress / message / hint */
#define AZMOD_UPDATE_LINE_LEN                                                                      \
    128 /* line 1 carries a failure's whole reason; drawn fitted */

/* Native stems tiles are up: suppress our overlay strips (defined in azmod.c). */
extern volatile int azmod_stem_native;

/* SELECT PLAYLIST banner's DONE button (menu.c draws/hit-tests, azmod.c acts). */
void azmod_tb_done_rect(const azmod_fb *fb, int *x, int *y, int *w, int *h);
void azmod_tb_done(void); /* azmod.c: forget every stick's target, drop the banner */
void azmod_tb_undo_rect(const azmod_fb *fb, int *x, int *y, int *w, int *h);
void azmod_tb_undo(void); /* azmod.c: undo the last playlist edit */

/* ticks the orange "pressed" flash holds after an item is tapped/clicked,
 * at azmod.c's ~30 Hz redraw rate (~200ms). */
#define AZMOD_FLASH_TICKS 6

typedef struct {
    azmod_mode mode;
    bool source_visible; /* set by the present-hook's screen detector */
    bool tb_visible;     /* SELECT PLAYLIST: our held strip is up, draw DONE over its right end */
    bool tb_undo;        /* ...and UNDO shows (the last edit's result line), LEFT of DONE, amber */
    bool tb_done_btn;    /* DONE shows too (a clipboard or ADD TO shortcut to clear) */
    bool search_visible; /* AZ is on the SEARCH screen (spi3 byte32 0x08) */
    char search_q[64];   /* our query text (typed on our keyboard) */
    bool search_live;    /* results on screen belong to a search launched since the last clear */
    char search_res[5][80]; /* result rows (dev SEARCH panel) */
    char search_st[64];     /* status line (dev SEARCH panel) */
    int search_sel;         /* encoder-selected result row */
    bool search_want;       /* manual toggle (dev SEARCH button) */
    bool search_on;         /* SEARCH view latch (screen button seen) */
    bool nm_visible;        /* naming a NEW PLAYLIST in EP147's search field: show CREATE */
    int menu_sel;           /* highlighted row in whichever screen is active */
    int menu_focused; /* docked menu has knob focus (FOCUSED); 0 = PREVIEW, no orange row */
    /* FULL-SCREEN MOD MENU: while FOCUSED the menu covers
     * the screen like EP147's own SOURCE page: the mods on the left, the selected
     * one's settings (or its sub-screen) on the right. */
    int pane;          /* knob focus: 0 = the left list, 1 = the right pane */
    int left_sel;      /* the left list's selected item (a top-level item id) */
    bool want_focus;   /* menu.c asks azmod.c to FOCUS the menu (preview card tapped) */
    bool want_unfocus; /* ...or to hand the cursor back to the SOURCE list (header back) */
    int last_action;   /* item index activated this tick, else -1 (for hooks/log) */
    int pressed_item;  /* row flashing (tap feedback), -1 = none */
    int pressed_ticks; /* remaining flash ticks for pressed_item */
    bool mod_enabled[AZMOD_MOD_COUNT]; /* per-mod on/off, set via the TOGGLES screen */
    bool config_dirty;    /* true for one call after a toggle; azmod.c persists + clears */
    uint64_t pinned_mask; /* PIN: bit i set => mod i is pinned as a permanent native
       SOURCE-list row (left column) below MOD MENU. Toggled by the pin glyph in the
       docked menu header (pins the highlighted item). Persisted on line 3 of azmod.cfg;
       the JUCE srcrow hooks read this field directly. */
    bool pinrows_on;      /* mirror of the azmod-pinrows.on flag (azmod.c sets it per tick)
       so menu.c can gate the pin glyph without a syscall. Off = feature disabled. */
    bool drum_no_samples; /* azstem_drum_available() == 0: no DRUM ROLLS bank on the
       stick, so the drum roll shows as having nothing to play. azmod.c sets it per tick
       so menu.c stays syscall-free. */
    char update_line[AZMOD_UPDATE_LINES][AZMOD_UPDATE_LINE_LEN]; /* AZ_UPDATE text: azmod.c
        renders /tmp/az-modupdate/status (written by deck/lib/az-modupdate under the
        loader's watcher) into these; refreshed ~2/s while the screen is up */
    int update_uptodate; /* 1 when the status says the stick is up to date (drawn green) */
    int update_avail;    /* 1 when the channel has a newer set (status state=available); drawn as a
      red dot on the Update Mods row's icon and refreshed while the menu is open */
    int update_pct;      /* 0..100 progress of a running update (-1 = idle, no bar) */
    char mods_local[32]; /* installed mods version, "" = unknown (azmod.c, from the status) */
    char mods_remote[32]; /* the channel's newest version when the last check found one, else "" */
    /* CHANGELOG box on the Update Mods screen, under the button.
     * azmod.c loads /tmp/az-deck/CHANGELOG.txt (the staged MODS/deck, so it follows
     * the installed set, the no-stick cache and a deck-side update) each time the
     * screen opens and bumps changelog_gen; menu.c wraps it once per gen/width and
     * scrolls it by whole lines (knob turn, touch drag). Format: "# " section,
     * "## " version, "+ " new, "! " fixed, "^ " improved, "~ " changed. */
    char changelog[AZMOD_CHANGELOG_MAX];
    int changelog_len;
    unsigned changelog_gen;
    int changelog_scroll; /* scroll target, px from the top (knob / drag set it) */
    int changelog_pos;    /* scroll drawn, px; eases toward the target in azmod_tick */
    int update_busy;      /* 0 idle; 1 a CHECK, 2 an UPDATE, from the click until the updater
                           * reaches that request's final state (a check also ends at "available"); the
       bar stays up and only ever moves forward while this is set (no blink between the
       updater's check -> available -> downloading phases) */
    int update_want;      /* set by the screen's click: 1 = check, 2 = update; azmod.c writes
       /run/az-modupdate.req and clears it. The overlay never touches the network. */
    char info_line[AZMOD_INFO_LINES][AZMOD_INFO_LINE_LEN]; /* AZ_INFO text, filled
        by azmod.c (menu.c stays syscall-free: no /proc reads here) */
    bool stem_on[AZMOD_STEM_DECKS][AZMOD_STEM_COUNT];      /* per deck: which stems
             enabled (default all on); azmod.c writes azstem.stems/.stems2 on a deck
             screen. Reset to all-on when that deck loads a new track. */
    bool stems_available[AZMOD_STEM_DECKS];                /* per deck: azstem.so reports the
                       loaded track HAS stems (azstem.avail/.avail2/3/4). The NO-STEM GATE
                       keys everything stem-shaped on this, ANDed with the STEMS
                       master (mod_enabled[0]): a false deck draws no stem UI (no strip,
                       buttons, dots, lanes, tiles or title), its physical pad LEDs go dark and
                       its BEAT LOOP pad-mode button LED stays native orange instead of green:
                       the at-a-glance "this track has no stems" state. Hit-testing skips it
                       too. */
    int cpu_temp_c10;         /* CPU (soc-thermal) in tenths of a degree C,
read 1/s by azmod.c while mod 13 is on; < 0 = no sensor */
    int cpu_use_pct;          /* all-core busy %, 1/s; < 0 = not yet */
    int cpu_clk_mhz;          /* fastest core's current clock; < 0 = unknown */
    bool cpu_clk_throttled;   /* that core runs below its own max */
    int ram_use_pct;          /* (MemTotal - MemAvailable) / MemTotal; < 0 = unknown */
    bool pledit_needs_reload; /* Playlist Editing is ON but its hooks were not installed at start */
    bool reload_pending;      /* the loader staged a set this EP147 has not loaded (/run/az-reload-pending) */
    int fps_paint, fps_ui;    /* measured 1/s by azmod.c: WaveformView
paints (= actual on-screen FPS, the only number drawn) and JUCE
repaint-timer ticks (diagnostic only; lives in /tmp/azmod-rates). */
    bool waveform_visible; /* on EP147's WAVEFORM (deck) screen; the stem strip shows only
                              here, never on Browse or other views */
    int drum_zone; /* x-pad zone 0..5, -1 = no finger (FX rate row) */
    bool four_deck; /* 4-DECK layout: compact left column, no strip */
    bool pad_beatloop[AZMOD_STEM_DECKS]; /* per deck: BEAT LOOP pad mode selected
        (tracked from the ERP mode-button edges by azmod.c). The strip sits ON
        the native pad-grid slots, so it is drawn only in that mode: any other
        pad mode owns those slots. */
    /* PAD-MODE COLOR PICKER (AZ_COLORS / AZ_COLOR_PICK): per pad-mode button,
     * the chosen palette index (-1 = leave native). Applies to both decks.
     * azmod.c reads these to recolor the ERP LED frame and persists them.
     * color_sel = the button whose grid AZ_COLOR_PICK is showing. */
    int8_t padmode_color[AZMOD_PADMODE_COUNT];
    int8_t padmode_color2[AZMOD_PADMODE_COUNT]; /* the same buttons on their SECONDARY page */
    int color_sel;                              /* 0..AZMOD_PADPICK_COUNT-1 = 2*j + page */
    /* Master enable for the whole custom-pad-color feature. Off = azmod.c
     * publishes nothing and every button and pad shows EP147's own colors.
     * Row 0 of AZ_COLORS (a checkbox);
     * persisted on line 2 of azmod.cfg. */
    bool pad_colors_on;
} azmod_state;

/* Are the row-2 native mini stem lanes (with their playhead) drawn for this deck
 * right now? the one predicate for them: azmod_composite draws on it and the
 * overlay loop repaints on it, so the drawing and the repaint cannot sit behind
 * different gates. */
bool azmod_native_lanes_drawn(const azmod_state *s, int deck);

/* Sub-settings: a mod whose parent (or grandparent) is off is shown grayed and
 * indented, cannot be toggled, and never takes effect. */
int azmod_mod_parent(int i);                        /* -1 = top level */
bool azmod_mod_active(const azmod_state *s, int i); /* every ancestor enabled */
/* Flip mod i (a boolean row); a 60 FPS member toggled by hand clears the group's saved
 * members. Every toggle path (overlay menu, Overlays screen, native SOURCE rows) calls this,
 * never flips directly. */
void azmod_mod_toggle(azmod_state *s, int i);
/* Zero the state and set the defaults. Call once before first use; azmod.c then overrides
 * mod_enabled[] from its persisted config file. */
void azmod_state_init(azmod_state *s);
/* Main-menu label for item i (0..AZMOD_ITEM_COUNT-1), or NULL out of range.
 * azmod.c uses this to render the items as native SOURCE-list rows. */
const char *azmod_item_label(int i);
/* True if item i is hidden from the mod menu (retired, gated or release-hidden rows).
 * azmod.c uses it to keep hidden mods out of the SOURCE-list/pinned rows. */
bool azmod_item_hidden(int i);

/* Input event. touch_* in framebuffer pixels. enc_delta = browse-encoder steps
 * this tick (from the mixer-bus position counter, see azmod.c); enc_click = knob
 * pushed. The SOURCE-row button (AZ_CLOSED) is tap-only: enc_delta/enc_click are
 * ignored there; the knob only navigates once a menu
 * screen is already open. Returns true if azmod consumed the event (hook must
 * swallow it from EP147). */
bool azmod_input(azmod_state *s, const azmod_fb *fb, int touch_x, int touch_y, bool touched,
                 int enc_delta, bool enc_click);

/* Physical BACK button: one level up (a sub-screen -> Open -> Closed), same
 * as the on-screen X. Returns true if azmod handled it (hook must swallow
 * it from EP147); false when the menu is closed (EP147 gets its native Back). */
bool azmod_back(azmod_state *s);

/* Touch drag inside the Update screen's changelog box, fed from azmod.c's X
 * events: phase 0 = press, 1 = motion, 2 = release; y in framebuffer pixels.
 * Returns true while a drag the box owns is in progress. */
bool azmod_changelog_drag(azmod_state *s, const azmod_fb *fb, int y, int phase);
bool azmod_cm_drag(azmod_state *s, const azmod_fb *fb, int y, int phase);
void azmod_menu_clock(long ms);        /* ms now, before each touch event (tap timing) */
const char *azmod_menu_last_hit(void); /* the last touch decision, for the touch log */
int azmod_pane_drag(
    azmod_state *s, const azmod_fb *fb, int x, int y,
    int phase); /* the Mod Menu right pane: 0 / 1 redraw / 2 redraw + a row tapped */

/* Draw the active menu screen (nothing while AZ_CLOSED). */
void azmod_composite(azmod_state *s, const azmod_fb *fb);
void azmod_draw_stats(const azmod_state *s, const azmod_fb *fb); /* the OVERLAYS readouts */
/* A readout is shown when its own toggle and the OVERLAYS master are on. */
bool azmod_stat_on(const azmod_state *s, int mod);
bool azmod_stats_any(const azmod_state *s);
/* The readout band actually used: the enabled readouts packed right-aligned
 * against the FPS slot's right edge. */
void azmod_stats_rect(const azmod_state *s, const azmod_fb *fb, int *x, int *y, int *w, int *h);

/* A screen's row rect, i in [0..N]. */
void azmod_item_rect(const azmod_fb *fb, int i, int *x, int *y, int *w, int *h);

/* Full-screen menu (FOCUSED): the window rect, whether the knob is at the menu's
 * top level (the left list), and a reset to the first item. */
void azmod_menu_full_rect(const azmod_fb *fb, int *x, int *y, int *w, int *h);
bool azmod_menu_at_top(const azmod_state *s);
void azmod_menu_reset(azmod_state *s);
/* Overlay-window rect for an open menu screen (compact left strip). azmod.c
 * sizes the override-redirect window to this so EP147 shows through on the right. */
void azmod_menu_win_rect(const azmod_fb *fb, int *x, int *y, int *w, int *h);

/* Fade-strip bounds: solid content ends at *content_w, window at *win_w. azmod.c
 * dithers an X-Shape mask across [content_w,win_w) to fade the menu to clear. */
void azmod_menu_fade_bounds(const azmod_fb *fb, int *content_w, int *win_w);
/* Docked layout: the menu lives in EP147's SOURCE-screen right pane (the
 * per-source detail area, 640x563 @ 640,140 on 1280x800) instead of the
 * full-height left strip; used when opened from the native "MOD MENU"
 * source row (azmod.c install_srcrow_hook). Affects every rect above. */
void azmod_set_docked(bool on);
void azmod_set_phasemeter_visible(bool on); /* mod 36's row (shown in every build) */
void azmod_set_daymode_visible(bool on);    /* mod 37's row, Themes (shown in every build) */
int azmod_naming_playlist(void);            /* azmod.c: NEW PLAYLIST naming is on screen */
void azmod_naming_create(void);             /* azmod.c: the CREATE tap */
void azmod_naming_cancel(void);             /* azmod.c: the CANCEL tap */
const char *azmod_naming_text(void);        /* azmod.c: the name typed so far */
int azmod_naming_folder(void);              /* azmod.c: the name is for a NEW FOLDER */
int azmod_naming_rename(void);              /* azmod.c: RENAME (not create) */
int azmod_naming_dirty(void);               /* azmod.c: the name changed since last asked */
/* NEW PLAYLIST's bar over EP147's title row, and its two buttons (shared: draw + touch) */
void azmod_naming_rects(const azmod_fb *fb, int *bar, int *cancel, int *create);
/* The stem engine is on (the Stems mod): the one gate for the stem pads, strips and audio. */
bool azmod_stems_engine(const azmod_state *s);
/* The native SOURCE-list "MOD MENU" row's icon cell (left 90 px of the row,
 * like the USB/Cloud icons): draws bg + a MOD glyph into fb at (x,y,w,h).
 * `open` = our pane is showing (row is the selected one -> EP147-blue). */
void azmod_draw_srcrow_icon(const azmod_fb *fb, int x, int y, int w, int h, bool open);

/* Themes (mod 37) for azmod's own drawing: every pixel primitive in menu.c and text.c
 * passes its color through azm_theme(), the active theme's map (day_mode.c dm_map).
 * Without AZMOD_HAVE_DAYMODE these are identity functions. */
#ifdef AZMOD_HAVE_DAYMODE
int azmod_dm_on(void);
uint32_t azmod_dm_map(uint32_t c); /* day_mode.c dm_map: the active theme's map, identity when off */
static inline uint32_t azm_theme(uint32_t c) {
    if (!azmod_dm_on())
        return c;
    return azmod_dm_map(c);
}
int azmod_dm_key(void); /* 0 = Themes off, else 1 + the active theme: for caches of themed pixels */
static inline unsigned azm_theme_key(void) {
    return (unsigned) azmod_dm_key();
}
/* Day Mode text ink: a colored text color gets the darker shade of its hue that
 * reads at >= 4.5:1 on white and on the light panel gray (day_mode.c dm_ink, the
 * same rule as EP147's glyph path). Text only: text.c applies it to TTF glyphs. */
uint32_t azmod_dm_ink(uint32_t c);
static inline uint32_t azm_ink(uint32_t c) {
    return azmod_dm_ink(c);
}
#else
static inline uint32_t azm_theme(uint32_t c) {
    return c;
}
static inline unsigned azm_theme_key(void) {
    return 0;
}
static inline uint32_t azm_ink(uint32_t c) {
    return c;
}
#endif
bool azmod_docked(void);

/* Advance the tap-flash animation by one redraw tick. Call every azmod.c loop
 * iteration; returns true when a redraw is needed (flash active or just ended). */
bool azmod_tick(azmod_state *s);

#endif
