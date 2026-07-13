#include "jit.h"
#ifndef _WIN32
#include <sys/mman.h>
#else
#include <windows.h>
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_PRIVATE 0
#define MAP_ANONYMOUS 0
#define MAP_FAILED NULL
static void* mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset) {
    (void)addr; (void)prot; (void)flags; (void)fd; (void)offset;
    return VirtualAlloc(NULL, length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}
static int mprotect(void *addr, size_t len, int prot) {
    DWORD oldProtect;
    DWORD newProtect = PAGE_READONLY;
    if ((prot & PROT_READ) && (prot & PROT_EXEC)) newProtect = PAGE_EXECUTE_READ;
    else if ((prot & PROT_READ) && (prot & PROT_WRITE)) newProtect = PAGE_READWRITE;
    return VirtualProtect(addr, len, newProtect, &oldProtect) ? 0 : -1;
}
static int munmap(void *addr, size_t length) {
    (void)length;
    return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1;
}
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern FuncEntry* func_get(const char *name);

typedef struct { uint8_t *code; size_t size; size_t capacity; } JITBuffer;

static void jit_emit_byte(JITBuffer *buf, uint8_t b) { 
    if (buf->size < buf->capacity) buf->code[buf->size++] = b; 
}
static void jit_emit_u32(JITBuffer *buf, uint32_t v) { 
    if (buf->size + 4 <= buf->capacity) { memcpy(buf->code + buf->size, &v, 4); buf->size += 4; } 
}
static void jit_emit_u64(JITBuffer *buf, uint64_t v) { 
    if (buf->size + 8 <= buf->capacity) { memcpy(buf->code + buf->size, &v, 8); buf->size += 8; } 
}

#if defined(__x86_64__) || defined(_M_X64)
static void emit_mov_rax_imm64(JITBuffer *buf, uint64_t imm) { 
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xb8); jit_emit_u64(buf, imm); 
}

static void emit_mov_rcx_imm64(JITBuffer *buf, uint64_t imm) { 
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xb9); jit_emit_u64(buf, imm); 
}

static void emit_mov_rdx_imm64(JITBuffer *buf, uint64_t imm) { 
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xba); jit_emit_u64(buf, imm); 
}

static void emit_mov_rdi_imm64(JITBuffer *buf, uint64_t imm) { 
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xbf); jit_emit_u64(buf, imm); 
}

static void emit_mov_rsi_imm64(JITBuffer *buf, uint64_t imm) { 
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xbe); jit_emit_u64(buf, imm); 
}

static void emit_mov_r10_imm64(JITBuffer *buf, uint64_t imm) { 
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0xba); jit_emit_u64(buf, imm); 
}

static void emit_mov_r11_imm64(JITBuffer *buf, uint64_t imm) { 
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0xbb); jit_emit_u64(buf, imm); 
}

static void emit_mov_r8_imm64(JITBuffer *buf, uint64_t imm) {
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0xb8); jit_emit_u64(buf, imm); 
}

static void emit_load_cpu_regs(JITBuffer *buf) {
    // mov r14, [r12 + 0]
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x34); jit_emit_byte(buf, 0x24);
    // mov r15, [r12 + 8]
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x7c); jit_emit_byte(buf, 0x24); jit_emit_byte(buf, 0x08);
    // mov rbx, [r12 + 16]
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x5c); jit_emit_byte(buf, 0x24); jit_emit_byte(buf, 0x10);
}
static void emit_save_cpu_regs(JITBuffer *buf) {
    // mov [r12 + 0], r14
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x34); jit_emit_byte(buf, 0x24);
    // mov [r12 + 8], r15
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x7c); jit_emit_byte(buf, 0x24); jit_emit_byte(buf, 0x08);
    // mov [r12 + 16], rbx
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x5c); jit_emit_byte(buf, 0x24); jit_emit_byte(buf, 0x10);
}

static void emit_prologue(JITBuffer *buf) {
    jit_emit_byte(buf, 0x55); 
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xe5);
    jit_emit_byte(buf, 0x53); // push rbx
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x54); jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x55);
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x56); jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x57);
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xfc); // r12 = R
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xf5); // r13 = pc_idx
    emit_load_cpu_regs(buf);
}

static void emit_pop_frame(JITBuffer *buf) {
    emit_save_cpu_regs(buf);
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x5f); jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x5e);
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x5d); jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x5c);
    jit_emit_byte(buf, 0x5b); // pop rbx
    jit_emit_byte(buf, 0x5d);
}

static void emit_epilogue(JITBuffer *buf) {
    emit_pop_frame(buf);
    jit_emit_byte(buf, 0xc3);
}

static void emit_call_rax(JITBuffer *buf) {
    emit_save_cpu_regs(buf);
    jit_emit_byte(buf, 0xff); jit_emit_byte(buf, 0xd0);
    emit_load_cpu_regs(buf);
}

static void emit_mov_reg_rax(JITBuffer *buf, int reg);

/* Inline self-recursion fast path (lever 1): instead of bouncing every
   recursive call through the C shim jit_self_call (which does a C function
   call + stack-frame push/pop per recursion), emit a direct call into this
   function's own JIT entry (buf.code). The calling convention is identical to
   what jit_self_call uses (args = rsp + r2*8, deopt detected via IS_DEOPT),
   so semantics are unchanged; we only remove the C-call overhead. On deopt we
   fall back to the real jit_self_call (same function as before) to preserve
   all capabilities. Multi-arg self-recursion keeps the C-shim path. */
static void emit_self_call_direct(JITBuffer *buf, FuncEntry *fe, Instruction *pc) {
    if (!fe || fe->param_count != 1) {
        jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xe2);
        jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xc2); jit_emit_u32(buf, pc->r2 * 8);
        jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd6);
        emit_mov_rdi_imm64(buf, (uintptr_t)fe);
        emit_mov_rax_imm64(buf, (uintptr_t)jit_self_call);
        emit_call_rax(buf);
        emit_mov_reg_rax(buf, pc->r0);
        return;
    }
    /* Save JIT registers so the recursive call (which clobbers them) cannot
       corrupt the caller's live values. Mirrors emit_save_cpu_regs. */
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x34); jit_emit_byte(buf, 0x24);
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x7c); jit_emit_byte(buf, 0x24); jit_emit_byte(buf, 0x08);
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x5c); jit_emit_byte(buf, 0x24); jit_emit_byte(buf, 0x10);
    /* args = rsp + r2*8 */
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xe2);
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xc2); jit_emit_u32(buf, pc->r2 * 8);
    /* deopt_pc slot on the C stack */
    jit_emit_byte(buf, 0x6a); jit_emit_byte(buf, 0x00);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xe6);
    jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd7);
    /* direct recursive call to this function's JIT entry (buf.code base) */
    uint32_t call_off = buf->size;
    jit_emit_byte(buf, 0xe8);
    jit_emit_u32(buf, (uint32_t)(0 - (int32_t)(call_off + 5)));
    /* result in rax; check deopt sentinel */
    emit_mov_rdx_imm64(buf, (uint64_t)(MASK_NAN | ((uint64_t)VAL_DEOPT << 48)));
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x39); jit_emit_byte(buf, 0xd0);
    uint32_t je_deopt = buf->size;
    jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x84); jit_emit_u32(buf, 0);
    /* normal path */
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x83); jit_emit_byte(buf, 0xc4); jit_emit_byte(buf, 0x08);
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x34); jit_emit_byte(buf, 0x24);
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x7c); jit_emit_byte(buf, 0x24); jit_emit_byte(buf, 0x08);
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x5c); jit_emit_byte(buf, 0x24); jit_emit_byte(buf, 0x10);
    emit_mov_reg_rax(buf, pc->r0);
    uint32_t jmp_done = buf->size;
    jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    /* DEOPT fallback: real C shim (handles deopt + edges) */
    uint32_t deopt_pos = buf->size;
    *(uint32_t*)(buf->code + je_deopt + 2) = deopt_pos - (je_deopt + 6);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x83); jit_emit_byte(buf, 0xc4); jit_emit_byte(buf, 0x08);
    emit_mov_rdi_imm64(buf, (uintptr_t)fe);
    jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd7);
    emit_mov_rax_imm64(buf, (uintptr_t)jit_self_call);
    emit_call_rax(buf);
    emit_mov_reg_rax(buf, pc->r0);
    uint32_t done_pos = buf->size;
    *(uint32_t*)(buf->code + jmp_done + 1) = done_pos - (jmp_done + 5);
}

static void emit_mov_rax_reg(JITBuffer *buf, int reg) {
    if (reg == 0) { jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xf0); }
    else if (reg == 1) { jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xf8); }
    else if (reg == 2) { jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd8); }
    else { jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x84); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8); }
}
static void emit_mov_reg_rax(JITBuffer *buf, int reg) {
    if (reg == 0) { jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc6); }
    else if (reg == 1) { jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc7); }
    else if (reg == 2) { jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc3); }
    else { jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x84); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8); }
}
static void emit_mov_rcx_reg(JITBuffer *buf, int reg) {
    if (reg == 0) { jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xf1); }
    else if (reg == 1) { jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xf9); }
    else if (reg == 2) { jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd9); }
    else { jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x8c); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8); }
}
static void emit_mov_rdx_reg(JITBuffer *buf, int reg) {
    if (reg == 0) { jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xf2); }
    else if (reg == 1) { jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xfa); }
    else if (reg == 2) { jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xda); }
    else { jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x94); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8); }
}
static void emit_mov_rdi_reg(JITBuffer *buf, int reg) {
    if (reg == 0) { jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xf7); }
    else if (reg == 1) { jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xff); }
    else if (reg == 2) { jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xdf); }
    else { jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0xbc); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8); }
}
static void emit_mov_rsi_reg(JITBuffer *buf, int reg) {
    if (reg == 0) { jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xf6); }
    else if (reg == 1) { jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xfe); }
    else if (reg == 2) { jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xde); }
    else { jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0xb4); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8); }
}

static void emit_fast_index(JITBuffer *buf, Instruction *pc) {
    emit_mov_rax_reg(buf, pc->r1); // RAX = coll
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2); // mov rdx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xfc); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7ffc (VAL_LIST)
    uint32_t jne_list = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0);

    emit_mov_rax_reg(buf, pc->r2); // RAX = idx
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2); // mov rdx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xf8); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7ff8 (VAL_INT)
    uint32_t jne_int = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0);

    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x63); jit_emit_byte(buf, 0xc8); // movsxd rcx, eax (signed 32-bit idx)
    emit_mov_rax_reg(buf, pc->r1); // reload coll into RAX
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe0); jit_emit_byte(buf, 0x10); // shl rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe8); jit_emit_byte(buf, 0x10); // shr rax, 16 (List*)

    jit_emit_byte(buf, 0x3b); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x24); // cmp ecx, [rax + 36] (size)
    uint32_t jae_bounds = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x83); jit_emit_u32(buf, 0);

    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x50); jit_emit_byte(buf, 0x30); // mov rdx, [rax + 48] (items)
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x04); jit_emit_byte(buf, 0xca); // mov rax, [rdx + rcx*8]
    emit_mov_reg_rax(buf, pc->r0);
    uint32_t jmp_done = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);

    // Fallback:
    uint32_t fb_pos = buf->size;
    *(uint32_t*)&buf->code[jne_list + 2] = fb_pos - (jne_list + 6);
    *(uint32_t*)&buf->code[jne_int + 2] = fb_pos - (jne_int + 6);
    *(uint32_t*)&buf->code[jae_bounds + 2] = fb_pos - (jae_bounds + 6);

    emit_mov_rdi_reg(buf, pc->r1); emit_mov_rsi_reg(buf, pc->r2);
    emit_mov_rax_imm64(buf, (uintptr_t)glow_index_jit);
    emit_call_rax(buf);
    emit_mov_reg_rax(buf, pc->r0);

    uint32_t done_pos = buf->size;
    *(uint32_t*)&buf->code[jmp_done + 1] = done_pos - (jmp_done + 5);
}

static void emit_fast_store_index(JITBuffer *buf, Instruction *pc) {
    emit_mov_rax_reg(buf, pc->r0); // RAX = coll
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2); // mov rdx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xfc); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7ffc (VAL_LIST)
    uint32_t jne_list = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0);

    emit_mov_rax_reg(buf, pc->r1); // RAX = idx
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2); // mov rdx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xf8); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7ff8 (VAL_INT)
    uint32_t jne_int = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0);

    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x63); jit_emit_byte(buf, 0xc8); // movsxd rcx, eax (signed 32-bit idx)
    emit_mov_rax_reg(buf, pc->r0); // reload coll into RAX
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe0); jit_emit_byte(buf, 0x10); // shl rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe8); jit_emit_byte(buf, 0x10); // shr rax, 16 (List*)

    jit_emit_byte(buf, 0x3b); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x24); // cmp ecx, [rax + 36] (size)
    uint32_t jae_bounds = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x83); jit_emit_u32(buf, 0);

    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x50); jit_emit_byte(buf, 0x30); // mov rdx, [rax + 48] (items)
    emit_mov_rax_reg(buf, pc->r2); // load val into RAX
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x04); jit_emit_byte(buf, 0xca); // mov [rdx + rcx*8], rax
    uint32_t jmp_done = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);

    // Fallback:
    uint32_t fb_pos = buf->size;
    *(uint32_t*)&buf->code[jne_list + 2] = fb_pos - (jne_list + 6);
    *(uint32_t*)&buf->code[jne_int + 2] = fb_pos - (jne_int + 6);
    *(uint32_t*)&buf->code[jae_bounds + 2] = fb_pos - (jae_bounds + 6);

    emit_mov_rdi_reg(buf, pc->r0); emit_mov_rsi_reg(buf, pc->r1); emit_mov_rdx_reg(buf, pc->r2);
    emit_mov_rax_imm64(buf, (uintptr_t)glow_store_index_jit);
    emit_call_rax(buf);

    uint32_t done_pos = buf->size;
    *(uint32_t*)&buf->code[jmp_done + 1] = done_pos - (jmp_done + 5);
}

// ---- Inline fast path for integer arithmetic / comparison ----
// Load r1,r2 into rax,rcx and verify both are inline ints. Records the two
// "not an int" jump patch positions (caller fills them to its slow path).
// On success rax/rcx hold the sign-extended 64-bit integers.
static void emit_jit_int_load_and_check(JITBuffer *buf, int r1, int r2,
                                        uint32_t *jne_a, uint32_t *jne_b) {
    emit_mov_rax_reg(buf, r1);
    emit_mov_rcx_reg(buf, r2);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2); // mov rdx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xf8); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7FF8 (INLINE_INT tag)
    *jne_a = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0); // jne slow
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xca); // mov rdx, rcx
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xf8); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7FF8
    *jne_b = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0); // jne slow
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe0); jit_emit_byte(buf, 0x10); // shl rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xf8); jit_emit_byte(buf, 0x10); // sar rax, 16 (sign-extend 48->64)
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe1); jit_emit_byte(buf, 0x10); // shl rcx, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xf9); jit_emit_byte(buf, 0x10); // sar rcx, 16
}

// Inline integer add/sub; promotes to a double (exact for the whole 48-bit
// range) when the result leaves the signed 48-bit range. Mixed types fall
// back to slow_fn (a jit_int_* helper).
static void emit_jit_int_arith(JITBuffer *buf, int is_sub, int r0, int r1, int r2, uintptr_t slow_fn) {
    uint32_t jne_a, jne_b;
    emit_jit_int_load_and_check(buf, r1, r2, &jne_a, &jne_b);
    if (is_sub) {
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x29); jit_emit_byte(buf, 0xc8); // sub rax, rcx
    } else {
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x01); jit_emit_byte(buf, 0xc8); // add rax, rcx
    }
    emit_mov_rdx_imm64(buf, INT48_MAX);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x3b); jit_emit_byte(buf, 0xc2); // cmp rax, rdx
    uint32_t jg_over = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x8f); jit_emit_u32(buf, 0); // jg promote
    emit_mov_rdx_imm64(buf, INT48_MIN);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x3b); jit_emit_byte(buf, 0xc2); // cmp rax, rdx
    uint32_t jl_under = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x8c); jit_emit_u32(buf, 0); // jl promote
    emit_mov_rdx_imm64(buf, MASK_NAN); // rebox as inline int
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x09); jit_emit_byte(buf, 0xd0); // or rax, rdx
    uint32_t jmp_done = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    uint32_t promote_pos = buf->size;
    jit_emit_byte(buf, 0xf2); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2a); jit_emit_byte(buf, 0xc0); // cvtsi2sd xmm0, rax
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x7e); jit_emit_byte(buf, 0xc0); // movq rax, xmm0
    uint32_t jmp_done2 = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    uint32_t slow_pos = buf->size;
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc7); // mov rdi, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xce); // mov rsi, rcx
    emit_mov_rax_imm64(buf, slow_fn);
    emit_call_rax(buf);
    uint32_t done_pos = buf->size;
    emit_mov_reg_rax(buf, r0);
    *(uint32_t*)(buf->code + jne_a + 2) = (uint32_t)(slow_pos - (jne_a + 6));
    *(uint32_t*)(buf->code + jne_b + 2) = (uint32_t)(slow_pos - (jne_b + 6));
    *(uint32_t*)(buf->code + jg_over + 2) = (uint32_t)(promote_pos - (jg_over + 6));
    *(uint32_t*)(buf->code + jl_under + 2) = (uint32_t)(promote_pos - (jl_under + 6));
    *(uint32_t*)(buf->code + jmp_done + 1) = (uint32_t)(done_pos - (jmp_done + 5));
    *(uint32_t*)(buf->code + jmp_done2 + 1) = (uint32_t)(done_pos - (jmp_done2 + 5));
}

// Inline integer ordered comparisons; produces a bool value. Mixed types fall
// back to slow_fn (a jit_int_* helper).
static void emit_jit_int_cmp(JITBuffer *buf, int op, int r0, int r1, int r2, uintptr_t slow_fn) {
    uint32_t jne_a, jne_b;
    emit_jit_int_load_and_check(buf, r1, r2, &jne_a, &jne_b);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x39); jit_emit_byte(buf, 0xc8); // cmp rax, rcx
    switch (op) {
        case OP_LT_INT:  jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x9c); break; // setl cl
        case OP_GT_INT:  jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x9f); break; // setg cl
        case OP_LTE_INT: jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x9e); break; // setle cl
        case OP_GTE_INT: jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x9d); break; // setge cl
        default:         jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x94); break; // sete cl
    }
    jit_emit_byte(buf, 0xc1); // modrm: cl
    jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0xb6); jit_emit_byte(buf, 0xc1); // movzx eax, cl
    emit_mov_rdx_imm64(buf, MASK_NAN | ((uint64_t)VAL_BOOL << 48)); // bool tag 0x7FFA...
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x09); jit_emit_byte(buf, 0xd0); // or rax, rdx
    uint32_t jmp_done = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    uint32_t slow_pos = buf->size;
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc7); // mov rdi, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xce); // mov rsi, rcx
    emit_mov_rax_imm64(buf, slow_fn);
    emit_call_rax(buf);
    uint32_t done_pos = buf->size;
    emit_mov_reg_rax(buf, r0);
    *(uint32_t*)(buf->code + jne_a + 2) = (uint32_t)(slow_pos - (jne_a + 6));
    *(uint32_t*)(buf->code + jne_b + 2) = (uint32_t)(slow_pos - (jne_b + 6));
    *(uint32_t*)(buf->code + jmp_done + 1) = (uint32_t)(done_pos - (jmp_done + 5));
}

 void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) {
    (void)current_sig;
    if (!fe) fe = bc->owner;
    if (bc->jit_code || bc->jit_failed) return;

    for (int i = 0; i < bc->count; i++) {
        Instruction *pc = &bc->code[i];
        switch (pc->op) {
            case OP_CONST_INT: case OP_CONST_FLOAT: case OP_CONST_BOOL:
            case OP_LOAD_VAR: case OP_STORE_VAR: case OP_STORE_LET:
            case OP_MOVE: case OP_NOP:
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
            case OP_GET_PROPERTY: case OP_SET_PROPERTY: case OP_LT: case OP_GT: case OP_LTE:
            case OP_GTE: case OP_EQ: case OP_NEQ: case OP_JUMP:
            case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_RETURN:
            case OP_CALL: case OP_CALL_VALUE: case OP_CALL_SELF_FAST: case OP_CALL_METHOD:
            case OP_INDEX: case OP_STORE_INDEX:
            case OP_SIMD_ADD: case OP_TAIL_CALL:
            case OP_ADD_INT: case OP_SUB_INT: case OP_MUL_INT:
            case OP_LT_INT: case OP_GT_INT: case OP_LTE_INT: case OP_GTE_INT: case OP_EQ_INT:
            case OP_LOAD_LOCAL: case OP_STORE_LOCAL:
                break;
            default: bc->jit_failed = 1; return;
        }
    }

    // Reject self-recursive functions with complex bodies (array mutation or
    // calls to other functions). The fast self-call JIT path is only proven
    // correct for pure scalar recursion (e.g. fib); complex bodies (e.g. a
    // mergesort that mutates arrays and calls a helper) must run through the
    // interpreter/specialization path to avoid miscompilation.
    {
        int has_self = 0, has_complex = 0;
        for (int i = 0; i < bc->count; i++) {
            OpCode op = bc->code[i].op;
            if (op == OP_CALL_SELF_FAST) has_self = 1;
            else if (op == OP_STORE_INDEX || op == OP_CALL || op == OP_CALL_VALUE || op == OP_CALL_METHOD) has_complex = 1;
        }
        if (has_self && has_complex) { bc->jit_failed = 1; return; }
    }

    size_t size = 65536; 
    uint8_t *code = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { bc->jit_failed = 1; return; }
    JITBuffer buf = {code, 0, size};

    // Prologue
    emit_prologue(&buf);

    uint32_t body_start = buf.size;

    uint32_t *offsets = calloc(bc->count + 1, sizeof(uint32_t));
    struct { int instr_idx; uint32_t patch_pos; } *jumps = calloc(bc->count + 1, sizeof(*jumps));
    int jump_count = 0;

    for (int i = 0; i < bc->count; i++) {
        offsets[i] = buf.size;
        Instruction *pc = &bc->code[i];
        Instruction *orig_pc = (fe && fe->body) ? &fe->body->code[i] : pc;
        switch (pc->op) {
            case OP_CONST_INT: emit_mov_rax_imm64(&buf, value_int(pc->arg)); emit_mov_reg_rax(&buf, pc->r0); break;
            case OP_CONST_FLOAT: emit_mov_rax_imm64(&buf, *(uint64_t*)pc->ptr); emit_mov_reg_rax(&buf, pc->r0); break;
            case OP_CONST_BOOL: emit_mov_rax_imm64(&buf, value_bool(pc->arg)); emit_mov_reg_rax(&buf, pc->r0); break;
            case OP_LOAD_VAR: {
                emit_mov_rcx_imm64(&buf, (uintptr_t)&global_table.values);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x09);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x81);
                jit_emit_u32(&buf, pc->arg * 8);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_STORE_VAR:
            case OP_STORE_LET: {
                emit_mov_rax_reg(&buf, pc->r0);
                emit_mov_rcx_imm64(&buf, (uintptr_t)&global_table.values);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x09);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x81);
                jit_emit_u32(&buf, pc->arg * 8);
                break;
            }
            case OP_MOVE: emit_mov_rax_reg(&buf, pc->r1); emit_mov_reg_rax(&buf, pc->r0); break;
            case OP_NOP: break;
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV: {
                if (pc->type == 1 && (pc->op == OP_ADD || pc->op == OP_SUB)) {
                    emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                    emit_mov_r10_imm64(&buf, 0xFFFF000000000000ULL);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc6);
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd6);
                    emit_mov_r11_imm64(&buf, 0x7FF8000000000000ULL);
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xde);
                    uint32_t p1 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xce);
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd6);
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xde);
                    uint32_t p2 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
                    if (pc->op == OP_ADD) { 
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0xc8);
                        jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xd8);
                    } else if (pc->op == OP_SUB) { 
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8);
                        jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xd8);
                    }
                    uint32_t pd = buf.size; jit_emit_byte(&buf, 0xe9); jit_emit_u32(&buf, 0);
                    uint32_t bailout = buf.size; *(uint32_t*)(buf.code + p1 + 2) = (uint32_t)(bailout - (p1 + 6)); *(uint32_t*)(buf.code + p2 + 2) = (uint32_t)(bailout - (p2 + 6));
                    emit_mov_rax_imm64(&buf, (uint64_t)i); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x45); jit_emit_byte(&buf, 0x00);
                    emit_mov_rax_imm64(&buf, MASK_NAN | (15ULL << 48)); emit_epilogue(&buf);
                    uint32_t done = buf.size; *(uint32_t*)(buf.code + pd + 1) = (uint32_t)(done - (pd + 5));
                    emit_mov_reg_rax(&buf, pc->r0);
                } else if (pc->type == 2) {
                    emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                    jit_emit_byte(&buf, 0x66); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x6e); jit_emit_byte(&buf, 0xc0);
                    jit_emit_byte(&buf, 0x66); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x6e); jit_emit_byte(&buf, 0xc1);
                    if (pc->op == OP_ADD) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x58); jit_emit_byte(&buf, 0xc1); }
                    else if (pc->op == OP_SUB) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0xc1); }
                    else if (pc->op == OP_MUL) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x59); jit_emit_byte(&buf, 0xc1); }
                    else if (pc->op == OP_DIV) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0xc1); }
                    else { bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return; }
                    jit_emit_byte(&buf, 0x66); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x7e); jit_emit_byte(&buf, 0xc0);
                    emit_mov_reg_rax(&buf, pc->r0);
                } else {
                    emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                    if (pc->op == OP_ADD) { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0xc8); emit_mov_rcx_imm64(&buf, 0x7FF8000000000000ULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8); }
                    else if (pc->op == OP_SUB) { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8); emit_mov_rcx_imm64(&buf, 0x7FF8000000000000ULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xc8); }
                    else if (pc->op == OP_MUL) {
                        emit_mov_rdx_imm64(&buf, 0x0000FFFFFFFFFFFFULL);
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd0);
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd1);
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0xaf); jit_emit_byte(&buf, 0xc1);
                        emit_mov_rdx_imm64(&buf, 0x7FF8000000000000ULL);
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xd0);
                    } else { bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return; }
                    emit_mov_reg_rax(&buf, pc->r0);
                }
                break;
            }
            case OP_GET_PROPERTY: {
                emit_mov_rax_reg(&buf, pc->r1);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7); // mov rdi, rax
                emit_mov_rsi_imm64(&buf, (uintptr_t)pc->ptr);
                emit_mov_rdx_imm64(&buf, (uintptr_t)orig_pc);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_get_property_helper);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_SET_PROPERTY: {
                emit_mov_rax_reg(&buf, pc->r0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7); // mov rdi, rax
                emit_mov_rsi_imm64(&buf, (uintptr_t)pc->ptr);
                emit_mov_rax_reg(&buf, pc->r1);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc2); // mov rdx, rax
                emit_mov_rcx_imm64(&buf, (uintptr_t)orig_pc);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_set_property_helper);
                emit_call_rax(&buf);
                break;
            }
            case OP_LT: case OP_GT: case OP_LTE: case OP_GTE: case OP_EQ: case OP_NEQ: {
                emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xc8);
                uint8_t set_op;
                switch(pc->op) { case OP_LT: set_op = 0x9c; break; case OP_GT: set_op = 0x9f; break; case OP_LTE: set_op = 0x9e; break; case OP_GTE: set_op = 0x9d; break; case OP_EQ: set_op = 0x94; break; case OP_NEQ: set_op = 0x95; break; default: set_op = 0x94; break; }
                jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, set_op); jit_emit_byte(&buf, 0xc1); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0xb6); jit_emit_byte(&buf, 0xc1); emit_mov_rax_imm64(&buf, value_bool(0)); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xc8); emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_JUMP: { jit_emit_byte(&buf, 0xe9); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
            case OP_JUMP_IF_FALSE: { emit_mov_rax_reg(&buf, pc->r0); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xc0); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
            case OP_JUMP_IF_TRUE: { emit_mov_rax_reg(&buf, pc->r0); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xc0); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
             case OP_CALL_SELF_FAST: {
                 if (fe) {
                     emit_self_call_direct(&buf, fe, pc);
                 } else {
                     emit_mov_rax_reg(&buf, pc->r1);
                     jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7);
                     emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_val);
                     emit_call_rax(&buf);
                     emit_mov_reg_rax(&buf, pc->r0);
                 }
                 break;
             }
             case OP_CALL:
             case OP_CALL_VALUE: {
                FuncEntry *target_fe = (pc->op == OP_CALL) ? func_get((const char*)pc->ptr) : (pc->op == OP_CALL_SELF_FAST ? fe : NULL);
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2);
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8);
                jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd7);
                emit_mov_rsi_imm64(&buf, (uint64_t)pc->arg);
                emit_mov_rax_imm64(&buf, (uintptr_t)compute_signature);
                emit_call_rax(&buf);
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2);
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8);
                emit_mov_rcx_imm64(&buf, (uintptr_t)&orig_pc->cache[0]);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x3b); jit_emit_byte(&buf, 0x01);
                uint32_t jne_cache = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0x79); jit_emit_byte(&buf, 0x08); jit_emit_byte(&buf, 0x00);
                uint32_t je_null = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84); jit_emit_u32(&buf, 0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x08);
                jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd7);
                jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xee);
                emit_call_rax(&buf);
                uint32_t jmp_done = buf.size; jit_emit_byte(&buf, 0xe9); jit_emit_u32(&buf, 0);
                uint32_t slow_path = buf.size; 
                *(uint32_t*)(buf.code + jne_cache + 2) = (uint32_t)(slow_path - (jne_cache + 6));
                *(uint32_t*)(buf.code + je_null + 2) = (uint32_t)(slow_path - (je_null + 6));
                emit_mov_rcx_imm64(&buf, (uintptr_t)orig_pc);
                jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd2);
                emit_mov_rsi_imm64(&buf, (uint64_t)pc->arg);
                if (target_fe) { 
                    emit_mov_rdi_imm64(&buf, (uintptr_t)target_fe); 
                    emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper); 
                } else { 
                    emit_mov_rax_reg(&buf, pc->r1); 
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7);
                    emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_val); 
                }
                emit_call_rax(&buf);
                uint32_t done = buf.size; *(uint32_t*)(buf.code + jmp_done + 1) = (uint32_t)(done - (jmp_done + 5));
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_CALL_METHOD: {
                // RDI: obj (from pc->r1)
                emit_mov_rax_reg(&buf, pc->r1); 
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7); // mov rdi, rax
                
                // RSI: method name (from pc->ptr)
                emit_mov_rsi_imm64(&buf, (uintptr_t)pc->ptr);
                
                // RDX: argc (from pc->arg)
                emit_mov_rdx_imm64(&buf, (uint64_t)pc->arg);
                
                // RCX: args (R + pc->r2 * 8)
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2); // mov r10, r12 (R base)
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8); // add r10, r2 offset
                jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd1); // mov rcx, r10
                
                // R8: orig_pc
                emit_mov_r8_imm64(&buf, (uintptr_t)orig_pc);
                
                // Call jit_call_method_helper
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_method_helper);
                emit_call_rax(&buf);
                
                // Store result
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_TAIL_CALL: {
                FuncEntry *target_fe = (pc->ptr) ? func_get((const char*)pc->ptr) : NULL;
                if (target_fe && target_fe == fe) {
                    for (int k = 0; k < pc->arg && k < fe->param_count; k++) {
                        emit_mov_rax_reg(&buf, pc->r2 + k);
                        emit_mov_reg_rax(&buf, k);
                    }
                    jit_emit_byte(&buf, 0xe9);
                    jumps[jump_count].instr_idx = -1; // Special index for body_start
                    jumps[jump_count].patch_pos = buf.size;
                    jit_emit_u32(&buf, 0);
                    jump_count++;
                } else {
                    // Dynamic or cross-function tail call
                    // 1. Prepare args
                    jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2); // r10 = R
                    jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8); // r10 points to args
                    
                    // 2. Setup call info
                    emit_mov_rcx_imm64(&buf, (uintptr_t)orig_pc);
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd2); // rdx = r10 (args)
                    emit_mov_rsi_imm64(&buf, (uint64_t)pc->arg); // rsi = argc

                    if (target_fe) { 
                        emit_mov_rdi_imm64(&buf, (uintptr_t)target_fe); 
                        emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper); 
                    } else { 
                        emit_mov_rax_reg(&buf, pc->r1); // Load callee
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7); // rdi = callee
                        emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_val); 
                    }
                    
                    // 3. Epilogue (Pop frame) before jumping
                    emit_pop_frame(&buf);
                    
                    // 4. Jump (true tail call since we popped our frame)
                    jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xe0); // jmp rax
                }
                break;
            }
            case OP_INDEX: {
                emit_fast_index(&buf, pc);
                break;
            }
            case OP_STORE_INDEX: {
                emit_fast_store_index(&buf, pc);
                break;
            }
            case OP_SIMD_ADD: {
                emit_mov_rdi_reg(&buf, pc->r1); emit_mov_rsi_reg(&buf, pc->r2); emit_mov_rdx_reg(&buf, pc->r0);
                emit_mov_rax_imm64(&buf, (uintptr_t)simd_add_helper);
                emit_call_rax(&buf);
                jit_emit_byte(&buf, 0xe9); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++;
                break;
            }
            case OP_LOAD_LOCAL: {
                emit_mov_rax_reg(&buf, pc->arg);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_STORE_LOCAL: {
                emit_mov_rax_reg(&buf, pc->r0);
                emit_mov_reg_rax(&buf, pc->arg);
                break;
            }
            case OP_ADD_INT: {
                emit_jit_int_arith(&buf, 0, pc->r0, pc->r1, pc->r2, (uintptr_t)jit_int_add);
                break;
            }
            case OP_SUB_INT: {
                emit_jit_int_arith(&buf, 1, pc->r0, pc->r1, pc->r2, (uintptr_t)jit_int_sub);
                break;
            }
            case OP_MUL_INT: {
                emit_mov_rdi_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_int_mul);
                emit_call_rax(&buf); emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_LT_INT: case OP_GT_INT: case OP_LTE_INT: case OP_GTE_INT: case OP_EQ_INT: {
                uintptr_t cmp_fn =
                    pc->op == OP_LT_INT ? (uintptr_t)jit_int_lt :
                    pc->op == OP_GT_INT ? (uintptr_t)jit_int_gt :
                    pc->op == OP_LTE_INT ? (uintptr_t)jit_int_lte :
                    pc->op == OP_GTE_INT ? (uintptr_t)jit_int_gte : (uintptr_t)jit_int_eq;
                emit_jit_int_cmp(&buf, pc->op, pc->r0, pc->r1, pc->r2, cmp_fn);
                break;
            }
            case OP_RETURN: { 
                emit_mov_rax_reg(&buf, pc->r0); 
                emit_epilogue(&buf); 
                break; 
            }
            default: bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return;
        }
    }
    offsets[bc->count] = buf.size;
    for (int i = 0; i < jump_count; i++) { 
        uint32_t target_offset = (jumps[i].instr_idx == -1) ? body_start : offsets[jumps[i].instr_idx]; 
        uint32_t patch_pos = jumps[i].patch_pos; 
        int32_t rel = (int32_t)target_offset - (int32_t)(patch_pos + 4); 
        memcpy(code + patch_pos, &rel, 4); 
    }
    mprotect(code, size, PROT_READ | PROT_EXEC);
    bc->jit_code = code; bc->jit_size = buf.size;
    free(offsets); free(jumps);
}

void jit_compile(Bytecode *bc) { jit_compile_ext(bc, NULL, 0); }

void func_maybe_tier_up(FuncEntry *fe) {
    if (!fe || !fe->body) return;
    fe->exec_count++;

    // Tier 1: Baseline JIT after 10 executions
    if (fe->body->jit_tier == JIT_TIER_INTERP && !fe->body->baseline_jit_code && !fe->body->jit_failed) {
        if (fe->exec_count > 10) {
            jit_compile_baseline(fe->body);
        }
    }

    // Tier 2: Optimizing JIT after 100 executions
    if (fe->body->jit_tier == JIT_TIER_BASELINE && !fe->body->jit_code && !fe->body->jit_failed) {
        if (fe->exec_count > 100) {
            jit_compile(fe->body);
        }
    }
}

void jit_maybe_tier_up(Bytecode *bc) {
    if (!bc || bc->jit_tier >= JIT_TIER_OPTIMIZING || bc->jit_failed) return;
    bc->exec_count++;
    
    // Tier 1: Baseline JIT after 10 executions
    if (bc->jit_tier == JIT_TIER_INTERP && !bc->baseline_jit_code) {
        if (bc->exec_count > 10) {
            jit_compile_baseline(bc);
        }
    } else if (bc->jit_tier == JIT_TIER_BASELINE && !bc->jit_code) {
        if (bc->exec_count > 100) {
            jit_compile(bc);
        }
    }
}

// ================================================================
// BASELINE JIT (Tier 1) - Fast compilation, minimal optimization
// ================================================================

static void jit_baseline_emit_prologue(JITBuffer *buf) {
    emit_prologue(buf);
}

static void jit_baseline_emit_epilogue(JITBuffer *buf) {
    emit_epilogue(buf);
}

 void jit_compile_baseline_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) {
    (void)current_sig;
    if (!fe) fe = bc->owner;
    if (bc->baseline_jit_code || bc->jit_failed) return;


    for (int i = 0; i < bc->count; i++) {
        Instruction *pc = &bc->code[i];
        switch (pc->op) {
            case OP_CONST_INT: case OP_CONST_FLOAT: case OP_CONST_BOOL:
            case OP_LOAD_VAR: case OP_STORE_VAR: case OP_STORE_LET:
            case OP_MOVE: case OP_NOP:
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
            case OP_GET_PROPERTY: case OP_SET_PROPERTY: case OP_LT: case OP_GT: case OP_LTE:
            case OP_GTE: case OP_EQ: case OP_NEQ: case OP_JUMP:
            case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_RETURN:
            case OP_CALL: case OP_CALL_VALUE: case OP_CALL_SELF_FAST: case OP_CALL_METHOD:
            case OP_INDEX: case OP_STORE_INDEX:
            case OP_SIMD_ADD: case OP_TAIL_CALL:
            case OP_ADD_INT: case OP_SUB_INT: case OP_MUL_INT:
            case OP_LT_INT: case OP_GT_INT: case OP_LTE_INT: case OP_GTE_INT: case OP_EQ_INT:
            case OP_LOAD_LOCAL: case OP_STORE_LOCAL:
                break;
            default: bc->jit_failed = 1; return;
        }
    }

    // See jit_compile_ext: reject complex self-recursive bodies from the fast path.
    {
        int has_self = 0, has_complex = 0;
        for (int i = 0; i < bc->count; i++) {
            OpCode op = bc->code[i].op;
            if (op == OP_CALL_SELF_FAST) has_self = 1;
            else if (op == OP_STORE_INDEX || op == OP_CALL || op == OP_CALL_VALUE || op == OP_CALL_METHOD) has_complex = 1;
        }
        if (has_self && has_complex) { bc->jit_failed = 1; return; }
    }

    size_t size = 65536;
    uint8_t *code = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { bc->jit_failed = 1; return; }
    JITBuffer buf = {code, 0, size};

    jit_baseline_emit_prologue(&buf);

    uint32_t body_start = buf.size;

    uint32_t *offsets = calloc(bc->count + 1, sizeof(uint32_t));
    struct { int instr_idx; uint32_t patch_pos; } *jumps = calloc(bc->count + 1, sizeof(*jumps));
    int jump_count = 0;

    for (int i = 0; i < bc->count; i++) {
        offsets[i] = buf.size;
        Instruction *pc = &bc->code[i];
        Instruction *orig_pc = (fe && fe->body) ? &fe->body->code[i] : pc;
        switch (pc->op) {
            case OP_CONST_INT: emit_mov_rax_imm64(&buf, value_int(pc->arg)); emit_mov_reg_rax(&buf, pc->r0); break;
            case OP_CONST_FLOAT: emit_mov_rax_imm64(&buf, *(uint64_t*)pc->ptr); emit_mov_reg_rax(&buf, pc->r0); break;
            case OP_CONST_BOOL: emit_mov_rax_imm64(&buf, value_bool(pc->arg)); emit_mov_reg_rax(&buf, pc->r0); break;
            case OP_LOAD_VAR: {
                emit_mov_rcx_imm64(&buf, (uintptr_t)&global_table.values);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x09);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x81);
                jit_emit_u32(&buf, pc->arg * 8);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_STORE_VAR:
            case OP_STORE_LET: {
                emit_mov_rax_reg(&buf, pc->r0);
                emit_mov_rcx_imm64(&buf, (uintptr_t)&global_table.values);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x09);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x81);
                jit_emit_u32(&buf, pc->arg * 8);
                break;
            }
            case OP_MOVE: emit_mov_rax_reg(&buf, pc->r1); emit_mov_reg_rax(&buf, pc->r0); break;
            case OP_NOP: break;
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV: {
                if (pc->type == 1 && (pc->op == OP_ADD || pc->op == OP_SUB)) {
                    emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                    emit_mov_r10_imm64(&buf, 0xFFFF000000000000ULL);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc6);
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd6);
                    emit_mov_r11_imm64(&buf, 0x7FF8000000000000ULL);
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xde);
                    uint32_t p1 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xce);
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd6);
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xde);
                    uint32_t p2 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
                    if (pc->op == OP_ADD) { 
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0xc8);
                        jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xd8);
                    } else if (pc->op == OP_SUB) { 
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8);
                        jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xd8);
                    }
                    uint32_t pd = buf.size; jit_emit_byte(&buf, 0xe9); jit_emit_u32(&buf, 0);
                    uint32_t bailout = buf.size; *(uint32_t*)(buf.code + p1 + 2) = (uint32_t)(bailout - (p1 + 6)); *(uint32_t*)(buf.code + p2 + 2) = (uint32_t)(bailout - (p2 + 6));
                    emit_mov_rax_imm64(&buf, (uint64_t)i); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x45); jit_emit_byte(&buf, 0x00);
                    emit_mov_rax_imm64(&buf, MASK_NAN | (15ULL << 48)); emit_epilogue(&buf);
                    uint32_t done = buf.size; *(uint32_t*)(buf.code + pd + 1) = (uint32_t)(done - (pd + 5));
                    emit_mov_reg_rax(&buf, pc->r0);
                } else if (pc->type == 2) {
                    emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                    jit_emit_byte(&buf, 0x66); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x6e); jit_emit_byte(&buf, 0xc0);
                    jit_emit_byte(&buf, 0x66); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x6e); jit_emit_byte(&buf, 0xc1);
                    if (pc->op == OP_ADD) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x58); jit_emit_byte(&buf, 0xc1); }
                    else if (pc->op == OP_SUB) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0xc1); }
                    else if (pc->op == OP_MUL) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x59); jit_emit_byte(&buf, 0xc1); }
                    else if (pc->op == OP_DIV) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0xc1); }
                    else { bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return; }
                    jit_emit_byte(&buf, 0x66); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x7e); jit_emit_byte(&buf, 0xc0);
                    emit_mov_reg_rax(&buf, pc->r0);
                } else {
                    emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                    if (pc->op == OP_ADD) { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0xc8); emit_mov_rcx_imm64(&buf, 0x7FF8000000000000ULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8); }
                    else if (pc->op == OP_SUB) { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8); emit_mov_rcx_imm64(&buf, 0x7FF8000000000000ULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xc8); }
                    else if (pc->op == OP_MUL) {
                        emit_mov_rdx_imm64(&buf, 0x0000FFFFFFFFFFFFULL);
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd0);
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd1);
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0xaf); jit_emit_byte(&buf, 0xc1);
                        emit_mov_rdx_imm64(&buf, 0x7FF8000000000000ULL);
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xd0);
                    } else { bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return; }
                    emit_mov_reg_rax(&buf, pc->r0);
                }
                break;
            }
            case OP_GET_PROPERTY: {
                emit_mov_rax_reg(&buf, pc->r1);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7); // mov rdi, rax
                emit_mov_rsi_imm64(&buf, (uintptr_t)pc->ptr);
                emit_mov_rdx_imm64(&buf, (uintptr_t)orig_pc);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_get_property_helper);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_SET_PROPERTY: {
                emit_mov_rax_reg(&buf, pc->r0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7); // mov rdi, rax
                emit_mov_rsi_imm64(&buf, (uintptr_t)pc->ptr);
                emit_mov_rax_reg(&buf, pc->r1);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc2); // mov rdx, rax
                emit_mov_rcx_imm64(&buf, (uintptr_t)orig_pc);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_set_property_helper);
                emit_call_rax(&buf);
                break;
            }
            case OP_LT: case OP_GT: case OP_LTE: case OP_GTE: case OP_EQ: case OP_NEQ: {
                emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xc8);
                uint8_t set_op;
                switch(pc->op) { case OP_LT: set_op = 0x9c; break; case OP_GT: set_op = 0x9f; break; case OP_LTE: set_op = 0x9e; break; case OP_GTE: set_op = 0x9d; break; case OP_EQ: set_op = 0x94; break; case OP_NEQ: set_op = 0x95; break; default: set_op = 0x94; break; }
                jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, set_op); jit_emit_byte(&buf, 0xc1); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0xb6); jit_emit_byte(&buf, 0xc1); emit_mov_rax_imm64(&buf, value_bool(0)); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xc8); emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_JUMP: { jit_emit_byte(&buf, 0xe9); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
            case OP_JUMP_IF_FALSE: { emit_mov_rax_reg(&buf, pc->r0); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xc0); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
            case OP_JUMP_IF_TRUE: { emit_mov_rax_reg(&buf, pc->r0); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xc0); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
             case OP_CALL_SELF_FAST: {
                 if (fe) {
                     emit_self_call_direct(&buf, fe, pc);
                 } else {
                     emit_mov_rax_reg(&buf, pc->r1);
                     jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7);
                     emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_val);
                     emit_call_rax(&buf);
                     emit_mov_reg_rax(&buf, pc->r0);
                 }
                 break;
             }
             case OP_CALL:
             case OP_CALL_VALUE: {
                FuncEntry *target_fe = (pc->op == OP_CALL) ? func_get((const char*)pc->ptr) : (pc->op == OP_CALL_SELF_FAST ? fe : NULL);
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2);
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8);
                jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd7);
                emit_mov_rsi_imm64(&buf, (uint64_t)pc->arg);
                emit_mov_rax_imm64(&buf, (uintptr_t)compute_signature);
                emit_call_rax(&buf);
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2);
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8);
                emit_mov_rcx_imm64(&buf, (uintptr_t)&orig_pc->cache[0]);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x3b); jit_emit_byte(&buf, 0x01);
                uint32_t jne_cache = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0x79); jit_emit_byte(&buf, 0x08); jit_emit_byte(&buf, 0x00);
                uint32_t je_null = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84); jit_emit_u32(&buf, 0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x08);
                jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd7);
                jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xee);
                emit_call_rax(&buf);
                uint32_t jmp_done = buf.size; jit_emit_byte(&buf, 0xe9); jit_emit_u32(&buf, 0);
                uint32_t slow_path = buf.size; 
                *(uint32_t*)(buf.code + jne_cache + 2) = (uint32_t)(slow_path - (jne_cache + 6));
                *(uint32_t*)(buf.code + je_null + 2) = (uint32_t)(slow_path - (je_null + 6));
                emit_mov_rcx_imm64(&buf, (uintptr_t)orig_pc);
                jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd2);
                emit_mov_rsi_imm64(&buf, (uint64_t)pc->arg);
                if (target_fe) { 
                    emit_mov_rdi_imm64(&buf, (uintptr_t)target_fe); 
                    emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper); 
                } else { 
                    emit_mov_rax_reg(&buf, pc->r1); 
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7);
                    emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_val); 
                }
                emit_call_rax(&buf);
                uint32_t done = buf.size; *(uint32_t*)(buf.code + jmp_done + 1) = (uint32_t)(done - (jmp_done + 5));
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_CALL_METHOD: {
                emit_mov_rax_reg(&buf, pc->r1); 
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7);
                emit_mov_rsi_imm64(&buf, (uintptr_t)pc->ptr);
                emit_mov_rdx_imm64(&buf, (uint64_t)pc->arg);
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2);
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8);
                jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd1);
                emit_mov_r8_imm64(&buf, (uintptr_t)orig_pc);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_method_helper);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_TAIL_CALL: {
                FuncEntry *target_fe = (pc->ptr) ? func_get((const char*)pc->ptr) : NULL;
                if (target_fe && target_fe == fe) {
                    for (int k = 0; k < pc->arg && k < fe->param_count; k++) {
                        emit_mov_rax_reg(&buf, pc->r2 + k);
                        emit_mov_reg_rax(&buf, k);
                    }
                    jit_emit_byte(&buf, 0xe9);
                    jumps[jump_count].instr_idx = -1;
                    jumps[jump_count].patch_pos = buf.size;
                    jit_emit_u32(&buf, 0);
                    jump_count++;
                } else {
                    jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2);
                    jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8);
                    emit_mov_rcx_imm64(&buf, (uintptr_t)orig_pc);
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd2);
                    emit_mov_rsi_imm64(&buf, (uint64_t)pc->arg);
                    if (target_fe) { 
                        emit_mov_rdi_imm64(&buf, (uintptr_t)target_fe); 
                        emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper); 
                    } else { 
                        emit_mov_rax_reg(&buf, pc->r1);
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc7);
                        emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_val); 
                    }
                    emit_pop_frame(&buf);
                    jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xe0);
                }
                break;
            }
            case OP_INDEX: {
                emit_fast_index(&buf, pc);
                break;
            }
            case OP_STORE_INDEX: {
                emit_fast_store_index(&buf, pc);
                break;
            }
            case OP_SIMD_ADD: {
                emit_mov_rdi_reg(&buf, pc->r1); emit_mov_rsi_reg(&buf, pc->r2); emit_mov_rdx_reg(&buf, pc->r0);
                emit_mov_rax_imm64(&buf, (uintptr_t)simd_add_helper);
                emit_call_rax(&buf);
                break;
            }
            case OP_LOAD_LOCAL: {
                emit_mov_rax_reg(&buf, pc->arg);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_STORE_LOCAL: {
                emit_mov_rax_reg(&buf, pc->r0);
                emit_mov_reg_rax(&buf, pc->arg);
                break;
            }
            case OP_ADD_INT: {
                emit_jit_int_arith(&buf, 0, pc->r0, pc->r1, pc->r2, (uintptr_t)jit_int_add);
                break;
            }
            case OP_SUB_INT: {
                emit_jit_int_arith(&buf, 1, pc->r0, pc->r1, pc->r2, (uintptr_t)jit_int_sub);
                break;
            }
            case OP_MUL_INT: {
                emit_mov_rdi_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_int_mul);
                emit_call_rax(&buf); emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_LT_INT: case OP_GT_INT: case OP_LTE_INT: case OP_GTE_INT: case OP_EQ_INT: {
                uintptr_t cmp_fn =
                    pc->op == OP_LT_INT ? (uintptr_t)jit_int_lt :
                    pc->op == OP_GT_INT ? (uintptr_t)jit_int_gt :
                    pc->op == OP_LTE_INT ? (uintptr_t)jit_int_lte :
                    pc->op == OP_GTE_INT ? (uintptr_t)jit_int_gte : (uintptr_t)jit_int_eq;
                emit_jit_int_cmp(&buf, pc->op, pc->r0, pc->r1, pc->r2, cmp_fn);
                break;
            }
            case OP_RETURN: { 
                emit_mov_rax_reg(&buf, pc->r0); 
                jit_baseline_emit_epilogue(&buf); 
                break; 
            }
            default: bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return;
        }
    }
    offsets[bc->count] = buf.size;
    for (int i = 0; i < jump_count; i++) { 
        uint32_t target_offset = (jumps[i].instr_idx == -1) ? body_start : offsets[jumps[i].instr_idx]; 
        uint32_t patch_pos = jumps[i].patch_pos; 
        int32_t rel = (int32_t)target_offset - (int32_t)(patch_pos + 4); 
        memcpy(code + patch_pos, &rel, 4); 
    }
    mprotect(code, size, PROT_READ | PROT_EXEC);
    bc->baseline_jit_code = code; bc->baseline_jit_size = buf.size; bc->baseline_jit_offsets = offsets; bc->jit_tier = JIT_TIER_BASELINE;
    free(jumps);
}

void jit_compile_baseline(Bytecode *bc) { jit_compile_baseline_ext(bc, NULL, 0); }

static void propagate_types(Instruction *code, int count, int param_count, TypeSignature sig, FuncEntry *fe) {
    unsigned char reg_types[256]; memset(reg_types, 0, 256);
    for (int i = 0; i < param_count && i < 16; i++) reg_types[i] = (sig >> (i * 4)) & 0xF;
    for (int i = 0; i < count; i++) {
        Instruction *pc = &code[i];
        switch (pc->op) {
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
                if (reg_types[pc->r1] == 1 && reg_types[pc->r2] == 1) { pc->type = 1; reg_types[pc->r0] = 1; }
                else if ((reg_types[pc->r1] == 1 || reg_types[pc->r1] == 2) && (reg_types[pc->r2] == 1 || reg_types[pc->r2] == 2)) { pc->type = 2; reg_types[pc->r0] = 2; }
                break;
            case OP_CONST_INT: reg_types[pc->r0] = 1; pc->type = 1; break;
            case OP_CONST_FLOAT: reg_types[pc->r0] = 2; pc->type = 2; break;
            case OP_MOVE: reg_types[pc->r0] = reg_types[pc->r1]; break;
            case OP_CALL: {
                FuncEntry *target_fe = func_get((const char*)pc->ptr);
                if (target_fe && target_fe == fe && sig == 1) reg_types[pc->r0] = 1;
                else reg_types[pc->r0] = 0;
                break;
            }
            case OP_CALL_SELF_FAST:
                reg_types[pc->r0] = (sig == 1) ? 1 : 0;
                break;
            default: break;
        }
    }
}

void jit_compile_specialized(FuncEntry *fe, TypeSignature sig) {
    if (!fe || !fe->body) return;
    Bytecode *sbc = calloc(1, sizeof(Bytecode));
    sbc->count = fe->body->count;
    sbc->code = malloc(sizeof(Instruction) * sbc->count);
    memcpy(sbc->code, fe->body->code, sizeof(Instruction) * sbc->count);
    for (int i=0; i<sbc->count; i++) sbc->code[i].trace = NULL;
    sbc->local_count = fe->body->local_count;
    propagate_types(sbc->code, sbc->count, fe->param_count, sig, fe);
    jit_compile_ext(sbc, fe, sig);
    if (sbc->jit_code) func_add_specialization(fe, sig, sbc->jit_code, sbc->jit_size);
    else func_add_specialization(fe, sig, (void*)1, 0);
    free(sbc->code); free(sbc);
}

#elif defined(__aarch64__) || defined(_M_ARM64)
void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { if (bc) bc->jit_failed = 1; }
void jit_compile(Bytecode *bc) { if (bc) bc->jit_failed = 1; }
void jit_compile_specialized(FuncEntry *fe, TypeSignature sig) { if (fe) func_add_specialization(fe, sig, (void*)1, 0); }

void jit_compile_baseline_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { if (bc) bc->jit_failed = 1; }
void jit_compile_baseline(Bytecode *bc) { if (bc) bc->jit_failed = 1; }
void jit_maybe_tier_up(Bytecode *bc) { (void)bc; }
void func_maybe_tier_up(FuncEntry *fe) { (void)fe; }
#else
void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { if (bc) bc->jit_failed = 1; }
void jit_compile(Bytecode *bc) { if (bc) bc->jit_failed = 1; }
void jit_compile_specialized(FuncEntry *fe, TypeSignature sig) { if (fe) func_add_specialization(fe, sig, (void*)1, 0); }

void jit_compile_baseline_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { if (bc) bc->jit_failed = 1; }
void jit_compile_baseline(Bytecode *bc) { if (bc) bc->jit_failed = 1; }
void jit_maybe_tier_up(Bytecode *bc) { (void)bc; }
void func_maybe_tier_up(FuncEntry *fe) { (void)fe; }
#endif
