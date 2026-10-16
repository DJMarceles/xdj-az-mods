/* SPDX-License-Identifier: GPL-3.0-only */
/* System Info > Save Logs to USB, the overlay's side (savelogs_write_req and savelogs_parse,
 * extracted out of guest/azmod/savelogs.c by run.sh):
 *   - a press hands the watcher ONE request file, "save <id>", through a temp file and a
 *     rename: when the temp file cannot be made, no request appears (never written in place);
 *   - the status line answers THIS press only: a result the file holds for an earlier request
 *     id leaves the screen waiting, never shows as this press's result;
 *   - a request nobody answers ends in a message after SAVELOGS_WAIT_MS, not a busy button
 *     forever;
 *   - with no press this run, the screen shows the last final result of this boot, and nothing
 *     for a run that is still going.
 * argv[1] = a scratch directory. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"

#include SAVELOGS_INC

static char g_dir[512], g_st[600];
static int files(char *buf, size_t n) { /* the files in g_dir; buf = the request's text */
    DIR *d = opendir(g_dir);
    struct dirent *de;
    int k = 0;
    buf[0] = 0;
    while (d && (de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        k++;
        if (!strcmp(de->d_name, "az-savelogs.req")) {
            char p[800];
            snprintf(p, sizeof p, "%s/%s", g_dir, de->d_name);
            FILE *f = fopen(p, "r");
            if (f) {
                size_t r = fread(buf, 1, n - 1, f);
                buf[r] = 0;
                fclose(f);
            }
        }
    }
    if (d)
        closedir(d);
    return k;
}
static void status(const char *text) {
    FILE *f = fopen(g_st, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

int main(int argc, char **argv) {
    ut_suite = "save logs request";
    if (argc < 2)
        return 2;
    mkdir(argv[1], 0755);
    snprintf(g_dir, sizeof g_dir, "%s/run", argv[1]);
    mkdir(g_dir, 0755);
    snprintf(g_st, sizeof g_st, "%s/status", argv[1]);
    char got[256], line[128];
    int tone = -1;

    CHECK("a request is written: 0", savelogs_write_req(g_dir, 1234567ULL) == 0);
    CHECK("one file, the request, no temp file left", files(got, sizeof got) == 1 && got[0]);
    CHECK("its text is \"save <id>\" and one line", !strcmp(got, "save 1234567\n"));
    unlink(strcat(strcpy(got, g_dir), "/az-savelogs.req"));
    {
        char t[600];
        snprintf(t, sizeof t, "%s/.az-savelogs.req.tmp", g_dir);
        mkdir(t, 0755); /* the temp file cannot be created */
        CHECK("the temp file cannot be made: -1", savelogs_write_req(g_dir, 7ULL) == -1);
        CHECK("... and no request appears (never written in place)", files(got, sizeof got) == 1 && !got[0]);
        rmdir(t);
    }
    CHECK("a run directory that does not exist: -1, nothing written",
          savelogs_write_req("/nonexistent-savelogs-dir", 8ULL) == -1);

    /* the status line */
    status("state=saved\nreq=111\nmsg=Saved to MODS/logs/20261014-101500\n");
    CHECK("this press answered: not busy", savelogs_parse(g_st, 111, 5000, line, sizeof line, &tone) == 0);
    CHECK("... the script's line, tone saved", !strcmp(line, "Saved to MODS/logs/20261014-101500") && tone == 1);
    CHECK("an EARLIER request's result: still busy, waiting for this one",
          savelogs_parse(g_st, 222, 5000, line, sizeof line, &tone) == 1);
    CHECK("... shown as waiting, never as this press's result",
          !strcmp(line, "Saving logs...") && tone == 0);
    CHECK("no answer within the wait: not busy any more",
          savelogs_parse(g_st, 222, SAVELOGS_WAIT_MS, line, sizeof line, &tone) == 0);
    CHECK("... it says so, as a failure", !strncmp(line, "No answer", 9) && tone == 3);
    CHECK("no press this run: the last result of this boot",
          savelogs_parse(g_st, 0, 0, line, sizeof line, &tone) == 0 && tone == 1 && !strncmp(line, "Saved to", 8));
    status("state=working\nreq=333\nmsg=Saving logs...\n");
    CHECK("this press, still running: busy",
          savelogs_parse(g_st, 333, 1000, line, sizeof line, &tone) == 1 && tone == 0);
    CHECK("this press running past the wait: still busy (it answered)",
          savelogs_parse(g_st, 333, SAVELOGS_WAIT_MS * 2, line, sizeof line, &tone) == 1);
    CHECK("no press this run, a run still going: nothing shown",
          savelogs_parse(g_st, 0, 0, line, sizeof line, &tone) == 0 && !line[0] && tone == 0);
    status("state=refused\nreq=444\nmsg=Eject every track first.\n");
    CHECK("a refusal: not busy, tone refused, its reason",
          savelogs_parse(g_st, 444, 1000, line, sizeof line, &tone) == 0 && tone == 2 &&
              !strcmp(line, "Eject every track first."));
    status("state=error\nreq=555\nmsg=Could not write the USB.\n");
    CHECK("a failure: tone failed", savelogs_parse(g_st, 555, 1000, line, sizeof line, &tone) == 0 && tone == 3);
    unlink(g_st);
    CHECK("no status file yet: busy while within the wait",
          savelogs_parse(g_st, 666, 1000, line, sizeof line, &tone) == 1);
    CHECK("no status file, no press: nothing shown",
          savelogs_parse(g_st, 0, 0, line, sizeof line, &tone) == 0 && !line[0]);
    return ut_summary();
}
