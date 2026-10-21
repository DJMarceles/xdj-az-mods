/* SPDX-License-Identifier: GPL-3.0-only */
/* ov_clip (guest/azmod/ov_clip.h): a window's rectangle of the overlay buffer, cut to the buffer as
 * XPutImage cuts it, so a shared-memory put (which the server checks, and refuses when it is out of
 * range) sends exactly what a plain put drew.
 *   - against XPutImage's own steps (Xlib PutImage: a negative offset shrinks the width and moves
 *     the destination, the width is cut at the image's edge, nothing is sent when nothing is left),
 *     for every position and size around all four edges;
 *   - a window parked off screen on any side sends nothing;
 *   - every part sent lies inside the buffer (the server's own check);
 *   - sizes at the int limits do not overflow. */
#include <limits.h>
#include "check.h"
#include "ov_clip.h"

/* Xlib's XPutImage clipping, step for step (x, y: destination; xo, yo: offset in the image) */
static int xlib_clip(int xo, int yo, int x, int y, int w, int h, int W, int H, struct ov_part *p) {
    if (xo < 0) {
        w += xo;
        x -= xo;
        xo = 0;
    }
    if (yo < 0) {
        h += yo;
        y -= yo;
        yo = 0;
    }
    if (xo + w > W) w = W - xo;
    if (yo + h > H) h = H - yo;
    if (w <= 0 || h <= 0) return 0;
    p->sx = xo, p->sy = yo, p->dx = x, p->dy = y, p->w = w, p->h = h;
    return 1;
}

int main(void) {
    ut_suite = "overlay put clipping";
    const int W = 64, H = 40;
    int same = 1, inside = 1, n = 0, sent = 0;
    for (int x = -80; x <= 80 && same; x += 3)
        for (int y = -60; y <= 60 && same; y += 3)
            for (int w = -2; w <= 90 && same; w += 7)
                for (int h = -2; h <= 70 && same; h += 5) {
                    struct ov_part a = {0}, b = {0};
                    int ra = ov_clip(x, y, w, h, W, H, &a), rb = w > 0 && h > 0 && xlib_clip(x, y, 0, 0, w, h, W, H, &b);
                    n++;
                    if (ra != rb || (ra && (a.sx != b.sx || a.sy != b.sy || a.dx != b.dx || a.dy != b.dy || a.w != b.w || a.h != b.h))) {
                        printf("      differs at x=%d y=%d w=%d h=%d\n", x, y, w, h);
                        same = 0;
                    }
                    if (ra) {
                        sent++;
                        if (a.sx < 0 || a.sy < 0 || a.w <= 0 || a.h <= 0 || a.sx + a.w > W || a.sy + a.h > H) inside = 0;
                    }
                }
    CHECK("the same parts as XPutImage's own clipping, every position and size around the edges", same && n > 10000);
    CHECK("every part sent lies inside the buffer", inside && sent > 1000);
    struct ov_part p;
    CHECK("parked left of the screen: nothing sent", !ov_clip(-300, 10, 300, 20, W, H, &p));
    CHECK("parked right of the screen: nothing sent", !ov_clip(W, 10, 30, 20, W, H, &p));
    CHECK("parked above: nothing sent", !ov_clip(5, -20, 30, 20, W, H, &p));
    CHECK("parked below: nothing sent", !ov_clip(5, H + 5, 30, 20, W, H, &p));
    CHECK("no size yet (-1 x -1): nothing sent", !ov_clip(0, 0, -1, -1, W, H, &p));
    CHECK("half off the left: the visible half, at its place in the window",
          ov_clip(-10, 0, 30, 20, W, H, &p) && p.sx == 0 && p.dx == 10 && p.w == 20 && p.h == 20);
    CHECK("sizes at the int limit do not overflow",
          ov_clip(INT_MAX - 5, 0, INT_MAX, 10, W, H, &p) == 0 && ov_clip(-5, -5, INT_MAX, INT_MAX, W, H, &p) && p.w == W && p.h == H);
    return ut_summary();
}
