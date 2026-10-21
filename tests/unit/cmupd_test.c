/* SPDX-License-Identifier: GPL-3.0-only */
/* Community Mods > Updates, Update all (guest/azmod/cm_updall.h):
 *   - the first install is sent at once; the next only after the status shows `done` for that
 *     install and the catalog was read again; never while the fetcher is busy;
 *   - an `error` stops the run, the ids after it are not tried;
 *   - an id that stopped qualifying is skipped;
 *   - a `done` for a different request ends the run; so does a fetcher that never answers;
 *   - cancel (a reload) ends it with no result;
 *   - the button's text.
 * CMUPD_HDR = the header under test. */
#include "check.h"
#include <stdio.h>
#include <string.h>
#include CMUPD_HDR

static int g_ok[3] = {1, 1, 1};
static const char *const IDS[3] = {"beat-counter", "echo-out", "big-clock"};
static int ok(const char *id, void *u) {
    (void) u;
    for (int i = 0; i < 3; i++)
        if (!strcmp(id, IDS[i])) return g_ok[i];
    return 0;
}

static cmu_in g_in;
static unsigned g_sgen, g_cgen;
static long g_now;
static void status(const char *st, const char *op, const char *id) {
    g_in.state = st;
    g_in.op = op;
    g_in.id = id;
    g_in.sgen = ++g_sgen;
}
static void catalog(void) { g_in.cgen = ++g_cgen; }
static int step(cmu_run *r) {
    g_in.now = g_now;
    return cmu_step(r, &g_in, ok, 0);
}
static void reset(cmu_run *r) {
    memset(&g_in, 0, sizeof g_in);
    g_in.state = g_in.op = g_in.id = "";
    g_sgen = g_cgen = 0;
    g_now = 1000;
    for (int i = 0; i < 3; i++) g_ok[i] = 1;
    cmu_start(r, IDS, 3);
}
/* the fetcher serving the install for the id just sent: busy, then the catalog, then done */
static void serve(const char *id) {
    status("downloading", "install", id);
    status("installing", "install", id);
    catalog();
    status("done", "install", id);
}

int main(void) {
    ut_suite = "community updates";
    static cmu_run r;
    char t[64];

    reset(&r);
    cmu_label(&r, 3, t, sizeof t);
    CHECK("a run starts on the ids given, the first one shown", r.on && r.n == 3 && !strcmp(t, "Updating 1 of 3..."));
    CHECK("the first install is sent at once", step(&r) == CMU_SEND && !strcmp(cmu_cur(&r), "beat-counter"));
    CHECK("nothing more while the fetcher has not answered", step(&r) == CMU_WAIT && step(&r) == CMU_WAIT);
    status("downloading", "install", "beat-counter");
    CHECK("nothing more while it downloads", step(&r) == CMU_WAIT);
    status("installing", "install", "beat-counter");
    CHECK("nothing more while it installs", step(&r) == CMU_WAIT);
    g_in.cgen = g_cgen; /* unchanged: the catalog not read again */
    status("done", "install", "beat-counter");
    CHECK("done, but the catalog not read again: still waiting", step(&r) == CMU_WAIT);
    catalog();
    CHECK("done and the catalog read again: the next install", step(&r) == CMU_SEND && !strcmp(cmu_cur(&r), "echo-out"));
    cmu_label(&r, 2, t, sizeof t);
    CHECK("the button: \"Updating 2 of 3...\"", !strcmp(t, "Updating 2 of 3..."));
    serve("echo-out");
    CHECK("the third", step(&r) == CMU_SEND && !strcmp(cmu_cur(&r), "big-clock"));
    serve("big-clock");
    CHECK("all three: the run ends FINISHED, once", step(&r) == CMU_FINISHED && !r.on && r.result == CMU_FINISHED && step(&r) == CMU_OFF);
    cmu_label(&r, 0, t, sizeof t);
    CHECK("the button after it: \"Update all (0)\"", !strcmp(t, "Update all (0)"));

    /* a stale done from before the run is not taken for the one sent */
    reset(&r);
    status("done", "install", "beat-counter");
    catalog();
    CHECK("CONTROL: a done already showing at the start: the first install is still sent", step(&r) == CMU_SEND);
    CHECK("... and the same done is not taken for it", step(&r) == CMU_WAIT && !strcmp(cmu_cur(&r), "beat-counter"));
    catalog();
    CHECK("... not even after the catalog is read again", step(&r) == CMU_WAIT && !strcmp(cmu_cur(&r), "beat-counter"));

    /* never sent while busy */
    reset(&r);
    status("refreshing", "refresh", "");
    CHECK("the fetcher busy at the start: nothing sent", step(&r) == CMU_WAIT && !r.sent);
    status("done", "refresh", "");
    CHECK("it finished: the first install is sent", step(&r) == CMU_SEND);

    /* an error stops the run */
    reset(&r);
    step(&r);
    serve("beat-counter");
    step(&r);
    status("downloading", "install", "echo-out");
    status("error", "install", "echo-out");
    CHECK("an error stops the run: FAILED", step(&r) == CMU_FAILED && !r.on && r.result == CMU_FAILED);
    CHECK("the rest are not tried", step(&r) == CMU_OFF && r.pos == 1);

    /* a mod that stopped qualifying is skipped */
    reset(&r);
    step(&r);
    g_ok[1] = 0;
    serve("beat-counter");
    CHECK("an id that no longer qualifies is skipped", step(&r) == CMU_SEND && !strcmp(cmu_cur(&r), "big-clock"));
    cmu_label(&r, 1, t, sizeof t);
    CHECK("the count keeps its place: \"Updating 3 of 3...\"", !strcmp(t, "Updating 3 of 3..."));
    reset(&r);
    g_ok[0] = 0;
    CHECK("the first one skipped too", step(&r) == CMU_SEND && !strcmp(cmu_cur(&r), "echo-out"));
    reset(&r);
    g_ok[0] = g_ok[1] = g_ok[2] = 0;
    CHECK("none qualifying: FINISHED with nothing sent", step(&r) == CMU_FINISHED && !r.sent);

    /* the install done and the id no longer offered: the catalog was read in the same pass */
    reset(&r);
    step(&r);
    g_ok[0] = 0;
    status("done", "install", "beat-counter");
    CHECK("done and the id no longer qualifies: the next, even with no new catalog count", step(&r) == CMU_SEND && !strcmp(cmu_cur(&r), "echo-out"));

    /* a different request served */
    reset(&r);
    step(&r);
    catalog();
    status("done", "refresh", "");
    CHECK("a done for another request ends the run: STOPPED", step(&r) == CMU_STOPPED && !r.on);
    reset(&r);
    step(&r);
    catalog();
    status("done", "install", "echo-out");
    CHECK("a done for another id ends it too", step(&r) == CMU_STOPPED);

    /* no answer */
    reset(&r);
    step(&r);
    g_now += CMU_QUIET_MS;
    CHECK("CONTROL: just under the limit: still waiting", step(&r) == CMU_WAIT);
    g_now += 1;
    CHECK("no answer for CMU_QUIET_MS: QUIET", step(&r) == CMU_QUIET && !r.on);
    reset(&r);
    step(&r);
    g_now += CMU_QUIET_MS / 2;
    status("downloading", "install", "beat-counter");
    g_now += CMU_QUIET_MS / 2 + 10;
    CHECK("a status change restarts the wait", step(&r) == CMU_WAIT);

    /* cancel */
    reset(&r);
    step(&r);
    cmu_cancel(&r);
    CHECK("cancel ends it with no result", !r.on && r.result == CMU_OFF && step(&r) == CMU_OFF);

    /* the button's count */
    memset(&r, 0, sizeof r);
    cmu_label(&r, 2, t, sizeof t);
    CHECK("no run: \"Update all (2)\"", !strcmp(t, "Update all (2)"));
    {
        const char *bad[3] = {"", "a-mod-id-that-is-much-longer-than-32", "ok-id"};
        cmu_start(&r, bad, 3);
        CHECK("an empty or too long id is not taken", r.n == 1 && !strcmp(r.ids[0], "ok-id"));
        cmu_start(&r, bad, 0);
        CHECK("no ids: no run", !r.on);
    }
    return ut_summary();
}
