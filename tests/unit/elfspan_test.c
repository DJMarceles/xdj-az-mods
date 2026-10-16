/* SPDX-License-Identifier: GPL-3.0-only */
/* The community library gate's bounds (cm_elf_span, cm_elf_strtab, extracted from
 * guest/azmod/community.c by run.sh). Every header table and string table of a mod's .so is read
 * from the file in memory before anything of the mod runs, at offsets the file itself gives:
 *   - a span is inside the file only if offset and length both are, with no 64-bit wrap (an
 *     offset near 2^64 plus a length that wraps to a small sum), and it is aligned as asked;
 *   - a string table is non-empty, inside the file and ends in NUL, so every name read from it
 *     ends inside it. The fuzz harness (tools/fuzz elfcheck) runs the whole gate; these cases
 *     name each bound. */
#include <stdint.h>
#if __has_include(<elf.h>)
#include <elf.h>
#else
#include "elf_min.h" /* macOS has no elf.h (tools/fuzz/compat) */
#endif
#include <stdio.h>
#include <string.h>
#include "check.h"

#include ELFSPAN_INC

int main(void) {
    ut_suite = "elf gate spans";
    const size_t sz = 4096;
    CHECK("a span inside the file", cm_elf_span(64, 128, sz, 8));
    CHECK("a span ending exactly at the end", cm_elf_span(4000, 96, sz, 8));
    CHECK("an empty span at the end", cm_elf_span(sz, 0, sz, 1));
    CHECK("an offset past the end: refused", !cm_elf_span(sz + 1, 0, sz, 1));
    CHECK("a length past the end: refused", !cm_elf_span(4000, 97, sz, 1));
    CHECK("an offset + length that wraps 64 bits to a small sum: refused",
          !cm_elf_span(8, UINT64_MAX - 7, sz, 1) && !cm_elf_span(UINT64_MAX, 2, sz, 1));
    CHECK("a misaligned offset: refused", !cm_elf_span(4, 64, sz, 8));
    unsigned char b[64];
    memset(b, 'a', sizeof b);
    b[47] = 0;
    Elf64_Shdr ss;
    memset(&ss, 0, sizeof ss);
    ss.sh_offset = 16;
    ss.sh_size = 32;
    CHECK("a string table ending in NUL", cm_elf_strtab(b, sizeof b, &ss));
    ss.sh_size = 31;
    CHECK("a string table whose last byte is not NUL: refused", !cm_elf_strtab(b, sizeof b, &ss));
    ss.sh_size = 0;
    CHECK("an empty string table: refused", !cm_elf_strtab(b, sizeof b, &ss));
    ss.sh_offset = 60;
    ss.sh_size = 8;
    CHECK("a string table running past the file: refused", !cm_elf_strtab(b, sizeof b, &ss));
    ss.sh_offset = UINT64_MAX - 3;
    ss.sh_size = 8;
    CHECK("a string table whose offset wraps: refused", !cm_elf_strtab(b, sizeof b, &ss));
    return ut_summary();
}
