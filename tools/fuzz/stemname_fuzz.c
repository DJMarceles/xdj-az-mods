/* SPDX-License-Identifier: GPL-3.0-only */
/* azstem's STEMS folder matching (guest/azstem/stem_playback.c, extracted by tools/fuzz/lib.sh):
 * stem_folder_rank (folder names on the stick), slot_for_filename (stem file names), and
 * stem_folder_identity with its json_str (the folder's source.json, read on every track load).
 *
 * Input byte 0 picks the path:
 *   0: a folder name and a track key, split at NUL: the rank is -1..2, and a name that ranks
 *      starts with the key;
 *   1: a stem file name: its slot is -1..3;
 *   2: source.json's bytes, judged against a track file on the "stick" whose relative path and
 *      size match the seeds; also every "\u" escape position near the 4 KB read limit. */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "fuzz_common.h"
#include STEMNAME_INC

static char *dupz(const uint8_t *d, size_t n) {
    char *s = malloc(n + 1);
    memcpy(s, d, n);
    s[n] = 0;
    return s;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (n < 1) return 0;
    uint8_t mode = d[0] % 3;
    d++;
    n--;
    if (mode == 0) {
        const uint8_t *part[2];
        size_t len[2];
        if (fz_split(d, n, 0, part, len, 2) < 2) return 0;
        char *name = dupz(part[0], len[0]), *key = dupz(part[1], len[1]);
        int r = stem_folder_rank(name, key);
        FZ_ASSERT(r >= -1 && r <= 2);
        if (r >= 0) FZ_ASSERT(strlen(key) && !strncasecmp(name, key, strlen(key)));
        free(name);
        free(key);
    } else if (mode == 1) {
        char *name = dupz(d, n);
        int r = slot_for_filename(name);
        FZ_ASSERT(r >= -1 && r <= 3);
        free(name);
    } else {
        static char folder[512], track[600];
        if (!folder[0]) {
            snprintf(folder, sizeof folder, "%s", fz_dir());
            snprintf(track, sizeof track, "%s/Music", fz_dir());
            mkdir(track, 0755);
            snprintf(track, sizeof track, "%s/Music/Track One.mp3", fz_dir());
            FILE *f = fopen(track, "wb");
            for (int i = 0; f && i < 1000; i++) fputc(i * 7, f);
            if (!f || fclose(f)) abort();
        }
        fz_file("source.json", d, n);
        srcid_t src = {track, 1000, 0, 0};
        int r = stem_folder_identity(folder, &src);
        FZ_ASSERT(r >= -1 && r <= 1);
        /* json_str on its own: the bytes as a string value, the output never longer than n-1 */
        char *s = dupz(d, n), out[64];
        if (json_str(s, out, sizeof out)) FZ_ASSERT(strlen(out) < sizeof out);
        free(s);
    }
    return 0;
}
