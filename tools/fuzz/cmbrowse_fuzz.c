/* SPDX-License-Identifier: GPL-3.0-only */
/* Community Mods > Browse (guest/azmod/cm_browse.h): the category and updated values come from a
 * mod's manifest and repository index through the fetcher's catalog, so any bytes.
 *
 * Input: byte 0 picks the category (byte 0 % 6: All, then the five) and the order (byte 0 / 10,
 * including orders that do not exist). The rest is split at '\n' into records, one per mod, its
 * fields split at '|': name, category, updated, installed, usable, downloads.
 *
 * Every category value maps into the list; an accepted date has the YYYY-MM-DD form; the view is
 * exactly the mods the category picks, each once, in cmb_cmp order with ties in catalog order;
 * the counts add up to the mods. A download count is read only from 1 to 13
 * digits, and its text is digits with a comma every three. Home's shelves are in shelf order,
 * each at least CMB_SHELF_MIN and at most CMB_SHELF_MAX mods, each a mod the shelf picks,
 * once, in the shelf's order. A mod page's "updated" text is empty exactly when the date is
 * not accepted, and always terminated. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fuzz_common.h"
#include "cm_browse.h"

#define FZ_MODS 64
#define FZ_LEN 4096

static char fz_buf[FZ_LEN + 1];

/* the next field of a record, cut at '|' (in place); "" past the end */
static char *fz_field(char **p) {
    char *f = *p;
    if (!f)
        return "";
    char *e = strchr(f, '|');
    if (e) {
        *e = 0;
        *p = e + 1;
    } else
        *p = NULL;
    return f;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (n < 1 || n > FZ_LEN) return 0;
    int cat = d[0] % (CMB_NCAT + 1) - 1, sort = d[0] / 10 - 1;
    memcpy(fz_buf, d + 1, n - 1);
    fz_buf[n - 1] = 0;
    cmb_mod m[FZ_MODS];
    int nm = 0;
    char *line = fz_buf;
    while (line && nm < FZ_MODS) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *p = line;
        cmb_mod *x = &m[nm++];
        x->name = fz_field(&p);
        x->cat = fz_field(&p);
        x->updated = fz_field(&p);
        x->installed = fz_field(&p)[0] == '1';
        x->usable = fz_field(&p)[0] == '1';
        const char *dl = fz_field(&p);
        long long v = -1;
        x->counted = cmb_dl_parse(dl, &v);
        x->downloads = x->counted ? v : 0;
        if (x->counted) {
            size_t L = strlen(dl);
            FZ_ASSERT(L >= 1 && L <= 13 && v >= 0);
            for (size_t i = 0; i < L; i++) FZ_ASSERT(dl[i] >= '0' && dl[i] <= '9');
        } else
            FZ_ASSERT(v == -1);
        line = next;
    }
    for (int i = 0; i < nm; i++) {
        int c = cmb_cat_of(m[i].cat);
        FZ_ASSERT(c >= 0 && c < CMB_NCAT);
        FZ_ASSERT(c == CMB_OTHER || strlen(m[i].cat) <= 8);
        if (cmb_date_ok(m[i].updated)) {
            const char *u = m[i].updated;
            FZ_ASSERT(strlen(u) == 10 && u[4] == '-' && u[7] == '-');
        }
    }
    int out[FZ_MODS], seen[FZ_MODS] = {0};
    int k = cmb_view(m, nm, cat, sort, out);
    int want = 0;
    for (int i = 0; i < nm; i++) want += cmb_shown(&m[i], cat);
    FZ_ASSERT(k == want);
    int s = sort >= 0 && sort < CMB_NSORT ? sort : CMB_SORT_NAME;
    for (int i = 0; i < k; i++) {
        FZ_ASSERT(out[i] >= 0 && out[i] < nm && !seen[out[i]]);
        seen[out[i]] = 1;
        FZ_ASSERT(cmb_shown(&m[out[i]], cat));
        if (i) {
            int c = cmb_cmp(&m[out[i - 1]], &m[out[i]], s);
            FZ_ASSERT(c < 0 || (c == 0 && out[i - 1] < out[i]));
        }
    }
    int cnt[CMB_NCAT + 1], sum = 0;
    cmb_counts(m, nm, cnt);
    for (int c = 0; c < CMB_NCAT; c++) {
        FZ_ASSERT(cnt[c] >= 0);
        sum += cnt[c];
    }
    FZ_ASSERT(sum == cnt[CMB_NCAT]);
    FZ_ASSERT(cmb_view(m, nm, CMB_ALL, s, out) == cnt[CMB_NCAT]);
    /* Home */
    cmb_shelf sh[CMB_NSHELF];
    int ns = cmb_shelves(m, nm, out, sh);
    FZ_ASSERT(ns >= 0 && ns <= CMB_NSHELF);
    for (int i = 0; i < ns; i++) {
        int kind = sh[i].kind, on[FZ_MODS] = {0};
        FZ_ASSERT(kind >= 0 && kind < CMB_NSHELF && (!i || sh[i - 1].kind < kind));
        FZ_ASSERT(sh[i].n >= CMB_SHELF_MIN && sh[i].n <= CMB_SHELF_MAX);
        for (int j = 0; j < sh[i].n; j++) {
            int x = sh[i].idx[j];
            FZ_ASSERT(x >= 0 && x < nm && !on[x]);
            on[x] = 1;
            FZ_ASSERT(cmb_on_shelf(&m[x], kind) && cmb_shown(&m[x], cmb_shelf_cat(kind)));
            if (j) {
                int c = cmb_cmp(&m[sh[i].idx[j - 1]], &m[x], cmb_shelf_sort(kind));
                FZ_ASSERT(c < 0 || (c == 0 && sh[i].idx[j - 1] < x));
            }
        }
    }
    /* the texts */
    char t[48];
    for (int i = 0; i < nm; i++) {
        if (m[i].counted) {
            cmb_count_text(m[i].downloads, t, sizeof t);
            size_t L = strlen(t);
            FZ_ASSERT(L >= 1 && L <= 17);
            for (size_t j = 0; j < L; j++) FZ_ASSERT((L - j) % 4 == 0 ? t[j] == ',' : t[j] >= '0' && t[j] <= '9');
        }
        long long now = (long long) (d[0] * 2654435761u % 4000000000u);
        cmb_updated_text(m[i].updated, now, t, sizeof t);
        FZ_ASSERT(strlen(t) < sizeof t && (t[0] != 0) == cmb_date_ok(m[i].updated));
    }
    return 0;
}
