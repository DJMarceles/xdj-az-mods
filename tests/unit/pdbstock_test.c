/* SPDX-License-Identifier: GPL-3.0-only */
/* pdb_stock.c (guest/azmod, #included whole as PST_INC by run.sh): when export.pdb is
 * re-synced after EP147's own playlist writes.
 *
 * The production loop is simulated at 50 ms steps: a library thread notes a stock write
 * (pst_note), the message tick asks whether a sync is due (pst_any_due) and, when it is,
 * the library threads each service their own stick (pst_service). sync() is counted and
 * its root recorded, so "exactly one sync per burst", "mirrors whether or not the Playlist
 * Editing mod is on" and "nothing for an unknown stick" are counts, and the two-burst case
 * proves the counter can see more than one. */
#include <stdio.h>
#include <string.h>
#include "check.h"
#include PST_INC

#define MA 0x0101u /* stick A's media id */
#define MB 0x0202u /* stick B's */

static int g_syncs;
static long long g_first_sync = -1, g_now;
static char g_root[64];
struct stick {
    unsigned media;
    const char *mount; /* 0 = the mount does not resolve */
};
static int t_mount(void *ctx, char *out, size_t n) {
    const struct stick *s = ctx;
    if (!s->mount)
        return 0;
    snprintf(out, n, "%s", s->mount);
    return 1;
}
static void t_sync(void *ctx, const char *root) {
    (void) ctx;
    g_syncs++;
    if (g_first_sync < 0)
        g_first_sync = g_now;
    snprintf(g_root, sizeof g_root, "%s", root);
}
static void reset(struct pst *p) {
    memset(p->e, 0, sizeof p->e);
    g_syncs = 0;
    g_first_sync = -1;
    g_root[0] = 0;
}
/* writes: times (ms) of stock writes on stick A; run the loop until `until`, every stick's
 * library thread servicing in the order given */
static void run(struct pst *p, const long long *writes, int nw, struct stick *sticks, int ns,
                long long until) {
    int w = 0;
    for (g_now = 0; g_now <= until; g_now += 50) {
        while (w < nw && writes[w] <= g_now)
            pst_note(p, MA, writes[w++]);
        if (pst_any_due(p, g_now))
            for (int i = 0; i < ns; i++)
                pst_service(p, sticks[i].media, g_now, t_mount, t_sync, &sticks[i]);
    }
}

int main(void) {
    ut_suite = "pdb_stock";
    static struct pst p = PST_INIT;
    struct stick a = {MA, "/media/usb/sda"}, b = {MB, "/media/usb/sdb"};
    struct stick both[2] = {a, b}, bfirst[2] = {b, a};

    /* 1. a burst: a create and its track rows land as several writes 200 ms apart */
    static const long long burst[] = {0, 200, 400, 600, 800};
    reset(&p);
    run(&p, burst, 5, both, 2, 6000);
    CHECK("a burst of 5 stock writes gives exactly one sync", g_syncs == 1);
    CHECK("the sync waits PST_DEBOUNCE_MS after the LAST write of the burst",
          g_first_sync >= 800 + PST_DEBOUNCE_MS);
    CHECK("the synced root is the written stick's own mount", !strcmp(g_root, "/media/usb/sda"));

    /* 2. two bursts far apart: two syncs (the counter can see more than one) */
    static const long long two[] = {0, 100, 5000, 5100};
    reset(&p);
    run(&p, two, 4, both, 2, 10000);
    CHECK("two separate bursts give two syncs", g_syncs == 2);

    /* 3. the other stick's library thread never takes it, whichever order they run in */
    reset(&p);
    run(&p, burst, 5, bfirst, 2, 6000);
    CHECK("stick B's thread running first still syncs only stick A, once",
          g_syncs == 1 && !strcmp(g_root, "/media/usb/sda"));
    reset(&p);
    pst_note(&p, MA, 0);
    g_now = 5000;
    CHECK("servicing stick B takes nothing of stick A's",
          pst_service(&p, MB, 5000, t_mount, t_sync, &b) == 0 && g_syncs == 0);

    /* 4. the stock sync is unconditional: it does not depend on the Playlist Editing mod.
     * pst_note takes no mod state, so this is the whole surface: a stock write mirrors
     * whichever way the mod's toggle sits. */
    reset(&p);
    CHECK("a stock write is noted (the mod's toggle does not gate it)", pst_note(&p, MA, 0) == 1);
    reset(&p);
    run(&p, burst, 5, both, 2, 6000);
    CHECK("a stock write mirrors with the Playlist Editing mod OFF (its own toggle aside)",
          g_syncs == 1 && !strcmp(g_root, "/media/usb/sda"));

    /* 5. unknown stick */
    reset(&p);
    CHECK("media 0 (the library could not be named) is not noted", pst_note(&p, 0, 0) == 0);
    CHECK("media 0: nothing falls due", !pst_any_due(&p, 5000));
    struct stick lost[2] = {{MA, 0}, b};
    reset(&p);
    run(&p, burst, 5, lost, 2, 6000);
    CHECK("the stick's mount does not resolve: no sync", g_syncs == 0);
    CHECK("...and it is dropped, not retried forever", !pst_any_due(&p, 6000));
    reset(&p);
    pst_note(&p, MA, 0);
    g_now = 2000;
    CHECK("pst_service reports the unknown stick (-1)",
          pst_service(&p, MA, 2000, t_mount, t_sync, &lost[0]) == -1 && g_syncs == 0);

    /* 6. nobody services it (the stick went away): it expires */
    reset(&p);
    pst_note(&p, MA, 0);
    CHECK("due right after the debounce", pst_any_due(&p, PST_DEBOUNCE_MS));
    CHECK("expired after PST_EXPIRE_MS unserviced",
          !pst_any_due(&p, PST_DEBOUNCE_MS + PST_EXPIRE_MS + 1));
    return ut_summary();
}
