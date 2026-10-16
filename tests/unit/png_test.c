/* SPDX-License-Identifier: GPL-3.0-only */
/* cmi_png_ok (community_icons.c): what may reach EP147's PNG decoder from a repository's icon.
 * Fixture files are written by run.sh; each must get the expected verdict. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "check.h"
#include PNG_INC

static unsigned char g_out[1 << 20];
static int g_outn;
static int verdict(const char *path) {   /* 1 accepted (g_out = what JUCE gets), 0 refused */
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int n = (int) fread(g_out, 1, sizeof g_out, f);
    fclose(f);
    g_outn = cmi_png_ok(g_out, n);
    return g_outn > 0;
}
static int same_as(const char *path) {   /* the accepted bytes equal this file */
    static unsigned char b[1 << 20];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int n = (int) fread(b, 1, sizeof b, f);
    fclose(f);
    return n == g_outn && !memcmp(b, g_out, n);
}
int main(int argc, char **argv) {
    ut_suite = "png";
    const char *dir = argc > 1 ? argv[1] : ".";
    struct {
        const char *file;
        int want;
        const char *what;
    } c[] = {
        {"rgba256.png", 1, "256x256 RGBA icon accepted"},
        {"rgb64.png", 1, "64x64 RGB icon accepted"},
        {"pal32.png", 1, "32x32 palette icon accepted"},
        {"badcrc.png", 0, "a chunk with a wrong CRC refused"},
        {"trailing.png", 0, "bytes after IEND refused"},
        {"interlaced.png", 0, "interlaced refused"},
        {"depth16.png", 0, "16-bit samples refused"},
        {"editor.png", 1, "editor metadata (iCCP tEXt tIME bKGD zTXt) accepted"},
        {"critical.png", 0, "an unknown critical chunk refused"},
        {"big1024.png", 0, "1024x1024 refused (512 max)"},
        {"noiend.png", 0, "no IEND refused"},
        {"lenover.png", 0, "a chunk length past the end refused"},
        {"twoihdr.png", 0, "a second IHDR refused"},
        {"notpng.png", 0, "not a PNG refused"},
    };
    for (size_t k = 0; k < sizeof c / sizeof c[0]; k++) {
        char p[512];
        snprintf(p, sizeof p, "%s/%s", dir, c[k].file);
        CHECK(c[k].what, verdict(p) == c[k].want);
    }
    {   /* the metadata is gone and the picture is untouched: exactly the plain icon */
        char p[512], q[512];
        snprintf(p, sizeof p, "%s/editor.png", dir);
        snprintf(q, sizeof q, "%s/editor-plain.png", dir);
        CHECK("editor metadata dropped before the decoder, image chunks byte-identical", verdict(p) == 1 && same_as(q));
        snprintf(p, sizeof p, "%s/rgba256.png", dir);
        CHECK("a plain icon passes through unchanged (CONTROL)", verdict(p) == 1 && same_as(p));
    }
    return ut_summary();
}
