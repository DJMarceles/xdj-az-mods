/* SPDX-License-Identifier: GPL-3.0-only */
/* sanplant_test.c: the sanitizer passes' own controls (run.sh, SAN=asan / SAN=tsan).
 *
 *   sanplant clean      no bug: must exit 0 under every sanitizer
 *   sanplant heap N     writes one byte past an N-byte heap block (AddressSanitizer)
 *   sanplant signed N   adds N to INT_MAX in int arithmetic (UndefinedBehaviorSanitizer)
 *   sanplant race N     two threads increment one plain int N times each (ThreadSanitizer)
 *
 * The sizes and counts come from the command line so the compiler cannot fold the bug away.
 * run.sh builds this with the same compiler and flags as the suites, and requires the
 * sanitizer's own report for each planted bug: a pass whose instrument cannot report these
 * proves nothing about the suites it ran. */
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_shared;
static long g_rounds;

static void *racer(void *arg) {
    for (long i = 0; i < g_rounds; i++)
        g_shared++;
    return arg;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s clean | heap N | signed N | race N\n", argv[0]);
        return 2;
    }
    long n = argc > 2 ? strtol(argv[2], NULL, 10) : 1;
    if (!strcmp(argv[1], "clean")) {
        char *p = malloc(16);
        if (!p)
            return 2;
        memset(p, 1, 16);
        int s = p[15];
        free(p);
        printf("PASS  sanplant: clean run (%d)\n", s);
        return 0;
    }
    if (!strcmp(argv[1], "heap")) {
        volatile char *p = malloc((size_t) n);
        if (!p)
            return 2;
        p[n] = 1; /* one past the end */
        free((void *) p);
        printf("FAIL  sanplant: a heap overflow went unreported\n");
        return 0;
    }
    if (!strcmp(argv[1], "signed")) {
        volatile int a = INT_MAX;
        int b = a + (int) n; /* signed overflow for n > 0 */
        printf("FAIL  sanplant: a signed overflow went unreported (%d)\n", b);
        return 0;
    }
    if (!strcmp(argv[1], "race")) {
        pthread_t t;
        g_rounds = n;
        if (pthread_create(&t, NULL, racer, NULL) != 0)
            return 2;
        racer(NULL);
        pthread_join(t, NULL);
        printf("FAIL  sanplant: a data race went unreported (%d)\n", g_shared);
        return 0;
    }
    fprintf(stderr, "unknown case %s\n", argv[1]);
    return 2;
}
