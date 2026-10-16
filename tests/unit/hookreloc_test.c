/* SPDX-License-Identifier: GPL-3.0-only */
/* Inline-hook relocation rules, azmod (inline_hooks.c) and azstem (azstem.c), extracted
 * from the sources at run time (AZM_INC, STEM_INC):
 *   - an adrp is re-encoded only when its target page still fits the signed 21-bit page
 *     field (+-4 GB) from the new pc; when it does, the re-encoded adrp yields exactly the
 *     page the original did; when it does not, the "fits" test must say no (the installer
 *     then materializes the address instead of truncating it);
 *   - a function that ends (ret / br) inside the instructions a patch covers is refused,
 *     as is any PC-relative instruction other than adrp in the first four;
 *   - an adrp out of reach is materialized as MOVZ + 3x MOVK into the same register, which an
 *     independent decoder reads back as exactly the page the original produced, and the
 *     trampoline's way back (LDR X17, #8; BR X17; .quad) jumps to the address given. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "check.h"

#define dbg(...) ((void) 0)
#include AZM_INC
#include STEM_INC

/* independent decoder: the page an adrp at pc produces */
static uint64_t ref_target(uint32_t in, uint64_t pc) {
    int64_t imm = (int64_t) ((((in >> 5) & 0x7FFFFu) << 2) | ((in >> 29) & 3u));
    if (imm & (1LL << 20))
        imm -= (1LL << 21);
    return (pc & ~0xFFFull) + (uint64_t) (imm * 4096);
}
/* adrp x<rd>, <target page> placed at pc (target must be in range of pc) */
static uint32_t enc_adrp(int rd, uint64_t pc, uint64_t target) {
    int64_t imm = ((int64_t) (target & ~0xFFFull) - (int64_t) (pc & ~0xFFFull)) / 4096;
    return 0x90000000u | ((uint32_t) (imm & 3) << 29) | ((uint32_t) ((imm >> 2) & 0x7FFFF) << 5) |
           (uint32_t) rd;
}

/* independent decoder: the value MOVZ + 3x MOVK (64-bit, hw 0..3 in order) leave in their
 * register; -1 in *rd when the four words are not that sequence on one register */
static uint64_t ref_movz_movk(const uint32_t *w, int *rd) {
    uint64_t v = 0;
    *rd = (int) (w[0] & 0x1F);
    for (int i = 0; i < 4; i++) {
        uint32_t op = w[i] & 0xFF800000u; /* sf=1, opc, 100101 */
        if (op != (i ? 0xF2800000u : 0xD2800000u) || ((w[i] >> 21) & 3u) != (uint32_t) i ||
            (int) (w[i] & 0x1F) != *rd)
            *rd = -1;
        v |= (uint64_t) ((w[i] >> 5) & 0xFFFFu) << (16 * i);
    }
    return v;
}
/* independent decoder of the 16-byte jump: the target, or 0 when it is not LDR X17,#8; BR X17 */
static uint64_t ref_jump(const uint32_t *w) {
    uint64_t t;
    memcpy(&t, &w[2], 8);
    return w[0] == 0x58000051u && w[1] == 0xD61F0220u ? t : 0;
}

int main(void) {
    ut_suite = "hookreloc";
    const uint64_t G = 1ull << 32; /* adrp reach: [-4 GB, +4 GB - 4 KB] in pages */
    const uint64_t old_pc = 0x14ab0d4ull;
    const uint64_t targets[] = {0x3f84a88ull, 0x2cd4b28ull, 0x10000ull, 0x8000000ull};
    /* new pcs: near, exactly at the edges of reach from the target, and far (an mmap'd page) */
    int bad_near = 0, bad_far = 0, bad_target = 0, bad_edge = 0, n_far = 0, n_near = 0;
    for (size_t t = 0; t < sizeof targets / sizeof targets[0]; t++) {
        uint64_t tg = targets[t] & ~0xFFFull;
        uint32_t in = enc_adrp(2, old_pc, tg);
        if (azm_adrp_target(in, old_pc) != tg || adrp_target(in, old_pc) != tg)
            bad_target++;
        const uint64_t news[] = {0x1000000ull, 0x10000000ull, tg + G - 4096, tg - G + 0x10000,
                                 tg + G, tg + G + 4096, 0x7f12345000ull, 0xffff8a3c1000ull};
        for (size_t k = 0; k < sizeof news / sizeof news[0]; k++) {
            uint64_t np = news[k];
            int64_t pages = ((int64_t) tg - (int64_t) (np & ~0xFFFull)) / 4096;
            int fits = pages >= -(1LL << 20) && pages < (1LL << 20);
            int ok1 = azm_adrp_reloc_ok(in, old_pc, np), ok2 = adrp_reloc_ok(in, old_pc, np);
            if (fits) {
                n_near++;
                if (!ok1 || !ok2 || ref_target(azm_reloc_adrp(in, old_pc, np), np) != tg ||
                    ref_target(reloc_adrp(in, old_pc, np), np) != tg)
                    bad_near++;
            } else {
                n_far++;
                if (ok1 || ok2)
                    bad_far++;
            }
        }
        /* the two exact edges: target - new page == +4 GB - 4 KB fits, == +4 GB does not */
        uint64_t np_in = tg - (G - 4096), np_out = tg - G;
        if (!azm_adrp_reloc_ok(in, old_pc, np_in) || azm_adrp_reloc_ok(in, old_pc, np_out) ||
            !adrp_reloc_ok(in, old_pc, np_in) || adrp_reloc_ok(in, old_pc, np_out))
            bad_edge++;
    }
    CHECK("adrp target decoded identically by both libraries", bad_target == 0);
    CHECK("in reach: re-encoded adrp yields the original page (both libraries)", n_near > 0 && bad_near == 0);
    CHECK("out of reach: never reported as re-encodable (both libraries)", n_far > 0 && bad_far == 0);
    CHECK("reach edges: +4 GB - 4 KB fits, +4 GB does not (both libraries)", bad_edge == 0);

    /* out of reach: the page is materialized into the adrp's own register */
    /* adrp sites and their pages (each within the adrp's own reach), up to the top of the space */
    const uint64_t far_pcs[] = {0x14ab0d4ull, 0x2cd4b28ull, 0x7f12345678ull, 0xffff8a3c1abcull};
    const uint64_t far_tg[] = {0x3f84000ull, 0x10000ull, 0x7f00001000ull, 0xffffffff0000ull};
    const int regs[] = {0, 2, 17, 30};
    int bad_mat = 0, n_mat = 0;
    for (size_t i = 0; i < sizeof far_pcs / sizeof far_pcs[0]; i++)
        for (size_t j = 0; j < sizeof regs / sizeof regs[0]; j++) {
            uint64_t pc = far_pcs[i], tg = far_tg[i] & ~0xFFFull;
            /* an adrp at pc in reach of tg: the trampoline that copies it is far away */
            int64_t pages = ((int64_t) tg - (int64_t) (pc & ~0xFFFull)) / 4096;
            if (pages < -(1LL << 20) || pages >= (1LL << 20))
                continue;
            uint32_t in = enc_adrp(regs[j], pc, tg), a[4], b[4];
            emit_abs_addr(a, (int) (in & 0x1F), adrp_target(in, pc));
            azm_emit_abs_addr(b, (int) (in & 0x1F), azm_adrp_target(in, pc));
            int ra, rb;
            uint64_t va = ref_movz_movk(a, &ra), vb = ref_movz_movk(b, &rb);
            n_mat++;
            if (va != ref_target(in, pc) || vb != ref_target(in, pc) || ra != regs[j] || rb != regs[j])
                bad_mat++;
        }
    CHECK("out of reach: MOVZ + 3x MOVK load exactly the original page into its register (both libraries)",
          n_mat == 16 && bad_mat == 0);
    {
        uint32_t a[4], b[4];
        const uint64_t v = 0xFEDCBA9876543210ull;
        emit_abs_addr(a, 5, v);
        azm_emit_abs_addr(b, 5, v);
        int ra, rb;
        CHECK("every 16-bit part of a 64-bit value lands in its own place (both libraries)",
              ref_movz_movk(a, &ra) == v && ra == 5 && ref_movz_movk(b, &rb) == v && rb == 5);
        uint32_t j[4], k[4];
        emit_abs_jump(j, 0x14ab0e4ull);
        azm_abs_jump(k, 0xffff8a3c1010ull);
        CHECK("the way back: LDR X17, #8; BR X17 to the address given (both libraries)",
              ref_jump(j) == 0x14ab0e4ull && ref_jump(k) == 0xffff8a3c1010ull);
    }

    /* function shape */
    const uint32_t STP = 0xA9BF7BFDu, MOV = 0x910003FDu, NOP = 0xD503201Fu, ADD = 0x91000400u;
    const uint32_t RET = 0xD65F03C0u, BR16 = 0xD61F0200u, BL = 0x94000010u, B = 0x14000004u,
                   ADRP = 0x90000002u, LDRLIT = 0x58000051u, RETAA = 0xD65F0BFFu,
                   BRAA = 0xD71F0A11u, BLR = 0xD63F0200u;
    uint32_t normal[4] = {STP, MOV, ADRP, ADD};
    uint32_t short_ret[4] = {MOV, RET, NOP, NOP};
    uint32_t tail_br[4] = {STP, BR16, NOP, NOP};
    uint32_t ret_last[4] = {STP, MOV, ADD, RET};
    uint32_t has_bl[4] = {STP, MOV, BL, ADD};
    uint32_t b_last[4] = {STP, MOV, ADD, B};
    uint32_t lit[4] = {LDRLIT, MOV, ADD, NOP};
    uint32_t paut[4] = {ADD, RETAA, NOP, NOP};
    CHECK("normal prologue with adrp: patchable (1 and 4 covered)", azm_patch_ok(normal, 1) && azm_patch_ok(normal, 4));
    CHECK("ret inside the 16 bytes: refused for the 4-instruction jump", !azm_patch_ok(short_ret, 4));
    CHECK("ret inside the 16 bytes: the 1-instruction island patch is still fine", azm_patch_ok(short_ret, 1));
    CHECK("br (tail call) inside the 16 bytes: refused for 4", !azm_patch_ok(tail_br, 4));
    CHECK("retaa inside the 16 bytes: refused for 4", !azm_patch_ok(paut, 4));
    CHECK("ret as the 4th instruction: the 16 bytes are all this function's", azm_patch_ok(ret_last, 4));
    CHECK("bl in the first four: refused", !azm_patch_ok(has_bl, 1) && !azm_patch_ok(has_bl, 4));
    CHECK("b in the first four: refused", !azm_patch_ok(b_last, 1));
    CHECK("ldr literal in the first four: refused", !azm_patch_ok(lit, 1));
    CHECK("azmod: ret/br/retaa/braa end a function; blr, bl, stp, nop do not",
          azm_ends_fn(RET) && azm_ends_fn(BR16) && azm_ends_fn(RETAA) && azm_ends_fn(BRAA) &&
              !azm_ends_fn(BLR) && !azm_ends_fn(STP) && !azm_ends_fn(BL) && !azm_ends_fn(NOP));
    CHECK("azstem: ret/br/retaa/braa end a function; blr, bl, stp, nop do not",
          ends_fn(RET) && ends_fn(BR16) && ends_fn(RETAA) && ends_fn(BRAA) && !ends_fn(BLR) &&
              !ends_fn(STP) && !ends_fn(BL) && !ends_fn(NOP));
    return ut_summary();
}
