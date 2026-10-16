/* SPDX-License-Identifier: GPL-3.0-only */
/* azprot.h (guest/common): the protection a patch restores is the one /proc/self/maps
 * reports for the whole range, never a constant. Fed synthetic maps files:
 *   - code in an r-x mapping restores r-x; RELRO vtables restore r--; .rodata that shares
 *     the r-x segment restores r-x (the case a hard-coded PROT_READ broke);
 *   - a range spanning two protections, or touching a hole, is refused (-1);
 *   - contiguous mappings with the same protection count as one;
 *   - malformed lines are skipped, never parsed into a range. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "check.h"
#include AZPROT_INC

static const char *MAPS =
    "00400000-02b00000 r-xp 00000000 b3:02 1234     /home/root/pdj/EP147\n"
    "02b00000-02c00000 r-xp 02700000 b3:02 1234     /home/root/pdj/EP147\n"
    "02c00000-02e00000 r--p 02800000 b3:02 1234     /home/root/pdj/EP147\n"
    "02e00000-02f00000 rw-p 02a00000 b3:02 1234     /home/root/pdj/EP147\n"
    "03000000-03100000 rw-p 00000000 00:00 0        [heap]\n"
    "garbage line without a range\n"
    "7f00000000-7f00010000 r-xp 00000000 00:00 0 \n";

static int q(const char *path, uintptr_t a, uintptr_t b) {
    int fd = open(path, O_RDONLY);
    int p = azprot_query_fd(fd, a, b);
    if (fd >= 0)
        close(fd);
    return p;
}

int main(int argc, char **argv) {
    ut_suite = "azprot";
    char path[512];
    snprintf(path, sizeof path, "%s/maps", argc > 1 ? argv[1] : ".");
    FILE *f = fopen(path, "w");
    if (!f || fputs(MAPS, f) < 0 || fclose(f) != 0) {
        printf("FAIL  azprot: cannot write %s\n", path);
        return 1;
    }
    const int RX = PROT_READ | PROT_EXEC, R = PROT_READ, RW = PROT_READ | PROT_WRITE;
    CHECK("code page: r-x", q(path, 0x14ab000, 0x14ac000) == RX);
    CHECK("RELRO vtable page: r--", q(path, 0x2cd4000, 0x2cd5000) == R);
    CHECK("art inside the r-x segment: r-x (not r--)", q(path, 0x2a00000, 0x2a10000) == RX);
    CHECK("two contiguous r-x mappings count as one", q(path, 0x2aff000, 0x2b01000) == RX);
    CHECK("range spanning r-x and r--: refused", q(path, 0x2bff000, 0x2c01000) == -1);
    CHECK("range spanning r-- and rw-: refused", q(path, 0x2dff000, 0x2e01000) == -1);
    CHECK("range running into a hole: refused", q(path, 0x2eff000, 0x3001000) == -1);
    CHECK("range wholly inside a hole: refused", q(path, 0x2f10000, 0x2f20000) == -1);
    CHECK("below every mapping: refused", q(path, 0x1000, 0x2000) == -1);
    CHECK("heap: rw-", q(path, 0x3000000, 0x3001000) == RW);
    CHECK("a mapping after a malformed line is still found", q(path, 0x7f00001000, 0x7f00002000) == RX);
    CHECK("empty range refused", q(path, 0x14ab000, 0x14ab000) == -1);
    uintptr_t lo, hi;
    int p;
    CHECK("parse: a normal line",
          azprot_parse_line("02c00000-02e00000 r--p 0 b3:02 1 /x", &lo, &hi, &p) && lo == 0x2c00000 &&
              hi == 0x2e00000 && p == R);
    CHECK("parse: no dash", !azprot_parse_line("02c00000 02e00000 r--p", &lo, &hi, &p));
    CHECK("parse: end below start", !azprot_parse_line("02e00000-02c00000 r--p", &lo, &hi, &p));
    CHECK("parse: bad permission letters", !azprot_parse_line("1000-2000 abcd", &lo, &hi, &p));
    CHECK("parse: 17 hex digits", !azprot_parse_line("11111111111111111-2 r--p", &lo, &hi, &p));
    return ut_summary();
}
