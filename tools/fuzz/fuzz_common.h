/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared by the fuzz harnesses (tools/fuzz/<name>_fuzz.c).
 *
 * Each harness defines LLVMFuzzerTestOneInput. Built with -fsanitize=fuzzer, libFuzzer
 * supplies main(). Built with -DFZ_REPLAY (any compiler, with or without sanitizers), this
 * header supplies a main() that runs every input named on the command line once (a
 * directory: each regular file in it), so a corpus and its crash regressions replay on a
 * host without libFuzzer.
 *
 * Parsers that read a file are fed through fz_file(): one scratch directory per process,
 * on tmpfs when the host has /dev/shm, removed at exit. */
#ifndef FUZZ_COMMON_H
#define FUZZ_COMMON_H
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static char fz_dir_path[256];
static void fz_cleanup(void) {
    if (!fz_dir_path[0]) return;
    DIR *d = opendir(fz_dir_path);
    if (d) {
        struct dirent *e;
        char p[512];
        while ((e = readdir(d)))
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
                snprintf(p, sizeof p, "%s/%s", fz_dir_path, e->d_name);
                unlink(p);
            }
        closedir(d);
    }
    rmdir(fz_dir_path);
}
/* The scratch directory (created on first use). */
static const char *fz_dir(void) {
    if (!fz_dir_path[0]) {
        const char *base = access("/dev/shm", W_OK) == 0 ? "/dev/shm" : (getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
        snprintf(fz_dir_path, sizeof fz_dir_path, "%s/azfuzz.XXXXXX", base);
        if (!mkdtemp(fz_dir_path)) {
            perror("mkdtemp");
            abort();
        }
        atexit(fz_cleanup);
    }
    return fz_dir_path;
}
/* <scratch>/<name>, written with exactly data[0..n) (a new inode each time: tmp + rename). */
static const char *fz_file(const char *name, const void *data, size_t n) {
    static char path[512];
    char tmp[600];
    snprintf(path, sizeof path, "%s/%s", fz_dir(), name);
    snprintf(tmp, sizeof tmp, "%s.fztmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || (n && write(fd, data, n) != (ssize_t) n) || close(fd) != 0 || rename(tmp, path) != 0) {
        perror("fz_file");
        abort();
    }
    return path;
}
static void fz_remove(const char *name) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", fz_dir(), name);
    unlink(path);
}
/* Split data at the first n-1 occurrences of the byte sep into parts (pointers into data). */
static int fz_split(const uint8_t *d, size_t n, uint8_t sep, const uint8_t **part, size_t *len, int max) {
    int k = 0;
    size_t s = 0;
    for (size_t i = 0; i < n && k < max - 1; i++)
        if (d[i] == sep) {
            part[k] = d + s;
            len[k++] = i - s;
            s = i + 1;
        }
    part[k] = d + s;
    len[k++] = n - s;
    return k;
}
/* An invariant the parser must keep: a broken one is a finding, reported like a crash. */
#define FZ_ASSERT(cond)                                                                      \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            fprintf(stderr, "FUZZ INVARIANT FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            abort();                                                                         \
        }                                                                                    \
    } while (0)

#ifdef FZ_REPLAY
static int fz_replay_one(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return 1;
    }
    size_t cap = 1 << 16, n = 0;
    uint8_t *b = malloc(cap);
    size_t r;
    while (b && (r = fread(b + n, 1, cap - n, f)) > 0) {
        n += r;
        if (n == cap) b = realloc(b, cap *= 2);
    }
    fclose(f);
    if (!b) return 1;
    uint8_t *exact = malloc(n ? n : 1); /* an exact-size copy, so ASan sees an overread */
    memcpy(exact, b, n);
    free(b);
    LLVMFuzzerTestOneInput(exact, n);
    free(exact);
    return 0;
}
int main(int argc, char **argv) {
    int bad = 0, runs = 0;
    for (int i = 1; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) != 0) {
            perror(argv[i]);
            bad++;
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            bad += fz_replay_one(argv[i]);
            runs++;
            continue;
        }
        DIR *d = opendir(argv[i]);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            char p[1024];
            snprintf(p, sizeof p, "%s/%s", argv[i], e->d_name);
            if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) {
                bad += fz_replay_one(p);
                runs++;
            }
        }
        if (d) closedir(d);
    }
    fprintf(stderr, "replayed %d inputs, %d unreadable\n", runs, bad);
    return bad || !runs;
}
#endif
#endif
