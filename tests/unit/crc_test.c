/* SPDX-License-Identifier: GPL-3.0-only */
/* CRC-16/X25 for panel bus frames (crc16_x25, extracted from
 * guest/azmod/bus_interpose.c by run.sh so this never tests a stale copy).
 *
 * Frames carry the CRC little-endian right after the covered bytes: mixer
 * [0:96] at 96/97, ErP [0:28] at 28/29, LED [0:0x78] at 0x78/0x79. stamp() and
 * valid() below are written the way bus_interpose.c stamps and checks them. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include CRC_INC
#include "check.h"

static void stamp(uint8_t *f, size_t n) {
    uint16_t c = crc16_x25(f, n);
    f[n] = (uint8_t) (c & 0xff);
    f[n + 1] = (uint8_t) (c >> 8);
}
static int valid(const uint8_t *f, size_t n) {
    uint16_t c = crc16_x25(f, n);
    return f[n] == (uint8_t) (c & 0xff) && f[n + 1] == (uint8_t) (c >> 8);
}

int main(void) {
    ut_suite = "crc16_x25";
    char name[128];
    CHECK("check value \"123456789\" -> 0x906E", crc16_x25((const uint8_t *) "123456789", 9) == 0x906E);
    CHECK("empty input -> 0x0000", crc16_x25((const uint8_t *) "", 0) == 0x0000);
    /* X.25 residue: CRC over data followed by its own LE CRC is a constant. */
    uint8_t r[11];
    memcpy(r, "123456789", 9);
    stamp(r, 9);
    CHECK("residue over data+crc -> 0x0F47", crc16_x25(r, 11) == 0x0F47);

    size_t lens[] = {28, 96, 0x78};
    const char *kind[] = {"ErP", "mixer", "LED"};
    for (int k = 0; k < 3; k++) {
        size_t n = lens[k];
        uint8_t f[0x7a], g[0x7a];
        for (size_t i = 0; i < n; i++) f[i] = (uint8_t) (i * 37 + 11 + k);
        stamp(f, n);
        snprintf(name, sizeof name, "%s frame: stamped frame validates", kind[k]);
        CHECK(name, valid(f, n));

        /* Every single-bit corruption, in data or in the CRC bytes, is rejected. */
        int undetected = 0;
        for (size_t i = 0; i < n + 2; i++)
            for (int b = 0; b < 8; b++) {
                memcpy(g, f, n + 2);
                g[i] ^= (uint8_t) (1u << b);
                undetected += valid(g, n);
            }
        snprintf(name, sizeof name, "%s frame: all %zu single-bit corruptions rejected", kind[k], (n + 2) * 8);
        CHECK(name, undetected == 0);

        /* An edit invalidates the frame until it is re-stamped. */
        memcpy(g, f, n + 2);
        g[n / 2] ^= 0x5a;
        snprintf(name, sizeof name, "%s frame: edited byte without re-stamp rejected", kind[k]);
        CHECK(name, !valid(g, n));
        stamp(g, n);
        snprintf(name, sizeof name, "%s frame: edited byte re-stamped accepted", kind[k]);
        CHECK(name, valid(g, n));

        /* Bus-idle garbage (all 0xFF, CRC bytes included) must not pass. */
        memset(g, 0xff, n + 2);
        snprintf(name, sizeof name, "%s frame: all-0xFF frame rejected", kind[k]);
        CHECK(name, !valid(g, n));
        memset(g, 0x00, n + 2);
        snprintf(name, sizeof name, "%s frame: all-0x00 frame rejected", kind[k]);
        CHECK(name, !valid(g, n));
    }
    /* Byte order: the low byte goes first. A frame with the bytes swapped fails. */
    uint8_t s[98];
    for (int i = 0; i < 96; i++) s[i] = (uint8_t) i;
    stamp(s, 96);
    uint8_t t = s[96];
    s[96] = s[97];
    s[97] = t;
    CHECK("mixer frame: CRC stored big-endian rejected", s[96] == s[97] || !valid(s, 96));
    return ut_summary();
}
