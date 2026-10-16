/* SPDX-License-Identifier: GPL-3.0-only */
/* The panel-bus read() path (guest/azmod/bus_interpose.c, the read-path region and crc16_x25,
 * extracted by tools/fuzz/lib.sh exactly as busread-trace.sh does), over the stand-ins that
 * tools/unit-tests/busread_trace_test.c provides for the rest of azmod. Frames come from the
 * hardware; the handlers must survive any bytes, any length and any read size.
 *
 * Input: byte 0 picks the fd (mixer / ErP / another bus) and the surroundings (menu, drag,
 * drum roll, stems), byte 1 the read size, bytes 2..9 the bus settings word, then the frame
 * (at most 160 bytes). The caller's buffer is allocated at exactly the read size, so a write
 * past it is an ASan report. */
#include <stdint.h>
#include <stddef.h>
#define main bustrace_main
#include "busread_trace_test.c"
#undef main
#include "fuzz_common.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    static int quiet;
    if (!quiet) { /* the stand-ins print their calls: not wanted here */
        quiet = 1;
        if (!freopen("/dev/null", "w", stdout)) abort();
    }
    if (n < 10) return 0;
    uint8_t sel = d[0], rsz = d[1];
    uint64_t cfg = 0;
    for (int i = 0; i < 8; i++) cfg |= (uint64_t) d[2 + i] << (8 * i);
    d += 10;
    n -= 10;
    g_cfg = cfg;
    int fd = (sel & 3) == 0 ? g_mixer_fd : (sel & 3) == 1 ? g_erp_fd
                                       : (sel & 3) == 2   ? g_bus_fd[1]
                                                          : 21;
    g_menu_open = sel >> 2 & 1;
    g_menu_focused = sel >> 3 & 1;
    g_drag_on = sel >> 4 & 1;
    g_drum_arm = sel >> 5 & 1;
    g_srcrow_active = sel >> 6 & 1;
    g_wave_forcestems = sel >> 7 & 1;
    for (int i = 0; i < 4; i++) g_erp_active[i] = g_drum_held[i] = (sel >> i) & 1;
    clock_ms += 17;
    g_overlay_beat++;
    frame_len = n < sizeof frame ? n : sizeof frame;
    memcpy(frame, d, frame_len);
    size_t want = rsz;
    unsigned char *buf = malloc(want ? want : 1);
    ssize_t r = bus_read(fd, buf, want);
    FZ_ASSERT(r <= (ssize_t) want);
    free(buf);
    return 0;
}
