/* SPDX-License-Identifier: GPL-3.0-only */
/* Folder and file names from a USB stick, for the FOLDER-view copy plan (guest/azmod/folder_copy.h).
 *
 * The input is a list of entries "path \x01 type \x02 size \n" under a source folder "S" (type 1
 * a file, 2 a folder); anything else in it is taken as part of a name. The plan must stay within
 * its room, and every destination path it lists must be made of valid components. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fuzz_common.h"
#include "folder_copy.h"

#define FZ_N 64
struct fz_ent {
    char path[FC_PATH_MAX];
    int type;
    uint64_t size;
};
static struct fz_ent fz_e[FZ_N + 1];
static int fz_n;
static struct fc_item fz_items[32];

static int fz_stat(void *ctx, const char *path, uint64_t *size) {
    (void) ctx;
    for (int i = 0; i < fz_n; i++)
        if (!strcmp(fz_e[i].path, path)) {
            *size = fz_e[i].size;
            return fz_e[i].type;
        }
    return 0;
}
static int fz_list(void *ctx, const char *path, int (*each)(void *, const char *), void *arg) {
    (void) ctx;
    size_t pl = strlen(path);
    for (int i = 0; i < fz_n; i++) {
        const char *p = fz_e[i].path;
        if (strncmp(p, path, pl) || p[pl] != '/')
            continue;
        if (each(arg, p + pl + 1))
            return 0;
    }
    return 0;
}
static int fz_none(void *ctx, const char *path, uint64_t *size) {
    (void) ctx;
    (void) path;
    (void) size;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (n > 8192) return 0;
    fz_n = 0;
    snprintf(fz_e[fz_n].path, sizeof fz_e[0].path, "S");
    fz_e[fz_n++].type = 2;
    size_t i = 0;
    while (i < n && fz_n < FZ_N) {
        const uint8_t *nl = memchr(d + i, '\n', n - i);
        size_t len = nl ? (size_t) (nl - (d + i)) : n - i;
        const uint8_t *t = memchr(d + i, 1, len);
        size_t nm = t ? (size_t) (t - (d + i)) : len;
        struct fz_ent *e = &fz_e[fz_n];
        int w = snprintf(e->path, sizeof e->path, "S/%.*s", (int) (nm < 400 ? nm : 400), d + i);
        if (w > 0 && memchr(e->path, 0, sizeof e->path) == e->path + w) {
            e->type = t && t + 1 < d + i + len ? (t[1] == 2 ? 2 : 1) : 1;
            e->size = (uint64_t) (len % 1000);
            fz_n++;
        }
        i += len + 1;
    }
    struct fc_fs src = {NULL, fz_stat, fz_list}, dst = {NULL, fz_none, fz_list};
    struct fc_plan p = {fz_items, 0, 32, 0, 0, 0, 0, 0};
    fc_plan_make(&src, &dst, "S", "", &p);
    FZ_ASSERT(p.n >= 0 && p.n <= p.cap);
    for (int k = 0; k < p.n; k++) {
        char tmp[FC_PATH_MAX];
        snprintf(tmp, sizeof tmp, "%s", p.items[k].dst);
        for (char *c = strtok(tmp, "/"); c; c = strtok(NULL, "/"))
            FZ_ASSERT(fc_name_ok(c));
    }
    return 0;
}
