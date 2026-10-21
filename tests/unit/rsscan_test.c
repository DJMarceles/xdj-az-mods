/* SPDX-License-Identifier: GPL-3.0-only */
/* The Remote Stems queue scan (extracted out of guest/azmod/remote_stems.c by run.sh): finding a
 * queued request reads directories on a USB stick, which can block for as long as the stick is
 * busy, so the stem-marks worker does it and the message thread only takes the answer.
 *   - rs_queue_has_req: 1 only when <root>/<stick>/MODS/azstems-queue holds a *.req file; a
 *     missing root, a stick without the folder, other file names, and hidden entries do not count;
 *   - rs_scan_publish / rs_work_take: the answer crosses threads as one atomic flag; taking it
 *     clears it, so one queued request starts the helper once per scan, not once per tick;
 *   - rs_scan_loop: the scan thread's body. It needs nothing but its own flags: no other mod's
 *     worker is involved, so a request is found even where Stem Marks never installed;
 *     it scans only while the feature is on and ends when told to stop;
 *   - threaded: a worker publishes over and over while the "message thread" takes. Built with
 *     -fsanitize=thread by run.sh, a plain flag shows up there as a data race.
 * argv[1] = a scratch directory. */
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"

#include <time.h>
static long long drag_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#include RSSCAN_INC

static char g_root[512];
static void path(char *out, size_t n, const char *rel) { snprintf(out, n, "%s/%s", g_root, rel); }
static void mk(const char *rel) {
    char p[800];
    path(p, sizeof p, rel);
    mkdir(p, 0755);
}
static void touch(const char *rel) {
    char p[800];
    path(p, sizeof p, rel);
    FILE *f = fopen(p, "w");
    if (f)
        fclose(f);
}
static void rm(const char *rel) {
    char p[800];
    path(p, sizeof p, rel);
    unlink(p);
}

static int g_stop; /* __atomic only */
static void *worker(void *a) {
    (void) a;
    while (!__atomic_load_n(&g_stop, __ATOMIC_ACQUIRE))
        rs_scan_publish(g_root);
    return 0;
}

static int g_loop_stop, g_loop_on; /* __atomic only */
static void *loop_thread(void *a) {
    (void) a;
    rs_scan_loop(g_root, &g_loop_stop, &g_loop_on, 20);
    return 0;
}

int main(int argc, char **argv) {
    ut_suite = "rs scan";
    if (argc < 2)
        return 2;
    snprintf(g_root, sizeof g_root, "%s/media.%d", argv[1], (int) getpid());
    mkdir(argv[1], 0755);
    mkdir(g_root, 0755);

    CHECK("a root that does not exist has no work", rs_queue_has_req("/nonexistent/az-rs") == 0);
    CHECK("an empty root has no work", rs_queue_has_req(g_root) == 0);
    mk("sda");
    CHECK("a stick without a queue folder has no work", rs_queue_has_req(g_root) == 0);
    mk("sda/MODS");
    mk("sda/MODS/azstems-queue");
    touch("sda/MODS/azstems-queue/1-5.tmp");
    touch("sda/MODS/azstems-queue/notes.txt");
    CHECK("other file names are not requests", rs_queue_has_req(g_root) == 0);
    mk(".hidden");
    mk(".hidden/MODS");
    mk(".hidden/MODS/azstems-queue");
    touch(".hidden/MODS/azstems-queue/1-5.req");
    CHECK("a hidden entry is not a stick", rs_queue_has_req(g_root) == 0);
    touch("sda/MODS/azstems-queue/1-5.req");
    CHECK("a .req file in a stick's queue is work", rs_queue_has_req(g_root) == 1);
    rm("sda/MODS/azstems-queue/1-5.req");
    mk("sdb");
    mk("sdb/MODS");
    mk("sdb/MODS/azstems-queue");
    touch("sdb/MODS/azstems-queue/9-1.req");
    CHECK("the second stick's request is found", rs_queue_has_req(g_root) == 1);
    rm("sdb/MODS/azstems-queue/9-1.req");
    CHECK("no request left: no work", rs_queue_has_req(g_root) == 0);

    /* the hand-over */
    touch("sdb/MODS/azstems-queue/9-2.req");
    CHECK("nothing published yet: nothing to take", rs_work_take() == 0);
    rs_scan_publish(g_root);
    CHECK("a published request is taken once", rs_work_take() == 1);
    CHECK("taking clears it", rs_work_take() == 0);
    rs_scan_publish(g_root);
    rm("sdb/MODS/azstems-queue/9-2.req");
    rs_scan_publish(g_root); /* the request is gone: the next scan overwrites the answer */
    CHECK("a later scan replaces an untaken answer", rs_work_take() == 0);

    /* two threads: the worker scans in a loop while this thread takes */
    touch("sdb/MODS/azstems-queue/9-3.req");
    pthread_t th;
    __atomic_store_n(&g_stop, 0, __ATOMIC_RELEASE);
    pthread_create(&th, 0, worker, 0);
    int got = 0;
    for (int i = 0; i < 2000000 && !got; i++)
        got = rs_work_take();
    CHECK("the message thread sees a request the worker found", got == 1);
    rm("sdb/MODS/azstems-queue/9-3.req");
    int stray = 0;
    for (int i = 0; i < 200; i++) { /* let any scan that began before the removal land */
        usleep(1000);
        rs_work_take();
    }
    for (int i = 0; i < 200000; i++)
        stray += rs_work_take();
    CHECK("with the request gone the worker publishes nothing", stray == 0);
    __atomic_store_n(&g_stop, 1, __ATOMIC_RELEASE);
    pthread_join(th, 0);

    /* the scan thread's loop, on its own (nothing else running) */
    touch("sdb/MODS/azstems-queue/9-4.req");
    rs_work_take();
    __atomic_store_n(&g_loop_stop, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_loop_on, 0, __ATOMIC_RELEASE);
    pthread_create(&th, 0, loop_thread, 0);
    usleep(400000);
    CHECK("the feature off: the loop scans nothing", rs_work_take() == 0);
    __atomic_store_n(&g_loop_on, 1, __ATOMIC_RELEASE);
    got = 0;
    for (int i = 0; i < 300 && !got; i++) {
        usleep(10000);
        got = rs_work_take();
    }
    CHECK("the feature on: the loop alone finds the request", got == 1);
    __atomic_store_n(&g_loop_stop, 1, __ATOMIC_RELEASE);
    pthread_join(th, 0);
    rm("sdb/MODS/azstems-queue/9-4.req");
    return ut_summary();
}
