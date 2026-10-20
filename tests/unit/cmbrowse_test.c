/* SPDX-License-Identifier: GPL-3.0-only */
/* Community Mods > Browse: which catalog mods show, and in what order (guest/azmod/cm_browse.h):
 *   - the category of a manifest value: the four keys, anything else (or none) Other;
 *   - the updated date: YYYY-MM-DD only, with a real month and day;
 *   - the search: every word, in the name, author, summary or description, ignoring ASCII case;
 *   - the three orders, mods that cannot install here last, ties in catalog order;
 *   - the per-category counts under a search.
 * CMBROWSE_HDR = the header under test. */
#include "check.h"
#include <stdio.h>
#include <string.h>
#include CMBROWSE_HDR

/* the catalog: name, author, summary, desc, category, updated, installed, usable */
static const cmb_mod M[] = {
    /* 0 */ {"Echo Out", "dubwise", "One-press echo out.", "A tempo-synced tail.", "audio", "2019-09-14", 1, 1},
    /* 1 */ {"beat Counter", "djmara", "Counts beats.", "", "visual", "2019-10-12", 1, 1},
    /* 2 */ {"Auto Gain", "levels", "Suggests a TRIM.", "", "controls", "2019-10-18", 0, 1},
    /* 3 */ {"Filter Sweep", "DubWise", "Filter sweeps.", "", "audio", "2019-04-03", 0, 1},
    /* 4 */ {"Zed Tool", "fixit", "Checks the stick.", "", "tools", "", 0, 1},
    /* 5 */ {"Old Deck", "prism", "Needs a newer core.", "", "controls", "2019-10-19", 0, 0},
    /* 6 */ {"Mystery", "someone", "No category.", "", "", "2019-01-30", 0, 1},
    /* 7 */ {"Odd", "someone", "Unknown category.", "", "Visual", "not-a-date", 0, 1},
    /* 8 */ {"Echo Out", "other", "The same name, later in the catalog.", "", "audio", "2019-09-14", 0, 1},
    /* 9 */ {"Big Clock", "nightowl", "A large clock.", "Shows the SET TIME.", "visual", "2019-08-02", 1, 0},
};
#define N ((int) (sizeof M / sizeof M[0]))

/* the view as a string of indexes, "2,1,0" */
static const char *view(int cat, int sort, const char *q) {
    static char s[64];
    int out[N], k = cmb_view(M, N, cat, sort, q, out), o = 0;
    s[0] = 0;
    for (int i = 0; i < k; i++)
        o += snprintf(s + o, sizeof s - o, "%s%d", i ? "," : "", out[i]);
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
    /* search */
    CHECK("search: empty shows every mod", same(view(CMB_ALL, CMB_SORT_NAME, ""), "2,1,0,8,3,6,7,4,9,5"));
    CHECK("search: by author, any case", same(view(CMB_ALL, CMB_SORT_NAME, "DUB"), "0,3"));
    CHECK("search: by name", same(view(CMB_ALL, CMB_SORT_NAME, "clock"), "9"));
    CHECK("search: by summary", same(view(CMB_ALL, CMB_SORT_NAME, "trim"), "2"));
    CHECK("search: by description", same(view(CMB_ALL, CMB_SORT_NAME, "tempo-synced"), "0"));
    CHECK("search: every word must match", same(view(CMB_ALL, CMB_SORT_NAME, "echo dub"), "0"));
    CHECK("search: words in different fields", same(view(CMB_ALL, CMB_SORT_NAME, "set clock"), "9"));
    CHECK("search: extra spaces", same(view(CMB_ALL, CMB_SORT_NAME, "  echo   dub "), "0"));
    CHECK("search: nothing", same(view(CMB_ALL, CMB_SORT_NAME, "zzz"), ""));
    /* the category filter */
    CHECK("filter: Audio", same(view(2, CMB_SORT_NAME, ""), "0,8,3"));
    CHECK("filter: Visual", same(view(0, CMB_SORT_NAME, ""), "1,9"));
    CHECK("filter: Other holds none and unknown", same(view(CMB_OTHER, CMB_SORT_NAME, ""), "6,7"));
    CHECK("filter: and the search", same(view(2, CMB_SORT_NAME, "dub"), "0,3"));
    CHECK("filter: Tools under a search that finds no tool", same(view(3, CMB_SORT_NAME, "dub"), ""));
    /* the orders; in each, mods that cannot install here go last, and ties keep catalog order */
    CHECK("sort A-Z: by name, case ignored, unusable last", same(view(CMB_ALL, CMB_SORT_NAME, ""), "2,1,0,8,3,6,7,4,9,5"));
    CHECK("sort A-Z: equal names keep catalog order", same(view(2, CMB_SORT_NAME, "echo"), "0,8"));
    CHECK("names: a number by its value", cmb_namecmp("Mod 2", "Mod 10") < 0 && cmb_namecmp("Mod 10", "Mod 9") > 0);
    CHECK("names: leading zeros do not count", cmb_namecmp("Mod 007", "Mod 7") == 0 && cmb_namecmp("Mod 08", "Mod 7") > 0);
    CHECK("names: a number before a letter, as text", cmb_namecmp("Mod 2", "Mod A") < 0);
    CHECK("names: text after equal numbers", cmb_namecmp("v2 beta", "v2 alpha") > 0 && cmb_namecmp("v2", "v2 x") < 0);
    CHECK("names: case ignored, NULL is empty", cmb_namecmp("ECHO", "echo") == 0 && cmb_namecmp(NULL, "") == 0);
    CHECK("sort Updated: newest first, undated after dated, unusable last",
          same(view(CMB_ALL, CMB_SORT_UPDATED, ""), "2,1,0,8,3,6,7,4,5,9"));
    CHECK("sort Updated: an invalid date counts as none", same(view(CMB_OTHER, CMB_SORT_UPDATED, ""), "6,7"));
    CHECK("sort Installed: installed first (unusable too), then the rest",
          same(view(CMB_ALL, CMB_SORT_INSTALLED, ""), "1,0,9,2,8,3,6,7,4,5"));
    CHECK("sort: an unknown order is A-Z", same(view(CMB_ALL, 99, ""), "2,1,0,8,3,6,7,4,9,5") &&
                                               same(view(CMB_ALL, -1, ""), "2,1,0,8,3,6,7,4,9,5"));
    /* counts */
    int c[CMB_NCAT + 1];
    cmb_counts(M, N, "", c);
    CHECK("counts: every category",
          c[0] == 2 && c[1] == 2 && c[2] == 3 && c[3] == 1 && c[CMB_OTHER] == 2 && c[CMB_NCAT] == N);
    cmb_counts(M, N, "dub", c);
    CHECK("counts: under the search", c[2] == 2 && c[CMB_NCAT] == 2 && c[0] == 0 && c[3] == 0);
    CHECK("counts: an empty catalog", (cmb_counts(M, 0, "", c), c[CMB_NCAT] == 0 && c[2] == 0));
    return ut_summary();
}
