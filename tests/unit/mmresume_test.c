/* SPDX-License-Identifier: GPL-3.0-only */
/* Reopening the Mod Menu when SOURCE comes back (guest/azmod/mm_resume.h): leaving SOURCE with
 * the menu up remembers it, focused or preview; the way back (SOURCE pressed from another view)
 * keeps that; SOURCE pressed with the menu up, or a real source picked, forgets it; a leave
 * sampled twice (the press, then the view hidden after the close dropped focus) keeps the
 * stronger level; SOURCE shown takes it once. Two threads updating the level at once (the bus
 * thread's presses, the message thread's hide/show) leave one of the values a press or hide can
 * write. The page (mmr_page_*): a close keeps the page it ends, an open after a resume shows it
 * once, any other open (none armed, or disarmed by a real close) resets as before.
 * MMR_HDR = the header under test. */
#include "check.h"
#include <pthread.h>
#include MMR_HDR

#define SRC 0x80
#define BROWSE 0x40
#define SEARCH 0x08

static int lvl;

/* leave SOURCE with BROWSE: the press (menu up), then the view hidden after the close */
static void leave(int open, int want, int focused) {
    mmr_press(&lvl, BROWSE, open, want, focused);
    mmr_hide(&lvl, 0, 0, 0); /* by the hide, the close has folded everything */
}

static void *presser(void *a) {
    (void) a;
    for (int i = 0; i < 20000; i++)
        mmr_press(&lvl, (i & 1) ? BROWSE : SRC, 1, 1, i & 2);
    return 0;
}
static void *hider(void *a) {
    (void) a;
    for (int i = 0; i < 20000; i++) {
        mmr_hide(&lvl, 1, 1, i & 1);
        if ((i % 7) == 0)
            (void) mmr_take(&lvl);
    }
    return 0;
}

int main(void) {
    ut_suite = "mm resume";

    lvl = 0;
    leave(1, 1, 1);
    CHECK("focused menu, BROWSE: remembered focused", lvl == MMR_FOCUSED);
    mmr_press(&lvl, SRC, 0, 0, 0);
    CHECK("SOURCE pressed from BROWSE keeps it (the way back)", lvl == MMR_FOCUSED);
    CHECK("SOURCE shown: focused comes back", mmr_take(&lvl) == MMR_FOCUSED);
    CHECK("taken once: nothing left", lvl == MMR_NONE && mmr_take(&lvl) == MMR_NONE);

    lvl = 0;
    leave(1, 1, 0);
    CHECK("preview, BROWSE: remembered preview", lvl == MMR_PREVIEW);

    lvl = 0;
    leave(0, 0, 0);
    CHECK("menu closed, BROWSE: nothing remembered", lvl == MMR_NONE);

    lvl = 0;
    mmr_press(&lvl, SRC, 1, 1, 1);
    CHECK("SOURCE with the focused menu up: nothing remembered", lvl == MMR_NONE);
    lvl = MMR_FOCUSED;
    mmr_press(&lvl, SRC, 1, 1, 0);
    CHECK("SOURCE with the preview up forgets an older level", lvl == MMR_NONE);

    lvl = 0;
    mmr_press(&lvl, SEARCH, 1, 1, 1);
    CHECK("SEARCH leaves too", lvl == MMR_FOCUSED);

    lvl = 0;
    mmr_hide(&lvl, 1, 1, 1);
    CHECK("a touch tab (only the view hidden) remembers focused", lvl == MMR_FOCUSED);
    mmr_hide(&lvl, 1, 1, 0);
    CHECK("a weaker sample of the same leave keeps focused", lvl == MMR_FOCUSED);

    lvl = MMR_FOCUSED;
    mmr_forget(&lvl);
    CHECK("a real source picked forgets it", lvl == MMR_NONE);
    lvl = 0;
    mmr_hide(&lvl, 1, 0, 0); /* the pick cleared want before EP147 hid the view */
    CHECK("a real source picked, then the view hidden: nothing remembered", lvl == MMR_NONE);

    lvl = 0;
    mmr_press(&lvl, 0, 1, 1, 1);
    CHECK("no rising button: unchanged", lvl == MMR_NONE);
    CHECK("the levels are distinct and ordered",
          MMR_NONE < MMR_PREVIEW && MMR_PREVIEW < MMR_FOCUSED);

    /* the page: 9 stands for a deep mode (the time zone list), 7 for the Community Mods view */
    int armed = 0, kept = MMR_PAGE_NONE;
    kept = mmr_page_keep(kept, 9);
    kept = mmr_page_keep(kept, MMR_PAGE_NONE); /* a second close sees the menu closed already */
    CHECK("a close keeps the page it ends; a close of a closed menu keeps it", kept == 9);
    CHECK("an open with no resume resets (no page)", mmr_page_take(&armed, kept) == MMR_PAGE_NONE);
    mmr_page_arm(&armed);
    CHECK("the open after a resume: the kept page", mmr_page_take(&armed, kept) == 9);
    CHECK("taken once: the next open resets", mmr_page_take(&armed, kept) == MMR_PAGE_NONE);
    mmr_page_arm(&armed);
    mmr_page_disarm(&armed);
    CHECK("a real close before the open (disarmed): the open resets",
          mmr_page_take(&armed, kept) == MMR_PAGE_NONE);
    kept = mmr_page_keep(kept, 7);
    mmr_page_arm(&armed);
    CHECK("the latest close's page comes back", mmr_page_take(&armed, kept) == 7);

    lvl = 0;
    pthread_t a, b;
    pthread_create(&a, 0, presser, 0);
    pthread_create(&b, 0, hider, 0);
    pthread_join(a, 0);
    pthread_join(b, 0);
    CHECK("two threads at once: a value a press or hide can write",
          lvl == MMR_NONE || lvl == MMR_PREVIEW || lvl == MMR_FOCUSED);
    return ut_summary();
}
