/* SPDX-License-Identifier: GPL-3.0-only */
/* The handoff guard (guest/azhandoff): the way back to the deck while a mod's program runs in
 * EP147's place. The BACK-hold decision is driven frame by frame through guard_logic.h; the real
 * binary, built for this host against a fake systemctl (GUARD_BIN, FAKE_LOG), is run end to end:
 * a FIFO stands in for the panel bus, and the fake records every systemctl call.
 * run.sh also builds it from mutated copies; each must fail. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include GUARD_LOGIC

static void frame(uint8_t *f, int back) {
    memset(f, 0, HG_MIXER_LEN);
    f[0] = 0x20;
    if (back)
        f[HG_BACK_BYTE] |= HG_BACK_BIT;
    uint16_t c = hg_crc16_x25(f, 96);
    f[96] = (uint8_t) (c & 0xff);
    f[97] = (uint8_t) (c >> 8);
}
/* feed `ms` of frames every 13 ms (the bus runs at ~76/s) from t0; returns when it acted, -1 never */
static long feed(struct hg_state *s, long t0, long ms, int back, long hold, const uint8_t *custom) {
    uint8_t f[HG_MIXER_LEN];
    frame(f, back);
    for (long t = t0; t < t0 + ms; t += 13)
        if (hg_frame(s, custom ? custom : f, HG_MIXER_LEN, t, hold))
            return t;
    return -1;
}

static char g_log[256], g_fake[256], g_fifo[256];
static int log_has(const char *needle) {
    FILE *f = fopen(g_log, "r");
    char b[4096] = {0};
    if (f) {
        size_t n = fread(b, 1, sizeof b - 1, f);
        b[n] = 0;
        fclose(f);
    }
    return strstr(b, needle) != NULL;
}
static int count(const char *needle) {
    FILE *f = fopen(g_log, "r");
    char line[512];
    int n = 0;
    while (f && fgets(line, sizeof line, f))
        if (strstr(line, needle))
            n++;
    if (f)
        fclose(f);
    return n;
}
/* the fake systemctl: logs its arguments; FAKE_START=n: the first n starts fail; FAKE_ACTIVE=n:
 * the first n is-active checks fail */
static void make_fake(void) {
    FILE *f = fopen(g_fake, "w");
    fprintf(f, "#!/bin/sh\necho \"$*\" >> %s\nc=%s.count\n"
               "case \"$1\" in\n"
               " start) n=$(grep -c '^start' %s); [ \"$n\" -le \"${FAKE_START:-0}\" ] && exit 1; exit 0;;\n"
               " is-active) n=$(grep -c '^is-active' %s); [ \"$n\" -le \"${FAKE_ACTIVE:-0}\" ] && exit 3; exit 0;;\n"
               "esac\nexit 0\n",
            g_log, g_log, g_log, g_log);
    fclose(f);
    chmod(g_fake, 0755);
}
static int restore_with(const char *start_fails, const char *active_fails) {
    unlink(g_log);
    setenv("FAKE_START", start_fails, 1);
    setenv("FAKE_ACTIVE", active_fails, 1);
    pid_t p = fork();
    if (p == 0) {
        execl(GUARD_BIN, GUARD_BIN, "--restore", (char *) NULL);
        _exit(127);
    }
    int st;
    waitpid(p, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
/* the guard on the FIFO, fed BACK for back_ms (then plain frames); 1 = it exited on its own */
static int guard_run(long back_ms, long hold_ms) {
    unlink(g_log);
    unlink(g_fifo);
    mkfifo(g_fifo, 0600);
    char hold[16];
    snprintf(hold, sizeof hold, "%ld", hold_ms);
    pid_t p = fork();
    if (p == 0) {
        execl(GUARD_BIN, GUARD_BIN, "azmod-handoff", hold, (char *) NULL);
        _exit(127);
    }
    int w = open(g_fifo, O_WRONLY);
    uint8_t on[HG_MIXER_LEN], off[HG_MIXER_LEN];
    frame(on, 1);
    frame(off, 0);
    int exited = 0, st;
    /* BACK for back_ms of wall time, measured, not counted in 13 ms steps: on a loaded machine
     * the steps stretch, and a short BACK counted in steps could outlast the hold. */
    struct timespec t0, tn;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long t = 0; t < back_ms + 4000;) {
        if (write(w, t < back_ms ? on : off, HG_MIXER_LEN) < 0) {
        }
        usleep(13000);
        clock_gettime(CLOCK_MONOTONIC, &tn);
        t = (tn.tv_sec - t0.tv_sec) * 1000L + (tn.tv_nsec - t0.tv_nsec) / 1000000L;
        if (waitpid(p, &st, WNOHANG) == p) {
            exited = 1;
            break;
        }
    }
    close(w);
    if (!exited) {
        kill(p, SIGKILL);
        waitpid(p, &st, 0);
    }
    return exited;
}

int main(void) {
    ut_suite = "handoff guard";
    signal(SIGPIPE, SIG_IGN); /* the guard closes the bus FIFO when it acts */
    struct hg_state s = HG_STATE_INIT, s0 = HG_STATE_INIT;
    CHECK("BACK held 3 s: acts at 3 s, not before", feed(&s, 0, 4000, 1, 3000, NULL) >= 3000 &&
                                                        feed(&s0, 0, 2990, 1, 3000, NULL) < 0);
    struct hg_state s1 = HG_STATE_INIT;
    CHECK("BACK held 1 s: nothing", feed(&s1, 0, 1000, 1, 3000, NULL) < 0 && feed(&s1, 1000, 3000, 0, 3000, NULL) < 0);
    struct hg_state s2 = HG_STATE_INIT;
    feed(&s2, 0, 2000, 1, 3000, NULL);
    feed(&s2, 2000, 100, 0, 3000, NULL);
    CHECK("a release in between starts the count again", feed(&s2, 2100, 2800, 1, 3000, NULL) < 0);
    struct hg_state s3 = HG_STATE_INIT;
    feed(&s3, 0, 2000, 1, 3000, NULL);
    CHECK("a gap in the stream lets the hold go", feed(&s3, 2600, 2900, 1, 3000, NULL) < 0);
    uint8_t ff[HG_MIXER_LEN];
    memset(ff, 0xff, sizeof ff);
    struct hg_state s4 = HG_STATE_INIT;
    CHECK("all-0xFF frames (a bus error) never count as BACK", feed(&s4, 0, 5000, 1, 3000, ff) < 0);
    uint8_t bad[HG_MIXER_LEN];
    frame(bad, 1);
    bad[10] ^= 1; /* a flipped bit: the CRC no longer matches */
    struct hg_state s5 = HG_STATE_INIT;
    CHECK("a frame with a bad CRC never counts", feed(&s5, 0, 5000, 1, 3000, bad) < 0);
    struct hg_state s6 = HG_STATE_INIT;
    uint8_t f6[HG_MIXER_LEN];
    frame(f6, 1);
    CHECK("a short read never counts", !hg_frame(&s6, f6, 97, 0, 0) && !hg_frame(&s6, f6, 97, 5000, 0));

    snprintf(g_log, sizeof g_log, "%s/.hg-test.%d.log", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid());
    snprintf(g_fifo, sizeof g_fifo, "%s", GUARD_FIFO); /* the bus path the guard was built with */
    snprintf(g_fake, sizeof g_fake, "%s", FAKE_SYSTEMCTL);
    make_fake();

    int rc = restore_with("0", "0");
    CHECK("restore: EP147 starts and stays up: no retry, no reboot",
          rc == 0 && count("start EP147.service") == 1 && !log_has("reboot"));
    rc = restore_with("1", "0");
    CHECK("restore: the first start fails: one more try, no reboot",
          rc == 0 && count("start EP147.service") == 2 && !log_has("reboot"));
    rc = restore_with("0", "1");
    CHECK("restore: started but not running after the settle: one more try",
          rc == 0 && count("start EP147.service") == 2 && !log_has("reboot"));
    rc = restore_with("2", "0");
    CHECK("restore: both starts fail: the deck reboots", rc == 1 && count("start EP147.service") == 2 &&
                                                             log_has("reboot"));

    CHECK("guard: BACK held past the hold stops the program (TERM, then KILL)",
          guard_run(1200, 600) && log_has("kill --kill-who=main --signal=SIGTERM azmod-handoff") &&
              log_has("kill --kill-who=main --signal=SIGKILL azmod-handoff"));
    CHECK("guard: a short BACK does nothing", !guard_run(300, 600) && !log_has("kill"));
    unlink(g_log);
    unlink(g_fifo);
    return ut_summary();
}
