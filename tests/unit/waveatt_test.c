/* SPDX-License-Identifier: GPL-3.0-only */
/* The stem waveform's height curve follows the pad toggles AND the Stem Volume gains
 * (guest/azmod/wave_att.h natively; guest/azmod/waveform.c build_atten and
 * guest/azmod/mod_menu_overlay.c ov_rebuild_attenuation, extracted and built against stand-in
 * globals).
 *
 * The rules under test:
 *   - every stem on at unity gain renders stock (identity), and at unity gains every pad mask
 *     gives exactly the curve the toggle-only rule gave;
 *   - a stem's knob at its bottom shapes the waveform exactly as that stem's pad off does, and
 *     a knob between scales that stem's share in between;
 *   - the factor never exceeds unity, so a boosted stem never makes the height carry into the
 *     bits EP147 packs beside it;
 *   - each deck's curve uses that deck's own gains;
 *   - a pad toggle rebuilds on the next tick; a gain-only change rebuilds at most every
 *     WATT_GAIN_MIN_MS, and the last value of a sweep still lands; a change of 1/200 in gain
 *     is not lost to quantization;
 *   - the overview tiles of only the deck that changed are refreshed: at once for a toggle, at
 *     most every WATT_OV_MIN_MS for a gain sweep, and the sweep's final value still reaches them;
 *   - while the envelope is still being built, the every-tick retry publishes nothing;
 *   - without azstem's gain export the curve is the toggle-only one;
 *   - the curve is rebuilt on one thread while another reads it (run under ThreadSanitizer).
 * run.sh also builds this from mutated copies: gains ignored (the behavior before Stem Volume
 * reached the waveform), the unity cap dropped, the gain rate limit dropped, deck 0's gains
 * used for every deck, the coarse 1/64 gain step, overview tiles refreshed on every gain
 * rebuild, every deck's tiles refreshed for one deck, and the retry publishing a curve each
 * tick. Each must fail. */
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "check.h"

#define AZMOD_STEM_DECKS 4
#define AZMOD_STEM_COUNT 4
#define AZMOD_STEM_PALETTE {0x22E022u, 0xFF3030u, 0xFF33CCu, 0x4060FFu}
#define ATT_N 16384
#include "wave_att.h"

/* Stand-ins for the azmod globals the extracted code uses. */
static struct {
    bool stem_on[AZMOD_STEM_DECKS][AZMOD_STEM_COUNT];
} g_state;
static int g_att_have[AZMOD_STEM_DECKS], g_att_ident[AZMOD_STEM_DECKS], g_ov_refresh;
static int ov_take(void) { return __atomic_exchange_n(&g_ov_refresh, 0, __ATOMIC_ACQ_REL); }
static int g_wave_dirty[AZMOD_STEM_DECKS];
static unsigned g_att_gen[AZMOD_STEM_DECKS];
static unsigned short g_att[AZMOD_STEM_DECKS][ATT_N];
static unsigned g_col[AZMOD_STEM_DECKS][ATT_N];
static void wwb_log(const char *fmt, ...) { (void) fmt; }

/* azstem's side: a 4-bucket envelope per deck with one stem alone in each bucket, then all
 * four together. Bucket k (k < 4) holds only stem k at 200; bucket 4 holds all four at 100. */
#define ENV_N 5
static unsigned char g_env[AZMOD_STEM_DECKS][AZMOD_STEM_COUNT * ENV_N];
static int g_have_env[AZMOD_STEM_DECKS];
static int stem_wave_stub(int deck, const unsigned char **env, int *n, int *nstem, float *pos) {
    *env = g_env[deck];
    *n = ENV_N;
    *nstem = AZMOD_STEM_COUNT;
    *pos = 0;
    return __atomic_load_n(&g_have_env[deck], __ATOMIC_RELAXED);
}
static int (*azmod_stem_wave)(int, const unsigned char **, int *, int *, float *) = stem_wave_stub;
static long g_fr[AZMOD_STEM_DECKS]; /* > 0: the frame count a decode in progress reports */
static int frames_stub(int deck) {
    long fr = __atomic_load_n(&g_fr[deck], __ATOMIC_RELAXED);
    return fr > 0 ? (int) fr : g_have_env[deck] ? 44100 * 60
                                                : 0;
}
static unsigned g_eg[AZMOD_STEM_DECKS]; /* azstem_env_gen */
static unsigned env_gen_stub(int deck) { return __atomic_load_n(&g_eg[deck], __ATOMIC_RELAXED); }
static unsigned (*azmod_stem_env_gen)(int) = env_gen_stub;
static int (*g_stem_frames)(int) = frames_stub;
static int g_gain[AZMOD_STEM_DECKS][AZMOD_STEM_COUNT]; /* azstem_stem_gain, x1000 */
static int gain_stub(int deck, int stem) {
    return __atomic_load_n(&g_gain[deck][stem], __ATOMIC_RELAXED);
}
static int (*g_stem_gain_fn)(int, int) = gain_stub;

/* A clock the test drives, for the rate limit. */
static long g_now_ms = 1000000;
static int fake_clock(struct timespec *t) {
    long ms = __atomic_load_n(&g_now_ms, __ATOMIC_RELAXED);
    t->tv_sec = ms / 1000;
    t->tv_nsec = (ms % 1000) * 1000000L;
    return 0;
}
#define clock_gettime(c, t) fake_clock(t)

#include WATT_INC

#undef clock_gettime

static int bucket_slot(int b) { return (int) (((long) b * ATT_N + ATT_N - 1) / ENV_N); }
static unsigned att_at(int d, int b) { return g_att[d][bucket_slot(b)]; }

static void setup(void) {
    memset(&g_state, 0, sizeof g_state);
    for (int d = 0; d < AZMOD_STEM_DECKS; d++) {
        for (int k = 0; k < AZMOD_STEM_COUNT; k++) {
            g_state.stem_on[d][k] = true;
            g_gain[d][k] = 1000;
            for (int b = 0; b < ENV_N; b++)
                g_env[d][k * ENV_N + b] = b == k ? 200 : (b == 4 ? 100 : 0);
        }
        g_have_env[d] = 1;
    }
}

/* The toggle-only rule as it stood before gains reached the curve. */
static unsigned old_rule(int d, int b) {
    unsigned on = 0, all = 0;
    for (int k = 0; k < AZMOD_STEM_COUNT; k++) {
        unsigned v = g_env[d][k * ENV_N + b];
        all += v;
        if (g_state.stem_on[d][k])
            on += v;
    }
    return all ? (on * 256u + all / 2) / all : 256;
}

static int curve_equals(int d, const unsigned short *ref) {
    return !memcmp(g_att[d], ref, sizeof g_att[d]);
}

/* One overlay tick some ms after the last. */
static void tick(long ms) {
    g_now_ms += ms;
    ov_rebuild_attenuation();
}

static int g_stop;
static void *reader(void *arg) {
    long sum = 0, bad = 0;
    while (!__atomic_load_n(&g_stop, __ATOMIC_ACQUIRE))
        for (int i = 0; i < ATT_N; i += 7) {
            unsigned a = __atomic_load_n(&g_att[1][i], __ATOMIC_RELAXED);
            sum += a;
            bad += a > WATT_UNITY;
            (void) __atomic_load_n(&g_col[1][i], __ATOMIC_RELAXED);
        }
    *(long *) arg = bad;
    (void) sum;
    return 0;
}

int main(void) {
    ut_suite = "wave att";
    setup();
    tick(1000);

    /* unity: stock */
    int stock = 1;
    for (int i = 0; i < ATT_N; i++)
        stock &= g_att[0][i] == WATT_UNITY;
    CHECK("every stem on at unity gain: the curve is unity", stock);
    CHECK("every stem on at unity gain: flagged identity", g_att_ident[0] == 1);

    /* unity gains reproduce the toggle-only rule for every mask */
    int same = 1;
    for (unsigned m = 0; m < 16; m++) {
        for (int k = 0; k < AZMOD_STEM_COUNT; k++)
            g_state.stem_on[0][k] = (m >> k) & 1;
        tick(10);
        for (int b = 0; b < ENV_N; b++)
            same &= att_at(0, b) == old_rule(0, b);
    }
    CHECK("unity gains: each of the 16 pad masks gives the toggle-only curve", same);

    /* drums knob at the bottom == drums pad off */
    setup();
    g_state.stem_on[0][3] = false;
    tick(1000);
    static unsigned short pad_off[ATT_N];
    memcpy(pad_off, g_att[0], sizeof pad_off);
    g_state.stem_on[0][3] = true;
    tick(1000);
    unsigned gen0 = g_att_gen[0];
    __atomic_store_n(&g_wave_dirty[0], 0, __ATOMIC_RELAXED);
    g_gain[0][3] = 0;
    tick(1000);
    CHECK("drums knob down: the curve was rebuilt", g_att_gen[0] != gen0);
    CHECK("drums knob down: the deck's waveform was marked for repaint", g_wave_dirty[0] == 1);
    CHECK("drums knob down: the same curve as the drums pad off", curve_equals(0, pad_off));
    CHECK("drums knob down: not identity", g_att_ident[0] == 0);
    CHECK("drums knob down: a drums-only position is flat", att_at(0, 3) == 0);
    CHECK("drums knob down: a vocal-only position is untouched", att_at(0, 0) == WATT_UNITY);

    /* half gain: half the share */
    g_gain[0][3] = 500;
    tick(1000);
    CHECK("drums knob at half gain: a drums-only position at half height", att_at(0, 3) == 128);
    CHECK("drums knob at half gain: the four-stem position at 7/8",
          att_at(0, 4) == (3 * 256 + 128) / 4);

    /* boost: capped at unity */
    g_gain[0][3] = 1000;
    g_gain[0][0] = 2000;
    tick(1000);
    unsigned mx = 0;
    for (int i = 0; i < ATT_N; i++)
        if (g_att[0][i] > mx)
            mx = g_att[0][i];
    CHECK("vocals boosted to +6 dB: no factor above unity", mx <= WATT_UNITY);
    CHECK("vocals boosted to +6 dB: the four-stem position stays at full height",
          att_at(0, 4) == WATT_UNITY);
    CHECK("vocals boosted, drums off: a mixed position is not flattened",
          (g_state.stem_on[0][3] = false, tick(1000), att_at(0, 4) == WATT_UNITY));
    g_state.stem_on[0][3] = true;
    g_gain[0][0] = 1000;
    tick(1000);

    /* per deck */
    setup();
    tick(1000);
    g_gain[1][3] = 0;
    tick(1000);
    CHECK("deck 2's drums knob down: deck 2's drums-only position is flat", att_at(1, 3) == 0);
    CHECK("deck 2's drums knob down: deck 1 is untouched", att_at(0, 3) == WATT_UNITY);
    CHECK("deck 2's drums knob down: deck 1 still identity", g_att_ident[0] == 1);

    /* rate limit and the trailing value */
    setup();
    tick(1000);
    unsigned g1 = g_att_gen[1];
    g_gain[1][3] = 800;
    tick(1000);
    CHECK("gain change long after the last rebuild: rebuilt on this tick", g_att_gen[1] != g1);
    g1 = g_att_gen[1];
    g_gain[1][3] = 600;
    tick(10);
    g_gain[1][3] = 400;
    tick(10);
    CHECK("gain sweep inside the interval: no rebuild", g_att_gen[1] == g1);
    tick(WATT_GAIN_MIN_MS);
    CHECK("gain sweep: the last value lands after the interval",
          g_att_gen[1] != g1 && att_at(1, 3) == (unsigned) watt_value((unsigned[]) {0, 0, 0, 200},
                                                                      0xf, (int[]) {WATT_GQ_ONE, WATT_GQ_ONE, WATT_GQ_ONE, watt_gain_q(400)}));
    g1 = g_att_gen[1];
    g_gain[1][3] = 300;
    tick(10);
    g_state.stem_on[1][0] = false;
    tick(10);
    CHECK("pad toggle inside the gain interval: rebuilt on this tick", g_att_gen[1] != g1);
    g1 = g_att_gen[1];
    g_gain[1][3] = 301; /* the same 1/256 step as 300 */
    tick(1000);
    CHECK("knob jitter inside one gain step: no rebuild", g_att_gen[1] == g1);
    g_gain[1][3] = 1000;
    tick(1000);
    g1 = g_att_gen[1];
    g_gain[1][3] = 995;
    tick(1000);
    CHECK("a 1/200 gain change: rebuilt (not lost to quantization)", g_att_gen[1] != g1);

    /* overview tiles: per deck, own cadence */
    setup();
    tick(1000);
    ov_take();
    g_gain[1][3] = 700;
    tick(1000);
    CHECK("gain change long after the last refresh: deck 2's tiles at once", ov_take() == 2);
    g1 = g_att_gen[1];
    g_gain[1][3] = 600;
    tick(WATT_GAIN_MIN_MS);
    CHECK("gain sweep: the main waveform's curve follows at the fast cadence", g_att_gen[1] != g1);
    CHECK("gain sweep inside the overview interval: tiles not refreshed", ov_take() == 0);
    g_gain[1][3] = 500;
    tick(WATT_GAIN_MIN_MS);
    CHECK("gain sweep, second step: tiles still not refreshed", ov_take() == 0);
    tick(WATT_OV_MIN_MS);
    CHECK("gain sweep: the final value reaches deck 2's tiles", ov_take() == 2);
    g_gain[1][3] = 400;
    tick(WATT_GAIN_MIN_MS);
    ov_take();
    g_state.stem_on[1][1] = false;
    tick(10);
    CHECK("pad toggle: deck 2's tiles refreshed at once", ov_take() == 2);

    /* the retry while the envelope is built */
    setup();
    tick(1000);
    ov_take();
    g_have_env[3] = 0;
    tick(10);
    unsigned g3 = g_att_gen[3];
    int bits = ov_take(), dirty = 0;
    __atomic_store_n(&g_wave_dirty[3], 0, __ATOMIC_RELAXED);
    for (int r = 0; r < 50; r++) {
        tick(10);
        bits |= ov_take();
        dirty |= __atomic_exchange_n(&g_wave_dirty[3], 0, __ATOMIC_RELAXED);
    }
    CHECK("envelope gone: deck 4's tiles refreshed once", bits == 8);
    CHECK("envelope still missing for 50 ticks: no curve published", g_att_gen[3] == g3 && !dirty);

    /* no export */
    setup();
    g_gain[2][3] = 0;
    g_stem_gain_fn = 0;
    g_state.stem_on[2][0] = false; /* a toggle, so the deck rebuilds */
    tick(1000);
    CHECK("no azstem gain export: unity (toggle-only) curve", att_at(2, 3) == WATT_UNITY);
    g_stem_gain_fn = gain_stub;

    /* quantization */
    CHECK("gain quantum: 1000 is unity", watt_gain_q(1000) == WATT_GQ_ONE);
    CHECK("gain quantum: 0 and below are silent", watt_gain_q(0) == 0 && watt_gain_q(-1) == 0);
    CHECK("gain quantum: above +6 dB clamps", watt_gain_q(5000) == WATT_GQ_MAX);

    /* concurrent rebuild and read */
    setup();
    tick(1000);
    long bad = 0;
    pthread_t th;
    pthread_create(&th, 0, reader, &bad);
    for (int r = 0; r < 40; r++) {
        __atomic_store_n(&g_gain[1][r & 3], (r * 137) % 2001, __ATOMIC_RELAXED);
        tick(WATT_GAIN_MIN_MS);
    }
    __atomic_store_n(&g_stop, 1, __ATOMIC_RELEASE);
    pthread_join(th, 0);
    CHECK("curve rebuilt while another thread reads it: every read within unity", bad == 0);

    /* a load's decode: the frame count grows on every 10 ms tick for 6 s */
    setup();
    tick(1000);
    (void) ov_take();
    unsigned g0 = g_att_gen[2];
    int refreshes = 0;
    for (int t = 0; t < 600; t++) {
        __atomic_store_n(&g_fr[2], 44100L * (t + 1), __ATOMIC_RELAXED);
        tick(10);
        refreshes += (ov_take() >> 2) & 1;
    }
    unsigned rebuilds = g_att_gen[2] - g0;
    CHECK("a decode's progress alone rebuilds the curve at most every WATT_PROG_MIN_MS",
          rebuilds >= 6 && rebuilds <= 6000 / WATT_PROG_MIN_MS + 1);
    CHECK("...and refreshes the overview tiles no more often", refreshes >= 6 && refreshes <= (int) rebuilds + 1);
    /* a pad toggle in the middle of the decode: on this tick */
    g0 = g_att_gen[2];
    g_state.stem_on[2][0] = false;
    __atomic_store_n(&g_fr[2], 44100L * 700, __ATOMIC_RELAXED);
    tick(10);
    CHECK("a pad toggle during the decode rebuilds the curve on the same tick", g_att_gen[2] == g0 + 1);
    CHECK("...and refreshes that deck's overview tiles on the same tick", (ov_take() >> 2) & 1);
    /* a new envelope (a cache hit, or the final build) during the decode: on this tick */
    g0 = g_att_gen[2];
    g_eg[2]++;
    __atomic_store_n(&g_fr[2], 44100L * 701, __ATOMIC_RELAXED);
    tick(10);
    CHECK("a new envelope rebuilds the curve on the same tick", g_att_gen[2] == g0 + 1);
    /* the decode ends: the last frame count is reached within WATT_PROG_MIN_MS */
    g0 = g_att_gen[2];
    __atomic_store_n(&g_fr[2], 44100L * 702, __ATOMIC_RELAXED);
    for (int t = 0; t < WATT_PROG_MIN_MS / 10 + 1; t++)
        tick(10);
    CHECK("the decode's last progress reaches the curve within WATT_PROG_MIN_MS", g_att_gen[2] == g0 + 1);
    __atomic_store_n(&g_fr[2], 0, __ATOMIC_RELAXED);

    return ut_summary();
}
