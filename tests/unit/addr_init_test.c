/* SPDX-License-Identifier: GPL-3.0-only */
/* ep147_addr_init: "inert unless complete".
 *
 * Part 1 runs the real generated header (ADDR_HEADER: azmod's or azstem's):
 * before init and on an unknown build every entry is 0 and init reports 0; the
 * shipped 1.30 and 2.00 tables are complete.
 *
 * Part 2 runs the same init function, extracted from that header by run.sh and
 * renamed to synth_*, against small mutable tables, because the real tables
 * contain no zero and so cannot show what a hole does. A zero anywhere must make
 * init report 0 and leave the live table untouched (no partial application). */
#include <stdint.h>
#include <string.h>
#include ADDR_HEADER
#include "check.h"

#define SYNTH_N 8
static uint32_t synth_v130[SYNTH_N], synth_v200[SYNTH_N];
static uintptr_t synth_addr[SYNTH_N];
#include SYNTH_INC

static int all_zero(const uintptr_t *a, int n) {
    for (int i = 0; i < n; i++)
        if (a[i]) return 0;
    return 1;
}
static int same(const uintptr_t *a, const uint32_t *t, int n) {
    for (int i = 0; i < n; i++)
        if (a[i] != t[i]) return 0;
    return 1;
}
static void fill(uint32_t *t, uint32_t base) {
    for (int i = 0; i < SYNTH_N; i++) t[i] = base + 0x10u * (uint32_t) i;
}

int main(void) {
    ut_suite = SUITE_NAME;
    char name[128];

    /* ---- part 1: the real tables ---- */
    CHECK("real: every entry 0 before init", all_zero(ep147_addr, EPI_N));
    CHECK("real: unknown build -> init reports 0", ep147_addr_init(EPB_UNKNOWN) == 0);
    CHECK("real: unknown build leaves every entry 0", all_zero(ep147_addr, EPI_N));
    CHECK("real: id with no table (999) -> init reports 0", ep147_addr_init(999) == 0);
    CHECK("real: id -1 -> init reports 0", ep147_addr_init(-1) == 0);
    CHECK("real: still every entry 0", all_zero(ep147_addr, EPI_N));
    int z130 = 0, z200 = 0;
    for (int i = 0; i < EPI_N; i++) {
        z130 += !ep147_addr_v130[i];
        z200 += !ep147_addr_v200[i];
    }
    CHECK("real: shipped 1.30 table has no zero entry", z130 == 0);
    CHECK("real: shipped 2.00 table has no zero entry", z200 == 0);
    CHECK("real: 1.30 init reports 1", ep147_addr_init(EPB_V130) == 1);
    CHECK("real: 1.30 init copies the 1.30 table", same(ep147_addr, ep147_addr_v130, EPI_N));
    CHECK("real: 2.00 init reports 1", ep147_addr_init(EPB_V200) == 1);
    CHECK("real: 2.00 init copies the 2.00 table", same(ep147_addr, ep147_addr_v200, EPI_N));

    /* ---- part 2: the extracted init against synthetic tables ---- */
    fill(synth_v130, 0x1000);
    fill(synth_v200, 0x2000);
    CHECK("synth: unknown build -> 0", synth_init(EPB_UNKNOWN) == 0);
    CHECK("synth: unknown build leaves live table 0", all_zero(synth_addr, SYNTH_N));
    CHECK("synth: id 999 -> 0", synth_init(999) == 0);
    CHECK("synth: id 999 leaves live table 0", all_zero(synth_addr, SYNTH_N));

    int holes[] = {0, 3, SYNTH_N - 1};
    for (int b = 0; b < 2; b++) {
        int build = b ? EPB_V200 : EPB_V130;
        uint32_t *t = b ? synth_v200 : synth_v130;
        for (unsigned h = 0; h < sizeof holes / sizeof *holes; h++) {
            fill(synth_v130, 0x1000);
            fill(synth_v200, 0x2000);
            memset(synth_addr, 0, sizeof synth_addr);
            t[holes[h]] = 0;
            snprintf(name, sizeof name, "synth: %s table, zero at [%d] -> 0", b ? "2.00" : "1.30", holes[h]);
            CHECK(name, synth_init(build) == 0);
            snprintf(name, sizeof name, "synth: %s table, zero at [%d] -> nothing applied", b ? "2.00" : "1.30", holes[h]);
            CHECK(name, all_zero(synth_addr, SYNTH_N));
        }
    }
    /* A hole in 2.00 must not fall through to the (complete) 1.30 table. */
    fill(synth_v130, 0x1000);
    fill(synth_v200, 0x2000);
    synth_v200[5] = 0;
    memset(synth_addr, 0, sizeof synth_addr);
    CHECK("synth: 2.00 hole does not fall back to 1.30", synth_init(EPB_V200) == 0 && all_zero(synth_addr, SYNTH_N));

    fill(synth_v130, 0x1000);
    fill(synth_v200, 0x2000);
    memset(synth_addr, 0, sizeof synth_addr);
    CHECK("synth: complete 1.30 -> 1", synth_init(EPB_V130) == 1);
    CHECK("synth: complete 1.30 copied exactly", same(synth_addr, synth_v130, SYNTH_N));
    CHECK("synth: complete 2.00 -> 1", synth_init(EPB_V200) == 1);
    CHECK("synth: complete 2.00 copied exactly", same(synth_addr, synth_v200, SYNTH_N));
    return ut_summary();
}
