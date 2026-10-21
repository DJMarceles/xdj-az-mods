/* SPDX-License-Identifier: GPL-3.0-only */
/* Beat jump into the Extended Lead-In (guest/azmod/leadin_jump.h): which targets before the
 * track's start may be jumped to. Targets come from a model of EP147's beat jump (the beat grid
 * extrapolated before its first beat at the first beat's tempo), checked against the positions
 * the emulator logged for a 134 BPM track at 44100 Hz whose first beat is at sample 2352. */
#include <stdio.h>
#include LJ_HDR

static int fails, checks;
static void check(const char *name, int ok) {
    checks++;
    printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    fails += !ok;
}

#define CAP_MS (-300000L)
/* EP147's target for a jump of n beats back from the beat grid's first beat: the first beat's
 * position less n beats of rate * 60 / bpm samples, rounded to the nearest sample */
static long model_target(long first, double bpm, long rate, double n) {
    double t = (double) first - n * (double) rate * 60.0 / bpm;
    return (long) (t < 0 ? t - 0.5 : t + 0.5);
}

int main(void) {
    const long first = 2352, rate = 44100;
    const double bpm = 134.0; /* the grid's tempo: 19746.27 samples a beat */
    long t1 = model_target(first, bpm, rate, 1), t4 = model_target(first, bpm, rate, 4),
         t16 = model_target(first, bpm, rate, 16);
    check("model: 1, 4 and 16 beats back land where the emulator logged (-17394, -76633, -313588)",
          t1 == -17394 && t4 == -76633 && t16 == -313588);
    check("a target before the start within the lead-in may be jumped to (1, 4, 16 beats)",
          lj_decide(t1, rate, CAP_MS) == LJ_ALLOW && lj_decide(t4, rate, CAP_MS) == LJ_ALLOW &&
              lj_decide(t16, rate, CAP_MS) == LJ_ALLOW);
    check("a target at or after the start is EP147's own question", lj_decide(0, rate, CAP_MS) == LJ_EP147 &&
                                                                        lj_decide(first, rate, CAP_MS) == LJ_EP147);
    check("one sample before the start may be jumped to", lj_decide(-1, rate, CAP_MS) == LJ_ALLOW);

    /* the floor: five minutes at the track's own sample rate */
    check("floor at 44100 Hz is -13230000", lj_floor(CAP_MS, 44100) == -13230000L);
    check("floor at 48000 Hz is -14400000", lj_floor(CAP_MS, 48000) == -14400000L);
    check("exactly the floor: allowed", lj_decide(-13230000L, 44100, CAP_MS) == LJ_ALLOW);
    check("one sample past the floor: refused", lj_decide(-13230001L, 44100, CAP_MS) == LJ_REFUSE);
    check("the same target at 48000 Hz is within its floor", lj_decide(-13230001L, 48000, CAP_MS) == LJ_ALLOW);
    /* repeated 16-beat jumps from the first beat: 41 land, the 42nd would pass -5:00 (the emulator
     * stopped at -12951188 and refused -13267128) */
    long pos = first;
    int landed = 0;
    for (int k = 0; k < 60; k++) {
        long t = pos - 315940; /* 16 beats */
        if (lj_decide(t, rate, CAP_MS) != LJ_ALLOW)
            break;
        pos = t;
        landed++;
    }
    check("41 jumps of 16 beats land, the last at -12951188", landed == 41 && pos == -12951188L);
    check("the next one (-13267128) is refused", lj_decide(pos - 315940, rate, CAP_MS) == LJ_REFUSE);

    /* malformed input: refused, as EP147 refuses a target before the start */
    check("unknown sample rate (0) refused", lj_decide(-1000, 0, CAP_MS) == LJ_REFUSE);
    check("sample rate below the range refused", lj_decide(-1000, LJ_RATE_MIN - 1, CAP_MS) == LJ_REFUSE);
    check("sample rate above the range refused", lj_decide(-1000, LJ_RATE_MAX + 1, CAP_MS) == LJ_REFUSE);
    check("a cap that is not before the start refuses every target before it",
          lj_decide(-1, rate, 0) == LJ_REFUSE && lj_decide(-1, rate, 1000) == LJ_REFUSE);
    check("the most negative target refused", lj_decide(-9223372036854775807L - 1, rate, CAP_MS) == LJ_REFUSE);
    check("the largest rate and cap do not overflow", lj_floor(CAP_MS, LJ_RATE_MAX) == -115200000L);
    printf("----  leadin jump: %d passed, %d failed\n", checks - fails, fails);
    return fails != 0;
}
