/* SPDX-License-Identifier: GPL-3.0-only */
/* Channel Input Select Memory at start (guest/azmod/input_memory.h): the SETTINGS.DAT entry
 * reader and the per-channel decision. The fixture bytes follow the file's real layout:
 * name, NUL, 0x01, payload length, type byte, value. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include INPUTMEM_HDR

static int fails, checks;
#define CHECK(name, cond)               \
    do {                                \
        checks++;                       \
        if (cond)                       \
            printf("PASS  %s\n", name); \
        else {                          \
            printf("FAIL  %s\n", name); \
            fails++;                    \
        }                               \
    } while (0)

static size_t put_int(uint8_t *b, size_t o, const char *name, uint32_t v) {
    size_t L = strlen(name);
    memcpy(b + o, name, L + 1);
    o += L + 1;
    uint8_t e[7] = {0x01, 5, 0x01, (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16),
                    (uint8_t) (v >> 24)};
    memcpy(b + o, e, sizeof e);
    return o + sizeof e;
}
static size_t put_str(uint8_t *b, size_t o, const char *name, const char *val) {
    size_t L = strlen(name), V = strlen(val);
    memcpy(b + o, name, L + 1);
    o += L + 1;
    b[o++] = 0x01;
    b[o++] = (uint8_t) (V + 2);
    b[o++] = 0x05;
    memcpy(b + o, val, V + 1);
    return o + V + 1;
}
/* A file shaped like the deck's: neighbours with similar names around the input entries. */
static size_t fixture(uint8_t *b, int mem, int ch3, int ch4) {
    size_t o = 0;
    memcpy(b, "Setting\0", 8);
    o = 8;
    o = put_str(b, o, "HistoryName", "HISTORY");
    o = put_int(b, o, "InputSelect0", 0);
    o = put_int(b, o, "InputSelect1", 0);
    o = put_int(b, o, "InputSelect2", (uint32_t) ch3);
    o = put_int(b, o, "InputSelect3", (uint32_t) ch4);
    o = put_int(b, o, "InputSelectMemory", (uint32_t) mem);
    o = put_int(b, o, "JogLcdBrightness", 2);
    return o;
}

int main(void) {
    uint8_t b[512];
    struct im_saved s;

    size_t n = fixture(b, 1, 2, 3);
    im_parse(b, n, &s);
    CHECK("the deck's layout: Memory on, CH3 PHONO, CH4 Bluetooth read back",
          s.memory == 1 && s.input[0] == 0 && s.input[1] == 0 && s.input[2] == 2 && s.input[3] == 3);
    CHECK("InputSelect0 is not read as part of InputSelectMemory", im_int(b, n, "InputSelect") == -1);

    n = fixture(b, 0, 2, 0);
    im_parse(b, n, &s);
    CHECK("Memory off is read as off", s.memory == 0);

    /* malformed entries: the value stays unknown */
    n = fixture(b, 1, 2, 0);
    uint8_t *e = (uint8_t *) memmem(b, n, "InputSelect2", 13) + 13;
    e[1] = 9;
    im_parse(b, n, &s);
    CHECK("a wrong payload length leaves CH3 unknown", s.input[2] == -1 && s.input[3] == 0);
    n = fixture(b, 1, 7, 0);
    im_parse(b, n, &s);
    CHECK("an input past Bluetooth is unknown", s.input[2] == -1);
    n = fixture(b, 1, 2, 0);
    im_parse(b, n - 8, &s);
    CHECK("a file cut inside the last entry still reads the earlier ones",
          s.memory == 1 && s.input[2] == 2);
    im_parse(b, 20, &s);
    CHECK("a file cut before the inputs reads nothing", s.memory == -1 && s.input[2] == -1);
    n = fixture(b, 1, 2, 0);
    n = put_int(b, n, "InputSelect2", 1);
    im_parse(b, n, &s);
    CHECK("a name found twice is ambiguous", s.input[2] == -1);
    n = fixture(b, 1, 2, 0);
    n = put_int(b, n, "XInputSelect2", 1);
    CHECK("a name inside a longer name is not matched", im_int(b, n, "InputSelect2") == 2);
    CHECK("NULL or empty input is refused", im_int(NULL, 0, "InputSelect2") == -1 && im_int(b, 0, "x") == -1);

    /* decisions */
    n = fixture(b, 1, 2, 0);
    im_parse(b, n, &s);
    CHECK("CH3 at DECK waits while EP147 may still apply it", im_decide(&s, 2, 0, 500) == IM_WAIT);
    CHECK("CH3 at DECK after the settle time is set to PHONO", im_decide(&s, 2, 0, IM_SETTLE_MS) == IM_SELECT);
    CHECK("CH3 already PHONO is left alone", im_decide(&s, 2, 2, IM_SETTLE_MS) == IM_DONE);
    CHECK("CH3 changed by the DJ (LINE) is left alone", im_decide(&s, 2, 1, IM_SETTLE_MS) == IM_DONE);
    CHECK("after the window CH3 is the DJ's", im_decide(&s, 2, 0, IM_WINDOW_MS + 1) == IM_DONE);
    CHECK("a channel saved as DECK is never touched", im_decide(&s, 0, 0, IM_SETTLE_MS) == IM_DONE);
    s.memory = 0;
    CHECK("Memory off: nothing is applied", im_decide(&s, 2, 0, IM_SETTLE_MS) == IM_DONE);
    s.memory = -1;
    CHECK("Memory unknown: nothing is applied", im_decide(&s, 2, 0, IM_SETTLE_MS) == IM_DONE);
    s.memory = 1;
    s.input[2] = -1;
    CHECK("an unknown saved input is never applied", im_decide(&s, 2, 0, IM_SETTLE_MS) == IM_DONE);
    CHECK("a channel index out of range is refused", im_decide(&s, 4, 0, IM_SETTLE_MS) == IM_DONE &&
                                                         im_decide(&s, -1, 0, IM_SETTLE_MS) == IM_DONE);

    printf("----  input memory: %d passed, %d failed\n", checks - fails, fails);
    return fails != 0;
}
