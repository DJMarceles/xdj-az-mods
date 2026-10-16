/* SPDX-License-Identifier: GPL-3.0-only */
/* Drum Roll samples (guest/azstem/drumroll.c, extracted by tools/fuzz/lib.sh: the WAV reader
 * drum_decode_wav with its helpers and limits, and drum_slot_for_name): any file in the stick's
 * DRUM ROLLS/Bank N folders.
 *
 * Input byte 0: even = the rest is the .wav file; odd = the rest is a file name.
 * A decoded sample is stereo and at most DRUM_MAX_FRAMES long. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fuzz_common.h"
#include "ep147rate.h"
#define DRUM_SR AZ_EP147_RATE
#include DRUMWAV_INC

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (n < 1 || n > (1 << 20)) return 0;
    if (d[0] & 1) {
        char *s = malloc(n);
        memcpy(s, d + 1, n - 1);
        s[n - 1] = 0;
        int r = drum_slot_for_name(s);
        FZ_ASSERT(r >= -1 && r <= 3);
        free(s);
        return 0;
    }
    const char *p = fz_file("sample.wav", d + 1, n - 1);
    long frames = -1;
    float *b = drum_decode_wav(p, &frames);
    if (b) {
        FZ_ASSERT(frames > 0 && frames <= DRUM_MAX_FRAMES);
        volatile float sink = 0; /* every sample is readable (the buffer is frames x 2) */
        for (long i = 0; i < frames * 2; i++) sink += b[i];
        free(b);
    } else {
        FZ_ASSERT(frames == 0);
    }
    return 0;
}
