/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * @file beat_jump.c
 * @brief "Beat Jump 2 First" mod: swaps which jump sizes the two BEAT JUMP pad pages perform.
 *
 * Page 1 keeps EP147's own look, state and LEDs but its pads jump 8/16/32/64; page 2
 * does the short jumps. Only what each page does is swapped. Gated by the
 * AZF_BEAT_JUMP_2_FIRST Mod Menu toggle.
 *
 * Pad action. input_devices::DeckOperationPadHandler<BeatJumpOperation> runs every jump
 * as a queued meow::AsyncTask. A pad press posts a pressPad task (run 0x2086748,
 * vt 0x2e15028), which posts the shared changeOperation task (run 0x2084ef8) that calls
 * BeatJumpHandler press 0x2082a98; the release posts releasePad (run 0x2085768,
 * vt 0x2e15058). Every task holds the op at +0x20 = { u32 len = (den<<16)|num,
 * u32 dir (1 = back) }. The hardware BEAT JUMP < > buttons post changeOperation directly
 * with the panel's size and reach BeatJumpHandler with the same caller and op layout as
 * the pads, so the swap cannot live there without changing the buttons' size: it is
 * applied to the pad's own task, before it runs, so everything downstream (including the
 * held pad EP147 records) sees one consistent op. EP147's pad sets are disjoint
 * (page 1 = 1/2 1 2 4, page 2 = 8 16 32 64), so the mapping is its own inverse.
 *
 * A release is swapped only when its own press was (bj_held), so toggling the mod with a
 * pad held cannot leave EP147 holding an op its release does not match. Each task is
 * checked by alignment and exact vtable at use. No page or deck state is consulted, so
 * it is right for every deck by construction.
 *
 * Tiles. PadBeatJumpWidget's refresh 0x1deb208 picks each tile's art as
 * pad*2 + (mode != 5)*16 from the pad-mode getter, returning to 0x1deb250; modeget_hook
 * answers 5<->6 to that caller only (BJ_TILE_RA), with that deck's own mode. With the mod
 * off both pass through untouched.
 *
 * Entry points: bj_padpress_hook(), bj_padrel_hook().
 */
#define BJ_PADPRESS_RUN EP_2086748 /* AsyncTask<pressPad lambda>::run(task)   */
#define BJ_PADPRESS_VT EP_2e15028  /*   its vtable (address point)            */
#define BJ_PADREL_RUN EP_2085768   /* AsyncTask<releasePad lambda>::run(task) */
#define BJ_PADREL_VT EP_2e15058
static uint32_t bj_swap_len(uint32_t len) {
    switch (len) {
    case 0x00020001u:
        return 0x00010008u; /* 1/2 <-> 8  */
    case 0x00010008u:
        return 0x00020001u;
    case 0x00010001u:
        return 0x00010010u; /* 1   <-> 16 */
    case 0x00010010u:
        return 0x00010001u;
    case 0x00010002u:
        return 0x00010020u; /* 2   <-> 32 */
    case 0x00010020u:
        return 0x00010002u;
    case 0x00010004u:
        return 0x00010040u; /* 4   <-> 64 */
    case 0x00010040u:
        return 0x00010004u;
    default:
        return len; /* anything else: EP147's own */
    }
}
/* the task's op len, when task really is the expected EP147 task; NULL otherwise. Both hooks
 * are the task's own run(), on EP147's task thread: `task` is live for the call, plain loads. */
static uint32_t *bj_task_len(long task, uintptr_t vt) {
    uintptr_t v;
    if (!task || (task & 7) || !own_q((uintptr_t) task, &v) || v != vt)
        return NULL;
    if (!own_q((uintptr_t) task + 0x20, &v))
        return NULL;
    return (uint32_t *) ((uintptr_t) task + 0x20);
}
/* presses whose op we swapped, by (pad handler, swapped op): their releases get
 * the same swap. Tasks run on EP147's one task thread; the lock is for safety. */
#define BJ_HELD 16
static struct {
    uintptr_t h;
    uint64_t op;
} bj_held[BJ_HELD];
static pthread_mutex_t bj_held_mx = PTHREAD_MUTEX_INITIALIZER;
static void (*g_orig_bj_padpress)(long);
static void (*g_orig_bj_padrel)(long);
static void bj_padpress_hook(long task) {
    uint32_t *len =
        az_on(AZF_BEAT_JUMP_2_FIRST) ? bj_task_len(task, BJ_PADPRESS_VT) : NULL;
    uintptr_t h;
    if (len && own_q((uintptr_t) task + 0x18, &h)) { /* h: an identity token only */
        uint32_t sw = bj_swap_len(*len);
        if (sw != *len) {
            *len = sw; /* this pad task's own op */
            uint64_t op = *(uint64_t *) len;
            pthread_mutex_lock(&bj_held_mx);
            int i, f = -1;
            for (i = 0; i < BJ_HELD; i++) {
                if (bj_held[i].h == h && bj_held[i].op == op) {
                    f = i;
                    break;
                }
                if (f < 0 && !bj_held[i].h)
                    f = i;
            }
            if (f >= 0) {
                bj_held[f].h = h;
                bj_held[f].op = op;
            }
            pthread_mutex_unlock(&bj_held_mx);
        }
    }
    g_orig_bj_padpress(task);
}
static void bj_padrel_hook(long task) {
    uint32_t *len = bj_task_len(task, BJ_PADREL_VT); /* not gated on the mod: follow the press */
    uintptr_t h;
    if (len && own_q((uintptr_t) task + 0x18, &h)) { /* h: an identity token only */
        uint32_t w[2] = {bj_swap_len(len[0]), len[1]};
        uint64_t op;
        memcpy(&op, w, 8);
        pthread_mutex_lock(&bj_held_mx);
        for (int i = 0; i < BJ_HELD; i++)
            if (bj_held[i].h == h && bj_held[i].op == op) {
                len[0] = w[0];
                bj_held[i].h = 0;
                break;
            }
        pthread_mutex_unlock(&bj_held_mx);
    }
    g_orig_bj_padrel(task);
}
