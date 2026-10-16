/* SPDX-License-Identifier: GPL-3.0-only */
/* Community mods: the azmod.json scanner (cm_json_walk / _str / _int / _bool / _fw), the
 * MAJOR.MINOR.PATCH compare, and the approved list (an id + sha256 pin per line), extracted
 * from community.c by tools/fuzz/lib.sh. azmod.json and the approved list come off
 * the stick and p7, so any bytes.
 *
 * Input byte 0 picks the path:
 *   0: azmod.json. Read as community.c reads it: the bytes up to the first NUL, the field
 *      reads with the buffer sizes cm_load_one and cm_names use (cm_names reads fields
 *      without the shape check first, so both orders run).
 *   1: the approved list file; every entry kept must be a valid id and a 64-hex pin.
 *   2: two version strings (split at NUL): the compare must be antisymmetric. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fuzz_common.h"

#define CM_APPROVED_MAX 64
static char fz_approved[512];
#define CM_APPROVED fz_approved
#include CMJSON_INC

static void json(const uint8_t *d, size_t n) {
    char *js = malloc(n + 1); /* exact size: an overread past the NUL is an ASan report */
    memcpy(js, d, n);
    js[n] = 0;
    char id[40], name[48], version[24], author[48], summary[120], mc[24], src[8], tiny[1];
    cm_json_str(js, "id", id, sizeof id);
    cm_json_str(js, "name", name, sizeof name);
    cm_json_str(js, "version", version, sizeof version);
    cm_json_str(js, "author", author, sizeof author);
    cm_json_str(js, "summary", summary, sizeof summary);
    cm_json_str(js, "x", tiny, sizeof tiny);
    FZ_ASSERT(tiny[0] == 0);
    const char *why = NULL;
    const char *ok = cm_json_walk(js, NULL, &why);
    FZ_ASSERT(ok ? ok == js : why != NULL);
    if (cm_json_str(js, "min_core", mc, sizeof mc)) (void) cm_vercmp(mc, "1.2.3");
    (void) cm_json_str(js, "source", src, sizeof src);
    (void) cm_json_int(js, "api", -1);
    (void) cm_json_bool(js, "hooks", 0);
    (void) cm_json_fw(js, "ep147-0123456789abcdef");
    (void) cm_json_fw(js, "");
    free(js);
}

static void approved(const uint8_t *d, size_t n) {
    snprintf(fz_approved, sizeof fz_approved, "%s", fz_file("approved", d, n));
    cm_approved_read();
    FZ_ASSERT(g_cm_nok >= 0 && g_cm_nok <= CM_APPROVED_MAX);
    for (int k = 0; k < g_cm_nok; k++) {
        FZ_ASSERT(cm_id_ok(g_cm_ok[k].id) && cm_sha_ok(g_cm_ok[k].sha));
        FZ_ASSERT(cm_is_approved(g_cm_ok[k].id, g_cm_ok[k].sha));
        for (int j = k + 1; j < g_cm_nok; j++) FZ_ASSERT(strcmp(g_cm_ok[k].id, g_cm_ok[j].id)); /* one per id */
    }
    FZ_ASSERT(!cm_is_approved("abc", "not-a-sha"));
}

static void versions(const uint8_t *d, size_t n) {
    const uint8_t *part[2];
    size_t len[2];
    if (fz_split(d, n, 0, part, len, 2) < 2) return;
    char *a = malloc(len[0] + 1), *b = malloc(len[1] + 1);
    memcpy(a, part[0], len[0]);
    a[len[0]] = 0;
    memcpy(b, part[1], len[1]);
    b[len[1]] = 0;
    int x = cm_vercmp(a, b), y = cm_vercmp(b, a);
    FZ_ASSERT(x == -2 ? y == -2 : y == -x);
    FZ_ASSERT(cm_vercmp(a, a) == -2 || cm_vercmp(a, a) == 0);
    free(a);
    free(b);
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (n < 1) return 0;
    switch (d[0] % 3) {
    case 0: json(d + 1, n - 1); break;
    case 1: approved(d + 1, n - 1); break;
    default: versions(d + 1, n - 1); break;
    }
    return 0;
}
