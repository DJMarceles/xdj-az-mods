/* SPDX-License-Identifier: GPL-3.0-only */
/* The Clock's Set Time Manually picker, the pure part (guest/azmod/clock_set.h):
 *   - the fields: each steps inside its own range and wraps without carrying into the next; a
 *     month or year change keeps the day inside the new month (leap years included);
 *   - civil dates to days and back, against the C library's timegm for every day of the range;
 *   - local fields to UTC through a real zone file (Denver, both offsets, the skipped spring hour,
 *     every hour of a year round-tripped), and UTC itself without a file;
 *   - the time shown only when synchronized or not before the last manual set;
 *   - the picker's start: the time itself, else a stick's newest file, else the range's start.
 *
 * Usage: clockset_test <fixtures/zoneinfo dir> */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "clock_set.h"
#include "check.h"

static struct ctz g_z;
static struct cs_fields F(int y, int mo, int d, int h, int mi) {
    struct cs_fields f = {{y, mo, d, h, mi}};
    return f;
} static int same(const struct cs_fields *a, const struct cs_fields *b) {
    return !memcmp(a->v, b->v, sizeof a->v);
}

int main(int argc, char **argv) {
    ut_suite = "clock set";
    if (argc < 2) {
        fprintf(stderr, "usage: clockset_test <zoneinfo fixtures dir>\n");
        return 2;
    }

    /* ---- the fields ---- */
    CHECK("leap years", cs_dim(2028, 2) == 29 && cs_dim(2026, 2) == 28 && cs_dim(2100, 2) == 28 &&
                            cs_dim(2000, 2) == 29 && cs_dim(2026, 4) == 30 && cs_dim(2026, 12) == 31);
    struct cs_fields f = F(2026, 10, 19, 23, 59);
    cs_step(&f, CS_MINUTE, 1);
    CHECK("minute 59 + 1 wraps to 0, the hour stays", f.v[CS_MINUTE] == 0 && f.v[CS_HOUR] == 23);
    cs_step(&f, CS_HOUR, 1);
    CHECK("hour 23 + 1 wraps to 0, the day stays", f.v[CS_HOUR] == 0 && f.v[CS_DAY] == 19);
    cs_step(&f, CS_MINUTE, -1);
    CHECK("minute 0 - 1 wraps to 59", f.v[CS_MINUTE] == 59);
    f = F(2026, 1, 31, 12, 0);
    cs_step(&f, CS_MONTH, -1);
    CHECK("January - 1 is December, the year stays", f.v[CS_MONTH] == 12 && f.v[CS_YEAR] == 2026);
    f = F(2028, 3, 31, 12, 0);
    cs_step(&f, CS_MONTH, -1);
    CHECK("March 31 to February of a leap year: the 29th", f.v[CS_MONTH] == 2 && f.v[CS_DAY] == 29);
    cs_step(&f, CS_YEAR, 1);
    CHECK("February 29 to a common year: the 28th", f.v[CS_YEAR] == 2029 && f.v[CS_DAY] == 28);
    f = F(2026, 4, 30, 0, 0);
    cs_step(&f, CS_DAY, 1);
    CHECK("April 30 + 1 wraps to the 1st, the month stays", f.v[CS_DAY] == 1 && f.v[CS_MONTH] == 4);
    f = F(CS_YEAR_MAX, 6, 1, 0, 0);
    cs_step(&f, CS_YEAR, 1);
    CHECK("the last year + 1 wraps to the first", f.v[CS_YEAR] == CS_YEAR_MIN);
    f = F(1999, 13, 40, 25, 61);
    cs_clamp(&f);
    CHECK("out-of-range fields clamped", same(&f, &(struct cs_fields) {{CS_YEAR_MIN, 12, 31, 23, 59}}));
    f = F(2026, 5, 10, 8, 30);
    cs_step(&f, CS_DAY, 31 * 3 + 2);
    CHECK("a large step wraps by its remainder", f.v[CS_DAY] == 12); /* 31 days: +95 = +2 mod 31 */

    /* ---- civil dates ---- */
    CHECK("1970-01-01 is day 0, 2000-03-01 day 11017", cs_days(1970, 1, 1) == 0 && cs_days(2000, 3, 1) == 11017);
    int civ_ok = 1;
    for (int64_t d = cs_days(CS_YEAR_MIN, 1, 1); d <= cs_days(CS_YEAR_MAX, 12, 31); d++) {
        int y, m, dd;
        cs_civil(d, &y, &m, &dd);
        struct tm tm = {0};
        tm.tm_year = y - 1900;
        tm.tm_mon = m - 1;
        tm.tm_mday = dd;
        civ_ok &= (int64_t) timegm(&tm) == d * 86400 && cs_days(y, m, dd) == d;
    }
    CHECK("every day of the range: civil and back, equal to timegm", civ_ok);
    cs_from_utc(1783188000LL /* a July afternoon, 18:00 UTC */, -6 * 3600, &f);
    CHECK("UTC to local fields at UTC-6", same(&f, &(struct cs_fields) {{2026, 7, 4, 12, 0}}));
    cs_from_utc(1783123200LL /* the same July day, 00:00 UTC */, -6 * 3600, &f);
    CHECK("UTC-6 across midnight: the day before", same(&f, &(struct cs_fields) {{2026, 7, 3, 18, 0}}));

    /* ---- local to UTC ---- */
    unsigned char b[1 << 16];
    char p[512];
    snprintf(p, sizeof p, "%s/America_Denver", argv[1]);
    FILE *fp = fopen(p, "rb");
    size_t n = fp ? fread(b, 1, sizeof b, fp) : 0;
    if (fp)
        fclose(fp);
    CHECK("Denver fixture parses", n > 1000 && ctz_parse(b, n, &g_z) == 0);
    int64_t t = 0;
    f = F(2026, 7, 4, 14, 5);
    CHECK("Denver July 14:05 is 20:05 UTC", cs_to_utc(&g_z, &f, &t) == 0 && t == 1783188000LL + 2 * 3600 + 5 * 60);
    f = F(2026, 1, 15, 9, 0);
    CHECK("Denver January 09:00 is 16:00 UTC", cs_to_utc(&g_z, &f, &t) == 0 && t == 1768435200LL + 16 * 3600);
    f = F(2026, 3, 8, 2, 30); /* skipped: 02:00 MST becomes 03:00 MDT */
    CHECK("the skipped 02:30 lands one hour on (03:30 MDT)", cs_to_utc(&g_z, &f, &t) == 0 && t == 1772928000LL + 9 * 3600 + 30 * 60);
    f = F(2026, 11, 1, 1, 30); /* 01:30 happens twice: the first one (MDT) */
    CHECK("the repeated 01:30: its first time", cs_to_utc(&g_z, &f, &t) == 0 && t == 1793491200LL + 7 * 3600 + 30 * 60);
    int rt = 1;
    for (int64_t u = 1767225600LL /* a January 1, 00:00 UTC */; u < 1798761600LL; u += 3600) {
        int32_t off;
        struct cs_fields g;
        int64_t back;
        rt &= ctz_offset(&g_z, u, &off) == 0;
        cs_from_utc(u, off, &g);
        rt &= cs_to_utc(&g_z, &g, &back) == 0 && (back == u || (g.v[CS_MONTH] == 11 && g.v[CS_HOUR] == 1));
    }
    CHECK("every hour of 2026 round-trips (but the repeated hour)", rt);
    f = F(2026, 7, 4, 14, 5);
    CHECK("UTC without a zone file", cs_to_utc(NULL, &f, &t) == 0 && t == 1783188000LL - 4 * 3600 + 5 * 60);
    struct ctz none = {0};
    CHECK("a zone that cannot answer: refused", cs_to_utc(&none, &f, &t) == -1);

    /* ---- shown or not, and the start ---- */
    int64_t now = 1792400000LL, mk = now - 3600;
    CHECK("synchronized: shown", cs_valid(1, 0, 1000));
    CHECK("not synchronized, never set: not shown", !cs_valid(0, 0, now));
    CHECK("set an hour ago: shown", cs_valid(0, mk, now));
    CHECK("reset below the set (power off): not shown", !cs_valid(0, mk, 1358499000LL));
    CHECK("a few seconds below the set: shown", cs_valid(0, now, now - CS_SLACK));
    CHECK("a marker before the range: not shown", !cs_valid(0, 1358499000LL, 1358500000LL));
    CHECK("start: the time when it is shown", cs_seed(1, now, mk) == now);
    CHECK("start: the stick's newest file when not", cs_seed(0, 1358499000LL, mk) == mk);
    CHECK("start: the range's start without either", cs_seed(0, 1358499000LL, 1000) == CS_T_MIN);
    return ut_summary();
}
