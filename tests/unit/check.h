/* SPDX-License-Identifier: GPL-3.0-only */
/* Minimal case reporter shared by the unit-test suites: one PASS/FAIL line per
 * case, a summary line, and a nonzero exit status when any case fails. */
#ifndef UNIT_CHECK_H
#define UNIT_CHECK_H
#include <stdio.h>

static int ut_pass, ut_fail;
static const char *ut_suite = "unit";

/* clang-format off */
#define CHECK(name, cond)                                                   \
    do {                                                                    \
        if (cond) { ut_pass++; printf("PASS  %s: %s\n", ut_suite, name); }  \
        else      { ut_fail++; printf("FAIL  %s: %s\n", ut_suite, name); }  \
    } while (0)
/* clang-format on */

static int ut_summary(void) {
    printf("----  %s: %d passed, %d failed\n", ut_suite, ut_pass, ut_fail);
    return ut_fail ? 1 : 0;
}
#endif
