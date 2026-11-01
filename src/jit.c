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
    BOOL res = VirtualProtect(addr, len, newProtect, &oldProtect);
    if (res && (prot & PROT_EXEC)) {
        FlushInstructionCache(GetCurrentProcess(), addr, len);
    }
    return res ? 0 : -1;
}
static int munmap(void *addr, size_t length) {
    (void)length;
    return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1;
}
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#define GLOW_JIT_WIN64 1
#else
#define GLOW_JIT_SYSV 1
#endif

/* JIT helper function declarations */
extern Value jit_val_lt(Value a, Value b);
extern Value jit_val_gt(Value a, Value b);
extern Value jit_val_lte(Value a, Value b);
extern Value jit_val_gte(Value a, Value b);
extern Value jit_val_eq(Value a, Value b);
extern Value jit_val_neq(Value a, Value b);

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern FuncEntry* func_get(const char *name);

typedef struct { uint8_t *code; size_t size; size_t capacity; } JITBuffer;

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
/* GLOW_JIT_DEBUG=1: report every function compiled, on stderr. */
static void jit_debug_compiled(Bytecode *bc, FuncEntry *fe, const char *tier, size_t bytes) {
    if (!glow_jit_debug) return;
    if (!fe) fe = bc->owner;
    fprintf(stderr, "[jit] %s: %s (%zu bytes)\n", tier, (fe && fe->name) ? fe->name : "<script>", bytes);
}
#endif

static void jit_emit_byte(JITBuffer *buf, uint8_t b) {
    if (buf->size < buf->capacity) buf->code[buf->size++] = b;
}
static void jit_emit_u32(JITBuffer *buf, uint32_t v) {
    if (buf->size + 4 <= buf->capacity) { memcpy(buf->code + buf->size, &v, 4); buf->size += 4; }
}
static void jit_emit_u64(JITBuffer *buf, uint64_t v) {
    if (buf->size + 8 <= buf->capacity) { memcpy(buf->code + buf->size, &v, 8); buf->size += 8; }
}

/*
 * NOTE: The machine-code emitter below targets the System V AMD64 ABI
 * (integer args in RDI, RSI, RDX, RCX, R8, R9; RSI/RDI caller-saved).
 * Windows x64 uses a different calling convention (Microsoft x64 ABI:
 * integer args in RCX, RDX, R8, R9; 32-byte shadow space; RSI/RDI and
 * XMM6-15 callee-saved). Running this SysV-targeted code under the
 * Microsoft x64 ABI corrupts argument passing / callee-saved registers,
 * causing access violations in JIT-compiled function calls. Until a
 * dedicated Windows x64 ABI backend is implemented, exclude Windows so it
 * uses the proven interpreter fallback (same path as macOS arm64). This
 * mirrors the existing idiom in stdlib.c. Linux/macOS x86-64 are
 * unaffected and keep the full JIT.
 */
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

static void emit_mov_r9_imm64(JITBuffer *buf, uint64_t imm) {
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0xb9); jit_emit_u64(buf, imm);
}

static void emit_load_cpu_regs(JITBuffer *buf) { (void)buf; }
static void emit_save_cpu_regs(JITBuffer *buf) { (void)buf; }

static void emit_prologue(JITBuffer *buf) {
    jit_emit_byte(buf, 0x55); // push rbp
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xe5); // mov rbp, rsp
    jit_emit_byte(buf, 0x53); // push rbx
#ifdef GLOW_JIT_WIN64
    jit_emit_byte(buf, 0x56); // push rsi
    jit_emit_byte(buf, 0x57); // push rdi
#endif
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x54); // push r12
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x55); // push r13
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x56); // push r14
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x57); // push r15
#ifdef GLOW_JIT_WIN64
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x83); jit_emit_byte(buf, 0xec); jit_emit_byte(buf, 0x28); // sub rsp, 40 (8 align + 32 shadow space, reserved once)
#else
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x83); jit_emit_byte(buf, 0xec); jit_emit_byte(buf, 0x08); // sub rsp, 8 (keep 16-byte aligned)
#endif
#ifdef GLOW_JIT_WIN64
    // R12 = regs (from RCX)
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xcc); // mov r12, rcx
    // R13 = pc_idx (from RDX)
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd5); // mov r13, rdx
#else
    // R12 = regs (from RDI)
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xfc); // mov r12, rdi
    // R13 = pc_idx (from RSI)
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xf5); // mov r13, rsi
#endif
    emit_load_cpu_regs(buf);
}

static void emit_mov_reg_rax(JITBuffer *buf, int reg);
static void emit_zero_locals(JITBuffer *buf, Bytecode *bc, FuncEntry *fe) {
    if (!bc) return;
    int start = (fe && fe->param_count > 0) ? fe->param_count : 0;
    if (fe && fe->name && strchr(fe->name, ':')) start++;
    int end = bc->local_count;
    if (start >= end) return;
    emit_mov_rax_imm64(buf, value_none());
    if (end - start <= 16) {
        for (int i = start; i < end; i++) emit_mov_reg_rax(buf, i);
        return;
    }
    /* lea rdi, [r12 + start*8]; mov ecx, end-start; rep stosq. rdi and rcx
       are free here (rdi is saved by the prologue on Win64). */
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8d); jit_emit_byte(buf, 0xbc); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, (uint32_t)(start * 8));
    jit_emit_byte(buf, 0xb9); jit_emit_u32(buf, (uint32_t)(end - start));
    jit_emit_byte(buf, 0xf3); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xab);
}

static void emit_pop_frame(JITBuffer *buf) {
    emit_load_cpu_regs(buf);
#ifdef GLOW_JIT_WIN64
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x83); jit_emit_byte(buf, 0xc4); jit_emit_byte(buf, 0x28); // add rsp, 40
#else
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x83); jit_emit_byte(buf, 0xc4); jit_emit_byte(buf, 0x08); // add rsp, 8
#endif
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x5f); // pop r15
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x5e); // pop r14
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x5d); // pop r13
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x5c); // pop r12
#ifdef GLOW_JIT_WIN64
    jit_emit_byte(buf, 0x5f); // pop rdi
    jit_emit_byte(buf, 0x5e); // pop rsi
#endif
    jit_emit_byte(buf, 0x5b); // pop rbx
    jit_emit_byte(buf, 0x5d); // pop rbp
}

static void emit_epilogue(JITBuffer *buf) {
    emit_pop_frame(buf);
    jit_emit_byte(buf, 0xc3);
}

static void emit_call_rax(JITBuffer *buf) {
    emit_save_cpu_regs(buf);
    // Shadow space is pre-reserved in prologue on Win64, so just call directly
    jit_emit_byte(buf, 0xff); jit_emit_byte(buf, 0xd0); // call rax
    emit_load_cpu_regs(buf);
}

/* Leave compiled code and resume the interpreter at instruction `i`. All
   live state is already in the register file, so this is just a return of
   the deopt sentinel with the resume index stored through r13. */
static void emit_side_exit(JITBuffer *buf, int i) {
    emit_mov_rax_imm64(buf, (uint64_t)i);
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x45); jit_emit_byte(buf, 0x00); // mov [r13], eax
    emit_mov_rax_imm64(buf, VAL_DEOPT_SENTINEL);
    emit_epilogue(buf);
}

extern int jit_exception_pending(void);

/* After a call into the runtime: if it raised, exit to the interpreter at
   this instruction, which sees the pending exception and unwinds properly. */
static void emit_exception_check(JITBuffer *buf, int i) {
    emit_mov_rax_imm64(buf, (uintptr_t)jit_exception_pending);
    emit_call_rax(buf);
    jit_emit_byte(buf, 0x85); jit_emit_byte(buf, 0xc0);   // test eax, eax
    jit_emit_byte(buf, 0x74);                              // jz skip
    uint32_t pos = buf->size; jit_emit_byte(buf, 0);
    emit_side_exit(buf, i);
    if (pos < buf->size) buf->code[pos] = (uint8_t)(buf->size - (pos + 1));
}

/* After a runtime call helper: the deopt sentinel means the helper declined
   the call without doing anything, so let the interpreter run this
   instruction. */
/* Exit to the interpreter at instruction i when rax holds `bits`. */
static void emit_deopt_if_rax_eq(JITBuffer *buf, uint64_t bits, int i) {
    emit_mov_rcx_imm64(buf, bits);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x39); jit_emit_byte(buf, 0xc8); // cmp rax, rcx
    jit_emit_byte(buf, 0x75);                                                     // jne skip
    uint32_t pos = buf->size; jit_emit_byte(buf, 0);
    emit_side_exit(buf, i);
    if (pos < buf->size) buf->code[pos] = (uint8_t)(buf->size - (pos + 1));
}

static void emit_deopt_check(JITBuffer *buf, int i) {
    emit_deopt_if_rax_eq(buf, VAL_DEOPT_SENTINEL, i);
}

extern Value jit_call_helper_name(const char *name, int argc, Value *args, Instruction *pc);

/* At loop back-edges: hand the GIL to a waiting thread, if any. */
static void emit_gil_poll(JITBuffer *buf) {
    emit_mov_rax_imm64(buf, (uintptr_t)&glow_gil_waiters);
    jit_emit_byte(buf, 0x83); jit_emit_byte(buf, 0x38); jit_emit_byte(buf, 0x00); // cmp dword [rax], 0
    jit_emit_byte(buf, 0x74);                                                     // je skip
    uint32_t pos = buf->size; jit_emit_byte(buf, 0);
    emit_mov_rax_imm64(buf, (uintptr_t)glow_gil_yield);
    emit_call_rax(buf);
    if (pos < buf->size) buf->code[pos] = (uint8_t)(buf->size - (pos + 1));
}

/* Code size budget: generous per-instruction bound plus room for OSR stubs. */
static size_t jit_buffer_size(Bytecode *bc) {
    size_t sz = (size_t)bc->count * 320 + 16384;
    return (sz + 65535) & ~(size_t)65535;
}

static void emit_mov_rax_reg(JITBuffer *buf, int reg) {
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x84); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
}
static void emit_mov_reg_rax(JITBuffer *buf, int reg) {
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x84); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
}
static void emit_mov_rcx_reg(JITBuffer *buf, int reg) {
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x8c); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
}
static void emit_mov_rdx_reg(JITBuffer *buf, int reg) {
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x94); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
}
static void emit_mov_rdi_reg(JITBuffer *buf, int reg) {
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0xbc); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
}
static void emit_mov_rsi_reg(JITBuffer *buf, int reg) {
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0xb4); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
}
static void emit_mov_r8_reg(JITBuffer *buf, int reg) {
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x84); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
}
static void emit_mov_r9_reg(JITBuffer *buf, int reg) {
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x8c); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
}

/* ABI Abstraction Helpers for C Function Calls */
static void emit_mov_arg1_imm64(JITBuffer *buf, uint64_t imm) {
#ifdef GLOW_JIT_WIN64
    emit_mov_rcx_imm64(buf, imm);
#else
    emit_mov_rdi_imm64(buf, imm);
#endif
}
static void emit_mov_arg1_reg(JITBuffer *buf, int reg) {
#ifdef GLOW_JIT_WIN64
    emit_mov_rcx_reg(buf, reg);
#else
    emit_mov_rdi_reg(buf, reg);
#endif
}
static void emit_mov_arg1_from_rax(JITBuffer *buf) {
#ifdef GLOW_JIT_WIN64
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc1); // mov rcx, rax
#else
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc7); // mov rdi, rax
#endif
}
static void emit_mov_arg1_r10(JITBuffer *buf) {
#ifdef GLOW_JIT_WIN64
    jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd1); // mov rcx, r10
#else
    jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd7); // mov rdi, r10
#endif
}

static void emit_mov_arg2_imm64(JITBuffer *buf, uint64_t imm) {
#ifdef GLOW_JIT_WIN64
    emit_mov_rdx_imm64(buf, imm);
#else
    emit_mov_rsi_imm64(buf, imm);
#endif
}
static void emit_mov_arg2_reg(JITBuffer *buf, int reg) {
#ifdef GLOW_JIT_WIN64
    emit_mov_rdx_reg(buf, reg);
#else
    emit_mov_rsi_reg(buf, reg);
#endif
}
static void emit_mov_arg2_from_rcx(JITBuffer *buf) {
#ifdef GLOW_JIT_WIN64
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xca); // mov rdx, rcx
#else
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xce); // mov rsi, rcx
#endif
}
static void emit_mov_arg2_r10(JITBuffer *buf) {
#ifdef GLOW_JIT_WIN64
    jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd2); // mov rdx, r10
#else
    jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd6); // mov rsi, r10
#endif
}

static void emit_mov_arg3_imm64(JITBuffer *buf, uint64_t imm) {
#ifdef GLOW_JIT_WIN64
    emit_mov_r8_imm64(buf, imm);
#else
    emit_mov_rdx_imm64(buf, imm);
#endif
}
static void emit_mov_arg3_reg(JITBuffer *buf, int reg) {
#ifdef GLOW_JIT_WIN64
    emit_mov_r8_reg(buf, reg);
#else
    emit_mov_rdx_reg(buf, reg);
#endif
}
static void emit_mov_arg3_r10(JITBuffer *buf) {
#ifdef GLOW_JIT_WIN64
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd0); // mov r8, r10
#else
    jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd2); // mov rdx, r10
#endif
}

static void emit_mov_arg4_imm64(JITBuffer *buf, uint64_t imm) {
#ifdef GLOW_JIT_WIN64
    emit_mov_r9_imm64(buf, imm);
#else
    emit_mov_rcx_imm64(buf, imm);
#endif
}
static void emit_mov_arg4_reg(JITBuffer *buf, int reg) {
#ifdef GLOW_JIT_WIN64
    emit_mov_r9_reg(buf, reg);
#else
    emit_mov_rcx_reg(buf, reg);
#endif
}
static void emit_mov_arg4_r10(JITBuffer *buf) {
#ifdef GLOW_JIT_WIN64
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd1); // mov r9, r10
#else
    jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd1); // mov rcx, r10
#endif
}

static void emit_mov_arg5_imm64(JITBuffer *buf, uint64_t imm) {
#ifdef GLOW_JIT_WIN64
    emit_mov_rax_imm64(buf, imm);
    // Shadow space is pre-reserved in prologue; arg5 goes to [rsp + 32]
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x44); jit_emit_byte(buf, 0x24); jit_emit_byte(buf, 0x20); // mov [rsp + 32], rax
#else
    emit_mov_r8_imm64(buf, imm);
#endif
}
static void emit_mov_arg5_reg(JITBuffer *buf, int reg) {
#ifdef GLOW_JIT_WIN64
    emit_mov_rax_reg(buf, reg);
    // Shadow space is pre-reserved in prologue; arg5 goes to [rsp + 32]
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x44); jit_emit_byte(buf, 0x24); jit_emit_byte(buf, 0x20); // mov [rsp + 32], rax
#else
    emit_mov_r8_reg(buf, reg);
#endif
}

/* Self-recursion: jit_self_call_direct(fe, &R[r2]) always completes the call;
   it returns the deopt sentinel only when the call raised, and then the
   interpreter takes over at this instruction to unwind. */
static void emit_self_call_direct(JITBuffer *buf, FuncEntry *fe, Instruction *pc, int i) {
    jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xe2);
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xc2); jit_emit_u32(buf, pc->r2 * 8);
    emit_mov_arg2_r10(buf);
    emit_mov_arg1_imm64(buf, (uintptr_t)fe);
    emit_mov_rax_imm64(buf, (uintptr_t)jit_self_call_direct);
    emit_call_rax(buf);
    emit_deopt_check(buf, i);
    emit_mov_reg_rax(buf, pc->r0);
}

static void emit_deopt_check(JITBuffer *buf, int i);
static void emit_fast_index(JITBuffer *buf, Instruction *pc, int i) {
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

    emit_mov_arg1_reg(buf, pc->r1); emit_mov_arg2_reg(buf, pc->r2);
    emit_mov_rax_imm64(buf, (uintptr_t)glow_index_jit);
    emit_call_rax(buf);
    emit_deopt_check(buf, i);
    emit_mov_reg_rax(buf, pc->r0);

    uint32_t done_pos = buf->size;
    *(uint32_t*)&buf->code[jmp_done + 1] = done_pos - (jmp_done + 5);
}

static void emit_fast_store_index(JITBuffer *buf, Instruction *pc, int i) {
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

    emit_mov_arg1_reg(buf, pc->r0); emit_mov_arg2_reg(buf, pc->r1); emit_mov_arg3_reg(buf, pc->r2);
    emit_mov_rax_imm64(buf, (uintptr_t)glow_store_index_jit);
    emit_call_rax(buf);
    emit_deopt_check(buf, i);

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

/* Re-box the signed 64-bit integer in rax (already checked to fit in 48
   bits) as an inline int: keep the low 48 bits, then add the tag. ORing the
   tag onto the sign-extended value (as before) turned negative results into
   garbage values. Clobbers rdx. */
static void emit_box_int_rax(JITBuffer *buf) {
    emit_mov_rdx_imm64(buf, MASK_PAYLOAD);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x21); jit_emit_byte(buf, 0xd0); // and rax, rdx
    emit_mov_rdx_imm64(buf, MASK_NAN);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x09); jit_emit_byte(buf, 0xd0); // or rax, rdx
}

/* Move the boxed operands R[r1], R[r2] into the first two argument
   registers and call a two-argument runtime helper. Leaves the result in rax. */
static void emit_call_binop_helper(JITBuffer *buf, int r1, int r2, uintptr_t fn) {
    emit_mov_arg1_reg(buf, r1);
    emit_mov_arg2_reg(buf, r2);
    emit_mov_rax_imm64(buf, fn);
    emit_call_rax(buf);
}

/* ---- Inline float arithmetic ----
   Compiled + - * / on floats, matching the interpreter's numeric tower
   (num_add & co. in vm.c) exactly:
   - an operand is an inline int or a double; anything else (big ints,
     strings, ...) goes to the helper;
   - + - * use doubles only when an operand is a fractional double (a
     float holding a whole number takes the exact integer path), so they go
     to the helper when neither is; NaN, infinity and doubles too big for an
     int64 also go to the helper, which knows how to treat them;
   - / always divides doubles, except by zero, which the helper declines so
     the interpreter raises the error;
   - the result is boxed as num_from_double does: a whole number that fits
     in 48 bits becomes an int, NaN becomes CANON_NAN, the rest stay doubles.
   Uses rax, rdx, r10, r11 and xmm0-xmm2. */

typedef struct { uint32_t at[8]; int n; } JumpList;

static void jump_list_add(JumpList *l, uint32_t pos) { if (l->n < 8) l->at[l->n++] = pos; }

static void jump_list_patch(JITBuffer *buf, JumpList *l, uint32_t target) {
    for (int k = 0; k < l->n; k++)
        *(uint32_t*)(buf->code + l->at[k] + 2) = (uint32_t)(target - (l->at[k] + 6));
}

/* jcc rel32 (0F cc), recorded in l for patching. */
static void emit_jcc_to(JITBuffer *buf, unsigned char cc, JumpList *l) {
    jump_list_add(l, buf->size);
    jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, cc); jit_emit_u32(buf, 0);
}

/* Load R[r] into xmm<x> as a double. Non-numbers jump to fail. With
   check_frac, a double that does not convert to int64 (NaN, infinity,
   |d| >= 2^63) also jumps to fail, and r11 gets 1 if it has a fraction. */
static void emit_load_number_xmm(JITBuffer *buf, int r, int x, int check_frac, JumpList *fail) {
    emit_mov_rax_reg(buf, r);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2); // mov rdx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xf8); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7FF8 (inline int)
    uint32_t jne_not_int = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe0); jit_emit_byte(buf, 0x10); // shl rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xf8); jit_emit_byte(buf, 0x10); // sar rax, 16
    jit_emit_byte(buf, 0xf2); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2a); jit_emit_byte(buf, (unsigned char)(0xc0 | (x << 3))); // cvtsi2sd xmmX, rax
    uint32_t jmp_next = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    uint32_t not_int = buf->size;
    /* A boxed non-double has its top 13 bits equal to 0x7FF8's (IS_FLOAT_BITS). */
    jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xe2); jit_emit_u32(buf, 0xfff8); // and edx, 0xFFF8
    jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_u32(buf, 0x7ff8); // cmp edx, 0x7FF8
    emit_jcc_to(buf, 0x84, fail); // je fail
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x6e); jit_emit_byte(buf, (unsigned char)(0xc0 | (x << 3))); // movq xmmX, rax
    if (check_frac) {
        jit_emit_byte(buf, 0xf2); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2c); jit_emit_byte(buf, (unsigned char)(0xd0 | x)); // cvttsd2si rdx, xmmX
        emit_mov_r10_imm64(buf, 0x8000000000000000ULL); // "integer indefinite": NaN, inf or out of range
        jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x39); jit_emit_byte(buf, 0xd2); // cmp rdx, r10
        emit_jcc_to(buf, 0x84, fail); // je fail
        jit_emit_byte(buf, 0xf2); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2a); jit_emit_byte(buf, 0xd2); // cvtsi2sd xmm2, rdx
        jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2e); jit_emit_byte(buf, (unsigned char)(0xc2 | (x << 3))); // ucomisd xmmX, xmm2
        jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x95); jit_emit_byte(buf, 0xc2); // setne dl
        jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0xb6); jit_emit_byte(buf, 0xd2); // movzx edx, dl
        jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x09); jit_emit_byte(buf, 0xd3); // or r11, rdx
    }
    uint32_t next = buf->size;
    *(uint32_t*)(buf->code + jne_not_int + 2) = (uint32_t)(not_int - (jne_not_int + 6));
    *(uint32_t*)(buf->code + jmp_next + 1) = (uint32_t)(next - (jmp_next + 5));
}

/* R[r1] op R[r2] in doubles, boxed into rax; jumps to fail when the
   helper has to decide. Falls through with the result in rax. */
static void emit_float_binop(JITBuffer *buf, int op, int r1, int r2, JumpList *fail) {
    int is_div = (op == OP_DIV);
    if (!is_div) { jit_emit_byte(buf, 0x45); jit_emit_byte(buf, 0x31); jit_emit_byte(buf, 0xdb); } // xor r11d, r11d
    emit_load_number_xmm(buf, r1, 0, !is_div, fail);
    emit_load_number_xmm(buf, r2, 1, !is_div, fail);
    if (is_div) {
        jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x57); jit_emit_byte(buf, 0xd2); // xorpd xmm2, xmm2
        jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2e); jit_emit_byte(buf, 0xca); // ucomisd xmm1, xmm2
        emit_jcc_to(buf, 0x84, fail); // je fail: zero (or NaN) divisor
    } else {
        jit_emit_byte(buf, 0x4d); jit_emit_byte(buf, 0x85); jit_emit_byte(buf, 0xdb); // test r11, r11
        emit_jcc_to(buf, 0x84, fail); // jz fail: no fractional operand, exact integer rules apply
    }
    unsigned char opc = op == OP_ADD ? 0x58 : op == OP_SUB ? 0x5c : op == OP_MUL ? 0x59 : 0x5e;
    jit_emit_byte(buf, 0xf2); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, opc); jit_emit_byte(buf, 0xc1); // addsd/subsd/mulsd/divsd xmm0, xmm1

    JumpList as_float = {0}, is_nan = {0};
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2e); jit_emit_byte(buf, 0xc0); // ucomisd xmm0, xmm0
    emit_jcc_to(buf, 0x8a, &is_nan); // jp nan
    jit_emit_byte(buf, 0xf2); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2c); jit_emit_byte(buf, 0xc0); // cvttsd2si rax, xmm0
    emit_mov_rdx_imm64(buf, INT48_MAX);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x3b); jit_emit_byte(buf, 0xc2); // cmp rax, rdx
    emit_jcc_to(buf, 0x8f, &as_float); // jg
    emit_mov_rdx_imm64(buf, INT48_MIN);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x3b); jit_emit_byte(buf, 0xc2); // cmp rax, rdx
    emit_jcc_to(buf, 0x8c, &as_float); // jl
    jit_emit_byte(buf, 0xf2); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2a); jit_emit_byte(buf, 0xd0); // cvtsi2sd xmm2, rax
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2e); jit_emit_byte(buf, 0xc2); // ucomisd xmm0, xmm2
    emit_jcc_to(buf, 0x85, &as_float); // jne: has a fraction
    emit_box_int_rax(buf);
    uint32_t jmp_end1 = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    jump_list_patch(buf, &as_float, buf->size);
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x7e); jit_emit_byte(buf, 0xc0); // movq rax, xmm0
    uint32_t jmp_end2 = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    jump_list_patch(buf, &is_nan, buf->size);
    emit_mov_rax_imm64(buf, CANON_NAN);
    uint32_t end = buf->size;
    *(uint32_t*)(buf->code + jmp_end1 + 1) = (uint32_t)(end - (jmp_end1 + 5));
    *(uint32_t*)(buf->code + jmp_end2 + 1) = (uint32_t)(end - (jmp_end2 + 5));
}

// Inline integer add/sub; promotes to a double (exact for the whole 48-bit
// range) when the result leaves the signed 48-bit range. Other operands try
// inline float arithmetic, then fall back to slow_fn (a jit_int_* helper).
static void emit_jit_int_arith(JITBuffer *buf, int is_sub, int r0, int r1, int r2, uintptr_t slow_fn, int i) {
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
    emit_box_int_rax(buf);
    uint32_t jmp_done = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    uint32_t promote_pos = buf->size;
    jit_emit_byte(buf, 0xf2); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2a); jit_emit_byte(buf, 0xc0); // cvtsi2sd xmm0, rax
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x7e); jit_emit_byte(buf, 0xc0); // movq rax, xmm0
    uint32_t jmp_done2 = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    uint32_t slow_pos = buf->size;
    JumpList helper = {0};
    emit_float_binop(buf, is_sub ? OP_SUB : OP_ADD, r1, r2, &helper);
    uint32_t jmp_done3 = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    jump_list_patch(buf, &helper, buf->size);
    emit_call_binop_helper(buf, r1, r2, slow_fn);
    emit_deopt_check(buf, i); /* the interpreter raises type errors */
    uint32_t done_pos = buf->size;
    emit_mov_reg_rax(buf, r0);
    *(uint32_t*)(buf->code + jne_a + 2) = (uint32_t)(slow_pos - (jne_a + 6));
    *(uint32_t*)(buf->code + jne_b + 2) = (uint32_t)(slow_pos - (jne_b + 6));
    *(uint32_t*)(buf->code + jg_over + 2) = (uint32_t)(promote_pos - (jg_over + 6));
    *(uint32_t*)(buf->code + jl_under + 2) = (uint32_t)(promote_pos - (jl_under + 6));
    *(uint32_t*)(buf->code + jmp_done + 1) = (uint32_t)(done_pos - (jmp_done + 5));
    *(uint32_t*)(buf->code + jmp_done2 + 1) = (uint32_t)(done_pos - (jmp_done2 + 5));
    *(uint32_t*)(buf->code + jmp_done3 + 1) = (uint32_t)(done_pos - (jmp_done3 + 5));
}

/* Inline 48-bit integer multiply with overflow detection. Overflow and
   non-integer operands go through jit_int_mul (the interpreter's num_mul),
   which promotes exactly like the interpreter does. */
static void emit_jit_int_mul(JITBuffer *buf, int r0, int r1, int r2, int i) {
    uint32_t jne_a, jne_b;
    emit_jit_int_load_and_check(buf, r1, r2, &jne_a, &jne_b);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0xaf); jit_emit_byte(buf, 0xc1); // imul rax, rcx
    uint32_t jo = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x80); jit_emit_u32(buf, 0); // jo slow
    emit_mov_rdx_imm64(buf, INT48_MAX);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x3b); jit_emit_byte(buf, 0xc2); // cmp rax, rdx
    uint32_t jg = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x8f); jit_emit_u32(buf, 0); // jg slow
    emit_mov_rdx_imm64(buf, INT48_MIN);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x3b); jit_emit_byte(buf, 0xc2); // cmp rax, rdx
    uint32_t jl = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x8c); jit_emit_u32(buf, 0); // jl slow
    emit_box_int_rax(buf);
    uint32_t jmp_done = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    uint32_t float_pos = buf->size;
    JumpList helper = {0};
    emit_float_binop(buf, OP_MUL, r1, r2, &helper);
    uint32_t jmp_done2 = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    uint32_t slow_pos = buf->size;
    jump_list_patch(buf, &helper, slow_pos);
    emit_call_binop_helper(buf, r1, r2, (uintptr_t)jit_int_mul);
    emit_deopt_check(buf, i);
    uint32_t done_pos = buf->size;
    emit_mov_reg_rax(buf, r0);
    *(uint32_t*)(buf->code + jmp_done2 + 1) = (uint32_t)(done_pos - (jmp_done2 + 5));
    *(uint32_t*)(buf->code + jne_a + 2) = (uint32_t)(float_pos - (jne_a + 6));
    *(uint32_t*)(buf->code + jne_b + 2) = (uint32_t)(float_pos - (jne_b + 6));
    *(uint32_t*)(buf->code + jo + 2) = (uint32_t)(slow_pos - (jo + 6));
    *(uint32_t*)(buf->code + jg + 2) = (uint32_t)(slow_pos - (jg + 6));
    *(uint32_t*)(buf->code + jl + 2) = (uint32_t)(slow_pos - (jl + 6));
    *(uint32_t*)(buf->code + jmp_done + 1) = (uint32_t)(done_pos - (jmp_done + 5));
}

extern Value jit_div_helper(Value a, Value b);

/* Inline integer modulo with the interpreter's floor rule (the result has
   the divisor's sign: -7 % 3 == 2). Other operands, and a zero divisor,
   go to the helper, which declines the latter so the interpreter raises. */
static void emit_jit_int_mod(JITBuffer *buf, int r0, int r1, int r2, int i) {
    uint32_t jne_a, jne_b;
    emit_jit_int_load_and_check(buf, r1, r2, &jne_a, &jne_b);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x85); jit_emit_byte(buf, 0xc9); // test rcx, rcx
    uint32_t jz_zero = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x84); jit_emit_u32(buf, 0);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x99);                     // cqo
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xf7); jit_emit_byte(buf, 0xf9); // idiv rcx (remainder in rdx)
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x85); jit_emit_byte(buf, 0xd2); // test rdx, rdx
    uint32_t jz_fixed = buf->size; jit_emit_byte(buf, 0x74); jit_emit_byte(buf, 0);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd0); // mov rax, rdx
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x31); jit_emit_byte(buf, 0xc8); // xor rax, rcx
    uint32_t jns_fixed = buf->size; jit_emit_byte(buf, 0x79); jit_emit_byte(buf, 0);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x01); jit_emit_byte(buf, 0xca); // add rdx, rcx
    uint32_t fixed = buf->size;
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd0); // mov rax, rdx
    emit_box_int_rax(buf);
    uint32_t jmp_done = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    uint32_t slow = buf->size;
    emit_call_binop_helper(buf, r1, r2, (uintptr_t)jit_mod_helper);
    emit_deopt_check(buf, i);
    uint32_t done = buf->size;
    emit_mov_reg_rax(buf, r0);
    buf->code[jz_fixed + 1] = (uint8_t)(fixed - (jz_fixed + 2));
    buf->code[jns_fixed + 1] = (uint8_t)(fixed - (jns_fixed + 2));
    *(uint32_t*)(buf->code + jne_a + 2) = (uint32_t)(slow - (jne_a + 6));
    *(uint32_t*)(buf->code + jne_b + 2) = (uint32_t)(slow - (jne_b + 6));
    *(uint32_t*)(buf->code + jz_zero + 2) = (uint32_t)(slow - (jz_zero + 6));
    *(uint32_t*)(buf->code + jmp_done + 1) = (uint32_t)(done - (jmp_done + 5));
}

/* Inline double division; the helper handles big ints and declines
   division by zero so the interpreter raises the error. */
static void emit_jit_div(JITBuffer *buf, int r0, int r1, int r2, int i) {
    JumpList helper = {0};
    emit_float_binop(buf, OP_DIV, r1, r2, &helper);
    uint32_t jmp_done = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    jump_list_patch(buf, &helper, buf->size);
    emit_call_binop_helper(buf, r1, r2, (uintptr_t)jit_div_helper);
    emit_deopt_check(buf, i);
    *(uint32_t*)(buf->code + jmp_done + 1) = (uint32_t)(buf->size - (jmp_done + 5));
    emit_mov_reg_rax(buf, r0);
}

// Inline integer ordered comparisons; produces a bool value. Mixed types fall
// back to slow_fn (a jit_int_* helper).
static void emit_jit_int_cmp(JITBuffer *buf, int op, int r0, int r1, int r2, uintptr_t slow_fn, int i) {
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
#ifdef GLOW_JIT_WIN64
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xca); // mov rdx, rcx
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc1); // mov rcx, rax
#else
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc7); // mov rdi, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xce); // mov rsi, rcx
#endif
    emit_mov_rax_imm64(buf, slow_fn);
    emit_call_rax(buf);
    emit_deopt_check(buf, i); /* incomparable operands: the interpreter raises */
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
            case OP_GET_PROPERTY: case OP_SET_PROPERTY: case OP_JUMP:
            case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_RETURN:
            case OP_CALL: case OP_CALL_VALUE: case OP_CALL_SELF_FAST: case OP_CALL_METHOD:
            case OP_INDEX: case OP_STORE_INDEX:
            case OP_SIMD_ADD: case OP_TAIL_CALL:
            case OP_ADD_INT: case OP_SUB_INT: case OP_MUL_INT:
            case OP_LT_INT: case OP_GT_INT: case OP_LTE_INT: case OP_GTE_INT: case OP_EQ_INT:
            case OP_LOAD_LOCAL: case OP_STORE_LOCAL:
            case OP_LT: case OP_GT: case OP_LTE: case OP_GTE: case OP_EQ: case OP_NEQ:
            case OP_MOD: case OP_POW: case OP_NEG: case OP_CONST_STRING:
            case OP_LIST_LITERAL: case OP_DICT_LITERAL: case OP_ITER_START: case OP_ITER_NEXT:
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

    size_t size = jit_buffer_size(bc);
    uint8_t *code = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { bc->jit_failed = 1; return; }
    JITBuffer buf = {code, 0, size};

    // Prologue
    emit_prologue(&buf);
    emit_zero_locals(&buf, bc, fe);

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
                /* A global never declared anywhere reads as none here; the
                   interpreter raises "name is not defined" for it. Declared
                   stays declared, so declared globals need no check. */
                if (!global_slot_declared(pc->arg)) emit_deopt_if_rax_eq(&buf, value_none(), i);
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
                /* Integer fast paths with the interpreter's own helpers as the
                   slow path, so every type combination behaves identically. */
                if (pc->op == OP_ADD || pc->op == OP_SUB) {
                    emit_jit_int_arith(&buf, (pc->op == OP_SUB ? 1 : 0), pc->r0, pc->r1, pc->r2, (pc->op == OP_SUB ? (uintptr_t)jit_int_sub : (uintptr_t)jit_int_add), i);
                } else if (pc->op == OP_MUL) {
                    emit_jit_int_mul(&buf, pc->r0, pc->r1, pc->r2, i);
                } else {
                    /* The helper declines division by zero so the interpreter
                       raises the error. */
                    emit_jit_div(&buf, pc->r0, pc->r1, pc->r2, i);
                }
                break;
            }
            case OP_GET_PROPERTY: {
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_arg2_imm64(&buf, (uintptr_t)pc->ptr);
                emit_mov_arg3_imm64(&buf, (uintptr_t)orig_pc);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_get_property_helper);
                emit_call_rax(&buf);
                emit_deopt_check(&buf, i);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_SET_PROPERTY: {
                emit_mov_arg1_reg(&buf, pc->r0);
                emit_mov_arg2_imm64(&buf, (uintptr_t)pc->ptr);
                emit_mov_arg3_reg(&buf, pc->r1);
                emit_mov_arg4_imm64(&buf, (uintptr_t)orig_pc);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_set_property_helper);
                emit_call_rax(&buf);
                break;
            }
            case OP_LT: case OP_GT: case OP_LTE: case OP_GTE: case OP_EQ: case OP_NEQ: {
                uintptr_t cmp_fn =
                    pc->op == OP_LT   ? (uintptr_t)jit_val_lt  :
                    pc->op == OP_GT   ? (uintptr_t)jit_val_gt  :
                    pc->op == OP_LTE  ? (uintptr_t)jit_val_lte :
                    pc->op == OP_GTE  ? (uintptr_t)jit_val_gte :
                    pc->op == OP_EQ   ? (uintptr_t)jit_val_eq  : (uintptr_t)jit_val_neq;
                int int_op = -1;
                switch (pc->op) {
                    case OP_LT:  int_op = OP_LT_INT; break;
                    case OP_GT:  int_op = OP_GT_INT; break;
                    case OP_LTE: int_op = OP_LTE_INT; break;
                    case OP_GTE: int_op = OP_GTE_INT; break;
                    case OP_EQ:  int_op = OP_EQ_INT; break;
                    default:     int_op = -1; break;
                }
                if (int_op >= 0) {
                    emit_jit_int_cmp(&buf, int_op, pc->r0, pc->r1, pc->r2, cmp_fn, i);
                } else {
                    emit_mov_arg1_reg(&buf, pc->r1);
                    emit_mov_arg2_reg(&buf, pc->r2);
                    emit_mov_rax_imm64(&buf, cmp_fn);
                    emit_call_rax(&buf);
                    emit_deopt_check(&buf, i);
                    emit_mov_reg_rax(&buf, pc->r0);
                }
                break;
            }
            case OP_JUMP: { if (pc->arg <= i) emit_gil_poll(&buf); jit_emit_byte(&buf, 0xe9); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
            case OP_JUMP_IF_FALSE: { if (pc->arg <= i) emit_gil_poll(&buf); emit_mov_rax_reg(&buf, pc->r0); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xc0); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
            case OP_JUMP_IF_TRUE: { if (pc->arg <= i) emit_gil_poll(&buf); emit_mov_rax_reg(&buf, pc->r0); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xc0); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
             case OP_CALL_SELF_FAST: {
                 if (fe) emit_self_call_direct(&buf, fe, pc, i);
                 else emit_side_exit(&buf, i);
                 break;
             }
             case OP_CALL:
             case OP_CALL_VALUE: {
                /* Always go through the runtime helpers: they set up a fresh
                   frame above vm_stack_top and finish deopts in the
                   interpreter. (An inline-cache jump straight into the
                   callee's compiled code used to reuse the caller's register
                   window and passed the callee's register 0 as its deopt-pc
                   out-parameter.) */
                FuncEntry *target_fe = (pc->op == OP_CALL) ? func_get((const char*)pc->ptr) : NULL;
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2); // mov r10, r12
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8); // add r10, r2*8
                emit_mov_arg4_imm64(&buf, (uintptr_t)orig_pc);
                emit_mov_arg3_r10(&buf);
                emit_mov_arg2_imm64(&buf, (uint64_t)pc->arg);
                if (target_fe) {
                    emit_mov_arg1_imm64(&buf, (uintptr_t)target_fe);
                    emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper);
                } else if (pc->op == OP_CALL) {
                    emit_mov_arg1_imm64(&buf, (uintptr_t)pc->ptr);
                    emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_name);
                } else {
                    emit_mov_arg1_reg(&buf, pc->r1);
                    emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_val);
                }
                emit_call_rax(&buf);
                emit_deopt_check(&buf, i);
                emit_mov_reg_rax(&buf, pc->r0);
                emit_exception_check(&buf, i);
                break;
            }
            case OP_CALL_METHOD: {
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_arg2_imm64(&buf, (uintptr_t)pc->ptr);
                emit_mov_arg3_imm64(&buf, (uint64_t)pc->arg);
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2); // mov r10, r12 (R base)
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8); // add r10, pc->r2 * 8
                emit_mov_arg4_r10(&buf);
                emit_mov_arg5_imm64(&buf, (uintptr_t)orig_pc);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_method_helper);
                emit_call_rax(&buf);
                emit_deopt_check(&buf, i);
                emit_mov_reg_rax(&buf, pc->r0);
                emit_exception_check(&buf, i);
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
                    jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2); // r10 = R
                    jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8); // r10 points to args
                    emit_mov_arg4_imm64(&buf, (uintptr_t)orig_pc);
                    emit_mov_arg3_r10(&buf);
                    emit_mov_arg2_imm64(&buf, (uint64_t)pc->arg);
                    if (target_fe) {
                        emit_mov_arg1_imm64(&buf, (uintptr_t)target_fe);
                        emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper);
                    } else {
                        emit_mov_arg1_reg(&buf, pc->r1);
                        emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_val);
                    }
                    emit_pop_frame(&buf);
                    jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xe0); // jmp rax
                }
                break;
            }
            case OP_INDEX: {
                emit_fast_index(&buf, pc, i);
                break;
            }
            case OP_STORE_INDEX: {
                emit_fast_store_index(&buf, pc, i);
                break;
            }
            case OP_SIMD_ADD: {
                emit_mov_arg1_reg(&buf, pc->r1); emit_mov_arg2_reg(&buf, pc->r2); emit_mov_arg3_reg(&buf, pc->r0);
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
                emit_jit_int_arith(&buf, 0, pc->r0, pc->r1, pc->r2, (uintptr_t)jit_int_add, i);
                break;
            }
            case OP_SUB_INT: {
                emit_jit_int_arith(&buf, 1, pc->r0, pc->r1, pc->r2, (uintptr_t)jit_int_sub, i);
                break;
            }
            case OP_MUL_INT: {
                emit_jit_int_mul(&buf, pc->r0, pc->r1, pc->r2, i);
                break;
            }
            case OP_LT_INT: case OP_GT_INT: case OP_LTE_INT: case OP_GTE_INT: case OP_EQ_INT: {
                uintptr_t cmp_fn =
                    pc->op == OP_LT_INT ? (uintptr_t)jit_int_lt :
                    pc->op == OP_GT_INT ? (uintptr_t)jit_int_gt :
                    pc->op == OP_LTE_INT ? (uintptr_t)jit_int_lte :
                    pc->op == OP_GTE_INT ? (uintptr_t)jit_int_gte : (uintptr_t)jit_int_eq;
                emit_jit_int_cmp(&buf, pc->op, pc->r0, pc->r1, pc->r2, cmp_fn, i);
                break;
            }
            case OP_MOD: emit_jit_int_mod(&buf, pc->r0, pc->r1, pc->r2, i); break;
            case OP_POW:
                emit_call_binop_helper(&buf, pc->r1, pc->r2, (uintptr_t)jit_pow_helper);
                emit_deopt_check(&buf, i);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_NEG:
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_neg_helper);
                emit_call_rax(&buf);
                emit_deopt_check(&buf, i);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_CONST_STRING:
                emit_mov_arg1_imm64(&buf, (uintptr_t)pc->ptr);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_const_string);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_LIST_LITERAL: case OP_DICT_LITERAL:
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2); // mov r10, r12
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r1 * 8); // add r10, r1*8
                emit_mov_arg1_r10(&buf);
                emit_mov_arg2_imm64(&buf, (uint64_t)(pc->arg < 0 ? 0 : pc->arg));
                emit_mov_rax_imm64(&buf, pc->op == OP_LIST_LITERAL ? (uintptr_t)jit_list_literal : (uintptr_t)jit_dict_literal);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_ITER_START:
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_iter_start);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_ITER_NEXT:
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_iter_next);
                emit_call_rax(&buf);
                emit_mov_rcx_imm64(&buf, JIT_ITER_DONE);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xc8); // cmp rax, rcx
                jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84);                          // je: the loop is over
                jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++;
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_RETURN: {
                emit_mov_rax_reg(&buf, pc->r0);
                emit_epilogue(&buf);
                break;
            }
            default: bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return;
        }
    }
    offsets[bc->count] = buf.size;
    if (buf.size + 64 >= buf.capacity) { bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return; }
    for (int i = 0; i < jump_count; i++) {
        uint32_t target_offset = (jumps[i].instr_idx == -1) ? body_start : offsets[jumps[i].instr_idx];
        uint32_t patch_pos = jumps[i].patch_pos;
        int32_t rel = (int32_t)target_offset - (int32_t)(patch_pos + 4);
        memcpy(code + patch_pos, &rel, 4);
    }
    mprotect(code, size, PROT_READ | PROT_EXEC);
    bc->jit_code = code; bc->jit_size = size;
    jit_debug_compiled(bc, fe, "optimizing", buf.size);
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
        if (fe->exec_count > 50) {
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
        if (bc->exec_count > 50) {
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

/* osr_mode compiles the whole bytecode for entry at loop headers only:
   opcodes the JIT cannot translate (and self/tail calls, whose fast paths
   assume function entry) become side exits to the interpreter instead of
   failing the compile, so any hot loop can run natively up to such an op. */
static void jit_compile_baseline_impl(Bytecode *bc, FuncEntry *fe, int osr_mode) {
    if (!fe) fe = bc->owner;
    if (osr_mode) {
        if (bc->osr_entries || bc->osr_failed) return;
    } else if (bc->baseline_jit_code || bc->jit_failed) return;

    for (int i = 0; !osr_mode && i < bc->count; i++) {
        Instruction *pc = &bc->code[i];
        switch (pc->op) {
            case OP_CONST_INT: case OP_CONST_FLOAT: case OP_CONST_BOOL:
            case OP_LOAD_VAR: case OP_STORE_VAR: case OP_STORE_LET:
            case OP_MOVE: case OP_NOP:
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
            case OP_GET_PROPERTY: case OP_SET_PROPERTY: case OP_JUMP:
            case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_RETURN:
            case OP_CALL: case OP_CALL_VALUE: case OP_CALL_SELF_FAST: case OP_CALL_METHOD:
            case OP_INDEX: case OP_STORE_INDEX:
            case OP_SIMD_ADD: case OP_TAIL_CALL:
            case OP_ADD_INT: case OP_SUB_INT: case OP_MUL_INT:
            case OP_LT_INT: case OP_GT_INT: case OP_LTE_INT: case OP_GTE_INT: case OP_EQ_INT:
            case OP_LOAD_LOCAL: case OP_STORE_LOCAL:
            case OP_LT: case OP_GT: case OP_LTE: case OP_GTE: case OP_EQ: case OP_NEQ:
            case OP_MOD: case OP_POW: case OP_NEG: case OP_CONST_STRING:
            case OP_LIST_LITERAL: case OP_DICT_LITERAL: case OP_ITER_START: case OP_ITER_NEXT:
                break;
            default: bc->jit_failed = 1; return;
        }
    }

    // See jit_compile_ext: reject complex self-recursive bodies from the fast path.
    if (!osr_mode) {
        int has_self = 0, has_complex = 0;
        for (int i = 0; i < bc->count; i++) {
            OpCode op = bc->code[i].op;
            if (op == OP_CALL_SELF_FAST) has_self = 1;
            else if (op == OP_STORE_INDEX || op == OP_CALL || op == OP_CALL_VALUE || op == OP_CALL_METHOD) has_complex = 1;
        }
        if (has_self && has_complex) { bc->jit_failed = 1; return; }
    }

    size_t size = jit_buffer_size(bc);
    uint8_t *code = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { if (osr_mode) bc->osr_failed = 1; else bc->jit_failed = 1; return; }
    JITBuffer buf = {code, 0, size};

    jit_baseline_emit_prologue(&buf);
    if (!osr_mode) emit_zero_locals(&buf, bc, fe);

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
                /* A global never declared anywhere reads as none here; the
                   interpreter raises "name is not defined" for it. Declared
                   stays declared, so declared globals need no check. */
                if (!global_slot_declared(pc->arg)) emit_deopt_if_rax_eq(&buf, value_none(), i);
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
                /* Integer fast paths with the interpreter's own helpers as the
                   slow path, so every type combination behaves identically. */
                if (pc->op == OP_ADD || pc->op == OP_SUB) {
                    emit_jit_int_arith(&buf, (pc->op == OP_SUB ? 1 : 0), pc->r0, pc->r1, pc->r2, (pc->op == OP_SUB ? (uintptr_t)jit_int_sub : (uintptr_t)jit_int_add), i);
                } else if (pc->op == OP_MUL) {
                    emit_jit_int_mul(&buf, pc->r0, pc->r1, pc->r2, i);
                } else {
                    /* The helper declines division by zero so the interpreter
                       raises the error. */
                    emit_jit_div(&buf, pc->r0, pc->r1, pc->r2, i);
                }
                break;
            }
            case OP_GET_PROPERTY: {
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_arg2_imm64(&buf, (uintptr_t)pc->ptr);
                emit_mov_arg3_imm64(&buf, (uintptr_t)orig_pc);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_get_property_helper);
                emit_call_rax(&buf);
                emit_deopt_check(&buf, i);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_SET_PROPERTY: {
                emit_mov_arg1_reg(&buf, pc->r0);
                emit_mov_arg2_imm64(&buf, (uintptr_t)pc->ptr);
                emit_mov_arg3_reg(&buf, pc->r1);
                emit_mov_arg4_imm64(&buf, (uintptr_t)orig_pc);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_set_property_helper);
                emit_call_rax(&buf);
                break;
            }
            case OP_LT: case OP_GT: case OP_LTE: case OP_GTE: case OP_EQ: case OP_NEQ: {
                uintptr_t cmp_fn =
                    pc->op == OP_LT   ? (uintptr_t)jit_val_lt  :
                    pc->op == OP_GT   ? (uintptr_t)jit_val_gt  :
                    pc->op == OP_LTE  ? (uintptr_t)jit_val_lte :
                    pc->op == OP_GTE  ? (uintptr_t)jit_val_gte :
                    pc->op == OP_EQ   ? (uintptr_t)jit_val_eq  : (uintptr_t)jit_val_neq;
                int int_op = -1;
                switch (pc->op) {
                    case OP_LT:  int_op = OP_LT_INT; break;
                    case OP_GT:  int_op = OP_GT_INT; break;
                    case OP_LTE: int_op = OP_LTE_INT; break;
                    case OP_GTE: int_op = OP_GTE_INT; break;
                    case OP_EQ:  int_op = OP_EQ_INT; break;
                    default:     int_op = -1; break;
                }
                if (int_op >= 0) {
                    emit_jit_int_cmp(&buf, int_op, pc->r0, pc->r1, pc->r2, cmp_fn, i);
                } else {
                    emit_mov_arg1_reg(&buf, pc->r1);
                    emit_mov_arg2_reg(&buf, pc->r2);
                    emit_mov_rax_imm64(&buf, cmp_fn);
                    emit_call_rax(&buf);
                    emit_deopt_check(&buf, i);
                    emit_mov_reg_rax(&buf, pc->r0);
                }
                break;
            }
            case OP_JUMP: { if (pc->arg <= i) emit_gil_poll(&buf); jit_emit_byte(&buf, 0xe9); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
            case OP_JUMP_IF_FALSE: { if (pc->arg <= i) emit_gil_poll(&buf); emit_mov_rax_reg(&buf, pc->r0); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xc0); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
            case OP_JUMP_IF_TRUE: { if (pc->arg <= i) emit_gil_poll(&buf); emit_mov_rax_reg(&buf, pc->r0); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xc0); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; break; }
             case OP_CALL_SELF_FAST: {
                 if (fe && !osr_mode) emit_self_call_direct(&buf, fe, pc, i);
                 else emit_side_exit(&buf, i);
                 break;
             }
             case OP_CALL:
             case OP_CALL_VALUE: {
                /* Always go through the runtime helpers: they set up a fresh
                   frame above vm_stack_top and finish deopts in the
                   interpreter. (An inline-cache jump straight into the
                   callee's compiled code used to reuse the caller's register
                   window and passed the callee's register 0 as its deopt-pc
                   out-parameter.) */
                FuncEntry *target_fe = (pc->op == OP_CALL) ? func_get((const char*)pc->ptr) : NULL;
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2); // mov r10, r12
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8); // add r10, r2*8
                emit_mov_arg4_imm64(&buf, (uintptr_t)orig_pc);
                emit_mov_arg3_r10(&buf);
                emit_mov_arg2_imm64(&buf, (uint64_t)pc->arg);
                if (target_fe) {
                    emit_mov_arg1_imm64(&buf, (uintptr_t)target_fe);
                    emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper);
                } else if (pc->op == OP_CALL) {
                    emit_mov_arg1_imm64(&buf, (uintptr_t)pc->ptr);
                    emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_name);
                } else {
                    emit_mov_arg1_reg(&buf, pc->r1);
                    emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_val);
                }
                emit_call_rax(&buf);
                emit_deopt_check(&buf, i);
                emit_mov_reg_rax(&buf, pc->r0);
                emit_exception_check(&buf, i);
                break;
            }
            case OP_CALL_METHOD: {
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_arg2_imm64(&buf, (uintptr_t)pc->ptr);
                emit_mov_arg3_imm64(&buf, (uint64_t)pc->arg);
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2);
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8);
                emit_mov_arg4_r10(&buf);
                emit_mov_arg5_imm64(&buf, (uintptr_t)orig_pc);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_method_helper);
                emit_call_rax(&buf);
                emit_deopt_check(&buf, i);
                emit_mov_reg_rax(&buf, pc->r0);
                emit_exception_check(&buf, i);
                break;
            }
            case OP_TAIL_CALL: {
                if (osr_mode) { emit_side_exit(&buf, i); break; }
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
                    emit_mov_arg4_imm64(&buf, (uintptr_t)orig_pc);
                    emit_mov_arg3_r10(&buf);
                    emit_mov_arg2_imm64(&buf, (uint64_t)pc->arg);
                    if (target_fe) {
                        emit_mov_arg1_imm64(&buf, (uintptr_t)target_fe);
                        emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper);
                    } else {
                        emit_mov_arg1_reg(&buf, pc->r1);
                        emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_val);
                    }
                    emit_pop_frame(&buf);
                    jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xe0);
                }
                break;
            }
            case OP_INDEX: {
                emit_fast_index(&buf, pc, i);
                break;
            }
            case OP_STORE_INDEX: {
                emit_fast_store_index(&buf, pc, i);
                break;
            }
            case OP_SIMD_ADD: {
                emit_mov_arg1_reg(&buf, pc->r1); emit_mov_arg2_reg(&buf, pc->r2); emit_mov_arg3_reg(&buf, pc->r0);
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
                emit_jit_int_arith(&buf, 0, pc->r0, pc->r1, pc->r2, (uintptr_t)jit_int_add, i);
                break;
            }
            case OP_SUB_INT: {
                emit_jit_int_arith(&buf, 1, pc->r0, pc->r1, pc->r2, (uintptr_t)jit_int_sub, i);
                break;
            }
            case OP_MUL_INT: {
                emit_jit_int_mul(&buf, pc->r0, pc->r1, pc->r2, i);
                break;
            }
            case OP_LT_INT: case OP_GT_INT: case OP_LTE_INT: case OP_GTE_INT: case OP_EQ_INT: {
                uintptr_t cmp_fn =
                    pc->op == OP_LT_INT ? (uintptr_t)jit_int_lt :
                    pc->op == OP_GT_INT ? (uintptr_t)jit_int_gt :
                    pc->op == OP_LTE_INT ? (uintptr_t)jit_int_lte :
                    pc->op == OP_GTE_INT ? (uintptr_t)jit_int_gte : (uintptr_t)jit_int_eq;
                emit_jit_int_cmp(&buf, pc->op, pc->r0, pc->r1, pc->r2, cmp_fn, i);
                break;
            }
            case OP_MOD: emit_jit_int_mod(&buf, pc->r0, pc->r1, pc->r2, i); break;
            case OP_POW:
                emit_call_binop_helper(&buf, pc->r1, pc->r2, (uintptr_t)jit_pow_helper);
                emit_deopt_check(&buf, i);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_NEG:
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_neg_helper);
                emit_call_rax(&buf);
                emit_deopt_check(&buf, i);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_CONST_STRING:
                emit_mov_arg1_imm64(&buf, (uintptr_t)pc->ptr);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_const_string);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_LIST_LITERAL: case OP_DICT_LITERAL:
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2); // mov r10, r12
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r1 * 8); // add r10, r1*8
                emit_mov_arg1_r10(&buf);
                emit_mov_arg2_imm64(&buf, (uint64_t)(pc->arg < 0 ? 0 : pc->arg));
                emit_mov_rax_imm64(&buf, pc->op == OP_LIST_LITERAL ? (uintptr_t)jit_list_literal : (uintptr_t)jit_dict_literal);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_ITER_START:
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_iter_start);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_ITER_NEXT:
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_iter_next);
                emit_call_rax(&buf);
                emit_mov_rcx_imm64(&buf, JIT_ITER_DONE);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xc8); // cmp rax, rcx
                jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84);                          // je: the loop is over
                jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++;
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_RETURN: {
                emit_mov_rax_reg(&buf, pc->r0);
                jit_baseline_emit_epilogue(&buf);
                break;
            }
            default:
                if (osr_mode) { emit_side_exit(&buf, i); break; }
                bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return;
        }
    }
    offsets[bc->count] = buf.size;

    /* OSR entry stubs: a fresh frame (prologue) that jumps straight into the
       body at each loop header, skipping local initialisation. */
    uint32_t *osr_offsets = NULL;
    if (osr_mode) {
        osr_offsets = calloc((size_t)bc->count, sizeof(uint32_t));
        for (int i = 0; i < bc->count; i++) {
            Instruction *pc = &bc->code[i];
            if ((pc->op == OP_JUMP || pc->op == OP_JUMP_IF_FALSE || pc->op == OP_JUMP_IF_TRUE) &&
                pc->arg >= 0 && pc->arg <= i && !osr_offsets[pc->arg]) {
                osr_offsets[pc->arg] = (uint32_t)buf.size;
                jit_baseline_emit_prologue(&buf);
                jit_emit_byte(&buf, 0xe9);
                int32_t rel = (int32_t)offsets[pc->arg] - (int32_t)(buf.size + 4);
                jit_emit_u32(&buf, (uint32_t)rel);
            }
        }
    }

    if (buf.size + 64 >= buf.capacity) {
        /* Ran out of code space: the emitter drops bytes once full, so the
           buffer cannot be trusted. Give up on this function. */
        if (osr_mode) bc->osr_failed = 1; else bc->jit_failed = 1;
        munmap(code, size); free(offsets); free(jumps); free(osr_offsets);
        return;
    }
    for (int i = 0; i < jump_count; i++) {
        uint32_t target_offset = (jumps[i].instr_idx == -1) ? body_start : offsets[jumps[i].instr_idx];
        uint32_t patch_pos = jumps[i].patch_pos;
        int32_t rel = (int32_t)target_offset - (int32_t)(patch_pos + 4);
        memcpy(code + patch_pos, &rel, 4);
    }
    mprotect(code, size, PROT_READ | PROT_EXEC);
    if (osr_mode) {
        void **entries = calloc((size_t)bc->count, sizeof(void*));
        for (int i = 0; i < bc->count; i++) if (osr_offsets[i]) entries[i] = code + osr_offsets[i];
        free(osr_offsets);
        free(offsets);
        bc->osr_code = code; bc->osr_code_size = size;
        bc->osr_entries = entries;
        jit_debug_compiled(bc, fe, "osr", buf.size);
    } else {
        bc->baseline_jit_code = code; bc->baseline_jit_size = size; bc->baseline_jit_offsets = offsets; bc->jit_tier = JIT_TIER_BASELINE;
        jit_debug_compiled(bc, fe, "baseline", buf.size);
    }
    free(jumps);
}

void jit_compile_baseline_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) {
    (void)current_sig;
    jit_compile_baseline_impl(bc, fe, 0);
}

void jit_compile_osr(Bytecode *bc) {
    jit_compile_baseline_impl(bc, NULL, 1);
}

void jit_compile_baseline(Bytecode *bc) { jit_compile_baseline_ext(bc, NULL, 0); }

static void propagate_types(Instruction *code, int count, int param_count, TypeSignature sig, FuncEntry *fe) {
    unsigned char reg_types[256]; memset(reg_types, 0, 256);
    int start_reg = (fe && fe->name && strchr(fe->name, ':')) ? 1 : 0;
    for (int i = 0; i < param_count && (start_reg + i) < 16; i++) reg_types[start_reg + i] = (sig >> (i * 4)) & 0xF;
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
/* ================================================================
 * ARM64 (AArch64) backend
 *
 * The same compiled-code protocol as the x86-64 backend above: compiled code
 * is `Value fn(Value *R, int *pc_idx)`. x19 holds R (the register file) and
 * x20 holds pc_idx for the whole function. Every live value stays in R, so
 * leaving compiled code anywhere (a "side exit") is: store the instruction
 * index through x20 and return VAL_DEOPT_SENTINEL; the interpreter resumes
 * at that instruction. Anything beyond moves, integer/float arithmetic,
 * comparisons and jumps calls the same C helpers as the x86 backend.
 *
 * Registers: x0-x7 arguments/scratch, x9-x11 scratch, x16 holds call
 * targets, x17 large offsets, d0-d2 floats. x18 is never touched (Apple and
 * Windows reserve it). The frame saves x29/x30 and x19/x20 (32 bytes, so sp
 * stays 16-byte aligned for calls).
 *
 * Code memory: Linux maps RW, then RX with the instruction cache flushed.
 * macOS needs MAP_JIT memory, writable only while the thread has turned
 * off write protection (pthread_jit_write_protect_np).
 * ================================================================ */
#include "jit_a64.h"
#if defined(__APPLE__)
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#endif

extern Value jit_div_helper(Value a, Value b);
extern Value jit_call_helper_name(const char *name, int argc, Value *args, Instruction *pc);
extern int jit_exception_pending(void);

#define A64_REGS 19   /* x19: Value *R */
#define A64_PCIDX 20  /* x20: int *pc_idx */

static uint8_t *a64_code_alloc(size_t size) {
#if defined(__APPLE__)
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (p == MAP_FAILED) return NULL;
    pthread_jit_write_protect_np(0);
    return p;
#else
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
#endif
}

static void a64_code_finish(uint8_t *code, size_t size) {
#if defined(__APPLE__)
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(code, size);
#elif defined(_WIN32)
    mprotect(code, size, PROT_READ | PROT_EXEC);   /* also flushes the icache */
#else
    mprotect(code, size, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char*)code, (char*)code + size);
#endif
}

static void a64_code_discard(uint8_t *code, size_t size) {
#if defined(__APPLE__)
    pthread_jit_write_protect_np(1);
#endif
    munmap(code, size);
}

/* Per-compile state: the buffer and whether a branch failed to reach. */
typedef struct { JITBuffer buf; int broken; } A64;

static void a64_emit(A64 *a, uint32_t ins) { jit_emit_u32(&a->buf, ins); }
static uint32_t a64_here(A64 *a) { return (uint32_t)a->buf.size; }

/* Point the branch emitted at `at` to `target` (both byte offsets). */
static void a64_link(A64 *a, uint32_t at, uint32_t target) {
    if (at + 4 > a->buf.size) { a->broken = 1; return; }
    if (!a64_patch_branch((uint32_t*)(a->buf.code + at), at, target)) a->broken = 1;
}
static void a64_link_here(A64 *a, uint32_t at) { a64_link(a, at, a64_here(a)); }

typedef struct { uint32_t at[8]; int n; } A64Jumps;
static void a64_jumps_add(A64Jumps *j, uint32_t at) { if (j->n < 8) j->at[j->n++] = at; }
static void a64_jumps_link_here(A64 *a, A64Jumps *j) { for (int k = 0; k < j->n; k++) a64_link_here(a, j->at[k]); }

/* Branch placeholders, patched later with a64_link. */
static uint32_t a64_b_fwd(A64 *a) { uint32_t at = a64_here(a); a64_emit(a, a64_b(0)); return at; }
static uint32_t a64_bcond_fwd(A64 *a, int cond) { uint32_t at = a64_here(a); a64_emit(a, a64_b_cond(cond, 0)); return at; }

static void a64_mov_imm(A64 *a, int rd, uint64_t v) {
    int first = 1;
    for (int hw = 0; hw < 4; hw++) {
        uint16_t part = (uint16_t)(v >> (hw * 16));
        if (part == 0 && !(first && hw == 3)) continue;
        a64_emit(a, first ? a64_movz(rd, part, hw) : a64_movk(rd, part, hw));
        first = 0;
    }
    if (first) a64_emit(a, a64_movz(rd, 0, 0));
}

/* rd = R[reg] / R[reg] = rt / rd = &R[reg] */
static void a64_load_reg(A64 *a, int rd, int reg) {
    uint32_t off = (uint32_t)reg * 8;
    if (off < 32768) a64_emit(a, a64_ldr(rd, A64_REGS, off));
    else { a64_mov_imm(a, 17, off); a64_emit(a, a64_ldr_reg(rd, A64_REGS, 17)); }
}
static void a64_store_reg(A64 *a, int rt, int reg) {
    uint32_t off = (uint32_t)reg * 8;
    if (off < 32768) a64_emit(a, a64_str(rt, A64_REGS, off));
    else { a64_mov_imm(a, 17, off); a64_emit(a, a64_str_reg(rt, A64_REGS, 17)); }
}
static void a64_addr_reg(A64 *a, int rd, int reg) {
    uint32_t off = (uint32_t)reg * 8;
    if (off < 4096) a64_emit(a, a64_add_imm(rd, A64_REGS, off));
    else { a64_mov_imm(a, 17, off); a64_emit(a, a64_add(rd, A64_REGS, 17)); }
}

static void a64_call(A64 *a, const void *fn) {
    a64_mov_imm(a, 16, (uintptr_t)fn);
    a64_emit(a, a64_blr(16));
}

static void a64_prologue(A64 *a) {
    a64_emit(a, a64_stp_pre(29, 30, A64_SP, -32));
    a64_emit(a, a64_mov_from_sp(29));
    a64_emit(a, a64_stp(19, 20, A64_SP, 16));
    a64_emit(a, a64_mov_reg(A64_REGS, 0));
    a64_emit(a, a64_mov_reg(A64_PCIDX, 1));
}
/* Restores the caller's registers; x0-x16 are left as they are. */
static void a64_pop_frame(A64 *a) {
    a64_emit(a, a64_ldp(19, 20, A64_SP, 16));
    a64_emit(a, a64_ldp_post(29, 30, A64_SP, 32));
}
static void a64_epilogue(A64 *a) { a64_pop_frame(a); a64_emit(a, a64_ret()); }

static void a64_side_exit(A64 *a, int i) {
    a64_mov_imm(a, 9, (uint32_t)i);
    a64_emit(a, a64_str_w(9, A64_PCIDX, 0));
    a64_mov_imm(a, 0, VAL_DEOPT_SENTINEL);
    a64_epilogue(a);
}

/* Leave for the interpreter at instruction i when x0 == bits. */
static void a64_deopt_if_x0_eq(A64 *a, uint64_t bits, int i) {
    a64_mov_imm(a, 9, bits);
    a64_emit(a, a64_cmp(0, 9));
    uint32_t skip = a64_bcond_fwd(a, A64_NE);
    a64_side_exit(a, i);
    a64_link_here(a, skip);
}
static void a64_deopt_check(A64 *a, int i) { a64_deopt_if_x0_eq(a, VAL_DEOPT_SENTINEL, i); }

static void a64_exception_check(A64 *a, int i) {
    a64_call(a, (const void*)jit_exception_pending);
    uint32_t skip = a64_here(a);
    a64_emit(a, a64_cbz_w(0, 0));
    a64_side_exit(a, i);
    a64_link_here(a, skip);
}

/* At loop back-edges: hand the GIL to a waiting thread, if any. */
static void a64_gil_poll(A64 *a) {
    a64_mov_imm(a, 9, (uintptr_t)&glow_gil_waiters);
    a64_emit(a, a64_ldr_w(9, 9, 0));
    uint32_t skip = a64_here(a);
    a64_emit(a, a64_cbz_w(9, 0));
    a64_call(a, (const void*)glow_gil_yield);
    a64_link_here(a, skip);
}

static void a64_zero_locals(A64 *a, Bytecode *bc, FuncEntry *fe) {
    int start = (fe && fe->param_count > 0) ? fe->param_count : 0;
    if (fe && fe->name && strchr(fe->name, ':')) start++;
    if (start >= bc->local_count) return;
    a64_mov_imm(a, 0, value_none());
    for (int r = start; r < bc->local_count; r++) a64_store_reg(a, 0, r);
}

/* x0 = R[r1], x1 = R[r2], both checked to be inline ints and sign-extended
   to 64 bits; other values branch to a jump in `fail`. */
static void a64_load_ints(A64 *a, int r1, int r2, A64Jumps *fail) {
    a64_load_reg(a, 0, r1);
    a64_load_reg(a, 1, r2);
    a64_emit(a, a64_movz_w(10, 0x7FF8));
    a64_emit(a, a64_lsr_imm(9, 0, 48));
    a64_emit(a, a64_cmp(9, 10));
    a64_jumps_add(fail, a64_bcond_fwd(a, A64_NE));
    a64_emit(a, a64_lsr_imm(9, 1, 48));
    a64_emit(a, a64_cmp(9, 10));
    a64_jumps_add(fail, a64_bcond_fwd(a, A64_NE));
    a64_emit(a, a64_sbfx(0, 0, 0, 48));
    a64_emit(a, a64_sbfx(1, 1, 0, 48));
}

/* x0 (a 64-bit integer known to fit 48 bits) -> inline int value. */
static void a64_box_int(A64 *a) {
    a64_emit(a, a64_ubfx(0, 0, 0, 48));
    a64_emit(a, a64_movz(9, 0x7FF8, 3));
    a64_emit(a, a64_orr(0, 0, 9));
}

/* Call a two-argument helper on R[r1], R[r2]; result in x0. */
static void a64_call_binop(A64 *a, int r1, int r2, const void *fn) {
    a64_load_reg(a, 0, r1);
    a64_load_reg(a, 1, r2);
    a64_call(a, fn);
}

/* Load R[r] into d<x> as a double; non-numbers jump to fail. With
   check_frac, x11 |= 1 when the double is not exactly an int64 (it has a
   fraction, or is NaN, infinite or too big). */
static void a64_load_number(A64 *a, int r, int x, int check_frac, A64Jumps *fail) {
    a64_load_reg(a, 0, r);
    a64_emit(a, a64_lsr_imm(9, 0, 48));
    a64_emit(a, a64_movz_w(10, 0x7FF8));
    a64_emit(a, a64_cmp(9, 10));
    uint32_t not_int = a64_bcond_fwd(a, A64_NE);
    a64_emit(a, a64_sbfx(0, 0, 0, 48));
    a64_emit(a, a64_scvtf(x, 0));
    uint32_t next = a64_b_fwd(a);
    a64_link_here(a, not_int);
    /* A boxed non-double has its top 13 bits equal to 0x7FF8's (IS_FLOAT_BITS). */
    a64_emit(a, a64_lsr_imm(9, 0, 51));
    a64_emit(a, a64_cmp_imm(9, 0xFFF));
    a64_jumps_add(fail, a64_bcond_fwd(a, A64_EQ));
    a64_emit(a, a64_fmov_d_x(x, 0));
    if (check_frac) {
        /* fcvtzs saturates and turns NaN into 0, so a value survives the
           round trip only if it is exactly an int64. */
        a64_emit(a, a64_fcvtzs(9, x));
        a64_emit(a, a64_scvtf(2, 9));
        a64_emit(a, a64_fcmp(x, 2));
        a64_emit(a, a64_cset(9, A64_NE));
        a64_emit(a, a64_orr(11, 11, 9));
    }
    a64_link_here(a, next);
}

/* R[r1] op R[r2] in doubles, boxed into x0 as num_from_double does; jumps
   to fail when the helper has to decide (see emit_float_binop above for
   the rules, which are the interpreter's). */
static void a64_float_binop(A64 *a, int op, int r1, int r2, A64Jumps *fail) {
    int is_div = (op == OP_DIV);
    if (!is_div) a64_emit(a, a64_movz(11, 0, 0));
    a64_load_number(a, r1, 0, !is_div, fail);
    a64_load_number(a, r2, 1, !is_div, fail);
    if (is_div) {
        a64_emit(a, a64_fcmp_zero(1));
        a64_jumps_add(fail, a64_bcond_fwd(a, A64_EQ));   /* division by zero */
    } else {
        uint32_t at = a64_here(a);
        a64_emit(a, a64_cbz(11, 0));                      /* no inexact operand */
        a64_jumps_add(fail, at);
    }
    a64_emit(a, op == OP_ADD ? a64_fadd(0, 0, 1) : op == OP_SUB ? a64_fsub(0, 0, 1) :
                op == OP_MUL ? a64_fmul(0, 0, 1) : a64_fdiv(0, 0, 1));
    a64_emit(a, a64_fcmp(0, 0));
    uint32_t nan = a64_bcond_fwd(a, A64_VS);
    a64_emit(a, a64_fcvtzs(0, 0));
    a64_emit(a, a64_sbfx(9, 0, 0, 48));
    a64_emit(a, a64_cmp(9, 0));
    uint32_t as_float1 = a64_bcond_fwd(a, A64_NE);         /* outside 48 bits */
    a64_emit(a, a64_scvtf(2, 0));
    a64_emit(a, a64_fcmp(0, 2));
    uint32_t as_float2 = a64_bcond_fwd(a, A64_NE);         /* has a fraction */
    a64_box_int(a);
    uint32_t end1 = a64_b_fwd(a);
    a64_link_here(a, as_float1);
    a64_link_here(a, as_float2);
    a64_emit(a, a64_fmov_x_d(0, 0));
    uint32_t end2 = a64_b_fwd(a);
    a64_link_here(a, nan);
    a64_mov_imm(a, 0, CANON_NAN);
    a64_link_here(a, end1);
    a64_link_here(a, end2);
}

/* + and - : inline int (promoting to a double past 48 bits, as the x86
   backend does), then inline float, then the helper. */
static void a64_int_arith(A64 *a, int is_sub, int r0, int r1, int r2, const void *slow_fn, int i) {
    A64Jumps not_ints = {0}, helper = {0};
    a64_load_ints(a, r1, r2, &not_ints);
    a64_emit(a, is_sub ? a64_sub(0, 0, 1) : a64_add(0, 0, 1));
    a64_emit(a, a64_sbfx(9, 0, 0, 48));
    a64_emit(a, a64_cmp(9, 0));
    uint32_t promote = a64_bcond_fwd(a, A64_NE);
    a64_box_int(a);
    uint32_t done1 = a64_b_fwd(a);
    a64_link_here(a, promote);
    a64_emit(a, a64_scvtf(0, 0));
    a64_emit(a, a64_fmov_x_d(0, 0));
    uint32_t done2 = a64_b_fwd(a);
    a64_jumps_link_here(a, &not_ints);
    a64_float_binop(a, is_sub ? OP_SUB : OP_ADD, r1, r2, &helper);
    uint32_t done3 = a64_b_fwd(a);
    a64_jumps_link_here(a, &helper);
    a64_call_binop(a, r1, r2, slow_fn);
    a64_deopt_check(a, i);   /* the interpreter raises type errors */
    a64_link_here(a, done1); a64_link_here(a, done2); a64_link_here(a, done3);
    a64_store_reg(a, 0, r0);
}

/* * : inline int with exact overflow detection; overflow goes to the
   helper, which promotes exactly like the interpreter. */
static void a64_int_mul(A64 *a, int r0, int r1, int r2, int i) {
    A64Jumps not_ints = {0}, helper = {0};
    a64_load_ints(a, r1, r2, &not_ints);
    a64_emit(a, a64_mul(9, 0, 1));
    a64_emit(a, a64_smulh(10, 0, 1));
    a64_emit(a, a64_asr_imm(11, 9, 63));
    a64_emit(a, a64_cmp(10, 11));
    a64_jumps_add(&helper, a64_bcond_fwd(a, A64_NE));      /* 64-bit overflow */
    a64_emit(a, a64_sbfx(11, 9, 0, 48));
    a64_emit(a, a64_cmp(11, 9));
    a64_jumps_add(&helper, a64_bcond_fwd(a, A64_NE));      /* past 48 bits */
    a64_emit(a, a64_mov_reg(0, 9));
    a64_box_int(a);
    uint32_t done1 = a64_b_fwd(a);
    a64_jumps_link_here(a, &not_ints);
    a64_float_binop(a, OP_MUL, r1, r2, &helper);
    uint32_t done2 = a64_b_fwd(a);
    a64_jumps_link_here(a, &helper);
    a64_call_binop(a, r1, r2, (const void*)jit_int_mul);
    a64_deopt_check(a, i);
    a64_link_here(a, done1); a64_link_here(a, done2);
    a64_store_reg(a, 0, r0);
}

static void a64_div(A64 *a, int r0, int r1, int r2, int i) {
    A64Jumps helper = {0};
    a64_float_binop(a, OP_DIV, r1, r2, &helper);
    uint32_t done = a64_b_fwd(a);
    a64_jumps_link_here(a, &helper);
    a64_call_binop(a, r1, r2, (const void*)jit_div_helper);
    a64_deopt_check(a, i);   /* declined: division by zero */
    a64_link_here(a, done);
    a64_store_reg(a, 0, r0);
}

/* % with the interpreter's floor rule (the result has the divisor's sign);
   other operands and a zero divisor go to the helper. */
static void a64_int_mod(A64 *a, int r0, int r1, int r2, int i) {
    A64Jumps slow = {0};
    a64_load_ints(a, r1, r2, &slow);
    uint32_t zero = a64_here(a);
    a64_emit(a, a64_cbz(1, 0));
    a64_jumps_add(&slow, zero);
    a64_emit(a, a64_sdiv(9, 0, 1));
    a64_emit(a, a64_msub(2, 9, 1, 0));      /* x2 = x0 - (x0 / x1) * x1 */
    uint32_t exact = a64_here(a);
    a64_emit(a, a64_cbz(2, 0));
    a64_emit(a, a64_eor(9, 2, 1));
    uint32_t same_sign = a64_here(a);
    a64_emit(a, a64_tbz(9, 63, 0));
    a64_emit(a, a64_add(2, 2, 1));
    a64_link_here(a, exact);
    a64_link_here(a, same_sign);
    a64_emit(a, a64_mov_reg(0, 2));
    a64_box_int(a);
    uint32_t done = a64_b_fwd(a);
    a64_jumps_link_here(a, &slow);
    a64_call_binop(a, r1, r2, (const void*)jit_mod_helper);
    a64_deopt_check(a, i);   /* declined: modulo by zero */
    a64_link_here(a, done);
    a64_store_reg(a, 0, r0);
}

/* Ordered comparison of two ints into a bool; otherwise the helper. */
static void a64_int_cmp(A64 *a, int op, int r0, int r1, int r2, const void *slow_fn, int i) {
    A64Jumps slow = {0};
    a64_load_ints(a, r1, r2, &slow);
    a64_emit(a, a64_cmp(0, 1));
    int cond = op == OP_LT_INT ? A64_LT : op == OP_GT_INT ? A64_GT :
               op == OP_LTE_INT ? A64_LE : op == OP_GTE_INT ? A64_GE : A64_EQ;
    a64_emit(a, a64_cset(2, cond));
    a64_mov_imm(a, 0, value_bool(0));
    a64_emit(a, a64_orr(0, 0, 2));
    uint32_t done = a64_b_fwd(a);
    a64_jumps_link_here(a, &slow);
    a64_call_binop(a, r1, r2, slow_fn);
    a64_deopt_check(a, i);   /* incomparable operands: the interpreter raises */
    a64_link_here(a, done);
    a64_store_reg(a, 0, r0);
}

static int a64_supported(OpCode op) {
    switch (op) {
        case OP_CONST_INT: case OP_CONST_FLOAT: case OP_CONST_BOOL:
        case OP_LOAD_VAR: case OP_STORE_VAR: case OP_STORE_LET:
        case OP_MOVE: case OP_NOP:
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
        case OP_GET_PROPERTY: case OP_SET_PROPERTY: case OP_JUMP:
        case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_RETURN:
        case OP_CALL: case OP_CALL_VALUE: case OP_CALL_SELF_FAST: case OP_CALL_METHOD:
        case OP_INDEX: case OP_STORE_INDEX:
        case OP_SIMD_ADD: case OP_TAIL_CALL:
        case OP_ADD_INT: case OP_SUB_INT: case OP_MUL_INT:
        case OP_LT_INT: case OP_GT_INT: case OP_LTE_INT: case OP_GTE_INT: case OP_EQ_INT:
        case OP_LOAD_LOCAL: case OP_STORE_LOCAL:
        case OP_LT: case OP_GT: case OP_LTE: case OP_GTE: case OP_EQ: case OP_NEQ:
        case OP_MOD: case OP_POW: case OP_NEG: case OP_CONST_STRING:
        case OP_LIST_LITERAL: case OP_DICT_LITERAL: case OP_ITER_START: case OP_ITER_NEXT:
            return 1;
        default:
            return 0;
    }
}

enum { A64_BASELINE, A64_OPTIMIZING, A64_OSR };

/* Compile bc. The tiers produce the same code here (as on x86, where the
   optimizing tier's emitters do not use the type information either); the
   mode only decides which fields receive it. OSR compiles the whole body
   with entry stubs at loop headers, and turns what it cannot translate
   into side exits instead of failing. */
static void a64_compile(Bytecode *bc, FuncEntry *fe, int mode) {
    if (!fe) fe = bc->owner;
    int osr = (mode == A64_OSR);
    if (osr) { if (bc->osr_entries || bc->osr_failed) return; }
    else if (mode == A64_BASELINE) { if (bc->baseline_jit_code || bc->jit_failed) return; }
    else { if (bc->jit_code || bc->jit_failed) return; }

    if (!osr) {
        int has_self = 0, has_complex = 0;
        for (int i = 0; i < bc->count; i++) {
            OpCode op = bc->code[i].op;
            if (!a64_supported(op)) { bc->jit_failed = 1; return; }
            if (op == OP_CALL_SELF_FAST) has_self = 1;
            else if (op == OP_STORE_INDEX || op == OP_CALL || op == OP_CALL_VALUE || op == OP_CALL_METHOD) has_complex = 1;
        }
        /* Same rule as x86: the self-call fast path is only for pure
           scalar recursion. */
        if (has_self && has_complex) { bc->jit_failed = 1; return; }
    }

    size_t size = ((size_t)bc->count * 640 + 16384 + 65535) & ~(size_t)65535;
    uint8_t *code = a64_code_alloc(size);
    if (!code) { if (osr) bc->osr_failed = 1; else bc->jit_failed = 1; return; }
    A64 a = { {code, 0, size}, 0 };
    A64 *A = &a;

    a64_prologue(A);
    if (!osr) a64_zero_locals(A, bc, fe);
    uint32_t body_start = a64_here(A);

    uint32_t *offsets = calloc((size_t)bc->count + 1, sizeof(uint32_t));
    struct { int target; uint32_t at; } *jumps = calloc((size_t)bc->count + 1, sizeof(*jumps));
    int njumps = 0;

    for (int i = 0; i < bc->count; i++) {
        offsets[i] = a64_here(A);
        Instruction *pc = &bc->code[i];
        Instruction *orig_pc = (fe && fe->body) ? &fe->body->code[i] : pc;
        switch (pc->op) {
            case OP_CONST_INT: a64_mov_imm(A, 0, value_int(pc->arg)); a64_store_reg(A, 0, pc->r0); break;
            case OP_CONST_FLOAT: a64_mov_imm(A, 0, *(uint64_t*)pc->ptr); a64_store_reg(A, 0, pc->r0); break;
            case OP_CONST_BOOL: a64_mov_imm(A, 0, value_bool(pc->arg)); a64_store_reg(A, 0, pc->r0); break;
            case OP_LOAD_VAR: {
                a64_mov_imm(A, 9, (uintptr_t)&global_table.values);
                a64_emit(A, a64_ldr(9, 9, 0));
                a64_mov_imm(A, 10, (uint64_t)pc->arg * 8);
                a64_emit(A, a64_ldr_reg(0, 9, 10));
                /* An undeclared global reads as none; the interpreter raises
                   "name is not defined" for it (see the x86 backend). */
                if (!global_slot_declared(pc->arg)) a64_deopt_if_x0_eq(A, value_none(), i);
                a64_store_reg(A, 0, pc->r0);
                break;
            }
            case OP_STORE_VAR: case OP_STORE_LET: {
                a64_load_reg(A, 0, pc->r0);
                a64_mov_imm(A, 9, (uintptr_t)&global_table.values);
                a64_emit(A, a64_ldr(9, 9, 0));
                a64_mov_imm(A, 10, (uint64_t)pc->arg * 8);
                a64_emit(A, a64_str_reg(0, 9, 10));
                break;
            }
            case OP_MOVE: a64_load_reg(A, 0, pc->r1); a64_store_reg(A, 0, pc->r0); break;
            case OP_NOP: break;
            case OP_LOAD_LOCAL: a64_load_reg(A, 0, pc->arg); a64_store_reg(A, 0, pc->r0); break;
            case OP_STORE_LOCAL: a64_load_reg(A, 0, pc->r0); a64_store_reg(A, 0, pc->arg); break;
            case OP_ADD: case OP_ADD_INT:
                a64_int_arith(A, 0, pc->r0, pc->r1, pc->r2, (const void*)jit_int_add, i); break;
            case OP_SUB: case OP_SUB_INT:
                a64_int_arith(A, 1, pc->r0, pc->r1, pc->r2, (const void*)jit_int_sub, i); break;
            case OP_MUL: case OP_MUL_INT:
                a64_int_mul(A, pc->r0, pc->r1, pc->r2, i); break;
            case OP_DIV:
                a64_div(A, pc->r0, pc->r1, pc->r2, i); break;
            case OP_LT: case OP_GT: case OP_LTE: case OP_GTE: case OP_EQ: {
                const void *fn = pc->op == OP_LT ? (const void*)jit_val_lt : pc->op == OP_GT ? (const void*)jit_val_gt :
                                 pc->op == OP_LTE ? (const void*)jit_val_lte : pc->op == OP_GTE ? (const void*)jit_val_gte :
                                 (const void*)jit_val_eq;
                int int_op = pc->op == OP_LT ? OP_LT_INT : pc->op == OP_GT ? OP_GT_INT :
                             pc->op == OP_LTE ? OP_LTE_INT : pc->op == OP_GTE ? OP_GTE_INT : OP_EQ_INT;
                a64_int_cmp(A, int_op, pc->r0, pc->r1, pc->r2, fn, i);
                break;
            }
            case OP_NEQ:
                a64_call_binop(A, pc->r1, pc->r2, (const void*)jit_val_neq);
                a64_deopt_check(A, i);
                a64_store_reg(A, 0, pc->r0);
                break;
            case OP_LT_INT: case OP_GT_INT: case OP_LTE_INT: case OP_GTE_INT: case OP_EQ_INT: {
                const void *fn = pc->op == OP_LT_INT ? (const void*)jit_int_lt : pc->op == OP_GT_INT ? (const void*)jit_int_gt :
                                 pc->op == OP_LTE_INT ? (const void*)jit_int_lte : pc->op == OP_GTE_INT ? (const void*)jit_int_gte :
                                 (const void*)jit_int_eq;
                a64_int_cmp(A, pc->op, pc->r0, pc->r1, pc->r2, fn, i);
                break;
            }
            case OP_JUMP:
                if (pc->arg <= i) a64_gil_poll(A);
                jumps[njumps].target = pc->arg; jumps[njumps].at = a64_b_fwd(A); njumps++;
                break;
            case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE:
                /* As on x86: the low bit of the value decides. Skip the jump
                   when the condition does not hold. */
                if (pc->arg <= i) a64_gil_poll(A);
                a64_load_reg(A, 0, pc->r0);
                a64_emit(A, pc->op == OP_JUMP_IF_FALSE ? a64_tbnz(0, 0, 8) : a64_tbz(0, 0, 8));
                jumps[njumps].target = pc->arg; jumps[njumps].at = a64_b_fwd(A); njumps++;
                break;
            case OP_GET_PROPERTY:
                a64_load_reg(A, 0, pc->r1);
                a64_mov_imm(A, 1, (uintptr_t)pc->ptr);
                a64_mov_imm(A, 2, (uintptr_t)orig_pc);
                a64_call(A, (const void*)jit_get_property_helper);
                a64_deopt_check(A, i);
                a64_store_reg(A, 0, pc->r0);
                break;
            case OP_SET_PROPERTY:
                a64_load_reg(A, 0, pc->r0);
                a64_mov_imm(A, 1, (uintptr_t)pc->ptr);
                a64_load_reg(A, 2, pc->r1);
                a64_mov_imm(A, 3, (uintptr_t)orig_pc);
                a64_call(A, (const void*)jit_set_property_helper);
                break;
            case OP_CALL_SELF_FAST:
                if (fe && !osr) {
                    a64_mov_imm(A, 0, (uintptr_t)fe);
                    a64_addr_reg(A, 1, pc->r2);
                    a64_call(A, (const void*)jit_self_call_direct);
                    a64_deopt_check(A, i);
                    a64_store_reg(A, 0, pc->r0);
                } else {
                    a64_side_exit(A, i);
                }
                break;
            case OP_CALL: case OP_CALL_VALUE: {
                /* Always through the runtime helpers (see the x86 backend). */
                FuncEntry *target_fe = (pc->op == OP_CALL) ? func_get((const char*)pc->ptr) : NULL;
                a64_addr_reg(A, 2, pc->r2);
                a64_mov_imm(A, 3, (uintptr_t)orig_pc);
                a64_mov_imm(A, 1, (uint64_t)pc->arg);
                if (target_fe) { a64_mov_imm(A, 0, (uintptr_t)target_fe); a64_call(A, (const void*)jit_call_helper); }
                else if (pc->op == OP_CALL) { a64_mov_imm(A, 0, (uintptr_t)pc->ptr); a64_call(A, (const void*)jit_call_helper_name); }
                else { a64_load_reg(A, 0, pc->r1); a64_call(A, (const void*)jit_call_helper_val); }
                a64_deopt_check(A, i);
                a64_store_reg(A, 0, pc->r0);
                a64_exception_check(A, i);
                break;
            }
            case OP_CALL_METHOD:
                a64_load_reg(A, 0, pc->r1);
                a64_mov_imm(A, 1, (uintptr_t)pc->ptr);
                a64_mov_imm(A, 2, (uint64_t)pc->arg);
                a64_addr_reg(A, 3, pc->r2);
                a64_mov_imm(A, 4, (uintptr_t)orig_pc);
                a64_call(A, (const void*)jit_call_method_helper);
                a64_deopt_check(A, i);
                a64_store_reg(A, 0, pc->r0);
                a64_exception_check(A, i);
                break;
            case OP_TAIL_CALL: {
                if (osr) { a64_side_exit(A, i); break; }
                FuncEntry *target_fe = pc->ptr ? func_get((const char*)pc->ptr) : NULL;
                if (target_fe && target_fe == fe) {
                    for (int k = 0; k < pc->arg && k < fe->param_count; k++) {
                        a64_load_reg(A, 0, pc->r2 + k);
                        a64_store_reg(A, 0, k);
                    }
                    jumps[njumps].target = -1; jumps[njumps].at = a64_b_fwd(A); njumps++;
                } else {
                    a64_addr_reg(A, 2, pc->r2);
                    a64_mov_imm(A, 3, (uintptr_t)orig_pc);
                    a64_mov_imm(A, 1, (uint64_t)pc->arg);
                    if (target_fe) { a64_mov_imm(A, 0, (uintptr_t)target_fe); a64_mov_imm(A, 16, (uintptr_t)jit_call_helper); }
                    else { a64_load_reg(A, 0, pc->r1); a64_mov_imm(A, 16, (uintptr_t)jit_call_helper_val); }
                    a64_pop_frame(A);
                    a64_emit(A, a64_br(16));
                }
                break;
            }
            case OP_INDEX:
                a64_call_binop(A, pc->r1, pc->r2, (const void*)glow_index_jit);
                a64_deopt_check(A, i);
                a64_store_reg(A, 0, pc->r0);
                break;
            case OP_STORE_INDEX:
                a64_load_reg(A, 0, pc->r0);
                a64_load_reg(A, 1, pc->r1);
                a64_load_reg(A, 2, pc->r2);
                a64_call(A, (const void*)glow_store_index_jit);
                a64_deopt_check(A, i);
                break;
            case OP_SIMD_ADD:
                a64_load_reg(A, 0, pc->r1);
                a64_load_reg(A, 1, pc->r2);
                a64_load_reg(A, 2, pc->r0);
                a64_call(A, (const void*)simd_add_helper);
                break;
            case OP_MOD: a64_int_mod(A, pc->r0, pc->r1, pc->r2, i); break;
            case OP_POW:
                a64_call_binop(A, pc->r1, pc->r2, (const void*)jit_pow_helper);
                a64_deopt_check(A, i);
                a64_store_reg(A, 0, pc->r0);
                break;
            case OP_NEG:
                a64_load_reg(A, 0, pc->r1);
                a64_call(A, (const void*)jit_neg_helper);
                a64_deopt_check(A, i);
                a64_store_reg(A, 0, pc->r0);
                break;
            case OP_CONST_STRING:
                a64_mov_imm(A, 0, (uintptr_t)pc->ptr);
                a64_call(A, (const void*)jit_const_string);
                a64_store_reg(A, 0, pc->r0);
                break;
            case OP_LIST_LITERAL: case OP_DICT_LITERAL:
                a64_addr_reg(A, 0, pc->r1);
                a64_mov_imm(A, 1, (uint64_t)(pc->arg < 0 ? 0 : pc->arg));
                a64_call(A, pc->op == OP_LIST_LITERAL ? (const void*)jit_list_literal : (const void*)jit_dict_literal);
                a64_store_reg(A, 0, pc->r0);
                break;
            case OP_ITER_START:
                a64_load_reg(A, 0, pc->r1);
                a64_call(A, (const void*)jit_iter_start);
                a64_store_reg(A, 0, pc->r0);
                break;
            case OP_ITER_NEXT:
                a64_load_reg(A, 0, pc->r1);
                a64_call(A, (const void*)jit_iter_next);
                a64_mov_imm(A, 9, JIT_ITER_DONE);
                a64_emit(A, a64_cmp(0, 9));
                a64_emit(A, a64_b_cond(A64_NE, 8));   /* not over: skip the exit */
                jumps[njumps].target = pc->arg; jumps[njumps].at = a64_b_fwd(A); njumps++;
                a64_store_reg(A, 0, pc->r0);
                break;
            case OP_RETURN:
                a64_load_reg(A, 0, pc->r0);
                a64_epilogue(A);
                break;
            default:
                if (osr) { a64_side_exit(A, i); break; }
                bc->jit_failed = 1;
                a64_code_discard(code, size); free(offsets); free(jumps);
                return;
        }
    }
    offsets[bc->count] = a64_here(A);

    uint32_t *osr_offsets = NULL;
    if (osr) {
        osr_offsets = calloc((size_t)bc->count, sizeof(uint32_t));
        for (int i = 0; i < bc->count; i++) {
            Instruction *pc = &bc->code[i];
            if ((pc->op == OP_JUMP || pc->op == OP_JUMP_IF_FALSE || pc->op == OP_JUMP_IF_TRUE) &&
                pc->arg >= 0 && pc->arg <= i && !osr_offsets[pc->arg]) {
                osr_offsets[pc->arg] = a64_here(A);
                a64_prologue(A);
                a64_link(A, a64_b_fwd(A), offsets[pc->arg]);
            }
        }
    }

    for (int k = 0; k < njumps; k++)
        a64_link(A, jumps[k].at, jumps[k].target == -1 ? body_start : offsets[jumps[k].target]);

    if (a.broken || a.buf.size + 64 >= a.buf.capacity) {
        /* A branch did not reach, or the buffer filled up (the emitter drops
           words once full): the code cannot be trusted. */
        if (osr) bc->osr_failed = 1; else bc->jit_failed = 1;
        a64_code_discard(code, size); free(offsets); free(jumps); free(osr_offsets);
        return;
    }
    a64_code_finish(code, size);
    jit_debug_compiled(bc, fe, osr ? "osr" : mode == A64_BASELINE ? "baseline" : "optimizing", a.buf.size);
    free(jumps);
    if (osr) {
        void **entries = calloc((size_t)bc->count, sizeof(void*));
        for (int i = 0; i < bc->count; i++) if (osr_offsets[i]) entries[i] = code + osr_offsets[i];
        free(osr_offsets); free(offsets);
        bc->osr_code = code; bc->osr_code_size = size; bc->osr_entries = entries;
    } else if (mode == A64_BASELINE) {
        bc->baseline_jit_code = code; bc->baseline_jit_size = size; bc->baseline_jit_offsets = offsets;
        bc->jit_tier = JIT_TIER_BASELINE;
    } else {
        bc->jit_code = code; bc->jit_size = size;
        free(offsets);
    }
}

void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { (void)current_sig; a64_compile(bc, fe, A64_OPTIMIZING); }
void jit_compile(Bytecode *bc) { jit_compile_ext(bc, NULL, 0); }
void jit_compile_baseline_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { (void)current_sig; a64_compile(bc, fe, A64_BASELINE); }
void jit_compile_baseline(Bytecode *bc) { jit_compile_baseline_ext(bc, NULL, 0); }
void jit_compile_osr(Bytecode *bc) { a64_compile(bc, NULL, A64_OSR); }

void jit_compile_specialized(FuncEntry *fe, TypeSignature sig) {
    if (!fe || !fe->body) return;
    Bytecode *sbc = calloc(1, sizeof(Bytecode));
    sbc->count = fe->body->count;
    sbc->code = malloc(sizeof(Instruction) * sbc->count);
    memcpy(sbc->code, fe->body->code, sizeof(Instruction) * sbc->count);
    for (int i = 0; i < sbc->count; i++) sbc->code[i].trace = NULL;
    sbc->local_count = fe->body->local_count;
    jit_compile_ext(sbc, fe, sig);
    if (sbc->jit_code) func_add_specialization(fe, sig, sbc->jit_code, sbc->jit_size);
    else func_add_specialization(fe, sig, (void*)1, 0);
    free(sbc->code); free(sbc);
}

void func_maybe_tier_up(FuncEntry *fe) {
    if (!fe || !fe->body) return;
    fe->exec_count++;
    if (fe->body->jit_tier == JIT_TIER_INTERP && !fe->body->baseline_jit_code && !fe->body->jit_failed && fe->exec_count > 10)
        jit_compile_baseline(fe->body);
    if (fe->body->jit_tier == JIT_TIER_BASELINE && !fe->body->jit_code && !fe->body->jit_failed && fe->exec_count > 50)
        jit_compile(fe->body);
}

void jit_maybe_tier_up(Bytecode *bc) {
    if (!bc || bc->jit_tier >= JIT_TIER_OPTIMIZING || bc->jit_failed) return;
    bc->exec_count++;
    if (bc->jit_tier == JIT_TIER_INTERP && !bc->baseline_jit_code) {
        if (bc->exec_count > 10) jit_compile_baseline(bc);
    } else if (bc->jit_tier == JIT_TIER_BASELINE && !bc->jit_code) {
        if (bc->exec_count > 50) jit_compile(bc);
    }
}
#else
void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { if (bc) bc->jit_failed = 1; }
void jit_compile(Bytecode *bc) { if (bc) bc->jit_failed = 1; }
void jit_compile_specialized(FuncEntry *fe, TypeSignature sig) { if (fe) func_add_specialization(fe, sig, (void*)1, 0); }

void jit_compile_baseline_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { if (bc) bc->jit_failed = 1; }
void jit_compile_baseline(Bytecode *bc) { if (bc) bc->jit_failed = 1; }
void jit_compile_osr(Bytecode *bc) { if (bc) bc->osr_failed = 1; }
void jit_maybe_tier_up(Bytecode *bc) { (void)bc; }
void func_maybe_tier_up(FuncEntry *fe) { (void)fe; }
#endif
