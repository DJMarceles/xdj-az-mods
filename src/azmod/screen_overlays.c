/* SPDX-License-Identifier: GPL-3.0-only */
/* screen_overlays.c: Overlays (AZ_OVERLAYS): its rows, and the deck-screen readouts it switches (FPS, CPU, RAM, temperature, clock).
 * Part of the Mod Menu's one translation unit: #included by menu.c, in the order listed there. */
/* AZ_OVERLAYS rows: the switch first, then its readouts (the table's rows under Overlays, in
 * their order). */
#define OVERLAYS_MAX 16
static int overlays_rows(int *out) {
    int ord[AZI_COUNT], n = menu_order(ord), m = 0;
    out[m++] = AZF_OVERLAYS;
    for (int p = 0; p < n && m < OVERLAYS_MAX; p++)
        if (az_parent(ord[p]) == AZF_OVERLAYS && !(az_row(ord[p])->d.flags & AZ_ROW_HIDDEN))
            out[m++] = ord[p];
    return m;
}
static const char *overlays_label(int r, int it) {
    return r == 0 ? "Show Overlays" : az_label(it);
}

#define FPS_X_FRAC 0.89060f /* 1140 @ 1280 */
#define FPS_Y_FRAC 0.07000f /*   56 @ 800  */
#define FPS_W_FRAC 0.10310f /*  132 */
#define FPS_H_FRAC 0.03250f /*   26 */
void azmod_fps_rect(const azmod_fb *fb, int *x, int *y, int *w, int *h) {
    *x = (int) (FPS_X_FRAC * fb->w);
    *y = (int) (FPS_Y_FRAC * fb->h);
    *w = (int) (FPS_W_FRAC * fb->w);
    *h = (int) (FPS_H_FRAC * fb->h);
}
/* On-screen FPS = WaveformView paints per second (the waveform is the only
 * animating surface; when it is not up the counter reads 0 by design). The JUCE
 * repaint-timer rate is a pipeline diagnostic, not an FPS (it runs ~2x paint),
 * so it is not shown here; it is in /tmp/azmod-rates as repaintTimer. */
static void draw_readout(const azmod_fb *fb, int x, int y, int w, int h, const char *t,
                         uint32_t col) {
    if (!azmod_ttf_center || !azmod_ttf_center(fb, x, y, w, h, t, (h * 3) / 5, 0, 0, col)) {
        int sc = h / 10;
        if (sc < 1)
            sc = 1;
        int tw = (int) strlen(t) * 6 * sc;
        draw_text(fb, x + (w - tw) / 2, y + (h - 7 * sc) / 2, t, sc, col);
    }
}
static void draw_fps_text(const azmod_state *s, const azmod_fb *fb, int x, int y, int w, int h) {
    char t[32];
    snprintf(t, sizeof t, "%d FPS", s->fps_paint);
    draw_readout(fb, x, y, w, h, t,
                 (s->fps_paint >= 50) ? 0x0030C040u : C_TEXT_DIM); /* green from 50 FPS up */
}
/* CPU temperature, whole degrees. Green while cool, yellow from 70 C, red from
 * 95 C, still before the RK3399's own passive throttle at
 * 105 C (the deck's soc-thermal trips: 105/110 passive, 115 critical). "\xB0"
 * is the degree sign: the TTF path loads glyphs by Latin-1 byte. */
#define TEMP_WARN_C10 700
#define TEMP_HOT_C10 950
#define C_READOUT_OK 0x0030C040u /* the readouts' green, a shade apart from ui_draw.c's C_OK */
#define C_WARN C_AVAIL
#define C_BAD 0x00FF4040u
static uint32_t level_col(int v, int warn, int bad) {
    return v >= bad ? C_BAD : v >= warn ? C_WARN
                                        : C_READOUT_OK;
}
static void draw_temp_text(const azmod_state *s, const azmod_fb *fb, int x, int y, int w, int h) {
    char t[16];
    if (s->cpu_temp_c10 < 0) {
        draw_readout(fb, x, y, w, h,
                     "--\xB0"
                     "C",
                     C_TEXT_DIM);
        return;
    }
    snprintf(t, sizeof t,
             "%d\xB0"
             "C",
             (s->cpu_temp_c10 + 5) / 10);
    draw_readout(fb, x, y, w, h, t, level_col(s->cpu_temp_c10, TEMP_WARN_C10, TEMP_HOT_C10));
}
static void draw_pct_text(const azmod_fb *fb, int x, int y, int w, int h, const char *tag, int pct,
                          int warn, int bad) {
    char t[16];
    if (pct < 0) {
        snprintf(t, sizeof t, "%s --%%", tag);
        draw_readout(fb, x, y, w, h, t, C_TEXT_DIM);
        return;
    }
    snprintf(t, sizeof t, "%s %d%%", tag, pct);
    draw_readout(fb, x, y, w, h, t, level_col(pct, warn, bad));
}
/* The fastest core's clock. The deck runs the performance governor (1.8 GHz
 * big / 1.416 GHz little, pinned), so this only moves when the SoC throttles,
 * which is what makes it worth showing: yellow whenever that core is below its
 * own maximum. */
static void draw_clk_text(const azmod_state *s, const azmod_fb *fb, int x, int y, int w, int h) {
    char t[16];
    if (s->cpu_clk_mhz < 0) {
        draw_readout(fb, x, y, w, h, "-.- GHz", C_TEXT_DIM);
        return;
    }
    int d = (s->cpu_clk_mhz + 50) / 100; /* tenths of a GHz */
    snprintf(t, sizeof t, "%d.%d GHz", d / 10, d % 10);
    draw_readout(fb, x, y, w, h, t, s->cpu_clk_throttled ? C_WARN : C_READOUT_OK);
}
/* The readouts, left to right, and each one's slot width (fraction of 1280),
 * packed against the FPS slot's right edge (x 1272). Only the ones switched on
 * take room. All five need x 788-1272; y 50-84 carries no native ink from x 400
 * rightward in every deck layout (the playhead line is at x 395). */
static const struct {
    int mod;
    float wfrac;
} STATS[] = {
    {AZF_OVERLAY_CPU_CLOCK, 0.0781f}, /* 100: "1.8 GHz" */
    {AZF_OVERLAY_CPU_USAGE, 0.0813f}, /* 104: "CPU 37%" */
    {AZF_OVERLAY_RAM, 0.0813f},    /* 104: "RAM 45%" */
    {AZF_OVERLAY_CPU_TEMP, 0.0594f},   /*  76: "59 C" */
    {AZF_OVERLAY_FPS, 0.0781f},    /* 100: "60 FPS" */
};
#define STATS_N ((int) (sizeof STATS / sizeof STATS[0]))
bool azmod_stat_on(const azmod_state *s, int mod) {
    (void) s; /* the API takes the state; the switches are global */
    return az_on(mod) && az_active(mod);
}
bool azmod_stats_any(const azmod_state *s) {
    for (int k = 0; k < STATS_N; k++)
        if (azmod_stat_on(s, STATS[k].mod))
            return true;
    return false;
}
void azmod_stats_rect(const azmod_state *s, const azmod_fb *fb, int *x, int *y, int *w, int *h) {
    int fx, fy, fw, fh;
    azmod_fps_rect(fb, &fx, &fy, &fw, &fh);
    int total = 0;
    for (int k = 0; k < STATS_N; k++)
        if (azmod_stat_on(s, STATS[k].mod))
            total += (int) (STATS[k].wfrac * fb->w);
    if (total < 1)
        total = 1;
    *x = fx + fw - total;
    *y = fy;
    *w = total;
    *h = fh;
}

void azmod_draw_stats(const azmod_state *s, const azmod_fb *fb) {
    int x, y, w, h;
    azmod_stats_rect(s, fb, &x, &y, &w, &h);
    fill_rect(fb, x, y, w, h, 0x00000000u);
    for (int k = 0; k < STATS_N; k++) {
        int m = STATS[k].mod;
        if (!azmod_stat_on(s, m))
            continue;
        int sw = (int) (STATS[k].wfrac * fb->w);
        switch (m) {
        case AZF_OVERLAY_CPU_CLOCK:
            draw_clk_text(s, fb, x, y, sw, h);
            break;
        case AZF_OVERLAY_CPU_USAGE:
            draw_pct_text(fb, x, y, sw, h, "CPU", s->cpu_use_pct, 70, 90);
            break;
        case AZF_OVERLAY_RAM:
            draw_pct_text(fb, x, y, sw, h, "RAM", s->ram_use_pct, 75, 90);
            break;
        case AZF_OVERLAY_CPU_TEMP:
            draw_temp_text(s, fb, x, y, sw, h);
            break;
        case AZF_OVERLAY_FPS:
            draw_fps_text(s, fb, x, y, sw, h);
            break;
        }
        x += sw;
    }
}
