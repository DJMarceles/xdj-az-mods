/* SPDX-License-Identifier: GPL-3.0-only */
/* cmask_test.c: the Mod Menu's Remove buttons ask in the core's yes/no dialog (menu.c cm_ask).
 * The code under test is extracted from menu.c by run.sh (ASK_INC); azcm_confirm, cm_req and
 * cm_busy are stubs that record what the menu asked for and what it sent.
 * Properties: nothing is sent before the answer; yes sends the request captured when asked,
 * once; no sends nothing; a second press while a question is open neither asks again nor
 * retargets the first; a busy core at the answer sends nothing; a refused dialog leaves no
 * question open. */
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(name, cond)                       \
    do {                                        \
        if (cond)                               \
            printf("PASS  cm_ask: %s\n", name); \
        else {                                  \
            printf("FAIL  cm_ask: %s\n", name); \
            fails++;                            \
        }                                       \
    } while (0)

/* ---- stubs ---- */
static int g_busy, g_confirm_rc, g_asked, g_sent;
static char g_title[64], g_text[128], g_yes[32], g_req[300];
static void (*g_fn)(int, void *);
static void *g_user;
int azcm_confirm(const char *title, const char *text, const char *yes, void (*fn)(int, void *),
                 void *user) {
    g_asked++;
    snprintf(g_title, sizeof g_title, "%s", title);
    snprintf(g_text, sizeof g_text, "%s", text);
    snprintf(g_yes, sizeof g_yes, "%s", yes);
    if (g_confirm_rc == 0) {
        g_fn = fn;
        g_user = user;
    }
    return g_confirm_rc;
}
/* Real functions with menu.c's signatures: the extracted forward declarations redeclare them. */
static void cm_req(const char *fmt, const char *a, const char *b) {
    (void) b;
    g_sent++;
    snprintf(g_req, sizeof g_req, fmt, a);
}
static int cm_busy(void) { return g_busy; }

#include ASK_INC

static void answer(int yes) {
    void (*fn)(int, void *) = g_fn;
    g_fn = 0;
    if (fn)
        fn(yes, g_user);
}
static void reset(void) {
    g_busy = g_confirm_rc = g_asked = g_sent = 0;
    g_req[0] = 0;
    g_fn = 0;
    memset(&g_cm_ask, 0, sizeof g_cm_ask);
}

int main(void) {
    reset();
    cm_ask("Remove mod?", "Alpha is deleted from this USB.", "remove %s", "alpha");
    CHECK("a press asks once", g_asked == 1);
    CHECK("the dialog says what it removes", !strcmp(g_title, "Remove mod?") &&
                                                 strstr(g_text, "Alpha") && !strcmp(g_yes, "Remove"));
    CHECK("nothing is sent before the answer", g_sent == 0);
    answer(1);
    CHECK("yes sends the request once", g_sent == 1 && !strcmp(g_req, "remove alpha"));

    reset();
    cm_ask("Remove mod?", "Alpha", "remove %s", "alpha");
    answer(0);
    CHECK("no sends nothing", g_sent == 0);
    cm_ask("Remove mod?", "Alpha", "remove %s", "alpha");
    CHECK("after an answer a press asks again", g_asked == 2);

    reset();
    cm_ask("Remove mod?", "Alpha", "remove %s", "alpha");
    cm_ask("Remove repository?", "Beta", "delrepo %s", "https://beta");
    CHECK("a second press while a question is open does not ask again", g_asked == 1);
    answer(1);
    CHECK("the open question keeps its own target", g_sent == 1 && !strcmp(g_req, "remove alpha"));

    reset();
    cm_ask("Remove mod?", "Alpha", "remove %s", "alpha");
    g_busy = 1;
    answer(1);
    CHECK("yes while the core is busy sends nothing", g_sent == 0);

    reset();
    g_confirm_rc = -1; /* another question (a mod's) is already up */
    cm_ask("Remove mod?", "Alpha", "remove %s", "alpha");
    CHECK("a refused dialog leaves no question open", !g_cm_ask.open);
    g_confirm_rc = 0;
    cm_ask("Remove mod?", "Alpha", "remove %s", "alpha");
    CHECK("so the next press can ask", g_asked == 2 && g_cm_ask.open);
    answer(1);
    CHECK("and its yes is sent", g_sent == 1 && !strcmp(g_req, "remove alpha"));

    reset();
    char longarg[400];
    memset(longarg, 'r', sizeof longarg - 1);
    longarg[sizeof longarg - 1] = 0;
    cm_ask("Remove repository?", "R", "delrepo %s", longarg);
    answer(1);
    CHECK("an over-long target is truncated, never overflowed", g_sent == 1 && strlen(g_req) < 300);

    return fails ? 1 : 0;
}
