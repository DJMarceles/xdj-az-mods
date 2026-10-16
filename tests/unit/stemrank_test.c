/* SPDX-License-Identifier: GPL-3.0-only */
/* stem_folder_rank (extracted from azstem/stem_playback.c by run.sh): which
 * STEMS/ folder names azstem accepts for a track key, and their rank. The accepted
 * names are exactly what the desktop app writes: "<key>", "<key>_", "<key>-<8 hex>".
 * Anything else must be rejected (-1), so a look-alike folder never plays as this track. */
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include STEMRANK_INC
#include "check.h"

int main(void) {
    ut_suite = "stem_folder_rank";
    const char *k = "Basic - Sammy Virji";
    CHECK("exact name -> 0", stem_folder_rank("Basic - Sammy Virji", k) == 0);
    CHECK("case-insensitive exact -> 0", stem_folder_rank("basic - SAMMY virji", k) == 0);
    CHECK("trailing underscore -> 1", stem_folder_rank("Basic - Sammy Virji_", k) == 1);
    CHECK("-<8 hex> (lower) -> 2", stem_folder_rank("Basic - Sammy Virji-1a2b3c4d", k) == 2);
    CHECK("-<8 hex> (upper) -> 2", stem_folder_rank("Basic - Sammy Virji-1A2B3C4D", k) == 2);
    CHECK("non-hex suffix -b1ue0011 rejected", stem_folder_rank("Basic - Sammy Virji-b1ue0011", k) == -1);
    CHECK("7 hex rejected", stem_folder_rank("Basic - Sammy Virji-1a2b3c4", k) == -1);
    CHECK("9 hex rejected", stem_folder_rank("Basic - Sammy Virji-1a2b3c4d5", k) == -1);
    CHECK("double underscore rejected", stem_folder_rank("Basic - Sammy Virji__", k) == -1);
    CHECK("longer title (prefix only) rejected", stem_folder_rank("Basic - Sammy Virji (Extended)", k) == -1);
    CHECK("different track rejected", stem_folder_rank("Basic - Someone Else", k) == -1);
    CHECK("shorter than key rejected", stem_folder_rank("Basic", k) == -1);
    CHECK("empty key rejected", stem_folder_rank("anything", "") == -1);
    return ut_summary();
}
