/* SPDX-License-Identifier: GPL-3.0-only */
/* The Mod Menu's question about a USB stick's SSH key (guest/azmod/ssh_approve.c), built natively
 * against stand-ins for the core's clock, deck state and yes/no dialog. A key logs in only after
 * Allow (the hook does the installing; here, Allow is what writes the approval file). Cancel and
 * BACK write nothing, and a key is asked about once per start. run.sh also builds it from mutated
 * copies of ssh_approve.c; each must fail. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"

/* ---- stand-ins for the core ---- */
static long g_now;
static long now_ms(void) { return g_now; }
static int g_loaded;
int azcm_any_loaded(void) { return g_loaded; }
static int g_asked, g_busy;
static char g_last_text[256], g_last_title[64], g_last_yes[32];
static void (*g_fn)(int, void *);
static void *g_user;
int azcm_confirm(const char *title, const char *text, const char *yes, void (*fn)(int, void *),
                 void *user) {
    if (g_busy)
        return -1;
    g_asked++;
    snprintf(g_last_title, sizeof g_last_title, "%s", title);
    snprintf(g_last_text, sizeof g_last_text, "%s", text);
    snprintf(g_last_yes, sizeof g_last_yes, "%s", yes);
    g_fn = fn, g_user = user;
    return 0;
}

#define SSHA_DIR "/tmp/az-sshapprove-test"
#include SSHA_C

static const char *FP1 = "SHA256:neIIrosesU6hYZdFGuofTdK+6slBDWGs3Iq+u6DvLbU";
static const char *FP2 = "SHA256:Zz9nb2tleTJmaW5nZXJwcmludGZvcnRoZXRlc3Q0MjQy";

static void put_pending(const char *body) {
    FILE *f = fopen(SSHA_PENDING, "w");
    fputs(body, f);
    fclose(f);
}
static int approve_says(const char *fp) {
    char b[128] = {0};
    FILE *f = fopen(SSHA_APPROVE, "r");
    if (!f)
        return 0;
    if (!fgets(b, sizeof b, f))
        b[0] = 0;
    fclose(f);
    b[strcspn(b, "\n")] = 0;
    return !strcmp(b, fp);
}
static void tick_after(long ms) {
    g_now += ms;
    ssh_approve_tick();
}
static void answer(int yes) {
    void (*fn)(int, void *) = g_fn;
    g_fn = 0;
    fn(yes, g_user);
}

int main(void) {
    ut_suite = "ssh approve";
    mkdir(SSHA_DIR, 0700);
    unlink(SSHA_PENDING);
    unlink(SSHA_APPROVE);
    g_now = 100000;

    /* nothing pending: no question */
    tick_after(2000);
    CHECK("no pending list: no question", g_asked == 0);

    char line[256];
    snprintf(line, sizeof line, "%s ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAREAMyUly6LPnwWcQ\n", FP1);
    put_pending(line);

    /* a track loaded: wait */
    g_loaded = 1;
    tick_after(2000);
    CHECK("never asks while a track is loaded", g_asked == 0);
    g_loaded = 0;
    tick_after(2000);
    CHECK("asks once no track is loaded", g_asked == 1);
    CHECK("the question names the key by its fingerprint", strstr(g_last_text, "SHA256:neIIrosesU6hYZdFGuofTd") != NULL);
    CHECK("the dialog's words", !strcmp(g_last_title, "Remote access") && !strcmp(g_last_yes, "Allow"));
    CHECK("nothing approved before an answer", access(SSHA_APPROVE, F_OK) != 0);
    tick_after(2000);
    CHECK("one question at a time", g_asked == 1);

    /* Cancel (and BACK, which the dialog reports as no) */
    answer(0);
    CHECK("Cancel / BACK approves nothing", access(SSHA_APPROVE, F_OK) != 0);
    tick_after(2000);
    tick_after(2000);
    CHECK("a canceled key is not asked again this start", g_asked == 1);

    /* a second key: asked; Allow writes exactly its fingerprint */
    snprintf(line, sizeof line, "%s ssh-ed25519 AAAA1\n%s ssh-ed25519 AAAA2\n", FP1, FP2);
    put_pending(line);
    tick_after(2000);
    CHECK("a new pending key is asked about", g_asked == 2 && strstr(g_last_text, "SHA256:Zz9nb2tleTJmaW5nZXJw"));
    answer(1);
    CHECK("Allow writes that key's fingerprint for the hook", approve_says(FP2));
    CHECK("no temp file left behind", access(SSHA_APPROVE ".new", F_OK) != 0);

    /* another question is open: ask later, not never */
    unlink(SSHA_APPROVE);
    memset(&g_ssha, 0, sizeof g_ssha);
    g_busy = 1;
    tick_after(2000);
    CHECK("dialog busy: no question shown", g_asked == 2);
    g_busy = 0;
    tick_after(2000);
    CHECK("asked once the dialog is free", g_asked == 3);
    answer(0);
    return ut_summary();
}
