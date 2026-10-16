/* SPDX-License-Identifier: GPL-3.0-only */
/* ls_tick and ls_source_hint (extracted from azmod/library_edits.c by run.sh): the
 * library spool service on the message thread.
 *
 * A cross-stick copy writes its rows as a spool and waits up to 30 s for the target stick's
 * library thread to apply it. When it stops waiting (-58), the spool is still on the stick,
 * and the deck promises "finishes on the other USB in a moment": so the service must stay
 * armed, at its low rate, until that spool is applied, with no developer flag and no copy
 * in flight. It must then disarm and go back to one access() per 2 s.
 *
 * ls_source_hint: the spool's first line names the source stick so the export.pdb sync can
 * still copy its rows later. The file sits on a stick anyone can write, so only a plain
 * "/media/usb/<name>" is taken.
 *
 * The real "/media/usb" and the flag path are redirected into a scratch directory (argv[1]);
 * opendir() calls are counted to prove the rate limit. */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"

static const char *g_root; /* scratch dir standing in for / */
static int g_opendirs;     /* opendir calls on the spool paths */
static long long g_now;    /* drag_now_ms() */
static int g_kicks;        /* plrenew_kick() calls */

static const char *redirect(const char *p, char *buf, size_t n) {
    snprintf(buf, n, "%s%s", g_root, p);
    return buf;
}
static DIR *t_opendir(const char *p) {
    char b[600];
    g_opendirs++;
    return opendir(redirect(p, b, sizeof b));
}
static int t_access(const char *p, int m) {
    char b[600];
    return access(redirect(p, b, sizeof b), m);
}
#define opendir t_opendir
#define access t_access

enum {
    CP_IDLE,
    CP_COPYING,
    CP_DONE
};
static struct {
    int state;
} g_cp;
static int g_ls_armed; /* _Atomic in azmod; plain here: clang refuses __atomic_* on _Atomic */
static int g_ls_leftover = 1;
static long long drag_now_ms(void) { return g_now; }
static int plrenew_kick(void) {
    g_kicks++;
    return 0;
}

#include TICK_INC
#include HINT_INC

#undef opendir
#undef access

static void put(const char *rel, const char *body) {
    char p[600];
    snprintf(p, sizeof p, "%s%s", g_root, rel);
    FILE *f = fopen(p, "w");
    if (f) {
        fputs(body, f);
        fclose(f);
    }
}
static void del(const char *rel) {
    char p[600];
    snprintf(p, sizeof p, "%s%s", g_root, rel);
    unlink(p);
}
static void mk(const char *rel) {
    char p[600];
    snprintf(p, sizeof p, "%s%s", g_root, rel);
    mkdir(p, 0755);
}
/* one tick after the gate has opened again */
static void tick_later(long long ms) {
    g_now += ms;
    ls_tick();
}

int main(int argc, char **argv) {
    ut_suite = "ls_tick";
    if (argc < 2)
        return 2;
    g_root = argv[1];
    mk("/media");
    mk("/media/usb");
    mk("/media/usb/sda");
    mk("/media/usb/sda/PIONEER");
    mk("/media/usb/sda/PIONEER/azspool");
    mk("/media/usb/sdb");
    mk("/media/usb/sdb/PIONEER");
    mk("/media/usb/sdb/PIONEER/azspool");
    mk("/home");
    mk("/home/root");
    mk("/home/root/settings");
    g_now = 100000;

    /* 1. EP147 start: a spool an earlier session left is found and serviced (no flag) */
    put("/media/usb/sdb/PIONEER/azspool/azcp-0001-5-1.sql", "-- x\n");
    ls_tick();
    CHECK("start: a leftover spool arms the service without the flag", g_ls_armed == 1);
    CHECK("start: the library threads are woken", g_kicks == 1);

    /* 2. low rate: ticks inside the 2 s gate scan nothing */
    int o = g_opendirs;
    for (int i = 0; i < 50; i++) {
        g_now += 30;
        ls_tick();
    }
    CHECK("leftover: no scan inside the 2 s gate (50 ticks over 1.5 s)", g_opendirs == o);
    tick_later(600);
    CHECK("leftover: rescans once the gate opens, still armed", g_opendirs > o && g_ls_armed == 1);

    /* 3. the library thread applied it (deleted): disarm and forget the leftover */
    del("/media/usb/sdb/PIONEER/azspool/azcp-0001-5-1.sql");
    tick_later(2000);
    CHECK("applied: disarmed", g_ls_armed == 0);
    CHECK("applied: the leftover is cleared", g_ls_leftover == 0);
    o = g_opendirs;
    tick_later(2000);
    CHECK("idle: no directory scan without a copy or a leftover", g_opendirs == o);

    /* 4. a spool with nothing waiting for it and no leftover does not arm the service */
    put("/media/usb/sda/PIONEER/azspool/dl-1.sql", "-- y\n");
    tick_later(2000);
    CHECK("no copy, no leftover: a spool alone does not arm", g_ls_armed == 0);
    del("/media/usb/sda/PIONEER/azspool/dl-1.sql");
    tick_later(2000);

    /* 5. a copy in flight: 250 ms rate */
    g_cp.state = CP_COPYING;
    put("/media/usb/sda/PIONEER/azspool/azcp-0002-7-2.sql", "-- z\n");
    tick_later(2000);
    CHECK("copying: armed", g_ls_armed == 1);
    o = g_opendirs;
    tick_later(260);
    CHECK("copying: rescans after 250 ms", g_opendirs > o);

    /* 6. the copy stops waiting (-58: the worker sets the leftover, then CP_DONE) */
    g_ls_leftover = 1;
    g_cp.state = CP_DONE;
    tick_later(2000);
    CHECK("after -58: still armed with the copy over", g_ls_armed == 1);
    tick_later(2000);
    tick_later(2000);
    CHECK("after -58: stays armed until the spool is applied", g_ls_armed == 1 && g_ls_leftover == 1);
    del("/media/usb/sda/PIONEER/azspool/azcp-0002-7-2.sql");
    tick_later(2000);
    CHECK("after -58: disarms once applied", g_ls_armed == 0 && g_ls_leftover == 0);

    /* 7. a leftover on a stick that was pulled: nothing left to find, so it is dropped */
    g_ls_leftover = 1;
    tick_later(2000);
    CHECK("pulled stick: leftover dropped, disarmed", g_ls_leftover == 0 && g_ls_armed == 0);

    /* ls_source_hint */
    char h[96];
    ls_source_hint("-- AZMOD cross-stick copy from /media/usb/sdb\nINSERT INTO x;\n", h, sizeof h);
    CHECK("hint: the source stick", !strcmp(h, "/media/usb/sdb"));
    ls_source_hint("-- AZMOD cross-stick copy from /media/usb/sdb\r\n", h, sizeof h);
    CHECK("hint: CRLF line", !strcmp(h, "/media/usb/sdb"));
    ls_source_hint("-- AZMOD cross-stick copy from /media/usb/../../etc\n", h, sizeof h);
    CHECK("hint: a path that climbs out is refused", h[0] == 0);
    ls_source_hint("-- AZMOD cross-stick copy from /media/usb/sdb/x\n", h, sizeof h);
    CHECK("hint: a nested path is refused", h[0] == 0);
    ls_source_hint("-- AZMOD cross-stick copy from /etc/passwd\n", h, sizeof h);
    CHECK("hint: outside /media/usb is refused", h[0] == 0);
    ls_source_hint("-- AZMOD cross-stick copy\nINSERT INTO x;\n", h, sizeof h);
    CHECK("hint: a spool that names no source", h[0] == 0);
    ls_source_hint("-- AZMOD cross-stick copy from /media/usb/\n", h, sizeof h);
    CHECK("hint: an empty name is refused", h[0] == 0);
    return ut_summary();
}
