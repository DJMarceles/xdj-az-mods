/* SPDX-License-Identifier: GPL-3.0-only */
/* Channel Input Select Memory's SETTINGS.DAT reader (guest/azmod/input_memory.h): the deck's
 * settings file, any bytes as far as the reader is concerned.
 *
 * Every value it returns must be in range or unknown, and the decision for every channel,
 * input and time must be one of the three actions, never a select for a channel saved as
 * DECK, an unknown input or Memory not on. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fuzz_common.h"
#include "input_memory.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (n > (1 << 16)) return 0;
    struct im_saved s;
    im_parse(d, n, &s);
    FZ_ASSERT(s.memory >= -1 && s.memory <= 1);
    for (int i = 0; i < IM_CHANNELS; i++) {
        FZ_ASSERT(s.input[i] >= -1 && s.input[i] <= IM_MAX_INPUT);
        for (int cur = -2; cur <= IM_MAX_INPUT + 1; cur++) {
            static const long times[] = {0, IM_SETTLE_MS - 1, IM_SETTLE_MS, IM_WINDOW_MS, IM_WINDOW_MS + 1};
            for (unsigned k = 0; k < sizeof times / sizeof times[0]; k++) {
                int a = im_decide(&s, i, cur, times[k]);
                FZ_ASSERT(a == IM_WAIT || a == IM_SELECT || a == IM_DONE);
                if (a == IM_SELECT)
                    FZ_ASSERT(s.memory == 1 && s.input[i] > IM_DECK && cur == IM_DECK &&
                              times[k] >= IM_SETTLE_MS && times[k] <= IM_WINDOW_MS);
            }
        }
    }
    return 0;
}
