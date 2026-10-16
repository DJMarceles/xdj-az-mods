/* SPDX-License-Identifier: GPL-3.0-only */
/* decode_ogg_stereo_44100 (extracted from azstem/stem_playback.c by run.sh)
 * against a scripted stb_vorbis: a stream of known rate, channels and length.
 *
 *   - a failed allocation of the resample buffer returns NULL with no frames and
 *     nothing leaked, never the native-rate buffer (which would play at 44100, at the
 *     wrong pitch and speed);
 *   - max_frames caps the output (the drum roll's per-sample cap), 0 decodes it all. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"

typedef struct {
    int unused;
} stb_vorbis;
typedef struct {
    unsigned int sample_rate;
    int channels;
} stb_vorbis_info;
static stb_vorbis fake;
static unsigned fake_sr = 48000, fake_total = 4800;
static int fake_ch = 2, fake_open = 0;
static stb_vorbis *stb_vorbis_open_filename(const char *p, int *err, void *a) {
    (void) p;
    (void) err;
    (void) a;
    fake_open++;
    return &fake;
}
static stb_vorbis_info stb_vorbis_get_info(stb_vorbis *v) {
    (void) v;
    stb_vorbis_info i = {fake_sr, fake_ch};
    return i;
}
static unsigned stb_vorbis_stream_length_in_samples(stb_vorbis *v) {
    (void) v;
    return fake_total;
}
static long last_req; /* frames the decoder was last asked for */
static int stb_vorbis_get_samples_float_interleaved(stb_vorbis *v, int ch, float *buf, int n) {
    (void) v;
    last_req = n / ch;
    for (int i = 0; i < n; i++)
        buf[i] = 0.5f;
    return n / ch;
}
static void stb_vorbis_close(stb_vorbis *v) {
    (void) v;
    fake_open--;
}

/* malloc that fails on call number fail_at (1-based, 0 = never) and counts what is live */
static int n_alloc, fail_at, live;
static void *ut_malloc(size_t n) {
    n_alloc++;
    if (fail_at && n_alloc == fail_at)
        return NULL;
    live++;
    return malloc(n);
}
static void ut_free(void *p) {
    if (p)
        live--;
    free(p);
}
#include "../../guest/azstem/ep147rate.h" /* AZ_EP147_RATE, as stem_playback.c sees it */
#define malloc(n) ut_malloc(n)
#define free(p) ut_free(p)
#include OGG_INC
#undef malloc
#undef free

static float *run(unsigned sr, unsigned total, int ch, int fail, long max, long *fr) {
    fake_sr = sr;
    fake_total = total;
    fake_ch = ch;
    n_alloc = 0;
    fail_at = fail;
    live = 0;
    *fr = -1;
    return decode_ogg_stereo_44100("x.ogg", fr, max);
}

int main(void) {
    ut_suite = "ogg decode";
    long fr;
    float *b = run(48000, 4800, 2, 0, 0, &fr);
    CHECK("48 kHz resampled to 44100 (4800 -> 4410 frames)", b && fr == 4410);
    ut_free(b);
    CHECK("...nothing else left allocated", live == 0);

    /* allocations: 1 native, 2 stereo, 3 resample */
    b = run(48000, 4800, 2, 3, 0, &fr);
    CHECK("resample allocation fails: NULL, not the native-rate buffer", b == NULL);
    CHECK("...with no frames reported", fr == 0);
    CHECK("...and nothing leaked", live == 0);
    if (b)
        ut_free(b);
    b = run(48000, 4800, 1, 2, 0, &fr);
    CHECK("stereo allocation fails: NULL, no frames, nothing leaked", b == NULL && fr == 0 && live == 0);
    CHECK("the stream is closed on every path", fake_open == 0);

    b = run(44100, 44100 * 30, 2, 0, 44100 * 8, &fr);
    CHECK("44100 source capped at max_frames", b && fr == 44100 * 8);
    ut_free(b);
    b = run(48000, 48000 * 30, 1, 0, 44100 * 8, &fr);
    CHECK("48 kHz source capped at max_frames after resampling", b && fr == 44100 * 8);
    CHECK("...and only the capped part is decoded (8 s of 48 kHz, not 30 s)", last_req <= 48000L * 8 + 2);
    ut_free(b);
    b = run(48000, 48000 * 30, 1, 0, 0, &fr);
    CHECK("max_frames 0 decodes the whole stream (stems)", b && fr == 44100 * 30);
    ut_free(b);
    b = run(0, 4800, 2, 0, 0, &fr);
    CHECK("0 Hz stream refused", b == NULL && fr == 0);
    return ut_summary();
}
