/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * @file cm_browse.h
 * @brief Community Mods > Browse: which catalog mods show, and in what order.
 *
 * The Browse tab narrows the catalog by one category and a search, and orders it one of three
 * ways. Every input comes from the fetcher's catalog line (screen_community.c cm_load), so
 * nothing here invents data: a mod with no category is under Other, a mod with no valid
 * updated date sorts after every dated one.
 *
 * Categories: the manifest's optional `category`, one of a fixed list (the fetcher drops any
 * other value). Anything else, or none, is Other.
 *
 * Search: every word of the query (split on spaces) must appear in the name, the author, the
 * summary or the description, ignoring ASCII case. An empty query matches every mod.
 *
 * Order: by name (A to Z, numbers by value), by updated date (newest first), or installed mods first. In every
 * order a mod that cannot be installed on this deck goes after the ones that can (as the tab
 * always did), except that "installed first" keeps every installed mod at the top. Ties keep
 * the catalog's order, so the result is stable.
 *
 * Pure logic: no globals, no allocation; the caller owns every string.
 */
#ifndef AZMOD_CM_BROWSE_H
#define AZMOD_CM_BROWSE_H

#include <stddef.h>

#define CMB_NCAT 5  /* the fixed categories, Other last */
#define CMB_OTHER 4 /* no category, or one not in the list */
#define CMB_ALL (-1)
#define CMB_QMAX 48 /* the longest query the keyboard builds, terminator included */
enum {
    CMB_SORT_NAME = 0,
    CMB_SORT_UPDATED,
    CMB_SORT_INSTALLED,
    CMB_NSORT
};

/* the manifest keys, in display order (Other has none) */
static const char *const cmb_cat_key[CMB_NCAT - 1] = {"visual", "controls", "audio", "tools"};
static const char *const cmb_cat_label[CMB_NCAT] = {"Visual", "Controls", "Audio", "Tools", "Other"};
static const char *const cmb_sort_label[CMB_NSORT] = {"A-Z", "Updated", "Installed"};

typedef struct {
    const char *name, *author, *summary, *desc; /* searched */
    const char *cat;                            /* the manifest's category value, or "" */
    const char *updated;                        /* YYYY-MM-DD, or "" */
    int installed;                              /* installed on this USB */
    int usable;                                 /* can be installed on this deck */
} cmb_mod;

static inline int cmb_lower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }

/* the category index of a manifest value; NULL, "", or a value not in the list: Other */
static inline int cmb_cat_of(const char *v) {
    if (!v)
        return CMB_OTHER;
    for (int k = 0; k < CMB_NCAT - 1; k++)
        if (!__builtin_strcmp(v, cmb_cat_key[k]))
            return k;
    return CMB_OTHER;
}

/* 1 when v is a date: YYYY-MM-DD, month 01 to 12, day 01 to 31 */
static inline int cmb_date_ok(const char *v) {
    if (!v)
        return 0;
    for (int i = 0; i < 10; i++) {
        int dash = i == 4 || i == 7;
        if (dash ? v[i] != '-' : (v[i] < '0' || v[i] > '9'))
            return 0;
    }
    if (v[10])
        return 0;
    int mo = (v[5] - '0') * 10 + (v[6] - '0'), d = (v[8] - '0') * 10 + (v[9] - '0');
    return mo >= 1 && mo <= 12 && d >= 1 && d <= 31;
}

/* 1 when the n bytes at w appear in s, ignoring ASCII case; n = 0 matches */
static inline int cmb_has(const char *s, const char *w, size_t n) {
    if (!n)
        return 1;
    if (!s)
        return 0;
    for (; *s; s++) {
        size_t i = 0;
        while (i < n && s[i] && cmb_lower((unsigned char) s[i]) == cmb_lower((unsigned char) w[i]))
            i++;
        if (i == n)
            return 1;
    }
    return 0;
}

/* 1 when every word of q is in the mod's name, author, summary or description */
static inline int cmb_match(const cmb_mod *m, const char *q) {
    if (!q)
        return 1;
    while (*q) {
        while (*q == ' ')
            q++;
        size_t n = 0;
        while (q[n] && q[n] != ' ')
            n++;
        if (n && !cmb_has(m->name, q, n) && !cmb_has(m->author, q, n) &&
            !cmb_has(m->summary, q, n) && !cmb_has(m->desc, q, n))
            return 0;
        q += n;
    }
    return 1;
}

static inline int cmb_digit(char c) { return c >= '0' && c <= '9'; }

/* the names in A to Z order, ignoring ASCII case, with a run of digits compared by its value
 * ("Mod 2" before "Mod 10"; leading zeros do not count): <0, 0, >0 */
static inline int cmb_namecmp(const char *a, const char *b) {
    a = a ? a : "";
    b = b ? b : "";
    for (;;) {
        if (cmb_digit(*a) && cmb_digit(*b)) {
            while (*a == '0')
                a++;
            while (*b == '0')
                b++;
            size_t la = 0, lb = 0;
            while (cmb_digit(a[la]))
                la++;
            while (cmb_digit(b[lb]))
                lb++;
            if (la != lb)
                return la < lb ? -1 : 1;
            for (size_t i = 0; i < la; i++)
                if (a[i] != b[i])
                    return a[i] - b[i];
            a += la;
            b += lb;
            continue;
        }
        int x = cmb_lower((unsigned char) *a), y = cmb_lower((unsigned char) *b);
        if (x != y || !x)
            return x - y;
        a++;
        b++;
    }
}

/* <0 when a goes before b in `sort` order, >0 after, 0 a tie */
static inline int cmb_cmp(const cmb_mod *a, const cmb_mod *b, int sort) {
    if (sort == CMB_SORT_INSTALLED && !a->installed != !b->installed)
        return a->installed ? -1 : 1;
    if (!a->usable != !b->usable)
        return a->usable ? -1 : 1;
    if (sort == CMB_SORT_UPDATED) {
        int da = cmb_date_ok(a->updated), db = cmb_date_ok(b->updated);
        if (da != db)
            return da ? -1 : 1;
        if (da) {
            int c = __builtin_strcmp(b->updated, a->updated); /* newest first */
            if (c)
                return c;
        }
    }
    return cmb_namecmp(a->name, b->name);
}

/* 1 when mod m shows under category `cat` (CMB_ALL: every one) and the query */
static inline int cmb_shown(const cmb_mod *m, int cat, const char *q) {
    return (cat == CMB_ALL || cmb_cat_of(m->cat) == cat) && cmb_match(m, q);
}

/* The mods to show, in order: their indexes into m[0..n) in out[] (room for n); returns how
 * many. An unknown sort is taken as by name. */
static inline int cmb_view(const cmb_mod *m, int n, int cat, int sort, const char *q, int *out) {
    if (sort < 0 || sort >= CMB_NSORT)
        sort = CMB_SORT_NAME;
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (!cmb_shown(&m[i], cat, q))
            continue;
        int j = k++;
        while (j > 0) { /* insertion: stable, and n is small (the catalog's cap) */
            int c = cmb_cmp(&m[out[j - 1]], &m[i], sort);
            if (c < 0 || (c == 0 && out[j - 1] < i))
                break;
            out[j] = out[j - 1];
            j--;
        }
        out[j] = i;
    }
    return k;
}

/* how many mods each category holds under the query; counts[CMB_NCAT] gets every one (All) */
static inline void cmb_counts(const cmb_mod *m, int n, const char *q, int counts[CMB_NCAT + 1]) {
    for (int c = 0; c <= CMB_NCAT; c++)
        counts[c] = 0;
    for (int i = 0; i < n; i++)
        if (cmb_match(&m[i], q)) {
            counts[cmb_cat_of(m[i].cat)]++;
            counts[CMB_NCAT]++;
        }
}

#endif
