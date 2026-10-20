/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * @file spin_back.c
 * @brief Extended Lead-In: the jog spins back far past the start of a track.
 *
 * EP147 lets a deck's position go only 2 s before the track: player_basis::PlayerCore keeps
 * that pre-roll in milliseconds, and the position a jog or scratch asks for is clamped to
 * [pre-roll x sample rate / 1000, end] by dj_player::PlayStartStopStrategy before it is used.
 * The same bound decides whether a deck may play (a deck sitting at the bound does not start).
 * With the row on, the pre-roll of the deck being moved is set to SB_CAP_MS just before
 * EP147's clamp runs, so the clamp and everything else that reads the bound use the longer
 * one; with the row off it goes back to the stock 2 s. Before the track a deck plays silence,
 * as it does in the stock 2 s, and runs into the track when it gets there.
 *
 * Entry points: install_spin_back.
 */
/* EP147:
 *   - PlayStartStopStrategy's jog clamp FUN_014a3e10(this, ..., position, readable, flag):
 *     the deck's PlayerCore at this+0x18; it computes the lower bound from that core, clamps
 *     the requested position and passes it on. It runs on the audio thread while a jog moves.
 *   - PlayerCore (vtable 0x2874158): the pre-roll at +0x2f0, int64 ms (-2000 on every player
 *     EP147 builds), read as ms x sample rate (+0x68) / 1000 by the clamp and by the play
 *     gate FUN_014a3f00. */
#define SB_CLAMP EP_14a3e10
#define SB_CORE_VT EP_2874158 /* player_basis::PlayerCore */
#define SB_CORE 0x18          /* the strategy's PlayerCore */
#define SB_PREROLL 0x2f0      /* PlayerCore: pre-roll before the track, ms (int64) */
#define SB_STOCK_MS (-2000L)
#define SB_CAP_MS (-300000L) /* 5 minutes: past any practical spin, far from any overflow */

/* The pre-roll to leave in place: the cap with the row on, stock with it off. Only the two
 * known values are ever replaced, so a player EP147 built with another pre-roll is left as
 * it is. Returns `cur` when nothing should change. */
static long sb_want(long cur, int on) {
    if (cur != SB_STOCK_MS && cur != SB_CAP_MS)
        return cur;
    return on ? SB_CAP_MS : SB_STOCK_MS;
}
/* The strategy's PlayerCore, or 0: aligned, readable and a PlayerCore. */
static uintptr_t sb_core(long strategy) {
    uintptr_t core = 0, vt = 0;
    if (!strategy || (strategy & 7) || !own_q((uintptr_t) strategy + SB_CORE, &core) || !core ||
        (core & 7) || !own_q(core, &vt) || vt != SB_CORE_VT)
        return 0;
    return core;
}
/* Audio thread: no system calls, one atomic load for the row. */
static void sb_apply(long strategy) {
    uintptr_t core = sb_core(strategy), cur = 0;
    if (!core || !own_q(core + SB_PREROLL, &cur))
        return;
    long want = sb_want((long) cur, az_value(AZF_EXTENDED_LEAD_IN) != 0);
    if (want != (long) cur)
        __atomic_store_n((long *) (core + SB_PREROLL), want, __ATOMIC_RELAXED);
}
static void (*g_o_sb_clamp)(long, long, long, long, long);
static void sb_clamp_hook(long strategy, long a1, long pos, long readable, long flag) {
    sb_apply(strategy);
    g_o_sb_clamp(strategy, a1, pos, readable, flag);
}
static void install_spin_back(void) {
    int a = azm_install_hook(SB_CLAMP, (void *) sb_clamp_hook, (void **) &g_o_sb_clamp);
    FILE *f = azlog_open("/tmp/azmod-spinback.log", "a");
    if (f) {
        fprintf(f, "[%ld] spin back: hook clamp=%d\n", (long) time(NULL), a);
        fclose(f);
    }
}
