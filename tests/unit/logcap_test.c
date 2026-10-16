/* SPDX-License-Identifier: GPL-3.0-only */
/* /tmp size caps (guest/common/azlogcap.h, guest/azmod/crash_guards.c cr_put):
 *   - azlog_rotate moves a file larger than max to <path>.1 (replacing an older .1) and
 *     leaves a file at or under max alone; azlog_fopen_capped appends after rotating;
 *   - a run of appends through azlog_fopen_capped never leaves more than max + one line
 *     in the live file, and the newest line is always in it;
 *   - the crash reporter's cr_put never grows the log past CR_LOG_MAX, and says once
 *     that it stopped.
 * argv[1] = scratch directory. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include LOGCAP_INC

static int g_cr_fd = -1;
#include CRPUT_INC

static long fsize(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 ? (long) st.st_size : -1;
}
static void fill(const char *p, long n) {
    FILE *f = fopen(p, "w");
    for (long i = 0; f && i < n; i++)
        fputc('x', f);
    if (f)
        fclose(f);
}

int main(int argc, char **argv) {
    ut_suite = "logcap";
    const char *dir = argc > 1 ? argv[1] : ".";
    char a[512], a1[512];
    snprintf(a, sizeof a, "%s/t.log", dir);
    snprintf(a1, sizeof a1, "%s/t.log.1", dir);
    unlink(a);
    unlink(a1);

    fill(a, 1000);
    CHECK("at the cap: not rotated", azlog_rotate(a, 1000) == 0 && fsize(a) == 1000 && fsize(a1) < 0);
    fill(a, 1001);
    CHECK("over the cap: moved to .1", azlog_rotate(a, 1000) == 1 && fsize(a) < 0 && fsize(a1) == 1001);
    fill(a, 2000);
    CHECK("an older .1 is replaced", azlog_rotate(a, 1000) == 1 && fsize(a1) == 2000);
    CHECK("missing file: nothing to do", azlog_rotate(a, 1000) == 0);

    /* steady appends: the live file stays near the cap, the newest line is in it */
    unlink(a);
    unlink(a1);
    long worst = 0;
    for (int i = 0; i < 5000; i++) {
        FILE *f = azlog_fopen_capped(a, 4096);
        if (!f)
            break;
        fprintf(f, "line %05d of a diagnostic that never stops\n", i);
        fclose(f);
        if (fsize(a) > worst)
            worst = fsize(a);
    }
    char last[128] = "";
    FILE *f = fopen(a, "r");
    char line[128];
    while (f && fgets(line, sizeof line, f))
        snprintf(last, sizeof last, "%s", line);
    if (f)
        fclose(f);
    CHECK("5000 appends: live file never past cap + one line", worst > 0 && worst <= 4096 + 64);
    CHECK("5000 appends: the .1 holds at most cap + one line", fsize(a1) > 0 && fsize(a1) <= 4096 + 64);
    CHECK("5000 appends: the newest line survived", strstr(last, "line 04999") != NULL);

    /* the crash reporter */
    snprintf(a, sizeof a, "%s/crash.log", dir);
    unlink(a);
    g_cr_fd = open(a, O_WRONLY | O_CREAT | O_APPEND, 0644);
    g_cr_bytes = 0;
    for (int i = 0; i < 20000; i++)
        cr_put("\n=== azmod crash: signal 11 at 1700000000 fault 0x0000000000000000 pc 0x00000000014ab0d4\n");
    close(g_cr_fd);
    long cs = fsize(a);
    int markers = 0;
    f = fopen(a, "r");
    while (f && fgets(line, sizeof line, f))
        if (strstr(line, "crash log full"))
            markers++;
    if (f)
        fclose(f);
    CHECK("crash log: 20000 records never pass CR_LOG_MAX", cs > 0 && cs <= CR_LOG_MAX);
    CHECK("crash log: says once that it is full", markers == 1);
    return ut_summary();
}
