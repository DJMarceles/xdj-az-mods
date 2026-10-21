/* SPDX-License-Identifier: GPL-3.0-only */
/* Column Edit's width file parser (guest/azmod/browse_edit.h be_parse): settings/azmod-colwidths
 * on the deck, so any bytes as far as the parser is concerned.
 *
 * A rejected file sets nothing. An accepted one sets only ids 1..BE_MAXID with widths in
 * [BE_MINW, BE_MAXW], as many as it reports; writing it out and reading it back gives the same
 * widths; fitting them into a header never changes the header's total width nor leaves the
 * title below BE_TITLE_MIN when it changes anything. */
#include <stdint.h>
#include <string.h>
#include "fuzz_common.h"
#include "browse_edit.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    struct be_widths w, r;
    int rc = be_parse((const char *) d, n, &w);
    int set = 0;
    for (int i = 0; i <= BE_MAXID; i++) {
        if (rc < 0)
            FZ_ASSERT(w.w[i] == 0);
        else if (w.w[i]) {
            FZ_ASSERT(i >= 1 && w.w[i] >= BE_MINW && w.w[i] <= BE_MAXW);
            set++;
        }
    }
    if (rc < 0)
        return 0;
    FZ_ASSERT(rc == set);
    char buf[BE_FILE_MAX];
    int len = be_format(&w, buf, sizeof buf);
    FZ_ASSERT(len > 0);
    FZ_ASSERT(be_parse(buf, (size_t) len, &r) == rc && !memcmp(&r, &w, sizeof w));
    struct be_col c[] = {{2, 52, 1}, {3, 78, 1}, {4, 400, 1}, {5, 345, 1}, {6, 110, 1}, {7, 90, 1}, {8, 60, 0}};
    int total = 0, after = 0;
    for (int i = 0; i < 7; i++)
        total += c[i].visible ? c[i].width : 0;
    int ch = be_fit(c, 7, &w, 4);
    for (int i = 0; i < 7; i++)
        after += c[i].visible ? c[i].width : 0;
    FZ_ASSERT(after == total);
    if (ch)
        FZ_ASSERT(c[2].width >= BE_TITLE_MIN);
    return 0;
}
