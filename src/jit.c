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
/* GLOW_JIT_DEBUG=1: report every function compiled, on stderr.
   GLOW_JIT_DUMP=<dir>: also write its machine code to <dir>/<name>.<tier>.bin
   (for `objdump -D -b binary -m i386:x86-64` or `-m aarch64`). */
static void jit_debug_compiled(Bytecode *bc, FuncEntry *fe, const char *tier, const void *code, size_t bytes) {
    static const char *dump_dir = NULL;
    static int dump_checked = 0;
    if (!dump_checked) { dump_checked = 1; dump_dir = getenv("GLOW_JIT_DUMP"); }
    if (!glow_jit_debug && !dump_dir) return;
    if (!fe) fe = bc->owner;
    const char *name = (fe && fe->name) ? fe->name : "script";
    if (glow_jit_debug) fprintf(stderr, "[jit] %s: %s (%zu bytes)\n", tier, name, bytes);
    if (dump_dir && code) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s.%s.bin", dump_dir, name, tier);
        for (char *p = path + strlen(dump_dir) + 1; *p; p++) if (*p == ':' || *p == '/' || *p == '\\') *p = '_';
        FILE *f = fopen(path, "wb");
        if (f) { fwrite(code, 1, bytes, f); fclose(f); }
    }
}
/* ... and every function the JIT declines, with the reason. */
extern const char *op_names[];
static void jit_debug_skipped(Bytecode *bc, FuncEntry *fe, const char *why, int op) {
    if (!glow_jit_debug) return;
    if (!fe) fe = bc->owner;
    const char *name = (fe && fe->name) ? fe->name : "<script>";
    if (op >= 0) fprintf(stderr, "[jit] skipped: %s (%s %s)\n", name, why, op < 256 ? op_names[op] : "?");
    else fprintf(stderr, "[jit] skipped: %s (%s)\n", name, why);
}

/* ---- Compare and branch in one ----
   `while i < n:` compiles to a comparison into a temporary register and a
   jump on it. When the comparison is the only way to reach the jump and the
   boolean is dead after it, the pair compiles to cmp + jcc, without making
   the boolean at all. */

/* For the few simple instructions the scan below understands: the register
   it writes (or -1) and the ones it reads. Returns 0 for any other op. */
static int jit_simple_rw(const Instruction *p, int *w, int rd[2], int *nrd) {
    *w = -1; *nrd = 0;
    switch (p->op) {
        case OP_CONST_INT: case OP_CONST_FLOAT: case OP_CONST_BOOL: case OP_CONST_STRING:
            *w = p->r0; return 1;
        case OP_MOVE: case OP_NEG: case OP_NOT:
            *w = p->r0; rd[0] = p->r1; *nrd = 1; return 1;
        case OP_LOAD_LOCAL:
            *w = p->r0; rd[0] = p->arg; *nrd = 1; return 1;
        case OP_STORE_LOCAL:
            *w = p->arg; rd[0] = p->r0; *nrd = 1; return 1;
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_POW:
        case OP_ADD_INT: case OP_SUB_INT: case OP_MUL_INT:
        case OP_LT: case OP_GT: case OP_LTE: case OP_GTE: case OP_EQ: case OP_NEQ:
        case OP_LT_INT: case OP_GT_INT: case OP_LTE_INT: case OP_GTE_INT: case OP_EQ_INT:
            *w = p->r0; rd[0] = p->r1; rd[1] = p->r2; *nrd = 2; return 1;
        case OP_NOP:
            return 1;
        default:
            return 0;
    }
}

/* Is register x overwritten before anything can read it, on every path
   from instruction `from`? Follows jumps (a conditional one needs both
   paths dead) within a budget of steps; anything the scan does not
   understand (a call, the end of the code, running out of budget) counts
   as a read. */
static int jit_reg_dead_walk(const Bytecode *bc, int k, int x, int *budget) {
    while (k >= 0 && k < bc->count) {
        if (--*budget < 0) return 0;
        const Instruction *p = &bc->code[k];
        switch (p->op) {
            case OP_RETURN: return p->r0 != x;
            case OP_JUMP: k = p->arg; continue;
            case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE:
                if (p->r0 == x) return 0;
                if (!jit_reg_dead_walk(bc, p->arg, x, budget)) return 0;
                k++;
                continue;
            default: break;
        }
        int w, rd[2], nrd;
        if (!jit_simple_rw(p, &w, rd, &nrd)) return 0;
        for (int j = 0; j < nrd; j++) if (rd[j] == x) return 0;
        if (w == x) return 1;
        k++;
    }
    return 0;
}
static int jit_reg_dead_from(const Bytecode *bc, int from, int x) {
    int budget = 48;
    return jit_reg_dead_walk(bc, from, x, &budget);
}

/* Instructions some jump can land on (instruction 0 counts: a tail call
   jumps back to the start). The caller frees the array. */
/* If instruction i indexes with a string literal loaded by the instruction
   just before it (and nothing jumps in between), the literal, interned for
   good; otherwise NULL. */
static const char *jit_const_key(const Instruction *pc, int i, const unsigned char *targets, int key_reg) {
    if (i < 1 || !targets || targets[i]) return NULL;
    if (pc[-1].op != OP_CONST_STRING || pc[-1].r0 != key_reg || !pc[-1].ptr) return NULL;
    return intern_string((const char*)pc[-1].ptr);
}

static unsigned char *jit_jump_targets(const Bytecode *bc) {
    unsigned char *t = calloc((size_t)bc->count + 1, 1);
    if (!t) return NULL;
    t[0] = 1;
    for (int k = 0; k < bc->count; k++) {
        OpCode op = bc->code[k].op;
        if (op == OP_JUMP || op == OP_JUMP_IF_FALSE || op == OP_JUMP_IF_TRUE || op == OP_ITER_NEXT || op == OP_ITER_NEXT2 ||
            ((op == OP_SKIP || op == OP_STOP) && bc->code[k].arg > 0)) {
            int a = bc->code[k].arg;
            if (a >= 0 && a <= bc->count) t[a] = 1;
        }
    }
    return t;
}

/* Instruction i compares into pc->r0; can it fuse with the jump after it?
   Returns the jump's sense (0: jump if false, 1: jump if true) or -1. */
static int jit_fusable_branch(const Bytecode *bc, int i, const unsigned char *targets) {
    if (!targets || i + 1 >= bc->count) return -1;
    const Instruction *c = &bc->code[i], *j = &bc->code[i + 1];
    if (j->op != OP_JUMP_IF_FALSE && j->op != OP_JUMP_IF_TRUE) return -1;
    if (j->r0 != c->r0 || targets[i + 1]) return -1;
    if (j->arg <= i + 1 || j->arg > bc->count) return -1;   /* forward jumps only (no GIL poll) */
    if (!jit_reg_dead_from(bc, i + 2, c->r0) || !jit_reg_dead_from(bc, j->arg, c->r0)) return -1;
    return j->op == OP_JUMP_IF_TRUE;
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

/* Set for the function being compiled when it has a loop: its prologue
   then pins constants that every int operation needs in callee-saved
   registers (r15 = the int tag, r14 = the 48-bit payload mask, rbx = the
   bool tag), so helper calls keep them. A function without a loop loads
   them where they are used instead: that is cheaper than the three loads
   on every call to, say, a small recursive function. */
static int x86_pins = 0;

static int jit_has_loop(const Bytecode *bc) {
    for (int k = 0; k < bc->count; k++) {
        OpCode op = bc->code[k].op;
        if ((op == OP_JUMP || op == OP_JUMP_IF_FALSE || op == OP_JUMP_IF_TRUE) && bc->code[k].arg <= k) return 1;
    }
    return 0;
}

static void emit_or_rax_int_tag(JITBuffer *buf) {
    if (x86_pins) {
        jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x09); jit_emit_byte(buf, 0xf8); // or rax, r15
    } else {
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xba); jit_emit_u64(buf, MASK_NAN); // mov rdx, int tag
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x09); jit_emit_byte(buf, 0xd0);    // or rax, rdx
    }
}

static void emit_or_rax_bool_tag(JITBuffer *buf) {
    if (x86_pins) {
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x09); jit_emit_byte(buf, 0xd8); // or rax, rbx
    } else {
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xba); jit_emit_u64(buf, MASK_NAN | ((uint64_t)VAL_BOOL << 48)); // mov rdx, bool tag
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x09); jit_emit_byte(buf, 0xd0);    // or rax, rdx
    }
}

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
    /* See x86_pins. Every entry into compiled code, OSR stubs included,
       comes through here. */
    if (x86_pins) {
        jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0xbf); jit_emit_u64(buf, MASK_NAN);                             // mov r15, int tag
        jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0xbe); jit_emit_u64(buf, MASK_PAYLOAD);                         // mov r14, payload mask
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xbb); jit_emit_u64(buf, MASK_NAN | ((uint64_t)VAL_BOOL << 48)); // mov rbx, bool tag
    }
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

    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc1); // mov rcx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe1); jit_emit_byte(buf, 0x10); // shl rcx, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xf9); jit_emit_byte(buf, 0x10); // sar rcx, 16 (the whole 48-bit idx)
    emit_mov_rax_reg(buf, pc->r1); // reload coll into RAX
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe0); jit_emit_byte(buf, 0x10); // shl rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe8); jit_emit_byte(buf, 0x10); // shr rax, 16 (List*)

    jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x50); jit_emit_byte(buf, 0x24); // mov edx, [rax + 36] (size)
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x39); jit_emit_byte(buf, 0xd1); // cmp rcx, rdx (negative or >= size: slow path)
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

    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc1); // mov rcx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe1); jit_emit_byte(buf, 0x10); // shl rcx, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xf9); jit_emit_byte(buf, 0x10); // sar rcx, 16 (the whole 48-bit idx)
    emit_mov_rax_reg(buf, pc->r0); // reload coll into RAX
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe0); jit_emit_byte(buf, 0x10); // shl rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe8); jit_emit_byte(buf, 0x10); // shr rax, 16 (List*)

    jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x50); jit_emit_byte(buf, 0x24); // mov edx, [rax + 36] (size)
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x39); jit_emit_byte(buf, 0xd1); // cmp rcx, rdx (negative or >= size: slow path)
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

/* list.push(v) / append / add with one argument: on a list, those names
   always mean the built-in append (lists have no user methods), which
   returns the list. While there is spare capacity the item is stored here;
   anything else (not a list, a full list that must grow) jumps to the
   ordinary method call the caller emits at the returned patch position. */
static int jit_is_list_append(Instruction *pc) {
    const char *n = (const char*)pc->ptr;
    return pc->op == OP_CALL_METHOD && pc->arg == 1 && n &&
           (strcmp(n, "push") == 0 || strcmp(n, "append") == 0 || strcmp(n, "add") == 0);
}
static void emit_list_append_fast(JITBuffer *buf, Instruction *pc, uint32_t *jne_list, uint32_t *jae_full, uint32_t *jmp_done) {
    int val_reg = pc->r2 + (pc->is_method ? 1 : 0);
    emit_mov_rax_reg(buf, pc->r1); // RAX = receiver
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2); // mov rdx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xfc); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7ffc (VAL_LIST)
    *jne_list = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe0); jit_emit_byte(buf, 0x10); // shl rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe8); jit_emit_byte(buf, 0x10); // shr rax, 16 (List*)
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2); // mov r10, rax
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x4a); jit_emit_byte(buf, 0x24); // mov ecx, [r10 + 36] (size)
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x3b); jit_emit_byte(buf, 0x4a); jit_emit_byte(buf, 0x28); // cmp ecx, [r10 + 40] (capacity)
    *jae_full = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x83); jit_emit_u32(buf, 0);
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x52); jit_emit_byte(buf, 0x30); // mov rdx, [r10 + 48] (items)
    emit_mov_rax_reg(buf, val_reg);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x04); jit_emit_byte(buf, 0xca); // mov [rdx + rcx*8], rax
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0xff); jit_emit_byte(buf, 0x42); jit_emit_byte(buf, 0x24); // inc dword [r10 + 36]
    emit_mov_rax_reg(buf, pc->r1);
    emit_mov_reg_rax(buf, pc->r0); // append returns the list
    *jmp_done = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    *(uint32_t*)&buf->code[*jne_list + 2] = buf->size - (*jne_list + 6);
    *(uint32_t*)&buf->code[*jae_full + 2] = buf->size - (*jae_full + 6);
}

/* len(xs) with xs a list, without a call. Instruction i calls the value the
   instruction before it loaded from a global; if that global holds the
   built-in len right now, emit: "still that very value, and the argument is
   a list -> its size". Either test failing jumps to the ordinary call that
   the caller emits next (patch[0], patch[1]); patch[2] is the jump over it.
   Returns 0 and emits nothing when the call is not such a len. */
static int emit_len_fast(JITBuffer *buf, Instruction *pc, int i, const unsigned char *targets, uint32_t patch[3]) {
    if (pc->op != OP_CALL_VALUE || pc->arg != 1 || pc->is_method) return 0;
    if (i < 1 || !targets || targets[i]) return 0;
    if (pc[-1].op != OP_LOAD_VAR || pc[-1].r0 != pc->r1) return 0;
    if (pc[-1].arg < 0 || pc[-1].arg >= global_table.count) return 0;
    Value fn = global_table.values[pc[-1].arg];
    if (GET_TYPE(fn) != VAL_OBJECT || !(fn & MASK_PAYLOAD) || GET_OBJ_TYPE(fn) != OBJ_NATIVE_FUNC) return 0;
    if (AS_NATIVE_FUNC(fn)->func != native_len) return 0;
    /* The code compares against this object's address, so it must never be
       freed (and its address reused) while the code lives. */
    static Value rooted = 0;
    if (rooted != fn) { gc_register_constant_root(fn); rooted = fn; }

    emit_mov_rax_reg(buf, pc->r1);
    emit_mov_rcx_imm64(buf, (uint64_t)fn);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x39); jit_emit_byte(buf, 0xc8); // cmp rax, rcx
    patch[0] = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0); // jne call
    emit_mov_rax_reg(buf, pc->r2);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2); // mov rdx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xfc); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7ffc (VAL_LIST)
    patch[1] = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0); // jne call
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe0); jit_emit_byte(buf, 0x10); // shl rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe8); jit_emit_byte(buf, 0x10); // shr rax, 16 (List*)
    jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x40); jit_emit_byte(buf, 0x24); // mov eax, [rax + 36] (size)
    emit_mov_rcx_imm64(buf, 0x7ff8000000000000ULL);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x09); jit_emit_byte(buf, 0xc8); // or rax, rcx (an int)
    emit_mov_reg_rax(buf, pc->r0);
    patch[2] = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0); // jmp done
    *(uint32_t*)&buf->code[patch[0] + 2] = buf->size - (patch[0] + 6);
    *(uint32_t*)&buf->code[patch[1] + 2] = buf->size - (patch[1] + 6);
    return 1;
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
    if (x86_pins) {
        jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x21); jit_emit_byte(buf, 0xf0); // and rax, r14 (payload mask)
    } else {
        emit_mov_rdx_imm64(buf, MASK_PAYLOAD);
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x21); jit_emit_byte(buf, 0xd0); // and rax, rdx
    }
    emit_or_rax_int_tag(buf);
}

/* Load the boxed R[r1], R[r2] into rax, rcx and check both are inline
   ints, without unboxing them; anything else jumps to `slow`. */
static void emit_int_tags_check(JITBuffer *buf, int r1, int r2, uint32_t *jne_a, uint32_t *jne_b) {
    emit_mov_rax_reg(buf, r1);
    emit_mov_rcx_reg(buf, r2);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2); // mov rdx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xf8); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7FF8
    *jne_a = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xca); // mov rdx, rcx
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xf8); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7FF8
    *jne_b = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0);
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

/* roundsd (SSE4.1) truncates a double in one instruction; every x86-64 CPU
   from the last fifteen years has it, and without it the longer
   convert-and-compare sequences are used. GLOW_JIT_NO_SSE41 forces those,
   so both can be tested on one machine. The CPUID instruction is asked
   directly (leaf 1, ECX bit 19): __builtin_cpu_supports needs a runtime
   library that the Windows build does not link. */
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
static int jit_sse41(void) {
    static int cached = -1;
    if (cached < 0) {
        unsigned int ecx = 0;
#if defined(_MSC_VER)
        int r[4];
        __cpuid(r, 1);
        ecx = (unsigned int)r[2];
#else
        unsigned int eax, ebx, edx;
        if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) ecx = 0;
#endif
        cached = ((ecx >> 19) & 1) && !getenv("GLOW_JIT_NO_SSE41");
    }
    return cached;
}

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
    jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x57); jit_emit_byte(buf, (unsigned char)(0xc0 | (x << 3) | x)); // xorps xmmX, xmmX (cvtsi2sd keeps the upper half: a false dependency)
    jit_emit_byte(buf, 0xf2); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2a); jit_emit_byte(buf, (unsigned char)(0xc0 | (x << 3))); // cvtsi2sd xmmX, rax
    uint32_t jmp_next = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    uint32_t not_int = buf->size;
    /* A boxed non-double has its top 13 bits equal to 0x7FF8's (IS_FLOAT_BITS). */
    jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xe2); jit_emit_u32(buf, 0xfff8); // and edx, 0xFFF8
    jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_u32(buf, 0x7ff8); // cmp edx, 0x7FF8
    emit_jcc_to(buf, 0x84, fail); // je fail
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x6e); jit_emit_byte(buf, (unsigned char)(0xc0 | (x << 3))); // movq xmmX, rax
    if (check_frac && jit_sse41()) {
        /* r11 |= "d is not an exact integer": it differs from its truncation
           (a fraction) or is NaN. An infinite or huge whole double sets
           nothing; if neither operand sets r11 the helper decides, and if
           the other one does, doubles are what the interpreter uses too. */
        jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x57); jit_emit_byte(buf, 0xd2); // xorps xmm2, xmm2 (roundsd keeps xmm2's upper half: a false dependency)
        jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x3a); jit_emit_byte(buf, 0x0b); jit_emit_byte(buf, (unsigned char)(0xd0 | x)); jit_emit_byte(buf, 0x03); // roundsd xmm2, xmmX, 3 (truncate)
        jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2e); jit_emit_byte(buf, (unsigned char)(0xc2 | (x << 3))); // ucomisd xmmX, xmm2
        /* Both flags are read before any `or`, which would overwrite PF. */
        jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x95); jit_emit_byte(buf, 0xc2); // setne dl
        jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x9a); jit_emit_byte(buf, 0xc6); // setp dh (NaN)
        jit_emit_byte(buf, 0x08); jit_emit_byte(buf, 0xf2); // or dl, dh
        jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x08); jit_emit_byte(buf, 0xd3); // or r11b, dl
    } else if (check_frac) {
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
    if (jit_sse41()) {
        /* Usually the result has a fraction: that is two instructions. A
           whole one becomes an int when it fits in 48 bits (infinity and
           huge values convert to 0x8000000000000000, which does not). */
        jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x57); jit_emit_byte(buf, 0xd2); // xorps xmm2, xmm2 (roundsd keeps xmm2's upper half: a false dependency)
        jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x3a); jit_emit_byte(buf, 0x0b); jit_emit_byte(buf, 0xd0); jit_emit_byte(buf, 0x03); // roundsd xmm2, xmm0, 3
        jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2e); jit_emit_byte(buf, 0xc2); // ucomisd xmm0, xmm2
        emit_jcc_to(buf, 0x85, &as_float); // jne: has a fraction
        jit_emit_byte(buf, 0xf2); jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2c); jit_emit_byte(buf, 0xc0); // cvttsd2si rax, xmm0
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2); // mov rdx, rax
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe2); jit_emit_byte(buf, 0x10); // shl rdx, 16
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0x10); // sar rdx, 16
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x39); jit_emit_byte(buf, 0xc2); // cmp rdx, rax
        emit_jcc_to(buf, 0x85, &as_float); // jne: outside 48 bits
    } else {
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
    }
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
    /* Add the payloads shifted to the top of the register: (a<<16)+(b<<16)
       overflows 64 bits exactly when a+b leaves the 48-bit range, so the
       overflow flag is the range check, and shifting back gives the new
       payload, ready for the tag. */
    emit_int_tags_check(buf, r1, r2, &jne_a, &jne_b);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe0); jit_emit_byte(buf, 0x10); // shl rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe1); jit_emit_byte(buf, 0x10); // shl rcx, 16
    if (is_sub) {
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x29); jit_emit_byte(buf, 0xc8); // sub rax, rcx
    } else {
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x01); jit_emit_byte(buf, 0xc8); // add rax, rcx
    }
    uint32_t jo_over = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x80); jit_emit_u32(buf, 0); // jo promote
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe8); jit_emit_byte(buf, 0x10); // shr rax, 16
    emit_or_rax_int_tag(buf);
    uint32_t jmp_done = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    /* Past 48 bits: the exact sum as a double (exact for the whole range). */
    uint32_t promote_pos = buf->size;
    emit_mov_rax_reg(buf, r1);
    emit_mov_rcx_reg(buf, r2);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe0); jit_emit_byte(buf, 0x10); // shl rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xf8); jit_emit_byte(buf, 0x10); // sar rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe1); jit_emit_byte(buf, 0x10); // shl rcx, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xf9); jit_emit_byte(buf, 0x10); // sar rcx, 16
    if (is_sub) {
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x29); jit_emit_byte(buf, 0xc8); // sub rax, rcx
    } else {
        jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x01); jit_emit_byte(buf, 0xc8); // add rax, rcx
    }
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
    *(uint32_t*)(buf->code + jo_over + 2) = (uint32_t)(promote_pos - (jo_over + 6));
    *(uint32_t*)(buf->code + jmp_done + 1) = (uint32_t)(done_pos - (jmp_done + 5));
    *(uint32_t*)(buf->code + jmp_done2 + 1) = (uint32_t)(done_pos - (jmp_done2 + 5));
    *(uint32_t*)(buf->code + jmp_done3 + 1) = (uint32_t)(done_pos - (jmp_done3 + 5));
}

/* Inline 48-bit integer multiply with overflow detection. Overflow and
   non-integer operands go through jit_int_mul (the interpreter's num_mul),
   which promotes exactly like the interpreter does. */
static void emit_jit_int_mul(JITBuffer *buf, int r0, int r1, int r2, int i) {
    uint32_t jne_a, jne_b;
    /* (a<<16) * b overflows 64 bits exactly when a*b leaves the 48-bit
       range; shifting back gives the new payload. */
    emit_int_tags_check(buf, r1, r2, &jne_a, &jne_b);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe0); jit_emit_byte(buf, 0x10); // shl rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe1); jit_emit_byte(buf, 0x10); // shl rcx, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xf9); jit_emit_byte(buf, 0x10); // sar rcx, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0xaf); jit_emit_byte(buf, 0xc1); // imul rax, rcx
    uint32_t jo = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x80); jit_emit_u32(buf, 0); // jo slow
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe8); jit_emit_byte(buf, 0x10); // shr rax, 16
    emit_or_rax_int_tag(buf);
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

/* x % d for a constant d, without a divide (which costs most of a loop
   like `t + (i % 7) * (i % 13)`): the division by d becomes a multiply by a
   precomputed reciprocal, as C compilers do.

   The CONST_INT just before the % must have put d in its divisor register,
   with nothing jumping in between, and 2 <= d < 2^31. For an inline int n
   (|n| <= 2^47) add off, a multiple of d above 2^47, so n' = n + off is
   positive and below 2^63 and n' mod d == n mod d. With sh = floor(log2(d -
   1)) (so 2^sh < d) and m = ceil(2^(64+sh) / d), which fits 64 bits,
   q = (n' * m) >> (64 + sh) is exactly n' / d: the rounding error
   e = m*d - 2^(64+sh) is below d <= 2^(sh+1), and n' * e < 2^(64+sh) for
   every n' < 2^63. Then n' - q*d is in [0, d): the floor modulo, whose
   sign is the divisor's, as GlowLang's % is. */
/* ceil(2^(64+sh) / d) for 2 <= d < 2^31 and 2^sh < d, in 64-bit
   arithmetic only (a 128-bit division needs a runtime routine that the
   Windows build does not link). 2^64 = d*q + r with 0 <= r < d, so
   2^(64+sh) = d*(q << sh) + (r << sh), and r << sh < 2^61 divides
   directly. */
static uint64_t jit_mod_reciprocal(uint64_t d, int sh) {
    uint64_t q = UINT64_MAX / d, r = UINT64_MAX % d + 1;   /* 2^64 = d*q + r */
    if (r == d) { q++; r = 0; }
    uint64_t rs = r << sh;
    uint64_t m = (q << sh) + rs / d;
    return m + (rs % d != 0);
}

static int jit_mod_const_divisor(const Bytecode *bc, int i, const unsigned char *targets, int64_t *d_out) {
    if (i < 1 || !targets || targets[i]) return 0;
    const Instruction *c = &bc->code[i - 1], *p = &bc->code[i];
    if (c->op != OP_CONST_INT || c->r0 != p->r2 || p->r1 == p->r2) return 0;
    int64_t d = c->arg;
    if (d < 2 || d > 0x7fffffff) return 0;
    *d_out = d;
    return 1;
}

static void emit_jit_int_mod_const(JITBuffer *buf, int r0, int r1, int r2, int64_t d, int i) {
    int sh = 0;
    while (((uint64_t)1 << (sh + 1)) <= (uint64_t)(d - 1)) sh++;
    uint64_t m = jit_mod_reciprocal((uint64_t)d, sh);
    uint64_t off = (uint64_t)d * ((((uint64_t)1) << 47) / (uint64_t)d + 1);

    emit_mov_rax_reg(buf, r1);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2);                          // mov rdx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xf8); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7FF8
    uint32_t jne_slow = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe0); jit_emit_byte(buf, 0x10); // shl rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xf8); jit_emit_byte(buf, 0x10); // sar rax, 16  (n)
    emit_mov_rdx_imm64(buf, off);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x01); jit_emit_byte(buf, 0xd0);                          // add rax, rdx (n')
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc1);                          // mov rcx, rax
    emit_mov_rdx_imm64(buf, m);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xf7); jit_emit_byte(buf, 0xe2);                          // mul rdx (rdx:rax = n' * m)
    if (sh) { jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, (uint8_t)sh); } // shr rdx, sh (q)
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x69); jit_emit_byte(buf, 0xd2); jit_emit_u32(buf, (uint32_t)d); // imul rdx, rdx, d
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x29); jit_emit_byte(buf, 0xd1);                          // sub rcx, rdx (n' - q*d)
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc8);                          // mov rax, rcx
    emit_box_int_rax(buf);
    uint32_t jmp_done = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    *(uint32_t*)(buf->code + jne_slow + 2) = (uint32_t)(buf->size - (jne_slow + 6));
    /* Not an inline int (a float, a big int, ...): the helper, as before. */
    emit_call_binop_helper(buf, r1, r2, (uintptr_t)jit_mod_helper);
    emit_deopt_check(buf, i);
    *(uint32_t*)(buf->code + jmp_done + 1) = (uint32_t)(buf->size - (jmp_done + 5));
    emit_mov_reg_rax(buf, r0);
}
#define EMIT_MOD(pc_, i_) do { \
        int64_t d_; \
        if (jit_mod_const_divisor(bc, (i_), targets, &d_)) emit_jit_int_mod_const(&buf, (pc_)->r0, (pc_)->r1, (pc_)->r2, d_, (i_)); \
        else emit_jit_int_mod(&buf, (pc_)->r0, (pc_)->r1, (pc_)->r2, (i_)); \
    } while (0)

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

typedef struct { int instr_idx; uint32_t patch_pos; } X86Jump;

static void emit_jcc_to_instr(JITBuffer *buf, unsigned char cc, int target, X86Jump *jumps, int *njumps) {
    jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, cc);
    jumps[*njumps].instr_idx = target; jumps[*njumps].patch_pos = buf->size; (*njumps)++;
    jit_emit_u32(buf, 0);
}

// Inline integer ordered comparisons; produces a bool value. Mixed types fall
// back to slow_fn (a jit_int_* helper). With sense >= 0 it is fused with the
// following conditional jump (see jit_fusable_branch): it jumps to `target`
// when the comparison is true (sense 1) or false (sense 0), and produces no
// boolean at all.
static void emit_jit_int_cmp_ex(JITBuffer *buf, int op, int r0, int r1, int r2, uintptr_t slow_fn, int i,
                                int sense, int target, X86Jump *jumps, int *njumps) {
    uint32_t jne_a, jne_b;
    /* Shifted payloads keep their signed order, so they compare as they are. */
    emit_int_tags_check(buf, r1, r2, &jne_a, &jne_b);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe0); jit_emit_byte(buf, 0x10); // shl rax, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xe1); jit_emit_byte(buf, 0x10); // shl rcx, 16
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x39); jit_emit_byte(buf, 0xc8); // cmp rax, rcx
    /* setcc / jcc condition codes for "true": l, g, le, ge, e */
    unsigned char cc_true = op == OP_LT_INT ? 0x0c : op == OP_GT_INT ? 0x0f : op == OP_LTE_INT ? 0x0e :
                            op == OP_GTE_INT ? 0x0d : 0x04;
    if (sense >= 0) {
        emit_jcc_to_instr(buf, (unsigned char)(0x80 | (sense ? cc_true : (cc_true ^ 1))), target, jumps, njumps);
    } else {
        jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, (unsigned char)(0x90 | cc_true)); jit_emit_byte(buf, 0xc1); // setcc cl
        jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0xb6); jit_emit_byte(buf, 0xc1); // movzx eax, cl
        emit_or_rax_bool_tag(buf);
    }
    uint32_t jmp_done = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    uint32_t slow_pos = buf->size;
    /* Ints and doubles compare as doubles: every 48-bit int converts
       exactly, so this matches value_cmp. NaN is unordered, which makes
       every comparison false, as in the interpreter. Anything else (big
       ints, bools, strings, ...) goes to the helper. */
    JumpList helper = {0};
    emit_load_number_xmm(buf, r1, 0, 0, &helper);
    emit_load_number_xmm(buf, r2, 1, 0, &helper);
    /* a < b is b > a and a <= b is b >= a: "above" conditions are false
       when the operands are unordered. */
    if (op == OP_LT_INT || op == OP_LTE_INT) {
        jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2e); jit_emit_byte(buf, 0xc8); // ucomisd xmm1, xmm0
    } else {
        jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x2e); jit_emit_byte(buf, 0xc1); // ucomisd xmm0, xmm1
    }
    /* "true" is ZF=1 and PF=0 for ==, "above" / "above or equal" otherwise;
       an unordered result (NaN) is false either way. */
    unsigned char fcc_true = (op == OP_LT_INT || op == OP_GT_INT) ? 0x07 : 0x03;   /* a / ae */
    if (sense < 0) {
        if (op == OP_EQ_INT) {
            jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x94); jit_emit_byte(buf, 0xc0); // sete al
            jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x9b); jit_emit_byte(buf, 0xc1); // setnp cl
            jit_emit_byte(buf, 0x20); jit_emit_byte(buf, 0xc8);                         // and al, cl
        } else {
            jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, (unsigned char)(0x90 | fcc_true)); jit_emit_byte(buf, 0xc0); // seta / setae al
        }
        jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0xb6); jit_emit_byte(buf, 0xc0); // movzx eax, al
        emit_or_rax_bool_tag(buf);
    } else if (op == OP_EQ_INT) {
        if (sense) {
            jit_emit_byte(buf, 0x7a); jit_emit_byte(buf, 0x06);                       // jp +6 (unordered: not equal)
            emit_jcc_to_instr(buf, 0x84, target, jumps, njumps);                    // je target
        } else {
            emit_jcc_to_instr(buf, 0x8a, target, jumps, njumps);                    // jp target
            emit_jcc_to_instr(buf, 0x85, target, jumps, njumps);                    // jne target
        }
    } else {
        /* false is "below or equal" / "below", which unordered also sets */
        emit_jcc_to_instr(buf, (unsigned char)(0x80 | (sense ? fcc_true : (fcc_true ^ 1))), target, jumps, njumps);
    }
    uint32_t jmp_done2 = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    jump_list_patch(buf, &helper, buf->size);
    emit_call_binop_helper(buf, r1, r2, slow_fn);
    emit_deopt_check(buf, i); /* incomparable operands: the interpreter raises */
    if (sense >= 0) {
        jit_emit_byte(buf, 0xa8); jit_emit_byte(buf, 0x01);                           // test al, 1
        emit_jcc_to_instr(buf, sense ? 0x85 : 0x84, target, jumps, njumps);         // jnz / jz target
    }
    *(uint32_t*)(buf->code + jmp_done2 + 1) = (uint32_t)(buf->size - (jmp_done2 + 5));
    uint32_t done_pos = buf->size;
    if (sense < 0) emit_mov_reg_rax(buf, r0);
    *(uint32_t*)(buf->code + jne_a + 2) = (uint32_t)(slow_pos - (jne_a + 6));
    *(uint32_t*)(buf->code + jne_b + 2) = (uint32_t)(slow_pos - (jne_b + 6));
    *(uint32_t*)(buf->code + jmp_done + 1) = (uint32_t)(done_pos - (jmp_done + 5));
}

/* In the x86 compile loops: the comparison at i, fused with the jump at
   i + 1 when jit_fusable_branch allows it (the jump is then done, so the
   loop moves past it). Uses the loop's buf, pc, i, offsets, jumps,
   jump_count, targets and cmp_fn. */
#define EMIT_CMP_MAYBE_FUSED(cmp_op) do { \
        int sense_ = jit_fusable_branch(bc, i, targets); \
        if (sense_ >= 0) { \
            emit_jit_int_cmp_ex(&buf, (cmp_op), pc->r0, pc->r1, pc->r2, cmp_fn, i, sense_, bc->code[i + 1].arg, jumps, &jump_count); \
            i++; \
            offsets[i] = buf.size; \
        } else { \
            emit_jit_int_cmp(&buf, (cmp_op), pc->r0, pc->r1, pc->r2, cmp_fn, i); \
        } \
    } while (0)

static void emit_jit_int_cmp(JITBuffer *buf, int op, int r0, int r1, int r2, uintptr_t slow_fn, int i) {
    emit_jit_int_cmp_ex(buf, op, r0, r1, r2, slow_fn, i, -1, 0, NULL, NULL);
}

static void emit_disp32(JITBuffer *buf, size_t off) { jit_emit_u32(buf, (uint32_t)off); }

/* R[r0] = R[r1].name. Fast path in machine code for an instance whose
   field sits where this instruction found it last time: cache[0] of the
   instruction holds that entry number + 1 and the interned key, both
   written by glow_get_property. The entry still holding that exact key
   means it is that field (keys are unique), so the check needs no class
   or layout of its own. A miss, a non-instance, or a field holding a bound
   method (which glow_get_property may rebind) goes to the helper.
   Uses rax, rcx, rdx, r10, r11. */
static void emit_get_property(JITBuffer *buf, Instruction *pc, Instruction *orig_pc, int i) {
    JumpList slow = {0};
    emit_mov_rax_reg(buf, pc->r1);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2);                          // mov rdx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xfe); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7FFE (object)
    emit_jcc_to(buf, 0x85, &slow);
    emit_mov_r11_imm64(buf, MASK_PAYLOAD);
    jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x21); jit_emit_byte(buf, 0xd8);                          // and rax, r11
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x85); jit_emit_byte(buf, 0xc0);                          // test rax, rax
    emit_jcc_to(buf, 0x84, &slow);
    jit_emit_byte(buf, 0x80); jit_emit_byte(buf, 0xb8); emit_disp32(buf, offsetof(GCObject, type)); jit_emit_byte(buf, OBJ_INSTANCE); // cmp byte [rax+type], OBJ_INSTANCE
    emit_jcc_to(buf, 0x85, &slow);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x80); emit_disp32(buf, offsetof(Instance, properties)); // mov rax, [rax+properties]
    emit_mov_r10_imm64(buf, (uintptr_t)&orig_pc->cache[0]);
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x8a); emit_disp32(buf, offsetof(CacheEntry, val)); // mov rcx, [r10+val]
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x83); jit_emit_byte(buf, 0xe9); jit_emit_byte(buf, 0x01); // sub rcx, 1
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x63); jit_emit_byte(buf, 0x90); emit_disp32(buf, offsetof(Dict, capacity)); // movsxd rdx, dword [rax+capacity]
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x39); jit_emit_byte(buf, 0xd1);                          // cmp rcx, rdx
    emit_jcc_to(buf, 0x83, &slow);                                                                          // jae: no entry cached (-1) or past the end
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x90); emit_disp32(buf, offsetof(Dict, keys)); // mov rdx, [rax+keys]
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x14); jit_emit_byte(buf, 0xca); // mov rdx, [rdx+rcx*8]
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xd2);                          // mov r10, rdx  (r10 reloaded below)
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr r10, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xfb); jit_emit_byte(buf, 0x7f); // cmp r10w, 0x7FFB (string)
    emit_jcc_to(buf, 0x85, &slow);
    jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x21); jit_emit_byte(buf, 0xda);                          // and rdx, r11
    emit_mov_r10_imm64(buf, (uintptr_t)&orig_pc->cache[0]);
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x3b); jit_emit_byte(buf, 0x92); emit_disp32(buf, offsetof(CacheEntry, key)); // cmp rdx, [r10+key]
    emit_jcc_to(buf, 0x85, &slow);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x90); emit_disp32(buf, offsetof(Dict, values)); // mov rdx, [rax+values]
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x04); jit_emit_byte(buf, 0xca); // mov rax, [rdx+rcx*8]
    /* A bound method in a field takes the helper (it may rebind it). */
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2);                          // mov rdx, rax
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0xc1); jit_emit_byte(buf, 0xea); jit_emit_byte(buf, 0x30); // shr rdx, 48
    jit_emit_byte(buf, 0x66); jit_emit_byte(buf, 0x81); jit_emit_byte(buf, 0xfa); jit_emit_byte(buf, 0xfe); jit_emit_byte(buf, 0x7f); // cmp dx, 0x7FFE
    uint32_t not_obj = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x85); jit_emit_u32(buf, 0);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xc2);                          // mov rdx, rax
    jit_emit_byte(buf, 0x4c); jit_emit_byte(buf, 0x21); jit_emit_byte(buf, 0xda);                          // and rdx, r11
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x85); jit_emit_byte(buf, 0xd2);                          // test rdx, rdx
    uint32_t null_obj = buf->size; jit_emit_byte(buf, 0x0f); jit_emit_byte(buf, 0x84); jit_emit_u32(buf, 0);
    jit_emit_byte(buf, 0x80); jit_emit_byte(buf, 0xba); emit_disp32(buf, offsetof(GCObject, type)); jit_emit_byte(buf, OBJ_BOUND_METHOD); // cmp byte [rdx+type], OBJ_BOUND_METHOD
    emit_jcc_to(buf, 0x84, &slow);
    uint32_t fast_done = buf->size;
    *(uint32_t*)(buf->code + not_obj + 2) = (uint32_t)(fast_done - (not_obj + 6));
    *(uint32_t*)(buf->code + null_obj + 2) = (uint32_t)(fast_done - (null_obj + 6));
    uint32_t jmp_store = buf->size; jit_emit_byte(buf, 0xe9); jit_emit_u32(buf, 0);
    jump_list_patch(buf, &slow, buf->size);
    emit_mov_arg1_reg(buf, pc->r1);
    emit_mov_arg2_imm64(buf, (uintptr_t)pc->ptr);
    emit_mov_arg3_imm64(buf, (uintptr_t)orig_pc);
    emit_mov_rax_imm64(buf, (uintptr_t)jit_get_property_helper);
    emit_call_rax(buf);
    emit_deopt_check(buf, i);
    *(uint32_t*)(buf->code + jmp_store + 1) = (uint32_t)(buf->size - (jmp_store + 5));
    emit_mov_reg_rax(buf, pc->r0);
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
            case OP_NOT: case OP_IN: case OP_SLICE: case OP_SKIP: case OP_STOP: case OP_ITER_NEXT2:
                break;
            default: jit_debug_skipped(bc, fe, "unsupported", pc->op); bc->jit_failed = 1; return;
        }
    }

    /* Recursive functions that also call other functions or store into
       lists used to be kept out of compiled code: the self-call path once
       reused the caller's register window. jit_self_call_direct now gives
       every call a fresh frame above vm_stack_top (as the interpreter's
       DO_CALL does), so they compile like any other function
       (tests/regress_jit_recursive_calls.glow). */

    size_t size = jit_buffer_size(bc);
    uint8_t *code = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { bc->jit_failed = 1; return; }
    JITBuffer buf = {code, 0, size};

    // Prologue
    x86_pins = jit_has_loop(bc);
    emit_prologue(&buf);
    emit_zero_locals(&buf, bc, fe);

    uint32_t body_start = buf.size;

    uint32_t *offsets = calloc(bc->count + 1, sizeof(uint32_t));
    X86Jump *jumps = calloc((size_t)bc->count * 4 + 4, sizeof(*jumps)); /* a fused compare-and-branch adds up to four */
    unsigned char *targets = jit_jump_targets(bc);
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
            case OP_GET_PROPERTY: emit_get_property(&buf, pc, orig_pc, i); break;
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
                    EMIT_CMP_MAYBE_FUSED(int_op);
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
            /* not, in and slices call the interpreter's own routines; skip
               and stop are jumps (a stop outside any loop ends the code, which
               the interpreter does). Without these a loop containing one left
               compiled code on every pass. */
            case OP_NOT:
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_rax_imm64(&buf, (uintptr_t)glow_not);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_IN:
                emit_call_binop_helper(&buf, pc->r1, pc->r2, (uintptr_t)glow_in);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_SLICE:
                emit_mov_arg1_reg(&buf, pc->r1); emit_mov_arg2_reg(&buf, pc->r2); emit_mov_arg3_reg(&buf, pc->r2 + 1);
                emit_mov_rax_imm64(&buf, (uintptr_t)glow_slice);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_SKIP: case OP_STOP:
                if (pc->arg > 0) { if (pc->arg <= i) emit_gil_poll(&buf); jit_emit_byte(&buf, 0xe9); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; }
                else if (pc->op == OP_STOP) emit_side_exit(&buf, i);
                break;
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
                uint32_t len_patch[3];
                int len_fast = emit_len_fast(&buf, pc, i, targets, len_patch);
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
                if (len_fast) *(uint32_t*)&buf.code[len_patch[2] + 1] = buf.size - (len_patch[2] + 5);
                break;
            }
            case OP_CALL_METHOD: {
                uint32_t ap_list = 0, ap_full = 0, ap_done = 0;
                int ap = jit_is_list_append(pc);
                if (ap) emit_list_append_fast(&buf, pc, &ap_list, &ap_full, &ap_done);
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
                if (ap) *(uint32_t*)&buf.code[ap_done + 1] = buf.size - (ap_done + 5);
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
                const char *ikey = jit_const_key(pc, i, targets, pc->r2);
                if (ikey) {
                    emit_mov_arg1_reg(&buf, pc->r1); emit_mov_arg2_reg(&buf, pc->r2);
                    emit_mov_arg3_imm64(&buf, (uintptr_t)ikey);
                    emit_mov_rax_imm64(&buf, (uintptr_t)glow_index_key_jit);
                    emit_call_rax(&buf);
                    emit_deopt_check(&buf, i);
                    emit_mov_reg_rax(&buf, pc->r0);
                } else emit_fast_index(&buf, pc, i);
                break;
            }
            case OP_STORE_INDEX: {
                const char *ikey = jit_const_key(pc, i, targets, pc->r1);
                if (ikey) {
                    emit_mov_arg1_reg(&buf, pc->r0); emit_mov_arg2_reg(&buf, pc->r1); emit_mov_arg3_reg(&buf, pc->r2);
                    emit_mov_arg4_imm64(&buf, (uintptr_t)ikey);
                    emit_mov_rax_imm64(&buf, (uintptr_t)glow_store_index_key_jit);
                    emit_call_rax(&buf);
                    emit_deopt_check(&buf, i);
                } else emit_fast_store_index(&buf, pc, i);
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
                EMIT_CMP_MAYBE_FUSED(pc->op);
                break;
            }
            case OP_MOD: EMIT_MOD(pc, i); break;
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
                /* A literal's string is created once and kept for good, so
                   its value goes straight into the code. */
                emit_mov_rax_imm64(&buf, (uint64_t)jit_const_string((const char*)pc->ptr));
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
            case OP_ITER_NEXT2:
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_iter_pair_first);
                emit_call_rax(&buf);
                emit_mov_rcx_imm64(&buf, JIT_ITER_DONE);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xc8); // cmp rax, rcx
                jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84);                          // je: the pairs are used up
                jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++;
                emit_mov_reg_rax(&buf, pc->r0);
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_iter_pair_second);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r2);
                break;
            case OP_RETURN: {
                emit_mov_rax_reg(&buf, pc->r0);
                emit_epilogue(&buf);
                break;
            }
            default: bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); free(targets); return;
        }
    }
    offsets[bc->count] = buf.size;
    if (buf.size + 64 >= buf.capacity) { bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); free(targets); return; }
    for (int i = 0; i < jump_count; i++) {
        uint32_t target_offset = (jumps[i].instr_idx == -1) ? body_start : offsets[jumps[i].instr_idx];
        uint32_t patch_pos = jumps[i].patch_pos;
        int32_t rel = (int32_t)target_offset - (int32_t)(patch_pos + 4);
        memcpy(code + patch_pos, &rel, 4);
    }
    mprotect(code, size, PROT_READ | PROT_EXEC);
    bc->jit_code = code; bc->jit_size = size;
    jit_debug_compiled(bc, fe, "optimizing", code, buf.size);
    free(offsets); free(jumps); free(targets);
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
            case OP_NOT: case OP_IN: case OP_SLICE: case OP_SKIP: case OP_STOP: case OP_ITER_NEXT2:
                break;
            default: jit_debug_skipped(bc, fe, "unsupported", pc->op); bc->jit_failed = 1; return;
        }
    }


    size_t size = jit_buffer_size(bc);
    uint8_t *code = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { if (osr_mode) bc->osr_failed = 1; else bc->jit_failed = 1; return; }
    JITBuffer buf = {code, 0, size};

    x86_pins = jit_has_loop(bc);
    jit_baseline_emit_prologue(&buf);
    if (!osr_mode) emit_zero_locals(&buf, bc, fe);

    uint32_t body_start = buf.size;

    uint32_t *offsets = calloc(bc->count + 1, sizeof(uint32_t));
    X86Jump *jumps = calloc((size_t)bc->count * 4 + 4, sizeof(*jumps)); /* a fused compare-and-branch adds up to four */
    unsigned char *targets = jit_jump_targets(bc);
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
            case OP_GET_PROPERTY: emit_get_property(&buf, pc, orig_pc, i); break;
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
                    EMIT_CMP_MAYBE_FUSED(int_op);
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
            /* not, in and slices call the interpreter's own routines; skip
               and stop are jumps (a stop outside any loop ends the code, which
               the interpreter does). Without these a loop containing one left
               compiled code on every pass. */
            case OP_NOT:
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_rax_imm64(&buf, (uintptr_t)glow_not);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_IN:
                emit_call_binop_helper(&buf, pc->r1, pc->r2, (uintptr_t)glow_in);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_SLICE:
                emit_mov_arg1_reg(&buf, pc->r1); emit_mov_arg2_reg(&buf, pc->r2); emit_mov_arg3_reg(&buf, pc->r2 + 1);
                emit_mov_rax_imm64(&buf, (uintptr_t)glow_slice);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            case OP_SKIP: case OP_STOP:
                if (pc->arg > 0) { if (pc->arg <= i) emit_gil_poll(&buf); jit_emit_byte(&buf, 0xe9); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++; }
                else if (pc->op == OP_STOP) emit_side_exit(&buf, i);
                break;
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
                uint32_t len_patch[3];
                int len_fast = emit_len_fast(&buf, pc, i, targets, len_patch);
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
                if (len_fast) *(uint32_t*)&buf.code[len_patch[2] + 1] = buf.size - (len_patch[2] + 5);
                break;
            }
            case OP_CALL_METHOD: {
                uint32_t ap_list = 0, ap_full = 0, ap_done = 0;
                int ap = jit_is_list_append(pc);
                if (ap) emit_list_append_fast(&buf, pc, &ap_list, &ap_full, &ap_done);
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
                if (ap) *(uint32_t*)&buf.code[ap_done + 1] = buf.size - (ap_done + 5);
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
                const char *ikey = jit_const_key(pc, i, targets, pc->r2);
                if (ikey) {
                    emit_mov_arg1_reg(&buf, pc->r1); emit_mov_arg2_reg(&buf, pc->r2);
                    emit_mov_arg3_imm64(&buf, (uintptr_t)ikey);
                    emit_mov_rax_imm64(&buf, (uintptr_t)glow_index_key_jit);
                    emit_call_rax(&buf);
                    emit_deopt_check(&buf, i);
                    emit_mov_reg_rax(&buf, pc->r0);
                } else emit_fast_index(&buf, pc, i);
                break;
            }
            case OP_STORE_INDEX: {
                const char *ikey = jit_const_key(pc, i, targets, pc->r1);
                if (ikey) {
                    emit_mov_arg1_reg(&buf, pc->r0); emit_mov_arg2_reg(&buf, pc->r1); emit_mov_arg3_reg(&buf, pc->r2);
                    emit_mov_arg4_imm64(&buf, (uintptr_t)ikey);
                    emit_mov_rax_imm64(&buf, (uintptr_t)glow_store_index_key_jit);
                    emit_call_rax(&buf);
                    emit_deopt_check(&buf, i);
                } else emit_fast_store_index(&buf, pc, i);
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
                EMIT_CMP_MAYBE_FUSED(pc->op);
                break;
            }
            case OP_MOD: EMIT_MOD(pc, i); break;
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
                /* A literal's string is created once and kept for good, so
                   its value goes straight into the code. */
                emit_mov_rax_imm64(&buf, (uint64_t)jit_const_string((const char*)pc->ptr));
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
            case OP_ITER_NEXT2:
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_iter_pair_first);
                emit_call_rax(&buf);
                emit_mov_rcx_imm64(&buf, JIT_ITER_DONE);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xc8); // cmp rax, rcx
                jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84);                          // je: the pairs are used up
                jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++;
                emit_mov_reg_rax(&buf, pc->r0);
                emit_mov_arg1_reg(&buf, pc->r1);
                emit_mov_rax_imm64(&buf, (uintptr_t)jit_iter_pair_second);
                emit_call_rax(&buf);
                emit_mov_reg_rax(&buf, pc->r2);
                break;
            case OP_RETURN: {
                emit_mov_rax_reg(&buf, pc->r0);
                jit_baseline_emit_epilogue(&buf);
                break;
            }
            default:
                if (osr_mode) { emit_side_exit(&buf, i); break; }
                bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); free(targets); return;
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
        munmap(code, size); free(offsets); free(jumps); free(targets); free(osr_offsets);
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
        jit_debug_compiled(bc, fe, "osr", code, buf.size);
    } else {
        bc->baseline_jit_code = code; bc->baseline_jit_size = size; bc->baseline_jit_offsets = offsets; bc->jit_tier = JIT_TIER_BASELINE;
        jit_debug_compiled(bc, fe, "baseline", code, buf.size);
    }
    free(jumps); free(targets);
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
/* x<rd> = the low byte of the GCObject type field of the object at x<rn>
   (a 32-bit load and a bit-field extract: there is no byte load here). */
static void a64_load_obj_type(A64 *a, int rd, int rn) {
    size_t off = offsetof(GCObject, type);
    a64_emit(a, a64_ldr_w(rd, rn, (uint32_t)(off & ~(size_t)3)));
    a64_emit(a, a64_ubfx(rd, rd, (int)(off & 3) * 8, 8));
}

/* R[r0] = R[r1].name with the same cached fast path as the x86
   emit_get_property (see there for why the key check is enough). Uses
   x0 and x9-x15. */
static void a64_get_property(A64 *a, Instruction *pc, Instruction *orig_pc, int i) {
    A64Jumps slow = {0};
    a64_load_reg(a, 0, pc->r1);
    a64_emit(a, a64_lsr_imm(9, 0, 48));
    a64_emit(a, a64_movz_w(10, 0x7FFE));
    a64_emit(a, a64_cmp(9, 10));
    a64_jumps_add(&slow, a64_bcond_fwd(a, A64_NE));              /* not an object */
    a64_emit(a, a64_ubfx(9, 0, 0, 48));
    a64_jumps_add(&slow, a64_here(a)); a64_emit(a, a64_cbz(9, 0));
    a64_load_obj_type(a, 10, 9);
    a64_emit(a, a64_cmp_imm(10, OBJ_INSTANCE));
    a64_jumps_add(&slow, a64_bcond_fwd(a, A64_NE));
    a64_emit(a, a64_ldr(9, 9, (uint32_t)offsetof(Instance, properties)));
    a64_mov_imm(a, 12, (uintptr_t)&orig_pc->cache[0]);
    a64_emit(a, a64_ldr(11, 12, (uint32_t)offsetof(CacheEntry, val)));
    a64_emit(a, a64_sub_imm(11, 11, 1));
    a64_emit(a, a64_ldr_w(10, 9, (uint32_t)offsetof(Dict, capacity)));
    a64_emit(a, a64_cmp(11, 10));
    a64_jumps_add(&slow, a64_bcond_fwd(a, A64_HS));              /* nothing cached, or past the end */
    a64_emit(a, a64_add(14, 11, 11));                            /* x14 = entry * 8 */
    a64_emit(a, a64_add(14, 14, 14));
    a64_emit(a, a64_add(14, 14, 14));
    a64_emit(a, a64_ldr(10, 9, (uint32_t)offsetof(Dict, keys)));
    a64_emit(a, a64_ldr_reg(10, 10, 14));
    a64_emit(a, a64_lsr_imm(13, 10, 48));
    a64_emit(a, a64_movz_w(15, 0x7FFB));
    a64_emit(a, a64_cmp(13, 15));
    a64_jumps_add(&slow, a64_bcond_fwd(a, A64_NE));              /* not a string key */
    a64_emit(a, a64_ubfx(10, 10, 0, 48));
    a64_emit(a, a64_ldr(13, 12, (uint32_t)offsetof(CacheEntry, key)));
    a64_emit(a, a64_cmp(10, 13));
    a64_jumps_add(&slow, a64_bcond_fwd(a, A64_NE));              /* another field */
    a64_emit(a, a64_ldr(10, 9, (uint32_t)offsetof(Dict, values)));
    a64_emit(a, a64_ldr_reg(0, 10, 14));
    /* A bound method in a field takes the helper (it may rebind it). */
    a64_emit(a, a64_lsr_imm(13, 0, 48));
    a64_emit(a, a64_movz_w(15, 0x7FFE));
    a64_emit(a, a64_cmp(13, 15));
    uint32_t not_obj = a64_bcond_fwd(a, A64_NE);
    a64_emit(a, a64_ubfx(13, 0, 0, 48));
    uint32_t null_obj = a64_here(a); a64_emit(a, a64_cbz(13, 0));
    a64_load_obj_type(a, 15, 13);
    a64_emit(a, a64_cmp_imm(15, OBJ_BOUND_METHOD));
    a64_jumps_add(&slow, a64_bcond_fwd(a, A64_EQ));
    a64_link_here(a, not_obj);
    a64_link_here(a, null_obj);
    uint32_t done = a64_b_fwd(a);
    a64_jumps_link_here(a, &slow);
    a64_load_reg(a, 0, pc->r1);
    a64_mov_imm(a, 1, (uintptr_t)pc->ptr);
    a64_mov_imm(a, 2, (uintptr_t)orig_pc);
    a64_call(a, (const void*)jit_get_property_helper);
    a64_deopt_check(a, i);
    a64_link_here(a, done);
    a64_store_reg(a, 0, pc->r0);
}

/* A jump to an instruction, patched once the code is laid out. */
typedef struct { int target; uint32_t at; } A64InstrJump;

/* Branch to instruction `target` when `cond` holds: b.cond only reaches
   1MB, so skip an unconditional b (which reaches the whole buffer). */
static void a64_branch_to_instr_if(A64 *a, int cond, int target, A64InstrJump *jumps, int *njumps) {
    a64_emit(a, a64_b_cond(cond ^ 1, 8));
    jumps[*njumps].target = target; jumps[*njumps].at = a64_b_fwd(a); (*njumps)++;
}

/* As the x86 emit_jit_int_cmp_ex: with sense >= 0 the comparison is fused
   with the conditional jump after it (see jit_fusable_branch). Inverting
   an A64 condition flips its low bit; for the float conditions below the
   inverse also holds when the result is unordered, so NaN is "false". */
static void a64_int_cmp_ex(A64 *a, int op, int r0, int r1, int r2, const void *slow_fn, int i,
                           int sense, int target, A64InstrJump *jumps, int *njumps) {
    A64Jumps slow = {0};
    a64_load_ints(a, r1, r2, &slow);
    a64_emit(a, a64_cmp(0, 1));
    int cond = op == OP_LT_INT ? A64_LT : op == OP_GT_INT ? A64_GT :
               op == OP_LTE_INT ? A64_LE : op == OP_GTE_INT ? A64_GE : A64_EQ;
    if (sense >= 0) {
        a64_branch_to_instr_if(a, sense ? cond : (cond ^ 1), target, jumps, njumps);
    } else {
        a64_emit(a, a64_cset(2, cond));
        a64_mov_imm(a, 0, value_bool(0));
        a64_emit(a, a64_orr(0, 0, 2));
    }
    uint32_t done = a64_b_fwd(a);
    a64_jumps_link_here(a, &slow);
    /* Ints and doubles compare as doubles (exact for 48-bit ints). After
       fcmp an unordered (NaN) result sets NZCV to 0011, which makes MI, LS,
       GT, GE and EQ all false, as the interpreter's comparisons are. */
    A64Jumps helper = {0};
    a64_load_number(a, r1, 0, 0, &helper);
    a64_load_number(a, r2, 1, 0, &helper);
    a64_emit(a, a64_fcmp(0, 1));
    int fcond = op == OP_LT_INT ? A64_MI : op == OP_GT_INT ? A64_GT :
                op == OP_LTE_INT ? A64_LS : op == OP_GTE_INT ? A64_GE : A64_EQ;
    if (sense >= 0) {
        a64_branch_to_instr_if(a, sense ? fcond : (fcond ^ 1), target, jumps, njumps);
    } else {
        a64_emit(a, a64_cset(2, fcond));
        a64_mov_imm(a, 0, value_bool(0));
        a64_emit(a, a64_orr(0, 0, 2));
    }
    uint32_t done2 = a64_b_fwd(a);
    a64_jumps_link_here(a, &helper);
    a64_call_binop(a, r1, r2, slow_fn);
    a64_deopt_check(a, i);   /* incomparable operands: the interpreter raises */
    if (sense >= 0) {
        /* the helper's boolean: its low bit decides */
        a64_emit(a, sense ? a64_tbz(0, 0, 8) : a64_tbnz(0, 0, 8));
        jumps[*njumps].target = target; jumps[*njumps].at = a64_b_fwd(a); (*njumps)++;
    }
    a64_link_here(a, done);
    a64_link_here(a, done2);
    if (sense < 0) a64_store_reg(a, 0, r0);
}

static void a64_int_cmp(A64 *a, int op, int r0, int r1, int r2, const void *slow_fn, int i) {
    a64_int_cmp_ex(a, op, r0, r1, r2, slow_fn, i, -1, 0, NULL, NULL);
}

/* The x86 EMIT_CMP_MAYBE_FUSED for a64_compile's loop (A, bc, pc, i, fn,
   offsets, jumps, njumps, targets). */
#define A64_CMP_MAYBE_FUSED(cmp_op) do { \
        int sense_ = jit_fusable_branch(bc, i, targets); \
        if (sense_ >= 0) { \
            a64_int_cmp_ex(A, (cmp_op), pc->r0, pc->r1, pc->r2, fn, i, sense_, bc->code[i + 1].arg, jumps, &njumps); \
            i++; \
            offsets[i] = a64_here(A); \
        } else { \
            a64_int_cmp(A, (cmp_op), pc->r0, pc->r1, pc->r2, fn, i); \
        } \
    } while (0)

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
        case OP_NOT: case OP_IN: case OP_SLICE: case OP_SKIP: case OP_STOP: case OP_ITER_NEXT2:
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
        for (int i = 0; i < bc->count; i++) {
            OpCode op = bc->code[i].op;
            if (!a64_supported(op)) { jit_debug_skipped(bc, fe, "unsupported", op); bc->jit_failed = 1; return; }
        }
        /* Recursive functions that call others compile too: see the note
           in jit_compile_ext. */
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
    A64InstrJump *jumps = calloc((size_t)bc->count * 4 + 4, sizeof(*jumps)); /* a fused compare-and-branch adds up to three */
    int njumps = 0;
    unsigned char *targets = jit_jump_targets(bc);

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
                A64_CMP_MAYBE_FUSED(int_op);
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
                A64_CMP_MAYBE_FUSED(pc->op);
                break;
            }
            case OP_NOT:
                a64_load_reg(A, 0, pc->r1);
                a64_call(A, (const void*)glow_not);
                a64_store_reg(A, 0, pc->r0);
                break;
            case OP_IN:
                a64_call_binop(A, pc->r1, pc->r2, (const void*)glow_in);
                a64_store_reg(A, 0, pc->r0);
                break;
            case OP_SLICE:
                a64_load_reg(A, 0, pc->r1); a64_load_reg(A, 1, pc->r2); a64_load_reg(A, 2, pc->r2 + 1);
                a64_call(A, (const void*)glow_slice);
                a64_store_reg(A, 0, pc->r0);
                break;
            case OP_SKIP: case OP_STOP:
                if (pc->arg > 0) { if (pc->arg <= i) a64_gil_poll(A); jumps[njumps].target = pc->arg; jumps[njumps].at = a64_b_fwd(A); njumps++; }
                else if (pc->op == OP_STOP) a64_side_exit(A, i);
                break;
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
                a64_get_property(A, pc, orig_pc, i);
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
            case OP_INDEX: {
                const char *ikey = jit_const_key(pc, i, targets, pc->r2);
                if (ikey) {
                    a64_load_reg(A, 0, pc->r1);
                    a64_load_reg(A, 1, pc->r2);
                    a64_mov_imm(A, 2, (uintptr_t)ikey);
                    a64_call(A, (const void*)glow_index_key_jit);
                } else a64_call_binop(A, pc->r1, pc->r2, (const void*)glow_index_jit);
                a64_deopt_check(A, i);
                a64_store_reg(A, 0, pc->r0);
                break;
            }
            case OP_STORE_INDEX: {
                const char *ikey = jit_const_key(pc, i, targets, pc->r1);
                a64_load_reg(A, 0, pc->r0);
                a64_load_reg(A, 1, pc->r1);
                a64_load_reg(A, 2, pc->r2);
                if (ikey) { a64_mov_imm(A, 3, (uintptr_t)ikey); a64_call(A, (const void*)glow_store_index_key_jit); }
                else a64_call(A, (const void*)glow_store_index_jit);
                a64_deopt_check(A, i);
                break;
            }
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
                a64_mov_imm(A, 0, (uint64_t)jit_const_string((const char*)pc->ptr));
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
            case OP_ITER_NEXT2:
                a64_load_reg(A, 0, pc->r1);
                a64_call(A, (const void*)jit_iter_pair_first);
                a64_mov_imm(A, 9, JIT_ITER_DONE);
                a64_emit(A, a64_cmp(0, 9));
                a64_emit(A, a64_b_cond(A64_NE, 8));   /* not over: skip the exit */
                jumps[njumps].target = pc->arg; jumps[njumps].at = a64_b_fwd(A); njumps++;
                a64_store_reg(A, 0, pc->r0);
                a64_load_reg(A, 0, pc->r1);
                a64_call(A, (const void*)jit_iter_pair_second);
                a64_store_reg(A, 0, pc->r2);
                break;
            case OP_RETURN:
                a64_load_reg(A, 0, pc->r0);
                a64_epilogue(A);
                break;
            default:
                if (osr) { a64_side_exit(A, i); break; }
                bc->jit_failed = 1;
                a64_code_discard(code, size); free(offsets); free(jumps); free(targets);
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
        a64_code_discard(code, size); free(offsets); free(jumps); free(targets); free(osr_offsets);
        return;
    }
    a64_code_finish(code, size);
    jit_debug_compiled(bc, fe, osr ? "osr" : mode == A64_BASELINE ? "baseline" : "optimizing", code, a.buf.size);
    free(jumps); free(targets);
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
