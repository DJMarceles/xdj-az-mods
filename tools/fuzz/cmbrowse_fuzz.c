/* SPDX-License-Identifier: GPL-3.0-only */
/* Community Mods > Browse (guest/azmod/cm_browse.h): the category and updated values come from a
 * mod's manifest and repository index through the fetcher's catalog, and the search from the
 * keyboard, so any bytes.
 *
 * Input: byte 0 picks the category (byte 0 % 6: All, then the five) and the order (byte 0 / 10,
 * including orders that do not exist). The rest is split at '\n' into records: the first is the
 * search, each other one a mod, its fields split at '|': name, author, summary, description,
 * category, updated, installed, usable.
 *
 * Every category value maps into the list; an accepted date has the YYYY-MM-DD form; the view is
 * exactly the mods the category and search pick, each once, in cmb_cmp order with ties in
 * catalog order; the counts add up to the matches. */
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
    char *q = fz_buf, *line = strchr(fz_buf, '\n');
    if (line) *line++ = 0;
    while (line && nm < FZ_MODS) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *p = line;
        cmb_mod *x = &m[nm++];
        x->name = fz_field(&p);
        x->author = fz_field(&p);
        x->summary = fz_field(&p);
        x->desc = fz_field(&p);
        x->cat = fz_field(&p);
        x->updated = fz_field(&p);
        x->installed = fz_field(&p)[0] == '1';
        x->usable = fz_field(&p)[0] == '1';
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
        FZ_ASSERT(cmb_match(&m[i], "") && cmb_match(&m[i], NULL));
    }
    int out[FZ_MODS], seen[FZ_MODS] = {0};
    int k = cmb_view(m, nm, cat, sort, q, out);
    int want = 0;
    for (int i = 0; i < nm; i++) want += cmb_shown(&m[i], cat, q);
    FZ_ASSERT(k == want);
    int s = sort >= 0 && sort < CMB_NSORT ? sort : CMB_SORT_NAME;
    for (int i = 0; i < k; i++) {
        FZ_ASSERT(out[i] >= 0 && out[i] < nm && !seen[out[i]]);
        seen[out[i]] = 1;
        FZ_ASSERT(cmb_shown(&m[out[i]], cat, q));
        if (i) {
            int c = cmb_cmp(&m[out[i - 1]], &m[out[i]], s);
            FZ_ASSERT(c < 0 || (c == 0 && out[i - 1] < out[i]));
        }
    }
    int cnt[CMB_NCAT + 1], sum = 0;
    cmb_counts(m, nm, q, cnt);
    for (int c = 0; c < CMB_NCAT; c++) {
        FZ_ASSERT(cnt[c] >= 0);
        sum += cnt[c];
    }
    FZ_ASSERT(sum == cnt[CMB_NCAT]);
    FZ_ASSERT(cmb_view(m, nm, CMB_ALL, s, q, out) == cnt[CMB_NCAT]);
    return 0;
}
