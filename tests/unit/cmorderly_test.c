/* SPDX-License-Identifier: GPL-3.0-only */
/* The community quarantine's orderly stop (guest/azmod/cm_orderly.h): a SIGTERM while EP147 is
 * alive clears the start count; one while it is frozen does not; the process ends by the signal,
 * as it did without the handler.
 *   - alive: the heartbeat moved within CMO_ALIVE_MS (the edge included); never seen moving, too
 *     old, or from the future (a clock that went back) is not alive;
 *   - the first count the overlay sees proves nothing (it may have stopped long ago); a change does;
 *   - in a child process: SIGTERM with a fresh heartbeat removes the count file and the child
 *     ends by SIGTERM; with a stale one, or none, the file stays and the child still ends by it;
 *   - the disposition before the handler gets the signal after it (EP147's own, were it to set one);
 *   - a thread noting the heartbeat while the signal arrives (the overlay thread and a stop).
 * Built with the sanitizers like every suite (run.sh SAN=asan / SAN=tsan). */
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "check.h"
#include "cm_orderly.h"

static char g_dir[256], g_file[300];
static void make_count(void) {
    int fd = open(g_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        if (write(fd, "1\n", 2) != 2) { /* the test only needs the file to exist */
        }
        close(fd);
    }
}
static int count_exists(void) {
    struct stat st;
    return stat(g_file, &st) == 0;
}
/* the child's heartbeat: 0 = never moved, 1 = moved just now, 2 = moved 5 s ago */
static void heartbeat(int kind) {
    long now = cmo_now_ms();
    if (kind == 1) {
        cmo_note(10, now - 20);
        cmo_note(11, now);
    } else if (kind == 2) {
        cmo_note(10, now - 6000);
        cmo_note(11, now - 5000);
    }
}
static void own_handler(int sig) { /* a disposition set before ours */
    (void) sig;
    _exit(42);
}
static volatile int g_spin = 1;
static void *noter(void *arg) {
    (void) arg;
    for (long k = 100; g_spin; k++)
        cmo_note(k, cmo_now_ms());
    return NULL;
}
/* fork, install over `prior` (NULL = the default), beat, SIGTERM self; the child's wait status */
static int run_child(int kind, void (*prior)(int), int with_thread) {
    make_count();
    pid_t p = fork();
    if (p == 0) {
        if (prior) signal(SIGTERM, prior);
        if (cmo_install(g_file) != 0) _exit(3);
        pthread_t th;
        if (with_thread) {
            if (pthread_create(&th, NULL, noter, NULL) != 0) _exit(4);
            usleep(20000);
        } else
            heartbeat(kind);
        kill(getpid(), SIGTERM);
        usleep(200000);
        _exit(0); /* not reached when the signal ends the process */
    }
    int st = 0;
    waitpid(p, &st, 0);
    return st;
}

int main(void) {
    ut_suite = "community orderly stop";
    long n = 100000;
    CHECK("alive: the heartbeat never seen moving is not alive", !cmo_alive(-1, n));
    CHECK("alive: moved 100 ms ago", cmo_alive(n - 100, n));
    CHECK("alive: moved exactly CMO_ALIVE_MS ago", cmo_alive(n - CMO_ALIVE_MS, n));
    CHECK("alive: moved CMO_ALIVE_MS + 1 ms ago is not alive", !cmo_alive(n - CMO_ALIVE_MS - 1, n));
    CHECK("alive: a heartbeat from the future (the clock went back) is not alive", !cmo_alive(n + 50, n));

    g_cmo_seen = -1;
    g_cmo_alive_ms = -1;
    cmo_note(7, n);
    CHECK("note: the first count seen proves nothing", g_cmo_alive_ms == -1);
    cmo_note(7, n + 10);
    CHECK("note: the same count again proves nothing", g_cmo_alive_ms == -1);
    cmo_note(8, n + 20);
    CHECK("note: a changed count is alive at that time", g_cmo_alive_ms == n + 20);
    g_cmo_seen = -1;
    g_cmo_alive_ms = -1;

    snprintf(g_dir, sizeof g_dir, "%s/cmorderly.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(g_dir)) {
        printf("FAIL  %s: no scratch directory\n", ut_suite);
        return 1;
    }
    snprintf(g_file, sizeof g_file, "%s/.starting", g_dir);

    int st = run_child(1, NULL, 0);
    CHECK("SIGTERM while alive: the start count is cleared", !count_exists());
    CHECK("SIGTERM while alive: the process still ends by SIGTERM", WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
    st = run_child(2, NULL, 0);
    CHECK("SIGTERM after 5 s without a heartbeat (frozen): the count stays", count_exists());
    CHECK("SIGTERM while frozen: the process still ends by SIGTERM", WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
    st = run_child(0, NULL, 0);
    CHECK("SIGTERM with no heartbeat ever seen: the count stays", count_exists());
    CHECK("SIGTERM with no heartbeat: the process still ends by SIGTERM", WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
    st = run_child(1, own_handler, 0);
    CHECK("the disposition set before the handler gets the signal after it", WIFEXITED(st) && WEXITSTATUS(st) == 42 && !count_exists());
    st = run_child(0, NULL, 1);
    CHECK("a thread noting the heartbeat while the stop comes: cleared, ended by SIGTERM",
          !count_exists() && WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);

    unlink(g_file);
    rmdir(g_dir);
    return ut_summary();
}
