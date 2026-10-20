/* SPDX-License-Identifier: GPL-3.0-only */
/* Which MENU panel refuses its first stock button while Playlist Editing is on
 * (guest/azmod/menu_popup_gate.h): only the PLAYLIST list, where stock ALL DELETE removes every
 * playlist and folder on the USB. The HISTORY list's ALL DELETE clears the play history and
 * stays stock. */
#include "menu_popup_gate.h"
#include "check.h"

int main(void) {
    ut_suite = "menu first button";
    CHECK("PLAYLIST list (1): ALL DELETE is refused", mp_b1_refused(1));
    CHECK("HISTORY list (0): ALL DELETE stays stock", !mp_b1_refused(0));
    CHECK("track row (2): PLAYED stays stock", !mp_b1_refused(2));
    CHECK("tag list (3): its first button stays stock", !mp_b1_refused(3));
    CHECK("the synthesized ADD menu (8): nothing refused", !mp_b1_refused(8));
    CHECK("no stock menu (-1): nothing refused", !mp_b1_refused(-1));
    return ut_summary();
}
