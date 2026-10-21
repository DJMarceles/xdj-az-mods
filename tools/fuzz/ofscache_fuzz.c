/* SPDX-License-Identifier: GPL-3.0-only */
/* azstem's stem offset cache line, <stems folder>/azstem.ofs (ofs_cache_parse in
 * guest/azstem/ofsload.c, extracted by tools/fuzz/lib.sh), read on every load of a track with
 * stems. The input is the file's first line as fgets returns it (cut at 127 bytes, NUL-ended),
 * judged against a fixed source and stem set. A line that is taken gives a lag inside the
 * correlator's window, and the same line is taken for no other source size. */
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fuzz_common.h"

#define NDECK 4
#define OFS_LAGMAX 8192
static char g_env_dir[NDECK][1024];
#include OFSCACHE_INC

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    char line[128];
    if (n > sizeof line - 1)
        n = sizeof line - 1;
    memcpy(line, d, n);
    line[n] = 0;
    long lag = 12345;
    if (ofs_cache_parse(line, 1000, 2000, 3000ULL, &lag)) {
        FZ_ASSERT(lag >= -OFS_LAGMAX && lag <= OFS_LAGMAX);
        long other = 0;
        FZ_ASSERT(!ofs_cache_parse(line, 1001, 2000, 3000ULL, &other));
    } else
        FZ_ASSERT(lag == 12345);
    return 0;
}
