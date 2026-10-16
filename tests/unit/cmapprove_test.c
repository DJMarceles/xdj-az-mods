/* SPDX-License-Identifier: GPL-3.0-only */
/* The approved list, the deck's own record of the community mods the user allowed
 * (<settings>/azmod-community/approved: "<id> <sha256 of its .so>" per line), and the files
 * written beside it (cm_write_line, cm_approve, cm_approved_read, cm_sha256_file, extracted from
 * guest/azmod/community.c by run.sh). Only a mod listed with its exact bytes runs, so:
 *   - Allow writes the line under the lock the fetcher takes too, one line per id (the newest),
 *     at most 64 lines (the oldest dropped), replaced whole (no .new left);
 *   - a lock held by the other writer, a temporary file that cannot be written, or a rename
 *     that fails: -1, the list on disk unchanged, and the list in memory is what is on disk;
 *   - malformed lines are never matched;
 *   - the sha256 of a library: the right digest, refused for a folder, a missing file or one
 *     over the 16 MB cap.
 * CM_TEST_ROOT (set by run.sh) = a scratch directory standing in for the settings partition. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "azsha256.h"

#define CM_STATE CM_TEST_ROOT "/azmod-community"
#define CM_APPROVED CM_STATE "/approved"
#define CM_APPROVED_LOCK CM_STATE "/.approved.lock"
static const char *g_state = CM_STATE, *g_approved = CM_APPROVED, *g_lock = CM_APPROVED_LOCK;
#define CM_APPROVED_MAX 64
#define CM_SO_CAP (16 * 1024 * 1024)
#include CMAPPROVE_INC

static char *slurp(const char *p) {
    static char b[1 << 16];
    FILE *f = fopen(p, "r");
    if (!f)
        return NULL;
    size_t n = fread(b, 1, sizeof b - 1, f);
    b[n] = 0;
    fclose(f);
    return b;
}
static const char *SHA_A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char *SHA_B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

int main(void) {
    ut_suite = "approved list";
    const char *root = CM_TEST_ROOT;
    mkdir(root, 0755);
    char nw[500];
    snprintf(nw, sizeof nw, "%s.new", g_approved);

    CHECK("Allow: written (0)", cm_approve("mod-a", SHA_A) == 0);
    char want[200];
    snprintf(want, sizeof want, "mod-a %s\n", SHA_A);
    CHECK("... one line, <id> <sha256>", slurp(g_approved) && !strcmp(slurp(g_approved), want));
    CHECK("... and no .new left", access(nw, F_OK) != 0);
    CHECK("... approved with its bytes, not with others'", cm_is_approved("mod-a", SHA_A) && !cm_is_approved("mod-a", SHA_B) &&
                                                               !cm_is_approved("mod-b", SHA_A));
    CHECK("Allow again with new bytes: one line per id, the newest", cm_approve("mod-a", SHA_B) == 0 && cm_is_approved("mod-a", SHA_B) &&
                                                                         !cm_is_approved("mod-a", SHA_A));
    CHECK("refused: a bad id or a short or upper-case sha256", cm_approve("Mod A", SHA_A) == -1 &&
                                                                   cm_approve("mod-c", "abc") == -1 &&
                                                                   cm_approve("mod-c", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") == -1);

    /* the other writer holds the lock */
    char before[1 << 12];
    snprintf(before, sizeof before, "%s", slurp(g_approved));
    int lk = open(g_lock, O_RDWR | O_CREAT, 0644);
    int held = lk >= 0 && flock(lk, LOCK_EX | LOCK_NB) == 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int rc = cm_approve("mod-c", SHA_A);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double s = (double) (t1.tv_sec - t0.tv_sec) + (double) (t1.tv_nsec - t0.tv_nsec) / 1e9;
    CHECK("the lock held by the other writer: -1 after waiting about a second, the list unchanged",
          held && rc == -1 && s > 0.5 && s < 5.0 && !strcmp(slurp(g_approved), before) && !cm_is_approved("mod-c", SHA_A));
    flock(lk, LOCK_UN);
    close(lk);
    CHECK("... and once it is free, written (CONTROL)", cm_approve("mod-c", SHA_A) == 0 && cm_is_approved("mod-c", SHA_A));

    /* the temporary file cannot be written, the rename fails */
    snprintf(before, sizeof before, "%s", slurp(g_approved));
    mkdir(nw, 0755);
    rc = cm_approve("mod-d", SHA_A);
    CHECK("a .new that cannot be written: -1, the list unchanged, memory = disk",
          rc == -1 && !strcmp(slurp(g_approved), before) && !cm_is_approved("mod-d", SHA_A) && cm_is_approved("mod-c", SHA_A));
    rmdir(nw);
    char real[500];
    snprintf(real, sizeof real, "%s.kept", g_approved);
    rename(g_approved, real);
    mkdir(g_approved, 0755);
    char inner[600];
    snprintf(inner, sizeof inner, "%s/x", g_approved);
    fclose(fopen(inner, "w"));
    rc = cm_approve("mod-d", SHA_A);
    CHECK("a rename that fails: -1, no .new left behind", rc == -1 && access(nw, F_OK) != 0);
    unlink(inner);
    rmdir(g_approved);
    rename(real, g_approved);

    /* the cap: the oldest dropped */
    for (int i = 0; i < 70; i++) {
        char id[16];
        snprintf(id, sizeof id, "cap-%02d", i);
        cm_approve(id, SHA_A);
    }
    int lines = 0;
    for (const char *p = slurp(g_approved); *p; p++)
        lines += *p == '\n';
    CHECK("70 approvals: 64 lines kept, the newest", lines == 64 && cm_is_approved("cap-69", SHA_A) && !cm_is_approved("cap-05", SHA_A) &&
                                                         !cm_is_approved("mod-a", SHA_B));

    /* malformed lines */
    FILE *f = fopen(g_approved, "w");
    fprintf(f, "good-one %s\nbad id %s\nshort abc\ntrailing %s extra\nUPPER %s\n\n", SHA_A, SHA_A, SHA_A, SHA_A);
    fclose(f);
    cm_approved_read();
    CHECK("malformed lines are never matched, a good one beside them is",
          cm_is_approved("good-one", SHA_A) && !cm_is_approved("trailing", SHA_A) && !cm_is_approved("UPPER", SHA_A) &&
              !cm_is_approved("short", "abc") && g_cm_nok == 1);

    /* one-line files */
    char one[500];
    snprintf(one, sizeof one, "%s/owner", g_state);
    CHECK("a one-line file: written whole", cm_write_line(one, "https://example.org/repo") == 0 &&
                                                !strcmp(slurp(one), "https://example.org/repo\n"));
    char onenew[520];
    snprintf(onenew, sizeof onenew, "%s.new", one);
    mkdir(onenew, 0755);
    CHECK("a one-line file whose .new cannot be written: -1, the old one kept",
          cm_write_line(one, "other") == -1 && !strcmp(slurp(one), "https://example.org/repo\n"));
    rmdir(onenew);

    /* the library's sha256 */
    char lib[500], out[65];
    snprintf(lib, sizeof lib, "%s/lib.so", root);
    f = fopen(lib, "w");
    fputs("abc", f);
    fclose(f);
    CHECK("sha256 of a file", cm_sha256_file(lib, out) == 0 &&
                                  !strcmp(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK("sha256: a folder, or a missing file: -1", cm_sha256_file(g_state, out) == -1 && !out[0] &&
                                                         cm_sha256_file("/nonexistent/x.so", out) == -1);
    truncate(lib, CM_SO_CAP + 1);
    CHECK("sha256: a file over the cap: -1", cm_sha256_file(lib, out) == -1);
    return ut_summary();
}
