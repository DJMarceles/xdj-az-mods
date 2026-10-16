/* SPDX-License-Identifier: GPL-3.0-only */
/* Bright Grid's tuning file (extracted from azmod/waveform.c by run.sh): brightgrid_poll_cfg
 * applies settings/azmod-brightgrid.cfg when it appears, changes or goes away, and leaves the
 * values alone while the file is unchanged. run.sh checks separately that the poll runs from
 * the overlay's 1 Hz poll and not from the rates log, which writes only with azmod-logs.on. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#define st_mtim st_mtimespec
#endif
static char g_cfg_path[512];
#define BG_CFG_PATH g_cfg_path
#include BRIGHTCFG_INC
#include "check.h"

static void put(const char *text) {
    FILE *f = fopen(g_cfg_path, "w");
    fputs(text, f);
    fclose(f);
}
static int is(int gain, int od, int gray, int mv, int rp) {
    return g_bg_gain_k == gain && g_bg_od_k == od && g_bg_gray == gray && g_bg_move_ms == mv &&
           g_bg_ramp_ms == rp;
}

int main(int argc, char **argv) {
    ut_suite = "bright grid cfg";
    if (argc < 2)
        return 2;
    snprintf(g_cfg_path, sizeof g_cfg_path, "%s/azmod-brightgrid.cfg", argv[1]);
    unlink(g_cfg_path);

    brightgrid_poll_cfg();
    CHECK("no file: the tuned defaults", is(1100, 1300, 0, 40, 60));

    put("1.5 2.0 32 50 100\n");
    brightgrid_poll_cfg();
    CHECK("a new file is applied on the next poll", is(1500, 2000, 32, 50, 100));

    g_bg_gain_k = 1;
    brightgrid_poll_cfg();
    CHECK("an unchanged file is not parsed again", g_bg_gain_k == 1);

    put("1.25 1.5 8\n");
    brightgrid_poll_cfg();
    CHECK("a changed file is applied (missing fields: their defaults)", is(1250, 1500, 8, 40, 60));

    put("9 9 500 5 9000\n");
    brightgrid_poll_cfg();
    CHECK("out-of-range values fall back to the defaults", is(1100, 1300, 0, 40, 60));

    put("1.75 1.0 16 100 200\n");
    brightgrid_poll_cfg();
    unlink(g_cfg_path);
    brightgrid_poll_cfg();
    CHECK("a removed file restores the defaults", is(1100, 1300, 0, 40, 60));
    return ut_summary();
}
