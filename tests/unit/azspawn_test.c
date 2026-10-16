/* SPDX-License-Identifier: GPL-3.0-only */
/* azspawn.h (guest/common): helpers start without a shell and without fork().
 *   - argv reaches the program as given (no shell parsing, no quoting);
 *   - AZSPAWN_NO_PRELOAD gives the child LD_PRELOAD= (empty), whatever the parent had;
 *   - AZSPAWN_QUIET sends the child's stdout and stderr to /dev/null;
 *   - azspawn_reap returns -1 while the child runs and its wait status after;
 *   - a program that does not exist is an error, not a silent success.
 * argv[1] = scratch directory. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "check.h"
#include AZSPAWN_INC

static int run(char *const argv[], int flags) { /* wait status, or -1 */
    pid_t p = azspawn(argv, flags);
    if (p < 0)
        return -1;
    int st = 0;
    while (waitpid(p, &st, 0) < 0) {
    }
    return st;
}
static int exited(int st, int code) { return st >= 0 && WIFEXITED(st) && WEXITSTATUS(st) == code; }

int main(int argc, char **argv) {
    ut_suite = "azspawn";
    const char *dir = argc > 1 ? argv[1] : ".";
    char out[512];
    snprintf(out, sizeof out, "%s/stdout", dir);

    char *const lit[] = {"sh", "-c", "[ \"$1\" = 'a b;$(x)' ]", "sh", "a b;$(x)", NULL};
    CHECK("argv passed verbatim (spaces, ; and $() not interpreted)", exited(run(lit, 0), 0));

    setenv("LD_PRELOAD", "/nonexistent/azmod.so", 1);
    char *const pre[] = {"sh", "-c", "[ \"${LD_PRELOAD-unset}\" = '' ]", NULL};
    char *const keep[] = {"sh", "-c", "[ \"$LD_PRELOAD\" = /nonexistent/azmod.so ]", NULL};
    CHECK("NO_PRELOAD: the child sees LD_PRELOAD= (empty)", exited(run(pre, AZSPAWN_NO_PRELOAD), 0));
    CHECK("without NO_PRELOAD the environment is inherited as is", exited(run(keep, 0), 0));
    unsetenv("LD_PRELOAD");

    fflush(stdout);
    int saved = dup(1);
    FILE *f = freopen(out, "w", stdout);
    char *const say[] = {"sh", "-c", "echo to-stdout; echo to-stderr >&2", NULL};
    int st = f ? run(say, AZSPAWN_QUIET) : -1;
    fflush(stdout);
    dup2(saved, 1);
    close(saved);
    struct stat sb;
    CHECK("QUIET: nothing reaches the parent's stdout", exited(st, 0) && stat(out, &sb) == 0 && sb.st_size == 0);

    char *const slow[] = {"sh", "-c", "sleep 1; exit 7", NULL};
    pid_t p = azspawn(slow, 0);
    int early = p > 0 ? azspawn_reap(p) : 0;
    int late = -1;
    for (int i = 0; i < 3000 && late < 0; i++) { /* ends at the exit; 30 s caps a child that hangs */
        usleep(10000);
        late = azspawn_reap(p);
    }
    CHECK("reap: -1 while running, then the exit status", p > 0 && early == -1 && exited(late, 7));

    char *const none[] = {"/nonexistent/az-helper", NULL};
    CHECK("a missing program is an error", azspawn(none, 0) < 0 || !exited(run(none, 0), 0));
    return ut_summary();
}
