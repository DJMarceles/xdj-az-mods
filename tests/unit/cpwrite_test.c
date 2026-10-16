/* SPDX-License-Identifier: GPL-3.0-only */
/* cp_spool_write (extracted out of guest/azmod/library_edits.c by run.sh): the cross-stick copy
 * puts its spool on the target stick, which may be full or pulled while it writes. The applier
 * takes any file of whole lines, so a spool cut at a line end would apply the rows before the
 * cut. The spool must land whole or not at all:
 *   - written whole: the file holds exactly the text, and no .tmp is left;
 *   - a write that stops short (here: a file size limit, as a full stick does): -1, and
 *     neither the spool nor its .tmp is left;
 *   - a folder that cannot be written: -1.
 * argv[1] = a scratch directory. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"

#include CPWRITE_INC

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *b = malloc(1 << 20);
    *n = fread(b, 1, (1 << 20) - 1, f);
    b[*n] = 0;
    fclose(f);
    return b;
}

int main(int argc, char **argv) {
    ut_suite = "spool write";
    if (argc < 2)
        return 2;
    mkdir(argv[1], 0755);
    char path[600], tmp[620];
    snprintf(path, sizeof path, "%s/azcp-0001-1-1.sql", argv[1]);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    /* a spool well over the limit used below, in whole lines */
    size_t big = 64 * 1024;
    char *sql = malloc(big + 1);
    for (size_t i = 0; i < big; i++)
        sql[i] = (i % 64 == 63) ? '\n' : 'x';
    sql[big] = 0;

    size_t n = 0;
    CHECK("written whole: 0", cp_spool_write(tmp, path, sql) == 0);
    char *got = slurp(path, &n);
    CHECK("the spool holds exactly the text", got && n == big && !memcmp(got, sql, big));
    CHECK("no .tmp is left", access(tmp, F_OK) != 0);
    free(got);
    unlink(path);

    /* a full stick: writes past 4 KB fail (EFBIG, the signal ignored) */
    signal(SIGXFSZ, SIG_IGN);
    struct rlimit old, lim;
    getrlimit(RLIMIT_FSIZE, &old);
    lim = old;
    lim.rlim_cur = 4096;
    setrlimit(RLIMIT_FSIZE, &lim);
    int rc = cp_spool_write(tmp, path, sql);
    setrlimit(RLIMIT_FSIZE, &old);
    CHECK("a write that stops short: -1", rc == -1);
    CHECK("... and no spool is left to be applied cut", access(path, F_OK) != 0);
    CHECK("... nor its .tmp", access(tmp, F_OK) != 0);

    char nodir[700], nodtmp[720];
    snprintf(nodir, sizeof nodir, "%s/missing/azcp.sql", argv[1]);
    snprintf(nodtmp, sizeof nodtmp, "%s.tmp", nodir);
    CHECK("a folder that cannot be written: -1", cp_spool_write(nodtmp, nodir, "x\n") == -1);
    free(sql);
    return ut_summary();
}
