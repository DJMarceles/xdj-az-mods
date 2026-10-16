/* SPDX-License-Identifier: GPL-3.0-only */
/* Two sticks synced at once (pdb_sync.c, #included whole as PDBS_INC): each stick has its own
 * library thread, so an edit on one can run while a paste writes the other. Two threads sync
 * two different generated sticks (pdbfix.h) over and over; every result must equal what a
 * sync of that stick alone writes. Built with -fsanitize=thread by run.sh, where a shared work
 * buffer used by both at once is reported as a data race. Control: the lock removed. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include PDBS_INC
#include "pdbfix.h"

static char g_tmp[600];
static const struct fx_trk TA[] = {{"/Contents/a1.mp3", 0, 0}, {"/Contents/a2.mp3", 0, 0}};
static const struct fx_trk TB[] = {{"/Contents/b1.mp3", 0, 0}, {"/Contents/b2.mp3", 0, 0}, {"/Contents/b3.mp3", 0, 0}};
struct job {
    char root[700];
    const struct fx_trk *t;
    int nt, rounds, bad;
    unsigned char *want;
    size_t wn;
};
static unsigned char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long L = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(L > 0 ? (size_t) L : 1);
    *n = fread(b, 1, (size_t) L, f);
    fclose(f);
    return b;
}
static int one(struct job *j) { /* reset the stick, sync it; 0 = synced */
    struct fx f = {j->t, j->nt, 0, 0};
    if (fx_write(j->root, &f))
        return -1;
    struct pdbs_pl pl[40];
    char nm[40][32];
    struct pdbs_item it[40];
    struct pdbs_track tr[4];
    for (int i = 0; i < 40; i++) {
        snprintf(nm[i], sizeof nm[i], "%s list %d", j->t[0].path + 10, i);
        pl[i] = (struct pdbs_pl) {(unsigned) i + 1, 0, (unsigned) i, 0, nm[i]};
        it[i] = (struct pdbs_item) {(unsigned) i + 1, j->t[i % j->nt].path};
    }
    for (int i = 0; i < j->nt; i++)
        tr[i] = (struct pdbs_track) {j->t[i].path, 0, 0};
    return pdbs_sync(j->root, 0, pl, 40, it, 40, tr, j->nt, 0) == 0 ? 0 : -1;
}
static void *run(void *a) {
    struct job *j = a;
    char p[800];
    snprintf(p, sizeof p, "%s/PIONEER/rekordbox/export.pdb", j->root);
    for (int r = 0; r < j->rounds; r++) {
        size_t n = 0;
        unsigned char *b = one(j) ? 0 : slurp(p, &n);
        if (!b || n != j->wn || memcmp(b, j->want, n))
            j->bad++;
        free(b);
    }
    return 0;
}
int main(int argc, char **argv) {
    ut_suite = "export.pdb sync: two sticks at once";
    const char *t = getenv("TMPDIR");
    snprintf(g_tmp, sizeof g_tmp, "%s/pdbthr.XXXXXX", t && t[0] ? t : "/tmp");
    if (!mkdtemp(g_tmp))
        return 2;
    int rounds = argc > 1 ? atoi(argv[1]) : 20;
    struct job ja = {"", TA, 2, rounds, 0, 0, 0}, jb = {"", TB, 3, rounds, 0, 0, 0};
    snprintf(ja.root, sizeof ja.root, "%s/a", g_tmp);
    snprintf(jb.root, sizeof jb.root, "%s/b", g_tmp);
    char p[800];
    struct job *js[2] = {&ja, &jb};
    for (int i = 0; i < 2; i++) { /* each stick alone: the expected file */
        snprintf(p, sizeof p, "%s/PIONEER/rekordbox/export.pdb", js[i]->root);
        if (one(js[i]) || !(js[i]->want = slurp(p, &js[i]->wn)))
            return 2;
    }
    pthread_t a, b;
    pthread_create(&a, 0, run, &ja);
    pthread_create(&b, 0, run, &jb);
    pthread_join(a, 0);
    pthread_join(b, 0);
    CHECK("each stick's export.pdb is what syncing it alone writes, every round", !ja.bad && !jb.bad);
    free(ja.want);
    free(jb.want);
    fx_rmrf(g_tmp);
    return ut_summary();
}
