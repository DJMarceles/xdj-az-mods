/* SPDX-License-Identifier: GPL-3.0-only */
/* The community mod ELF gate (cm_elf_check, guest/azmod/community.c, extracted with its symbol
 * and library lists by tools/fuzz/lib.sh). It reads a mod's .so from the stick inside EP147,
 * before the user has approved it: the file is any bytes.
 * Invariant: an accepted file yields its 64-hex sha256 fingerprint; a refused one a reason. */
#if __has_include(<elf.h>)
#include <elf.h>
#else
#include "compat/elf_min.h"
#endif
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fuzz_common.h"
#include "azsha256.h"

#define CM_SO_CAP (16 * 1024 * 1024)
#include ELFCHECK_INC

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (n > (1 << 20)) return 0;
    const char *p = fz_file("mod.so", d, n);
    char why[160] = "", sha[65];
    int rc = cm_elf_check(p, why, sizeof why, sha);
    if (getenv("FZ_VERBOSE")) fprintf(stderr, "rc=%d %s\n", rc, rc ? why : sha);
    if (rc == 0) {
        FZ_ASSERT(strlen(sha) == 64 && strspn(sha, "0123456789abcdef") == 64);
    } else {
        FZ_ASSERT(rc == -1 && why[0] && !sha[0]);
    }
    return 0;
}
