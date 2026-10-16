/* SPDX-License-Identifier: GPL-3.0-only */
/* azaltstack.h test (Linux; the unit tests run it in the aarch64 build container):
 *   1. a thread started with az_thread_create that overflows its stack still runs an
 *      SA_ONSTACK SIGSEGV handler;
 *   2. the same overflow on a thread from plain pthread_create gets no handler (the control:
 *      shows the probe can report the other answer);
 *   3. the main thread after azalt_this_thread(0) is handled too;
 *   4. 2000 started-and-joined threads leave no alternate stacks mapped behind them.
 * Build: cc -O0 -pthread [-I<dir with a mutated azaltstack.h>] -I<guest>/common azaltstack_test.c */
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <azaltstack.h> /* from the -I path: a control puts a mutated copy first */

static void on_segv(int sig, siginfo_t *si, void *uc) {
    (void) sig;
    (void) si;
    (void) uc;
    _exit(42); /* the handler ran: it had a stack */
}
static void arm(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_segv;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK; /* exactly the crash reporter's flags */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, 0);
}
static long recurse(long n) {
    volatile char pad[1024];
    pad[0] = (char) n;
    return recurse(n + 1) + pad[0]; /* not a tail call: every level keeps its frame */
}
static void *overflow(void *a) {
    (void) a;
    return (void *) recurse(0);
}
/* run fn in a child: 42 = handled, a signal number (negative) = killed without a handler */
static int in_child(void (*fn)(void)) {
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        arm();
        fn();
        _exit(0);
    }
    int st = 0;
    waitpid(p, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -WTERMSIG(st);
}
static void case_ours(void) {
    pthread_t t;
    if (az_thread_create(&t, 0, overflow, 0) == 0)
        pthread_join(t, 0);
}
static void case_raw(void) {
    pthread_t t;
    if (pthread_create(&t, 0, overflow, 0) == 0)
        pthread_join(t, 0);
}
static void case_main(void) {
    azalt_this_thread(0);
    recurse(0);
}
static void *noop(void *a) { return a; }
static int maps_lines(void) {
    FILE *f = fopen("/proc/self/maps", "r");
    int n = 0, c;
    while (f && (c = fgetc(f)) != EOF)
        n += c == '\n';
    if (f)
        fclose(f);
    return n;
}
static int pass, fail;
static void check(const char *what, int ok) {
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) pass++;
    else fail++;
}
int main(void) {
    int r = in_child(case_ours);
    char m[160];
    snprintf(m, sizeof m, "az_thread_create: a stack overflow runs the SA_ONSTACK handler (child: %d)", r);
    check(m, r == 42);
    r = in_child(case_raw);
    snprintf(m, sizeof m, "control in the same run: plain pthread_create, the overflow gets no handler (child: %d)", r);
    check(m, r == -SIGSEGV);
    r = in_child(case_main);
    snprintf(m, sizeof m, "main thread after azalt_this_thread(0): handled (child: %d)", r);
    check(m, r == 42);
    for (int i = 0; i < 50; i++) { /* warm up: the allocator's own arenas and thread stacks */
        pthread_t t;
        az_thread_create(&t, 0, noop, 0);
        pthread_join(t, 0);
    }
    int before = maps_lines();
    for (int i = 0; i < 2000; i++) {
        pthread_t t;
        if (az_thread_create(&t, 0, noop, 0) == 0)
            pthread_join(t, 0);
    }
    int after = maps_lines();
    snprintf(m, sizeof m, "2000 threads started and joined: maps lines %d -> %d (alternate stacks freed)", before, after);
    check(m, after - before < 10);
    printf("passed %d failed %d\n", pass, fail);
    return fail != 0;
}
