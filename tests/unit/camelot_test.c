/* SPDX-License-Identifier: GPL-3.0-only */
/* Camelot text for the deck screen's key displays (guest/azmod/camelot_text.h): every pitch
 * class, major and minor, against the Camelot wheel; the same 24 keys through their classic
 * names (EP147's classic formatter names a minor key from its relative major's pitch class plus
 * nine semitones) against a wheel keyed by name; out-of-table and malformed pitch classes as
 * EP147's display does; the text's length and terminator. */
#include <stdio.h>
#include <string.h>
#include CAMELOT_HDR

static int fails, checks;
static void check(const char *name, int ok) {
    checks++;
    printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    fails += !ok;
}

int main(void) {
    /* pitch class 0..11 = C, Db, D, Eb, E, F, F#, G, Ab, A, Bb, B (a minor key: its relative major) */
    static const char *major[12] = {"8B", "3B", "10B", "5B", "12B", "7B", "2B", "9B", "4B", "11B", "6B", "1B"};
    static const char *minor[12] = {"8A", "3A", "10A", "5A", "12A", "7A", "2A", "9A", "4A", "11A", "6A", "1A"};
    char t[4], name[64];
    int all = 1;
    for (int p = 0; p < 12; p++) {
        int n = ct_camelot(p, 1, t);
        all &= !strcmp(t, major[p]) && n == (int) strlen(major[p]);
        n = ct_camelot(p, 0, t);
        all &= !strcmp(t, minor[p]) && n == (int) strlen(minor[p]);
        if (!all) {
            snprintf(name, sizeof name, "pitch class %d first wrong: %s", p, t);
            check(name, 0);
            break;
        }
    }
    check("all 24 keys match the Camelot wheel (C major 8B, A minor 8A, B major 1B)", all);
    /* the classic notation: the name EP147's classic formatter gives each key, looked up on a
     * wheel written by name, must be the Camelot text of the same key */
    static const char *note[12] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    static const struct {
        const char *classic, *camelot;
    } wheel[24] = {
        {"B", "1B"},
        {"G#m", "1A"},
        {"F#", "2B"},
        {"Ebm", "2A"},
        {"Db", "3B"},
        {"Bbm", "3A"},
        {"Ab", "4B"},
        {"Fm", "4A"},
        {"Eb", "5B"},
        {"Cm", "5A"},
        {"Bb", "6B"},
        {"Gm", "6A"},
        {"F", "7B"},
        {"Dm", "7A"},
        {"C", "8B"},
        {"Am", "8A"},
        {"G", "9B"},
        {"Em", "9A"},
        {"D", "10B"},
        {"Bm", "10A"},
        {"A", "11B"},
        {"F#m", "11A"},
        {"E", "12B"},
        {"C#m", "12A"},
    };
    /* the wheel's minor names spelled as the note table spells them */
    static const char *alias[][2] = {{"G#m", "Abm"}, {"Ebm", "Ebm"}, {"C#m", "Dbm"}};
    int named = 0;
    for (int p = 0; p < 12; p++)
        for (int major = 0; major < 2; major++) {
            char cl[8], want[8] = "";
            snprintf(cl, sizeof cl, "%s%s", note[major ? p : (p + 9) % 12], major ? "" : "m");
            for (int k = 0; k < 24; k++) {
                const char *w = wheel[k].classic;
                for (unsigned a = 0; a < sizeof alias / sizeof alias[0]; a++)
                    if (!strcmp(w, alias[a][0]))
                        w = alias[a][1];
                if (!strcmp(w, cl))
                    snprintf(want, sizeof want, "%s", wheel[k].camelot);
            }
            ct_camelot(p, major, t);
            named += want[0] && !strcmp(t, want);
        }
    check("all 24 keys by their classic names (Am 8A, C 8B, F#m 11A, Db 3B)", named == 24);
    ct_camelot(12, 1, t);
    check("a pitch class past the table gives 8, as EP147's display does", !strcmp(t, "8B"));
    ct_camelot(-1, 0, t);
    check("a negative pitch class gives 8", !strcmp(t, "8A"));
    int ok = 1;
    for (int p = -128; p < 128; p++) {
        memset(t, 'x', sizeof t);
        int n = ct_camelot(p, p & 1, t);
        ok &= (n == 2 || n == 3) && t[n] == 0 && (int) strlen(t) == n && (t[n - 1] == 'A' || t[n - 1] == 'B');
        if (p < 1 || p > 11)
            ok &= t[0] == '8' && n == 2;
    }
    check("every signed byte: 2 or 3 characters, terminated, A or B last; out of 1..11 gives 8", ok);
    printf("----  camelot text: %d passed, %d failed\n", checks - fails, fails);
    return fails != 0;
}
