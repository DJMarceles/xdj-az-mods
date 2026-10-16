/* SPDX-License-Identifier: GPL-3.0-only */
/* azmod.json, the manifest of a community mod on the stick, read by the core before anything of
 * the mod runs (cm_json_ws .. cm_json_fw, extracted out of guest/azmod/community.c by run.sh).
 * One flat object; the fuzz harness proves it never reads out of bounds, these cases name each
 * answer:
 *   - every way the shape can be wrong is refused with its own reason (not an object, a key
 *     that is not a string or has no value, an unterminated string or escape, a nested object,
 *     a list of something else or never closed, a missing , or }, a repeated key: also one
 *     repeated after many others, so the core never reads a different value than the fetcher);
 *   - a well-formed manifest gives each key's value, absent keys their defaults (strings
 *     empty, numbers and booleans the default, a boolean that is neither true nor false too);
 *   - \n in a string reads as a space; a string cut by an escape at the very end is unreadable;
 *   - the firmware list: this build's tag, "any", neither, or unreadable. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"

#include CMJSON_INC

/* the refusal reason for js, or "" when the shape is accepted */
static const char *why_of(const char *js) {
    const char *why = "?";
    return cm_json_walk(js, NULL, &why) ? "" : why;
}
static int refused(const char *js, const char *reason) { return strstr(why_of(js), reason) != NULL; }

int main(void) {
    ut_suite = "azmod.json";
    /* ---- the shape */
    CHECK("accepted: an empty object", !*why_of("{}") && !*why_of("  { \n }  "));
    CHECK("accepted: strings, numbers, true/false/null, lists of strings",
          !*why_of("{\"id\":\"a-b\",\"api\":1,\"hooks\":false,\"x\":null,\"firmware\":[\"any\", \"ep147-1\"],\"e\":[]}"));
    CHECK("refused: not an object", refused("[1]", "not a JSON object") && refused("", "not a JSON object"));
    CHECK("refused: a key that is not a string", refused("{id:1}", "a key is not a string"));
    CHECK("refused: an unterminated key", refused("{\"id", "an unterminated string"));
    CHECK("refused: an escape at the very end of a key", refused("{\"id\\", "an unterminated string"));
    CHECK("refused: a key with no value", refused("{\"id\" 1}", "a key has no value"));
    CHECK("refused: an unterminated value", refused("{\"id\":\"abc", "an unterminated string"));
    CHECK("refused: a value ending in an escape", refused("{\"id\":\"ab\\", "an unterminated string"));
    CHECK("refused: a nested object", refused("{\"a\":{\"b\":1}}", "nested object"));
    CHECK("refused: a list holding a number", refused("{\"firmware\":[1]}", "something other than text"));
    CHECK("refused: a list holding an object", refused("{\"firmware\":[{}]}", "something other than text"));
    CHECK("refused: a list never closed", refused("{\"firmware\":[\"a\" \"b\"]}", "a list is not closed") &&
                                              refused("{\"firmware\":[\"a\"", "a list is not closed"));
    CHECK("refused: a missing , between members", refused("{\"a\":1 \"b\":2}", "expected , or }"));
    CHECK("refused: an object never closed", refused("{\"a\":1", "expected , or }"));
    CHECK("refused: a repeated key", refused("{\"id\":\"a\",\"id\":\"b\"}", "repeats a key"));
    CHECK("a key that only starts like another is not a repeat (CONTROL)", !*why_of("{\"id\":1,\"idx\":2}"));
    {
        char big[4096];
        int n = snprintf(big, sizeof big, "{\"id\":\"first\"");
        for (int i = 0; i < 60; i++)
            n += snprintf(big + n, sizeof big - n, ",\"k%02d\":1", i);
        snprintf(big + n, sizeof big - n, ",\"k55\":2}"); /* a key past the 48th, again */
        CHECK("refused: a key repeated after many others (never two readings of one file)",
              refused(big, "repeats a key") || refused(big, "too many keys"));
        char ok[4096];
        n = snprintf(ok, sizeof ok, "{\"id\":\"first\"");
        for (int i = 0; i < 40; i++)
            n += snprintf(ok + n, sizeof ok - n, ",\"k%02d\":1", i);
        snprintf(ok + n, sizeof ok - n, "}");
        CHECK("a manifest of 41 distinct keys is accepted (CONTROL)", !*why_of(ok));
    }

    /* ---- values */
    const char *m = "{\"id\":\"mod-a\",\"name\":\"Line\\none\",\"api\":2,\"neg\":-3,\"hooks\":true,"
                    "\"off\":false,\"odd\":yes,\"firmware\":[\"ep147-1\"]}";
    char s[64];
    CHECK("a string value", cm_json_str(m, "id", s, sizeof s) && !strcmp(s, "mod-a"));
    CHECK("\\n in a string reads as a space", cm_json_str(m, "name", s, sizeof s) && !strcmp(s, "Line one"));
    CHECK("a string cut to the buffer, still terminated", cm_json_str(m, "id", s, 4) && !strcmp(s, "mod"));
    CHECK("an absent string: empty, 0", !cm_json_str(m, "author", s, sizeof s) && !s[0]);
    CHECK("a number, and a negative one", cm_json_int(m, "api", 1) == 2 && cm_json_int(m, "neg", 0) == -3);
    CHECK("an absent number or a string where a number is expected: the default",
          cm_json_int(m, "min", 7) == 7 && cm_json_int(m, "id", 7) == 7);
    CHECK("true and false", cm_json_bool(m, "hooks", 0) == 1 && cm_json_bool(m, "off", 1) == 0);
    CHECK("an absent boolean, or one that is neither true nor false: the default",
          cm_json_bool(m, "nope", 5) == 5 && cm_json_bool(m, "odd", 5) == 5 && cm_json_bool(m, "api", 5) == 5);
    CHECK("a key is looked up at the top level only (a list's text is not a key)",
          cm_json_key("{\"firmware\":[\"id\"],\"id\":\"x\"}", "id") && cm_json_str("{\"firmware\":[\"id\"],\"id\":\"x\"}", "id", s, sizeof s) &&
              !strcmp(s, "x"));
    CHECK("a string ending in an escape is unreadable", !cm_json_strat("\"abc\\", s, sizeof s));

    /* ---- the firmware list */
    CHECK("firmware: this build's tag", cm_json_fw("{\"firmware\":[\"ep147-0\",\"ep147-1\"]}", "ep147-1") == 1);
    CHECK("firmware: any", cm_json_fw("{\"firmware\":[\"any\"]}", "ep147-1") == 2);
    CHECK("firmware: this tag beside any is this tag", cm_json_fw("{\"firmware\":[\"any\",\"ep147-1\"]}", "ep147-1") == 1);
    CHECK("firmware: neither", cm_json_fw("{\"firmware\":[\"ep147-0\"]}", "ep147-1") == 0 &&
                                   cm_json_fw("{\"firmware\":[]}", "ep147-1") == 0);
    CHECK("firmware: no list, or not a list: unreadable",
          cm_json_fw("{\"id\":1}", "ep147-1") == -1 && cm_json_fw("{\"firmware\":\"any\"}", "ep147-1") == -1);
    CHECK("firmware: an unknown build matches no tag, still reads any",
          cm_json_fw("{\"firmware\":[\"ep147-1\"]}", "") == 0 && cm_json_fw("{\"firmware\":[\"any\"]}", "") == 2);
    return ut_summary();
}
