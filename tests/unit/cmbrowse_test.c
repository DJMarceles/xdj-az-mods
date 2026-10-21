/* SPDX-License-Identifier: GPL-3.0-only */
/* Community Mods > Browse: which catalog mods show, and in what order (guest/azmod/cm_browse.h):
 *   - the category of a manifest value: the four keys, anything else (or none) Other;
 *   - the updated date: YYYY-MM-DD only, with a real month and day;
 *   - the three orders, mods that cannot install here last, ties in catalog order;
 *   - the per-category counts;
 *   - the download count (catalog field 21), its text, and the Downloads order;
 *   - Home's shelves: which mods, in what order, which shelves show;
 *   - a mod page's "updated N days ago".
 * CMBROWSE_HDR = the header under test. */
#include "check.h"
#include <stdio.h>
#include <string.h>
#include CMBROWSE_HDR

/* the catalog: name, category, updated, installed, usable, counted, downloads */
static const cmb_mod M[] = {
    /* 0 */ {"Echo Out", "audio", "2019-09-14", 1, 1, 1, 1213},
    /* 1 */ {"beat Counter", "visual", "2019-10-12", 1, 1, 1, 1840},
    /* 2 */ {"Auto Gain", "controls", "2019-10-18", 0, 1, 1, 512},
    /* 3 */ {"Filter Sweep", "audio", "2019-04-03", 0, 1, 1, 655},
    /* 4 */ {"Zed Tool", "tools", "", 0, 1, 0, 0},
    /* 5 */ {"Old Deck", "controls", "2019-10-19", 0, 0, 1, 5000},
    /* 6 */ {"Mystery", "", "2019-01-30", 0, 1, 1, 0},
    /* 7 */ {"Odd", "Visual", "not-a-date", 0, 1, 1, 655},
    /* 8 */ {"Echo Out", "audio", "2019-09-14", 0, 1, 0, 0},
    /* 9 */ {"Big Clock", "visual", "2019-08-02", 1, 0, 1, 99},
};
#define N ((int) (sizeof M / sizeof M[0]))

/* the view as a string of indexes, "2,1,0" */
static const char *view(int cat, int sort) {
    static char s[64];
    int out[N], k = cmb_view(M, N, cat, sort, out), o = 0;
    s[0] = 0;
    for (int i = 0; i < k; i++)
        o += snprintf(s + o, sizeof s - o, "%s%d", i ? "," : "", out[i]);
    return s;
}
/* Home's shelves as a string, "T:1,0;N:2,1;C0:1,9" (T top, N new, C<k> a category) */
static const char *shelves(const cmb_mod *m, int n) {
    static char s[512];
    int tmp[64], o = 0;
    cmb_shelf sh[CMB_NSHELF];
    int ns = cmb_shelves(m, n, tmp, sh);
    s[0] = 0;
    for (int i = 0; i < ns; i++) {
        if (sh[i].kind == CMB_SH_TOP)
            o += snprintf(s + o, sizeof s - o, "%sT:", i ? ";" : "");
        else if (sh[i].kind == CMB_SH_NEW)
            o += snprintf(s + o, sizeof s - o, "%sN:", i ? ";" : "");
        else
            o += snprintf(s + o, sizeof s - o, "%sC%d:", i ? ";" : "", sh[i].kind - CMB_SH_CAT);
        for (int j = 0; j < sh[i].n; j++)
            o += snprintf(s + o, sizeof s - o, "%s%d", j ? "," : "", sh[i].idx[j]);
    }
    return s;
}
/* cmb_dl_parse as text: the count, or "-" */
static const char *dl(const char *v) {
    static char s[32];
    long long n = -7;
    if (!cmb_dl_parse(v, &n))
        return "-";
    snprintf(s, sizeof s, "%lld", n);
    return s;
}
static const char *ctext(long long v) {
    static char s[32];
    cmb_count_text(v, s, sizeof s);
    return s;
}
#define DAY 86400LL
#define T2031 1950134400LL /* 2031-10-19 00:00 UTC */
static const char *utext(const char *upd, long long now) {
    static char s[48];
    cmb_updated_text(upd, now, s, sizeof s);
    return s;
}
static int same(const char *a, const char *b) {
    if (strcmp(a, b))
        printf("      got %s, want %s\n", a, b);
    return !strcmp(a, b);
}

int main(void) {
    ut_suite = "community browse";
    /* categories */
    CHECK("cat: visual", cmb_cat_of("visual") == 0);
    CHECK("cat: controls", cmb_cat_of("controls") == 1);
    CHECK("cat: audio", cmb_cat_of("audio") == 2);
    CHECK("cat: tools", cmb_cat_of("tools") == 3);
    CHECK("cat: none is Other", cmb_cat_of("") == CMB_OTHER && cmb_cat_of(NULL) == CMB_OTHER);
    CHECK("cat: an unknown key is Other", cmb_cat_of("synths") == CMB_OTHER);
    CHECK("cat: keys are lower case only", cmb_cat_of("Visual") == CMB_OTHER);
    CHECK("cat: a key with more after it is Other", cmb_cat_of("visuals") == CMB_OTHER);
    CHECK("cat: the labels", !strcmp(cmb_cat_label[0], "Visual") && !strcmp(cmb_cat_label[CMB_OTHER], "Other"));
    /* dates */
    CHECK("date: a date", cmb_date_ok("2019-10-19"));
    CHECK("date: month 13", !cmb_date_ok("2019-13-01"));
    CHECK("date: day 00", !cmb_date_ok("2019-10-00"));
    CHECK("date: day 32", !cmb_date_ok("2019-10-32"));
    CHECK("date: too long", !cmb_date_ok("2019-10-190"));
    CHECK("date: too short", !cmb_date_ok("2019-10-1"));
    CHECK("date: slashes", !cmb_date_ok("2019/10/19"));
    CHECK("date: empty and NULL", !cmb_date_ok("") && !cmb_date_ok(NULL));
    /* the category filter */
    CHECK("filter: Audio", same(view(2, CMB_SORT_NAME), "0,8,3"));
    CHECK("filter: Visual", same(view(0, CMB_SORT_NAME), "1,9"));
    CHECK("filter: Other holds none and unknown", same(view(CMB_OTHER, CMB_SORT_NAME), "6,7"));
    CHECK("filter: Tools holds one", same(view(3, CMB_SORT_NAME), "4"));
    /* the orders; in each, mods that cannot install here go last, and ties keep catalog order */
    CHECK("sort A-Z: by name, case ignored, unusable last", same(view(CMB_ALL, CMB_SORT_NAME), "2,1,0,8,3,6,7,4,9,5"));
    CHECK("sort A-Z: equal names keep catalog order", same(view(2, CMB_SORT_NAME), "0,8,3"));
    CHECK("names: a number by its value", cmb_namecmp("Mod 2", "Mod 10") < 0 && cmb_namecmp("Mod 10", "Mod 9") > 0);
    CHECK("names: leading zeros do not count", cmb_namecmp("Mod 007", "Mod 7") == 0 && cmb_namecmp("Mod 08", "Mod 7") > 0);
    CHECK("names: a number before a letter, as text", cmb_namecmp("Mod 2", "Mod A") < 0);
    CHECK("names: text after equal numbers", cmb_namecmp("v2 beta", "v2 alpha") > 0 && cmb_namecmp("v2", "v2 x") < 0);
    CHECK("names: case ignored, NULL is empty", cmb_namecmp("ECHO", "echo") == 0 && cmb_namecmp(NULL, "") == 0);
    CHECK("sort Updated: newest first, undated after dated, unusable last",
          same(view(CMB_ALL, CMB_SORT_UPDATED), "2,1,0,8,3,6,7,4,5,9"));
    CHECK("sort Updated: an invalid date counts as none", same(view(CMB_OTHER, CMB_SORT_UPDATED), "6,7"));
    CHECK("sort Installed: installed first (unusable too), then the rest",
          same(view(CMB_ALL, CMB_SORT_INSTALLED), "1,0,9,2,8,3,6,7,4,5"));
    CHECK("sort: an unknown order is A-Z", same(view(CMB_ALL, 99), "2,1,0,8,3,6,7,4,9,5") &&
                                               same(view(CMB_ALL, -1), "2,1,0,8,3,6,7,4,9,5"));
    /* counts */
    int c[CMB_NCAT + 1];
    cmb_counts(M, N, c);
    CHECK("counts: every category",
          c[0] == 2 && c[1] == 2 && c[2] == 3 && c[3] == 1 && c[CMB_OTHER] == 2 && c[CMB_NCAT] == N);
    CHECK("counts: an empty catalog", (cmb_counts(M, 0, c), c[CMB_NCAT] == 0 && c[2] == 0));
    /* the download count */
    CHECK("count: digits", same(dl("1840"), "1840") && same(dl("0"), "0") && same(dl("0012"), "12"));
    CHECK("count: 13 digits, the most there is", same(dl("9999999999999"), "9999999999999"));
    CHECK("count: 14 digits refused", same(dl("12345678901234"), "-"));
    CHECK("count: not digits refused", same(dl(""), "-") && same(dl(NULL), "-") && same(dl("-4"), "-") && same(dl("1.5"), "-") &&
                                           same(dl("12 "), "-") && same(dl(" 12"), "-") && same(dl("1e3"), "-") && same(dl("+3"), "-"));
    CHECK("count text: a comma every three digits", same(ctext(1840), "1,840") && same(ctext(999), "999") && same(ctext(1000), "1,000") &&
                                                        same(ctext(1234567), "1,234,567") && same(ctext(0), "0"));
    CHECK("count text: the largest count", same(ctext(9999999999999LL), "9,999,999,999,999"));
    {
        char b[6];
        cmb_count_text(1234567, b, sizeof b);
        CHECK("count text: cut to the room given, terminated", same(b, "1,234"));
    }
    CHECK("sort Downloads: most first, ties in catalog order, uncounted after counted (by name), unusable last",
          same(view(CMB_ALL, CMB_SORT_DOWNLOADS), "1,0,3,7,2,6,8,4,5,9"));
    CHECK("sort Downloads: within a category", same(view(2, CMB_SORT_DOWNLOADS), "0,3,8"));
    CHECK("sort Downloads: the label", !strcmp(cmb_sort_label[CMB_SORT_DOWNLOADS], "Downloads"));
    /* Home */
    CHECK("shelves: Most downloaded (counts above 0), New & updated (dated), then the categories; Tools (one mod) hidden",
          same(shelves(M, N), "T:1,0,3,7,2;N:2,1,0,8,3;C0:1,9;C1:2,5;C2:0,3,8;C4:7,6"));
    {
        const cmb_mod few[] = {M[1], M[6], M[9]};   /* 1840, a count of 0, 99: the 0 would fit on the row */
        CHECK("shelves: a count of 0 is not on Most downloaded, even with room for it", !strncmp(shelves(few, 3), "T:0,2;", 6));
    }
    CHECK("shelves: an empty catalog", same(shelves(M, 0), ""));
    {
        cmb_mod big[20];
        for (int i = 0; i < 20; i++)
            big[i] = (cmb_mod) {"Mod", "tools", "", 0, 1, 1, 100 + i};
        CHECK("shelves: at most 5 mods on one (the widest row), the most downloaded",
              same(shelves(big, 20), "T:19,18,17,16,15;C3:19,18,17,16,15"));
        for (int i = 0; i < 20; i++)
            big[i].counted = 0;
        CHECK("shelves: no counts at all (none fetched yet): no Most downloaded, a category by name",
              same(shelves(big, 3), "C3:0,1,2"));
    }
    CHECK("shelf: titles", !strcmp(cmb_shelf_title(CMB_SH_TOP), "Most downloaded") && !strcmp(cmb_shelf_title(CMB_SH_NEW), "New & updated") &&
                               !strcmp(cmb_shelf_title(CMB_SH_CAT + 2), "Audio") && !strcmp(cmb_shelf_title(CMB_NSHELF), ""));
    CHECK("shelf: See all opens All mods by downloads, by date, or the category by downloads",
          cmb_shelf_cat(CMB_SH_TOP) == CMB_ALL && cmb_shelf_sort(CMB_SH_TOP) == CMB_SORT_DOWNLOADS &&
              cmb_shelf_cat(CMB_SH_NEW) == CMB_ALL && cmb_shelf_sort(CMB_SH_NEW) == CMB_SORT_UPDATED &&
              cmb_shelf_cat(CMB_SH_CAT + 1) == 1 && cmb_shelf_sort(CMB_SH_CAT + 1) == CMB_SORT_DOWNLOADS);
    /* updated N days ago */
    CHECK("days: known dates", cmb_days("1970-01-01") == 0 && cmb_days("2000-03-01") == 11017 && cmb_days("2031-10-19") == T2031 / DAY &&
                                   cmb_days("1969-12-31") == -1);
    CHECK("updated: today", same(utext("2031-10-19", T2031 + 3600), "updated today"));
    CHECK("updated: yesterday", same(utext("2031-10-18", T2031 + 3600), "updated yesterday"));
    CHECK("updated: N days ago", same(utext("2031-10-10", T2031), "updated 9 days ago") && same(utext("2030-10-19", T2031), "updated 365 days ago"));
    CHECK("updated: a clock that is not set shows the date", same(utext("2031-10-10", 1000), "updated 2031-10-10") &&
                                                                 same(utext("2022-12-01", 1672531200LL), "updated 2022-12-01"));
    CHECK("updated: a date ahead of the clock shows the date", same(utext("2031-10-20", T2031), "updated 2031-10-20"));
    CHECK("updated: over ten years back shows the date", same(utext("2010-01-01", T2031), "updated 2010-01-01"));
    CHECK("updated: no date, nothing", same(utext("", T2031), "") && same(utext("2026-1-8", T2031), "") && same(utext(NULL, T2031), ""));
    return ut_summary();
}
