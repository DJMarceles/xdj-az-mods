/* SPDX-License-Identifier: GPL-3.0-only */
/* source_fp (extracted from azstem/stem_playback.c by run.sh): the "fp" value of a stems
 * folder's source.json is taken only in the form AZ-MODS writes it, "fnv1a64:" and 16 hex
 * digits in quotes. A value strtoull would also read (a 0x prefix, a sign, a space) is
 * rejected, so such a folder is matched by its path and size instead. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include SRCFP_INC
#include "check.h"

static int ok(const char *v, unsigned long long want) {
    unsigned long long fp = 0;
    return source_fp(v, &fp) == 1 && fp == want;
}
static int no(const char *v) {
    unsigned long long fp = 7;
    return source_fp(v, &fp) == 0 && fp == 7;
}

int main(void) {
    ut_suite = "source.json fp";
    CHECK("the app's form (lowercase) is read", ok("\"fnv1a64:0123456789abcdef\"", 0x0123456789abcdefULL));
    CHECK("uppercase hex is read", ok("\"fnv1a64:FEDCBA9876543210\", \"x\": 1", 0xfedcba9876543210ULL));
    CHECK("a 0x prefix is rejected", no("\"fnv1a64:0x23456789abcdef\""));
    CHECK("a 0X prefix is rejected", no("\"fnv1a64:0X23456789abcdef\""));
    CHECK("a leading space is rejected", no("\"fnv1a64: 123456789abcdef\""));
    CHECK("a sign is rejected", no("\"fnv1a64:-123456789abcdef\""));
    CHECK("15 digits are rejected", no("\"fnv1a64:0123456789abcde\""));
    CHECK("17 digits are rejected", no("\"fnv1a64:0123456789abcdef0\""));
    CHECK("a missing closing quote is rejected", no("\"fnv1a64:0123456789abcdef"));
    CHECK("another hash name is rejected", no("\"sha1:0123456789abcdef\""));
    return ut_summary();
}
