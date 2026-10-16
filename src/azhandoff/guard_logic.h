/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * @file guard_logic.h
 * @brief The handoff guard's decisions, kept apart from its I/O so the host tests drive them
 *
 * hg_frame() judges one frame from the panel input bus: only a mixer frame with a valid
 * CRC-16/X25 over [0:96] counts, so a bus error (an all-0xFF frame has every bit set) can
 * never fake a press. BACK is byte 33 bit 7. hg_frame() returns 1 once BACK has been held,
 * in valid frames with no gap longer than HG_GAP_MS between them, for hold_ms.
 */
#ifndef AZ_HANDOFF_GUARD_LOGIC_H
#define AZ_HANDOFF_GUARD_LOGIC_H
#include <stddef.h>
#include <stdint.h>

#define HG_MIXER_LEN 98   /* a mixer input frame: 96 bytes of state, then its CRC */
#define HG_BACK_BYTE 33
#define HG_BACK_BIT 0x80
#define HG_GAP_MS 500     /* no valid frame for this long: whatever was held is let go */

struct hg_state {
    long since_ms;   /* when the current BACK hold began, -1 = not held */
    long last_ms;    /* the last valid frame, -1 = none yet */
};
#define HG_STATE_INIT {-1, -1}

static inline uint16_t hg_crc16_x25(const uint8_t *d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
    }
    return crc ^ 0xFFFF;
}

static inline int hg_frame_valid(const uint8_t *f, long n) {
    if (n < HG_MIXER_LEN)
        return 0;
    uint16_t c = hg_crc16_x25(f, 96);
    return f[96] == (uint8_t) (c & 0xff) && f[97] == (uint8_t) (c >> 8);
}

/* One read from the bus (n bytes, n <= 0 = nothing) at now_ms. 1 = BACK held long enough. */
static inline int hg_frame(struct hg_state *s, const uint8_t *f, long n, long now_ms, long hold_ms) {
    if (s->last_ms >= 0 && now_ms - s->last_ms > HG_GAP_MS)
        s->since_ms = -1; /* the stream stopped: a hold does not survive a gap */
    if (n <= 0 || !hg_frame_valid(f, n))
        return 0;
    s->last_ms = now_ms;
    if (!(f[HG_BACK_BYTE] & HG_BACK_BIT)) {
        s->since_ms = -1;
        return 0;
    }
    if (s->since_ms < 0)
        s->since_ms = now_ms;
    return now_ms - s->since_ms >= hold_ms;
}
#endif
