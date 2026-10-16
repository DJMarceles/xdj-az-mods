/* SPDX-License-Identifier: GPL-3.0-only */
/* rs_write_req (extracted out of guest/azmod/remote_stems.c by run.sh): one Remote Stems request,
 * <stick>/MODS/azstems-queue/<ms><seq>-<content id>.req, read by the helper (azrs.sh) as
 * key=value lines, the first of each key taken. The title and the path come from the library on
 * the stick, so:
 *   - a request is "file=<path>" then "title=<title>", written through a .tmp and renamed;
 *   - a title holding a line break cannot add a line of its own (job=, srv=, up= are the
 *     helper's own keys: a second line would hand it a job or a server the user never chose);
 *   - a path holding a line break, or too long to be written whole, is not queued: a cut or
 *     split file= line names another file;
 *   - a track already queued (in the list handed over, or in a request file) is not queued twice;
 *   - a queue folder that cannot be written: -71.
 * argv[1] = a scratch directory. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"

static long long drag_now_ms(void) { return 1700000000000LL; }
#include RSREQ_INC

static char g_q[512];
/* the one request file in the queue: its text into buf; the number of .req files */
static int queue(char *buf, size_t n) {
    DIR *d = opendir(g_q);
    struct dirent *de;
    int k = 0;
    buf[0] = 0;
    while (d && (de = readdir(d))) {
        size_t l = strlen(de->d_name);
        if (l > 4 && !strcmp(de->d_name + l - 4, ".req")) {
            char p[800];
            snprintf(p, sizeof p, "%s/%s", g_q, de->d_name);
            FILE *f = fopen(p, "r");
            if (f) {
                size_t r = fread(buf, 1, n - 1, f);
                buf[r] = 0;
                fclose(f);
            }
            k++;
        } else if (l > 4 && !strcmp(de->d_name + l - 4, ".tmp"))
            k += 100;
    }
    if (d)
        closedir(d);
    return k;
}
static void empty(void) {
    DIR *d = opendir(g_q);
    struct dirent *de;
    while (d && (de = readdir(d))) {
        char p[800];
        snprintf(p, sizeof p, "%s/%s", g_q, de->d_name);
        if (de->d_name[0] != '.')
            unlink(p);
    }
    if (d)
        closedir(d);
}
/* lines of the request that start with key= */
static int lines_with(const char *buf, const char *key) {
    int n = 0;
    size_t k = strlen(key);
    for (const char *l = buf; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL)
        n += !strncmp(l, key, k);
    return n;
}
static int nlines(const char *buf) {
    int n = 0;
    for (; *buf; buf++)
        n += *buf == '\n';
    return n;
}

int main(int argc, char **argv) {
    ut_suite = "remote stems request";
    if (argc < 2)
        return 2;
    mkdir(argv[1], 0755);
    snprintf(g_q, sizeof g_q, "%s/azstems-queue", argv[1]);
    mkdir(g_q, 0755);
    char got[2048];

    CHECK("a request is written: 0", rs_write_req(g_q, 7, "/Contents/a.mp3", "Song A", NULL) == 0);
    CHECK("one request file, no .tmp left", queue(got, sizeof got) == 1);
    CHECK("file= first, then title=", !strcmp(got, "file=/Contents/a.mp3\ntitle=Song A\n"));
    CHECK("the same track again (found in the queue folder): -73, not queued twice",
          rs_write_req(g_q, 7, "/Contents/a.mp3", "Song A", NULL) == -73 && queue(got, sizeof got) == 1);
    CHECK("the same track again (in the list handed over): -73",
          rs_write_req(g_q, 7, "/Contents/a.mp3", "Song A", "file=/Contents/a.mp3\n") == -73);
    empty();
    CHECK("a request without a title has the file= line only",
          rs_write_req(g_q, 8, "/Contents/b.mp3", "", NULL) == 0 && queue(got, sizeof got) == 1 &&
              !strcmp(got, "file=/Contents/b.mp3\n"));
    empty();

    /* the library's text cannot add lines */
    int rc = rs_write_req(g_q, 9, "/Contents/c.mp3", "Song\njob=job-of-someone-else\nsrv=evil\rup=1", NULL);
    queue(got, sizeof got);
    CHECK("a title holding line breaks is queued", rc == 0);
    CHECK("... as two lines, file= and title=", nlines(got) == 2 && lines_with(got, "file=") == 1 &&
                                                    lines_with(got, "title=") == 1);
    CHECK("... with no job=, srv= or up= line of its own",
          !lines_with(got, "job=") && !lines_with(got, "srv=") && !lines_with(got, "up="));
    CHECK("... the breaks written as spaces", strstr(got, "title=Song job=job-of-someone-else srv=evil up=1\n") != NULL);
    empty();
    CHECK("a path holding a line break is not queued",
          rs_write_req(g_q, 10, "/Contents/d.mp3\njob=x", "D", NULL) < 0 && queue(got, sizeof got) == 0);
    char longp[700];
    memset(longp, 'p', sizeof longp - 1);
    longp[0] = '/';
    longp[sizeof longp - 1] = 0;
    CHECK("a path too long to be written whole is not queued (a cut file= names another file)",
          rs_write_req(g_q, 11, longp, "E", NULL) < 0 && queue(got, sizeof got) == 0);
    longp[600] = 0;
    CHECK("a long path that fits is queued whole (CONTROL for the length)",
          rs_write_req(g_q, 12, longp, "E", NULL) == 0 && queue(got, sizeof got) == 1 &&
              !strncmp(got + 5, longp, 600) && got[605] == '\n');
    empty();

    char noq[600];
    snprintf(noq, sizeof noq, "%s/missing", argv[1]);
    CHECK("a queue folder that cannot be written: -71", rs_write_req(noq, 13, "/Contents/f.mp3", "F", NULL) == -71);
    return ut_summary();
}
