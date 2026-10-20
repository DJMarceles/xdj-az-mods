/* SPDX-License-Identifier: GPL-3.0-only */
/* A scrolled list's motion (guest/azmod/scroll_glide.h): while the finger is down the list is at
 * the finger's point the moment each report arrives (no lag), a lift while moving flings the list
 * on and it slows to a stop, a finger that stopped before lifting does not fling, a fling stops at either end, a
 * finger landing catches it, and a knob step glides. The finger stream is the hardware's: a
 * report every 60 ms, 10 px apart. GLIDE_HDR = the header under test. */
#include "check.h"
#include GLIDE_HDR

#define FRAME 10 /* ms between frames, the menu's tick */
#define MAX 2000.f

static struct glide g;
static long now;

/* run frames up to ms; the largest move of one frame, and the lag behind the goal at the end */
static float frames(long until, float *lag) {
    float big = 0;
    while (now + FRAME <= until) {
        now += FRAME;
        float was = g.pos;
        glide_step(&g, now, MAX);
        float d = g.pos - was;
        if (d < 0)
            d = -d;
        if (d > big)
            big = d;
    }
    if (lag)
        *lag = g.goal - g.pos;
    return big;
}
/* a drag from y, 10 px per report every 60 ms, n reports; returns the largest frame step */
static float drag(float y, int n, float dir) {
    float big = 0;
    for (int k = 1; k <= n; k++) {
        float b = frames(now + 60, NULL);
        if (b > big)
            big = b;
        glide_drag(&g, y + dir * 10.f * (float) k, now);
    }
    return big;
}
/* frames until it stops (or limit ms); returns the ms it took */
static long settle(long limit) {
    long t0 = now;
    while (g.on && now - t0 < limit) {
        now += FRAME;
        glide_step(&g, now, MAX);
    }
    return now - t0;
}

int main(void) {
    ut_suite = "scroll glide";

    /* following the finger */
    now = 1000;
    glide_set(&g, 500);
    CHECK("a set offset is still", !g.on && glide_px(&g) == 500 && glide_step(&g, now, MAX) == 0);
    /* the finger-to-list lag: after each report (drawn in the same frame, as the menu does), how
     * far the list is from the finger and how long until it is within 1 px */
    float worst = 0;
    long reach = 0;
    for (int k = 1; k <= 12; k++) {
        frames(now + 60, NULL);
        float y = 500 + 10.f * (float) k;
        glide_drag(&g, y, now);
        glide_step(&g, now, MAX);
        float d = y - g.pos;
        if (d < 0)
            d = -d;
        if (d > worst)
            worst = d;
        long t0 = now, t = now;
        while (y - g.pos > 1.f && t - t0 < 60) {
            t += FRAME;
            glide_step(&g, t, MAX);
        }
        if (t - t0 > reach)
            reach = t - t0;
        now = t;
    }
    printf("      lag: %.1f px behind the finger at a report, %ld ms to reach it (worst of 12)\n", worst,
           reach);
    CHECK("while dragging the list is at the finger's point at every report", worst < 0.5f && reach == 0);
    settle(400);
    CHECK("after the finger stops the list rests on the finger's point", !g.on && glide_px(&g) == 620);

    /* a finger that stops, waits and lifts does not fling */
    glide_set(&g, 500);
    drag(500, 10, 1);
    frames(now + 300, NULL);
    glide_release(&g, 600, now, 0.05f); /* a low threshold: any speed left over would fling */
    CHECK("a lift after the finger stopped is no fling", !glide_flinging(&g));
    settle(1000);
    CHECK("and the list stays where the finger left it", glide_px(&g) == 600);

    /* a lift while moving flings on, slows down and stops */
    glide_set(&g, 500);
    drag(500, 10, 1);
    frames(now + 30, NULL);
    glide_release(&g, 603, now, GLIDE_STOP_V); /* the last step: 3 px in 30 ms */
    CHECK("a lift while moving flings at its last step's speed (EP147)", glide_flinging(&g) && g.v > 0.0999f && g.v < 0.1001f);
    {
        struct glide c = g;
        glide_step(&c, now + FRAME, MAX);
        float ratio = c.v / g.v, ep = 0.9512f; /* 0.92 per 1/60 s, over one 10 ms frame */
        CHECK("a fling slows as EP147's does (x0.92 per 1/60 s)", ratio > ep - 0.002f && ratio < ep + 0.002f);
    }
    float vrel = g.v, v0 = g.v, prev = g.pos;
    int slows = 1, onward = 1;
    for (int k = 0; k < 40 && glide_flinging(&g); k++) {
        now += FRAME;
        glide_step(&g, now, MAX);
        if (g.v > v0)
            slows = 0;
        if (g.pos < prev)
            onward = 0;
        v0 = g.v;
        prev = g.pos;
    }
    CHECK("a fling only slows down and only moves on", slows && onward);
    long took = settle(5000);
    CHECK("a fling comes to a stop", !g.on && took < 5000);
    float run = (vrel - GLIDE_STOP_V) * (GLIDE_FLING_MS + FRAME); /* the decaying steps until the stop speed */
    CHECK("a fling carries the list on past the lift ((speed - stop speed) x time constant)",
          g.pos > 603 + 0.75f * run && g.pos < 603 + 1.25f * run);

    /* a step's speed is over at least 5 ms (EP147): two reports 2 ms apart */
    glide_set(&g, 500);
    glide_drag(&g, 500, now);
    glide_drag(&g, 510, now + 2);
    glide_release(&g, 520, now + 4, GLIDE_STOP_V);
    CHECK("a step's speed is over at least 5 ms", g.v > 1.999f && g.v < 2.001f);
    /* a fling stops below 60 px/s (EP147) */
    glide_set(&g, 500);
    glide_drag(&g, 500, now);
    glide_release(&g, 501, now + 10, GLIDE_STOP_V); /* 0.1 px/ms */
    long t0 = now;
    while (glide_flinging(&g) && now - t0 < 5000) {
        now += FRAME;
        glide_step(&g, now, MAX);
    }
    float stop_after = (float) (now - t0); /* 0.1 -> 0.06 px/ms at 200 ms: ~102 ms */
    CHECK("a fling ends below 60 px/s", !glide_flinging(&g) && stop_after > 80 && stop_after < 130);
    glide_set(&g, 500);
    glide_drag(&g, 500, now);
    glide_release(&g, 500.5f, now + 10, GLIDE_STOP_V); /* 0.05 px/ms: under the stop speed */
    CHECK("a lift slower than 60 px/s is no fling", !glide_flinging(&g));

    /* a slow drag below the threshold */
    glide_set(&g, 500);
    drag(500, 10, 1);
    frames(now + 30, NULL);
    glide_release(&g, 603, now, 0.5f);
    CHECK("a lift slower than the threshold is no fling", !glide_flinging(&g));

    /* the ends */
    glide_set(&g, 60);
    drag(60, 5, -1);
    frames(now + 20, NULL);
    glide_release(&g, 8, now, 0.1f);
    CHECK("a fling toward the top", glide_flinging(&g) && g.v < 0);
    settle(5000);
    CHECK("stops at the top, never past it", !g.on && g.pos == 0 && g.v == 0);
    glide_set(&g, MAX - 60);
    drag(MAX - 60, 5, 1);
    frames(now + 20, NULL);
    glide_release(&g, MAX - 8, now, 0.1f);
    settle(5000);
    CHECK("stops at the bottom, never past it", !g.on && g.pos == MAX);
    glide_set(&g, 300);
    glide_to(&g, 50, now);
    settle(2000);
    CHECK("a list that got shorter clamps the offset", glide_step(&g, now, 40.f) == 0 && glide_px(&g) == 40);

    /* a finger landing catches a fling */
    glide_set(&g, 500);
    drag(500, 10, 1);
    frames(now + 30, NULL);
    glide_release(&g, 603, now, 0.1f);
    frames(now + 100, NULL);
    float at = g.pos;
    glide_stop(&g);
    settle(1000);
    CHECK("a caught fling stays where it was shown", !glide_flinging(&g) && g.pos == at);

    /* the knob */
    glide_set(&g, 100);
    glide_to(&g, 160, now);
    float big = frames(now + 30, NULL);
    CHECK("a knob step glides (no jump to the row)", g.on && g.pos > 100 && g.pos < 160 && big < 40);
    settle(1000);
    CHECK("and lands on the row", !g.on && glide_px(&g) == 160);

    /* the clock */
    glide_set(&g, 100);
    glide_to(&g, 400, now);
    glide_step(&g, now - 500, MAX);
    CHECK("a clock that goes back moves nothing", g.pos == 100);
    glide_step(&g, now + 100000, MAX);
    CHECK("a long stall is one bounded frame, never past the goal", g.pos > 100 && g.pos <= 400);

    return ut_summary();
}
