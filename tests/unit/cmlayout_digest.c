/* SPDX-License-Identifier: GPL-3.0-only */
/* cmlayout_digest.c: the digest cmlayout_digest.h declares; cmlayout-golden.sh appends it to the
 * test copies, after their own definitions. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long cmd_fnv(const void *p, size_t n, unsigned long long h) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}

static unsigned long long cmd_digest(int nctl) {
    unsigned long long h = 1469598103934665603ull;
    h = cmd_fnv(g_px, sizeof g_px, h);
    h = cmd_fnv(g_ctl, sizeof(g_ctl[0]) * (size_t) nctl, h);
    return cmd_fnv(g_seen, g_seen_n, h);
}

static void cmd_record(void) {
    static uint8_t px[sizeof g_px];
    static char seen[sizeof g_seen];
    static cm_ctl ctl[CM_MAXCTL];
    FILE *f = fopen(getenv("CMLAYOUT_OUT"), "a");
    if (!f)
        return;
    fprintf(f, "%d %d %d %d %d %016llx\n", g_cm_view, g_cm_tab, g_cm_cat, g_cm_full, g_nctl, cmd_digest(g_nctl));
    memcpy(px, g_px, sizeof g_px);
    memcpy(seen, g_seen, sizeof g_seen);
    memcpy(ctl, g_ctl, sizeof g_ctl);
    size_t seen_n = g_seen_n;
    int nctl = g_nctl, view = g_cm_view, tab = g_cm_tab, cat = g_cm_cat, full = g_cm_full, scroll = g_cm_scroll,
        page = g_cm_page;
    for (int v = 0; v <= 5; v++)
        for (int fu = 0; fu <= 1; fu++)
            for (int tb = 0; tb <= 3; tb++)
                for (int ca = -1; ca <= 2; ca++)
                    for (int pg = -1; pg <= 1; pg++)
                        for (int sk = 0; sk <= 1; sk++) {
                            memset(g_px, 0, sizeof g_px);
                            g_seen_n = 0;
                            g_seen[0] = 0;
                            g_cm_view = v;
                            g_cm_full = fu;
                            g_cm_tab = tb;
                            g_cm_cat = ca;
                            g_cm_page = pg;
                            g_cm_scroll = sk * 120;
                            int n = cm_layout(&g_s, &g_fb, g_ctl, 1);
                            fprintf(f, "  %d %d %d %d %d %d %d %016llx\n", v, fu, tb, ca, pg, sk, n, cmd_digest(n));
                        }
    g_cm_view = view;
    g_cm_tab = tab;
    g_cm_cat = cat;
    g_cm_full = full;
    g_cm_scroll = scroll;
    g_cm_page = page;
    memcpy(g_px, px, sizeof g_px);
    memcpy(g_seen, seen, sizeof g_seen);
    memcpy(g_ctl, ctl, sizeof g_ctl);
    g_seen_n = seen_n;
    g_nctl = nctl;
    fclose(f);
}
