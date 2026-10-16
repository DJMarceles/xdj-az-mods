/* SPDX-License-Identifier: GPL-3.0-only */
/* rs_status_banner (extracted from azmod/remote_stems.c by run.sh): which lines of
 * the Remote Stems helper's /tmp/azrs/status reach EP147's banner strip. The helper marks
 * each message on line 2 ("banner=1" / "banner=0"); a file without that line comes from an
 * older helper and is judged by its wording. Two messages the wording never matched
 * ("None of this USB's computers are on the network", "Plug this USB into AZ-MODS once")
 * must reach the strip through the marker. */
#include <string.h>
#include STATUS_INC
#include "check.h"

static int banner(const char *file, char *line_out, size_t cap) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", file);
    int r = rs_status_banner(buf);
    snprintf(line_out, cap, "%s", buf);
    return r;
}

int main(void) {
    ut_suite = "rs_status_banner";
    char l[1024];
    CHECK("marked: none of this USB's computers -> banner",
          banner("None of this USB's computers are on the network\nbanner=1\n", l, sizeof l) == 1);
    CHECK("marked: the message is line 1 only (the marker is not shown)",
          !strcmp(l, "None of this USB's computers are on the network"));
    CHECK("marked: plug this USB into AZ-MODS -> banner",
          banner("Plug this USB into AZ-MODS once, with Remote Stems on\nbanner=1\n", l, sizeof l) == 1);
    CHECK("marked: not paired -> banner",
          banner("Not paired with Studio Mac: plug this USB into AZ-MODS once, with Remote Stems on\nbanner=1\n", l, sizeof l) == 1);
    CHECK("marked 0: progress stays on the row",
          banner("Making stems on Studio Mac: 37% - Song\nbanner=0\n", l, sizeof l) == 0);
    CHECK("marked 0 overrides the wording (a title containing 'failed')",
          banner("Stems ready: Failed Love\nbanner=0\n", l, sizeof l) == 0);
    CHECK("marked 0: line 1 kept", !strcmp(l, "Stems ready: Failed Love"));
    CHECK("older helper (no marker): 'failed' -> banner",
          banner("Stems failed on Studio Mac: no usable folder name\n", l, sizeof l) == 1);
    CHECK("older helper (no marker): 'not found' -> banner",
          banner("Studio Mac not found on the network\n", l, sizeof l) == 1);
    CHECK("older helper (no marker): progress -> no banner",
          banner("Sending to Studio Mac: Song\n", l, sizeof l) == 0);
    CHECK("older helper, no newline at all: wording decides",
          banner("Stems: the file is missing - Song", l, sizeof l) == 1);
    CHECK("empty file -> no banner", banner("", l, sizeof l) == 0);
    return ut_summary();
}
