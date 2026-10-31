/* AArch64 (A64) instruction encodings for the ARM64 JIT backend in jit.c.
 * Pure functions: each returns one 32-bit instruction word, so the
 * encodings can be checked on any host (tools/jit_a64_check.c compares them
 * with the system assembler). Registers are numbered 0-31; 31 is sp or xzr
 * depending on the instruction, as in the architecture manual. */
#ifndef GLOWLANG_JIT_A64_H
#define GLOWLANG_JIT_A64_H

#include <stdint.h>

enum {
    A64_EQ = 0, A64_NE = 1, A64_HS = 2, A64_LO = 3, A64_MI = 4, A64_PL = 5,
    A64_VS = 6, A64_VC = 7, A64_HI = 8, A64_LS = 9, A64_GE = 10, A64_LT = 11,
    A64_GT = 12, A64_LE = 13, A64_AL = 14
};
#define A64_SP 31
#define A64_XZR 31

#define A64_R(x) ((uint32_t)(x) & 31u)

/* Moves and immediates */
static inline uint32_t a64_movz(int rd, uint16_t imm, int hw) { return 0xD2800000u | ((uint32_t)hw << 21) | ((uint32_t)imm << 5) | A64_R(rd); }
static inline uint32_t a64_movk(int rd, uint16_t imm, int hw) { return 0xF2800000u | ((uint32_t)hw << 21) | ((uint32_t)imm << 5) | A64_R(rd); }
static inline uint32_t a64_movz_w(int rd, uint16_t imm) { return 0x52800000u | ((uint32_t)imm << 5) | A64_R(rd); }
static inline uint32_t a64_mov_reg(int rd, int rm) { return 0xAA0003E0u | (A64_R(rm) << 16) | A64_R(rd); }      /* orr xd, xzr, xm */
static inline uint32_t a64_mov_from_sp(int rd) { return 0x910003E0u | A64_R(rd); }                              /* add xd, sp, #0 */

/* Arithmetic */
static inline uint32_t a64_add_imm(int rd, int rn, uint32_t imm12) { return 0x91000000u | ((imm12 & 0xFFFu) << 10) | (A64_R(rn) << 5) | A64_R(rd); }
static inline uint32_t a64_sub_imm(int rd, int rn, uint32_t imm12) { return 0xD1000000u | ((imm12 & 0xFFFu) << 10) | (A64_R(rn) << 5) | A64_R(rd); }
static inline uint32_t a64_cmp_imm(int rn, uint32_t imm12) { return 0xF1000000u | ((imm12 & 0xFFFu) << 10) | (A64_R(rn) << 5) | 31u; }
static inline uint32_t a64_add(int rd, int rn, int rm) { return 0x8B000000u | (A64_R(rm) << 16) | (A64_R(rn) << 5) | A64_R(rd); }
static inline uint32_t a64_sub(int rd, int rn, int rm) { return 0xCB000000u | (A64_R(rm) << 16) | (A64_R(rn) << 5) | A64_R(rd); }
static inline uint32_t a64_cmp(int rn, int rm) { return 0xEB000000u | (A64_R(rm) << 16) | (A64_R(rn) << 5) | 31u; }
static inline uint32_t a64_and(int rd, int rn, int rm) { return 0x8A000000u | (A64_R(rm) << 16) | (A64_R(rn) << 5) | A64_R(rd); }
static inline uint32_t a64_orr(int rd, int rn, int rm) { return 0xAA000000u | (A64_R(rm) << 16) | (A64_R(rn) << 5) | A64_R(rd); }
static inline uint32_t a64_mul(int rd, int rn, int rm) { return 0x9B007C00u | (A64_R(rm) << 16) | (A64_R(rn) << 5) | A64_R(rd); }
static inline uint32_t a64_smulh(int rd, int rn, int rm) { return 0x9B407C00u | (A64_R(rm) << 16) | (A64_R(rn) << 5) | A64_R(rd); }
/* Bitfield moves: sbfx/ubfx extract `width` bits from `lsb`; asr/lsr. */
static inline uint32_t a64_sbfx(int rd, int rn, int lsb, int width) { return 0x93400000u | ((uint32_t)lsb << 16) | ((uint32_t)(lsb + width - 1) << 10) | (A64_R(rn) << 5) | A64_R(rd); }
static inline uint32_t a64_ubfx(int rd, int rn, int lsb, int width) { return 0xD3400000u | ((uint32_t)lsb << 16) | ((uint32_t)(lsb + width - 1) << 10) | (A64_R(rn) << 5) | A64_R(rd); }
static inline uint32_t a64_asr_imm(int rd, int rn, int sh) { return 0x9340FC00u | ((uint32_t)sh << 16) | (A64_R(rn) << 5) | A64_R(rd); }
static inline uint32_t a64_lsr_imm(int rd, int rn, int sh) { return 0xD340FC00u | ((uint32_t)sh << 16) | (A64_R(rn) << 5) | A64_R(rd); }
/* cset xd, cond (csinc xd, xzr, xzr, !cond) */
static inline uint32_t a64_cset(int rd, int cond) { return 0x9A9F07E0u | ((uint32_t)(cond ^ 1) << 12) | A64_R(rd); }

/* Loads and stores. Unsigned offsets are in bytes and must be a multiple of
   the access size (8 for x, 4 for w) and below 4096 * size. */
static inline uint32_t a64_ldr(int rt, int rn, uint32_t off) { return 0xF9400000u | ((off / 8) << 10) | (A64_R(rn) << 5) | A64_R(rt); }
static inline uint32_t a64_str(int rt, int rn, uint32_t off) { return 0xF9000000u | ((off / 8) << 10) | (A64_R(rn) << 5) | A64_R(rt); }
static inline uint32_t a64_ldr_w(int rt, int rn, uint32_t off) { return 0xB9400000u | ((off / 4) << 10) | (A64_R(rn) << 5) | A64_R(rt); }
static inline uint32_t a64_str_w(int rt, int rn, uint32_t off) { return 0xB9000000u | ((off / 4) << 10) | (A64_R(rn) << 5) | A64_R(rt); }
static inline uint32_t a64_ldr_reg(int rt, int rn, int rm) { return 0xF8606800u | (A64_R(rm) << 16) | (A64_R(rn) << 5) | A64_R(rt); }
static inline uint32_t a64_str_reg(int rt, int rn, int rm) { return 0xF8206800u | (A64_R(rm) << 16) | (A64_R(rn) << 5) | A64_R(rt); }
/* Frame records: stp rt, rt2, [sp, #-imm]! / ldp rt, rt2, [sp], #imm, and
   the plain signed-offset forms. Offsets are bytes, multiples of 8. */
static inline uint32_t a64_stp_pre(int rt, int rt2, int rn, int off) { return 0xA9800000u | (((uint32_t)(off / 8) & 0x7Fu) << 15) | (A64_R(rt2) << 10) | (A64_R(rn) << 5) | A64_R(rt); }
static inline uint32_t a64_ldp_post(int rt, int rt2, int rn, int off) { return 0xA8C00000u | (((uint32_t)(off / 8) & 0x7Fu) << 15) | (A64_R(rt2) << 10) | (A64_R(rn) << 5) | A64_R(rt); }
static inline uint32_t a64_stp(int rt, int rt2, int rn, int off) { return 0xA9000000u | (((uint32_t)(off / 8) & 0x7Fu) << 15) | (A64_R(rt2) << 10) | (A64_R(rn) << 5) | A64_R(rt); }
static inline uint32_t a64_ldp(int rt, int rt2, int rn, int off) { return 0xA9400000u | (((uint32_t)(off / 8) & 0x7Fu) << 15) | (A64_R(rt2) << 10) | (A64_R(rn) << 5) | A64_R(rt); }

/* Branches. Offsets are in bytes from the branch itself. */
static inline uint32_t a64_b(int32_t off) { return 0x14000000u | (((uint32_t)(off / 4)) & 0x3FFFFFFu); }
static inline uint32_t a64_b_cond(int cond, int32_t off) { return 0x54000000u | ((((uint32_t)(off / 4)) & 0x7FFFFu) << 5) | (uint32_t)cond; }
static inline uint32_t a64_cbz(int rt, int32_t off) { return 0xB4000000u | ((((uint32_t)(off / 4)) & 0x7FFFFu) << 5) | A64_R(rt); }
static inline uint32_t a64_cbnz(int rt, int32_t off) { return 0xB5000000u | ((((uint32_t)(off / 4)) & 0x7FFFFu) << 5) | A64_R(rt); }
static inline uint32_t a64_cbz_w(int rt, int32_t off) { return 0x34000000u | ((((uint32_t)(off / 4)) & 0x7FFFFu) << 5) | A64_R(rt); }
static inline uint32_t a64_tbz(int rt, int bit, int32_t off) { return 0x36000000u | ((uint32_t)(bit >> 5) << 31) | ((uint32_t)(bit & 31) << 19) | ((((uint32_t)(off / 4)) & 0x3FFFu) << 5) | A64_R(rt); }
static inline uint32_t a64_tbnz(int rt, int bit, int32_t off) { return 0x37000000u | ((uint32_t)(bit >> 5) << 31) | ((uint32_t)(bit & 31) << 19) | ((((uint32_t)(off / 4)) & 0x3FFFu) << 5) | A64_R(rt); }
static inline uint32_t a64_blr(int rn) { return 0xD63F0000u | (A64_R(rn) << 5); }
static inline uint32_t a64_br(int rn) { return 0xD61F0000u | (A64_R(rn) << 5); }
static inline uint32_t a64_ret(void) { return 0xD65F03C0u; }

/* Scalar double precision */
static inline uint32_t a64_fmov_d_x(int dd, int xn) { return 0x9E670000u | (A64_R(xn) << 5) | A64_R(dd); }
static inline uint32_t a64_fmov_x_d(int xd, int dn) { return 0x9E660000u | (A64_R(dn) << 5) | A64_R(xd); }
static inline uint32_t a64_scvtf(int dd, int xn) { return 0x9E620000u | (A64_R(xn) << 5) | A64_R(dd); }
static inline uint32_t a64_fcvtzs(int xd, int dn) { return 0x9E780000u | (A64_R(dn) << 5) | A64_R(xd); }
static inline uint32_t a64_fadd(int dd, int dn, int dm) { return 0x1E602800u | (A64_R(dm) << 16) | (A64_R(dn) << 5) | A64_R(dd); }
static inline uint32_t a64_fsub(int dd, int dn, int dm) { return 0x1E603800u | (A64_R(dm) << 16) | (A64_R(dn) << 5) | A64_R(dd); }
static inline uint32_t a64_fmul(int dd, int dn, int dm) { return 0x1E600800u | (A64_R(dm) << 16) | (A64_R(dn) << 5) | A64_R(dd); }
static inline uint32_t a64_fdiv(int dd, int dn, int dm) { return 0x1E601800u | (A64_R(dm) << 16) | (A64_R(dn) << 5) | A64_R(dd); }
static inline uint32_t a64_fcmp(int dn, int dm) { return 0x1E602000u | (A64_R(dm) << 16) | (A64_R(dn) << 5); }
static inline uint32_t a64_fcmp_zero(int dn) { return 0x1E602008u | (A64_R(dn) << 5); }

/* Patch the offset of the branch at `at` (b, b.cond, cbz/cbnz, tbz/tbnz)
   so it lands on `target`; both are byte positions in the same buffer.
   Returns 0 when the distance does not fit the instruction's field. */
static inline int a64_patch_branch(uint32_t *ins, int64_t at, int64_t target) {
    int64_t off = (target - at) / 4;
    uint32_t w = *ins;
    if ((w & 0x7C000000u) == 0x14000000u) {                       /* b */
        if (off < -(1 << 25) || off >= (1 << 25)) return 0;
        *ins = (w & 0xFC000000u) | ((uint32_t)off & 0x3FFFFFFu);
    } else if ((w & 0xFF000010u) == 0x54000000u || (w & 0x7E000000u) == 0x34000000u) { /* b.cond, cbz, cbnz */
        if (off < -(1 << 18) || off >= (1 << 18)) return 0;
        *ins = (w & ~(0x7FFFFu << 5)) | (((uint32_t)off & 0x7FFFFu) << 5);
    } else if ((w & 0x7E000000u) == 0x36000000u) {                /* tbz, tbnz */
        if (off < -(1 << 13) || off >= (1 << 13)) return 0;
        *ins = (w & ~(0x3FFFu << 5)) | (((uint32_t)off & 0x3FFFu) << 5);
    } else {
        return 0;
    }
    return 1;
}

#endif
