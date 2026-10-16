/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * @file az-handoff-guard.c
 * @brief The way back to the deck while a mod's program runs in EP147's place (the SDK's handoff)
 *
 * handoff() stops EP147 and runs a mod's program in a transient service. If that program hangs,
 * nothing of AZ-MODS is running to bring the deck back, so the service carries this guard:
 *
 *   az-handoff-guard <unit> [hold_ms]
 *       started beside the program, inside the same service. It reads the panel input bus
 *       read-only and, when BACK is held for hold_ms (default 3000), stops the program: SIGTERM,
 *       then SIGKILL two seconds later. The service then ends and its ExecStopPost runs. When
 *       the program exits by itself the service ends and takes the guard with it. It never
 *       writes to the bus, and a frame counts only with a valid CRC (guard_logic.h).
 *
 *   az-handoff-guard --restore
 *       the service's ExecStopPost: start EP147 and check that it stays up; once more if not;
 *       and if it still is not running, reboot. A deck with no EP147 cannot play, so a reboot
 *       is the better failure.
 *
 * Plain C, no preload, started by systemd outside EP147. Shipped on the stick in MODS/deck/lib.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "guard_logic.h"

#ifndef HG_SYSTEMCTL
#define HG_SYSTEMCTL "/bin/systemctl"
#endif
#ifndef HG_BUS
#define HG_BUS "/dev/subucom_spi3.0" /* the mixer/panel input bus: knob, click, BACK */
#endif
#ifndef HG_SETTLE_S
#define HG_SETTLE_S 5 /* EP147 started: how long it must stay up before it counts as running */
#endif
#define HG_KILL_GRACE_S 2

extern char **environ;

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

/* systemctl with these arguments; its exit status, or -1 if it could not run */
static int systemctl(const char *a, const char *b, const char *c, const char *d) {
    char *argv[] = {(char *) HG_SYSTEMCTL, (char *) a, (char *) b, (char *) c, (char *) d, NULL};
    pid_t pid;
    if (posix_spawn(&pid, HG_SYSTEMCTL, NULL, NULL, argv, environ) != 0)
        return -1;
    int st;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR)
            return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int ep147_started(void) {
    if (systemctl("start", "EP147.service", NULL, NULL) != 0)
        return 0;
    sleep(HG_SETTLE_S);
    return systemctl("is-active", "--quiet", "EP147.service", NULL) == 0;
}

static int restore(void) {
    if (ep147_started())
        return 0;
    fprintf(stderr, "az-handoff-guard: EP147 did not start; trying once more\n");
    sleep(2);
    if (ep147_started())
        return 0;
    fprintf(stderr, "az-handoff-guard: EP147 still not running; rebooting the deck\n");
    systemctl("reboot", NULL, NULL, NULL);
    return 1;
}

static int guard(const char *unit, long hold_ms) {
    int fd = open(HG_BUS, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "az-handoff-guard: cannot read %s (%s): the BACK escape is off\n", HG_BUS,
                strerror(errno));
        return 1;
    }
    struct hg_state s = HG_STATE_INIT;
    uint8_t f[512];
    for (;;) {
        ssize_t n = read(fd, f, sizeof f);
        if (hg_frame(&s, f, n, now_ms(), hold_ms))
            break;
        if (n <= 0)
            usleep(10000);
    }
    close(fd);
    fprintf(stderr, "az-handoff-guard: BACK held: stopping %s\n", unit);
    char who[] = "--kill-who=main";
    systemctl("kill", who, "--signal=SIGTERM", unit);
    sleep(HG_KILL_GRACE_S);
    systemctl("kill", who, "--signal=SIGKILL", unit); /* fails harmlessly once it has exited */
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--restore"))
        return restore();
    if (argc < 2 || argc > 3 || argv[1][0] == '-') {
        fprintf(stderr, "usage: az-handoff-guard <unit> [hold_ms] | --restore\n");
        return 2;
    }
    long hold = argc == 3 ? atol(argv[2]) : 3000;
    if (hold < 500 || hold > 60000)
        hold = 3000;
    signal(SIGPIPE, SIG_IGN);
    return guard(argv[1], hold);
}
