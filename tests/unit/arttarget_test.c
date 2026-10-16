/* SPDX-License-Identifier: GPL-3.0-only */
/* az_art_target_len (azmod.c) + ep147png.h: where art read from a stick may be written.
 * A target must be the first byte of one of the running build's PNG images and the blob a PNG
 * no longer than that image. Every shipped art set must still be accepted.
 *
 *   arttarget_test <set130-dir> [<set200-dir>]
 * Each dir holds N.bin + N.addr (1.30) or N.addr.2.00 (2.00): the art sets the release ships. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ep147png.h"
static int g_ep147_build;
#include ART_INC

static int pass, fail;
static void check(const char *what, int ok) {
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok)
        pass++;
    else
        fail++;
}
static unsigned char png[65536];
static size_t fake_png(size_t n) { /* a PNG signature and n bytes in all */
    memset(png, 0x11, sizeof png);
    memcpy(png, AZ_PNG_SIG, 8);
    return n;
}
/* every N.bin of a set at its N.addr<sfx>, under `build` -> number accepted, -1 when the set
 * is missing or empty */
static int set_accepted(const char *dir, const char *sfx, int build, int *total) {
    g_ep147_build = build;
    int ok = 0, n = 0;
    for (;; n++) {
        char bp[1024], ap[1024];
        snprintf(bp, sizeof bp, "%s/%d.bin", dir, n);
        snprintf(ap, sizeof ap, "%s/%d.addr%s", dir, n, sfx);
        FILE *b = fopen(bp, "rb"), *a = fopen(ap, "r");
        if (!b || !a) {
            if (b) fclose(b);
            if (a) fclose(a);
            break;
        }
        unsigned long va = 0;
        int got = fscanf(a, "%lx", &va);
        size_t len = fread(png, 1, sizeof png, b);
        fclose(a);
        fclose(b);
        if (got == 1 && az_art_target_len((uintptr_t) va, png, len))
            ok++;
    }
    *total = n;
    return n ? ok : -1;
}

int main(int argc, char **argv) {
    const struct ep147_png *im = &EP147_PNG_V130[100]; /* an arbitrary image, mid-table */
    uintptr_t va = im->va;
    g_ep147_build = EPB_V130;
    check("1.30: a PNG blob at an image's first byte, exactly its length, is accepted",
          az_art_target_len(va, png, fake_png(im->len)) == im->len);
    check("1.30: a shorter PNG blob at an image's first byte is accepted",
          az_art_target_len(va, png, fake_png(64)) == im->len);
    check("1.30: one byte longer than the image is refused",
          az_art_target_len(va, png, fake_png(im->len + 1)) == 0);
    check("1.30: an address inside the image (+8, where a planted fake PNG would sit) is refused",
          az_art_target_len(va + 8, png, fake_png(32)) == 0);
    check("1.30: an address one byte before the image is refused",
          az_art_target_len(va - 1, png, fake_png(32)) == 0);
    check("1.30: the gap after the image is refused (not an image start)",
          im->va + im->len != im[1].va ? az_art_target_len(im->va + im->len, png, fake_png(32)) == 0 : 1);
    /* a vtable: 1.30 DevicelessLoginWidgetController's at 0x2a4e668 (crash_guards.c), in .rodata */
    check("1.30: a vtable address in .rodata is refused", az_art_target_len(0x2a4e668, png, fake_png(32)) == 0);
    fake_png(64);
    png[0] = 0;
    check("1.30: a blob that is not a PNG is refused at an image start", az_art_target_len(va, png, 64) == 0);
    g_ep147_build = EPB_UNKNOWN;
    check("unknown build: an image start of 1.30 is refused (no table)",
          az_art_target_len(va, png, fake_png(64)) == 0);
    g_ep147_build = EPB_V200;
    int is200 = ep147png_len(EPB_V200, va) != 0;
    check("2.00: a 1.30 image address is refused (unless it happens to start a 2.00 image too)",
          is200 || az_art_target_len(va, png, fake_png(64)) == 0);
    const struct ep147_png *i2 = &EP147_PNG_V200[100];
    check("2.00: a PNG blob at one of its own image starts is accepted",
          az_art_target_len(i2->va, png, fake_png(i2->len)) == i2->len);

    if (argc < 2) {
        printf("NOTE  no art set given: the shipped sets' acceptance not checked here\n");
        printf("passed %d failed %d\n", pass, fail);
        return fail != 0;
    }
    int total = 0, ok = set_accepted(argv[1], "", EPB_V130, &total);
    char m[256];
    snprintf(m, sizeof m, "the shipped 1.30 art set: %d of %d blobs accepted", ok, total);
    check(m, ok > 0 && ok == total);
    if (argc > 2) {
        ok = set_accepted(argv[2], ".2.00", EPB_V200, &total);
        snprintf(m, sizeof m, "the shipped 2.00 art set: %d of %d blobs accepted", ok, total);
        check(m, ok > 0 && ok == total);
    } else
        printf("NOTE  no 2.00 art set given: its acceptance not checked here\n");
    printf("passed %d failed %d\n", pass, fail);
    return fail != 0;
}
