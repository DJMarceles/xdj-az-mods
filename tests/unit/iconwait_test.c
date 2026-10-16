/* SPDX-License-Identifier: GPL-3.0-only */
/* azcm_icon (community_icons.c): what a Community tile draws while its icon is decoded. A queued
 * decode draws nothing (no placeholder that the picture then replaces); a refused icon, a decode
 * waiting longer than CMI_WAIT_MS, or no decode hook at all draws the placeholder; a decoded one
 * is returned. The code under test is extracted from community_icons.c by run.sh (ICON_INC). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"

static long g_now = 5000;
static int g_tick = 1;
static long now_ms(void) { return g_now; }
static int install_msgtick_hook(const char *who) {
    (void) who;
    return g_tick;
}
#include ICON_INC

static int slot_of(const char *path) {
    for (int i = 0; i < CMI_N; i++)
        if (g_cmi[i].st != CMI_FREE && !strcmp(g_cmi[i].path, path))
            return i;
    return -1;
}

int main(int argc, char **argv) {
    ut_suite = "icon wait";
    static uint32_t px[16 * 16];
    int pending = -1;
    if (argc > 1 && !strcmp(argv[1], "nohook")) { /* the hook is looked up once per process */
        g_tick = 0; /* the message-tick hook could not be installed: no decode will ever run */
        CHECK("with no decode hook, the placeholder at once",
              azcm_icon("/i/d.png", 16, &pending) == NULL && pending == 0);
        return ut_summary();
    }

    CHECK("the first request returns no pixels", azcm_icon("/i/a.png", 16, &pending) == NULL);
    CHECK("the first request is pending: the tile draws nothing", pending == 1);
    int a = slot_of("/i/a.png");
    CHECK("the request is queued for the message thread", a >= 0 && g_cmi[a].st == CMI_WANTED);

    g_now += CMI_WAIT_MS - 1;
    azcm_icon("/i/a.png", 16, &pending);
    CHECK("still pending just inside the wait", pending == 1);
    g_now += 1;
    azcm_icon("/i/a.png", 16, &pending);
    CHECK("a decode waiting CMI_WAIT_MS shows the placeholder", pending == 0);

    unsigned done = azcm_icons_done();
    g_cmi[a].px = px;
    g_cmi_ok++;
    g_cmi[a].st = CMI_READY;
    CHECK("a decoded icon is returned", azcm_icon("/i/a.png", 16, &pending) == px && pending == 0);
    CHECK("a finished icon moves the redraw counter", azcm_icons_done() == done + 1);
    g_cmi[a].px = NULL;

    azcm_icon("/i/b.png", 16, &pending);
    int b = slot_of("/i/b.png");
    g_cmi_bad++;
    g_cmi[b].st = CMI_FAILED;
    CHECK("a refused icon shows the placeholder", azcm_icon("/i/b.png", 16, &pending) == NULL && pending == 0);
    CHECK("a refused icon moves the redraw counter too", azcm_icons_done() == done + 2);

    CHECK("no path: the placeholder", azcm_icon("", 16, &pending) == NULL && pending == 0);
    CHECK("a size out of range: the placeholder", azcm_icon("/i/c.png", 4, &pending) == NULL && pending == 0);
    return ut_summary();
}
