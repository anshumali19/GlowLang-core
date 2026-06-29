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

void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) {
    (void)current_sig;
    if (bc->jit_code || bc->jit_failed) return;

    for (int i = 0; i < bc->count; i++) {
        Instruction *pc = &bc->code[i];
        switch (pc->op) {
            case OP_CONST_INT: case OP_CONST_FLOAT: case OP_CONST_BOOL:
            case OP_LOAD_VAR: case OP_STORE_VAR: case OP_STORE_LET:
            case OP_MOVE: case OP_NOP:
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
            case OP_GET_PROPERTY: case OP_LT: case OP_GT: case OP_LTE:
            case OP_GTE: case OP_EQ: case OP_NEQ: case OP_JUMP:
            case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_RETURN:
            case OP_CALL: case OP_CALL_VALUE: case OP_CALL_METHOD:
            case OP_INDEX: case OP_STORE_INDEX:
            case OP_SIMD_ADD: case OP_TAIL_CALL:
                break;
            default: bc->jit_failed = 1; return;
        }
    }

    size_t size = 65536; 
    uint8_t *code = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { bc->jit_failed = 1; return; }
    JITBuffer buf = {code, 0, size};

    // Prologue
    jit_emit_byte(&buf, 0x55); 
    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe5);
    jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x54); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x55);
    jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x56); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x57);
    jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xfc); // r12 = R
    jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xf5); // r13 = pc_idx

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
                    emit_mov_rax_imm64(&buf, MASK_NAN | (15ULL << 48)); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3);
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
                if (pc->cache[0].key) {
                    emit_mov_rax_reg(&buf, pc->r1);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0xd0); emit_mov_rsi_imm64(&buf, MASK_NAN | (6ULL << 48)); emit_mov_rdi_imm64(&buf, MASK_NAN | (7ULL << 48)); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xfa); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xf2);
                    uint32_t p1 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
                    emit_mov_rsi_imm64(&buf, 0x0000FFFFFFFFFFFFULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xf0); emit_mov_rsi_imm64(&buf, (uintptr_t)pc->cache[0].key); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x3b); jit_emit_byte(&buf, 0x70); jit_emit_byte(&buf, 40);
                    uint32_t p2 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x40); jit_emit_byte(&buf, 48); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x40); jit_emit_byte(&buf, pc->cache[0].val * 8);
                    uint32_t pd = buf.size; jit_emit_byte(&buf, 0xe9); jit_emit_u32(&buf, 0);
                    uint32_t bailout = buf.size; *(uint32_t*)(buf.code + p1 + 2) = (uint32_t)(bailout - (p1 + 6)); *(uint32_t*)(buf.code + p2 + 2) = (uint32_t)(bailout - (p2 + 6));
                    emit_mov_rax_imm64(&buf, (uint64_t)i); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x45); jit_emit_byte(&buf, 0x00); emit_mov_rax_imm64(&buf, MASK_NAN | (15ULL << 48)); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3);
                    uint32_t done = buf.size; *(uint32_t*)(buf.code + pd + 1) = (uint32_t)(done - (pd + 5));
                    emit_mov_reg_rax(&buf, pc->r0);
                } else { bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return; }
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
            case OP_CALL:
            case OP_CALL_VALUE: {
                FuncEntry *target_fe = (pc->op == OP_CALL) ? func_get((const char*)pc->ptr) : NULL;
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2);
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8);
                jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd7);
                emit_mov_rsi_imm64(&buf, (uint64_t)pc->arg);
                emit_mov_rax_imm64(&buf, (uintptr_t)compute_signature);
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
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
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
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
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
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
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0); // call rax
                
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
                    jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); 
                    jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); 
                    jit_emit_byte(&buf, 0x5d);
                    
                    // 4. Jump (true tail call since we popped our frame)
                    jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xe0); // jmp rax
                }
                break;
            }
            case OP_INDEX: {
                emit_mov_rdi_reg(&buf, pc->r1); emit_mov_rsi_reg(&buf, pc->r2);
                emit_mov_rax_imm64(&buf, (uintptr_t)glow_index_jit);
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_STORE_INDEX: {
                emit_mov_rdi_reg(&buf, pc->r0); emit_mov_rsi_reg(&buf, pc->r1); emit_mov_rdx_reg(&buf, pc->r2);
                emit_mov_rax_imm64(&buf, (uintptr_t)glow_store_index_jit);
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
                break;
            }
            case OP_SIMD_ADD: {
                emit_mov_rdi_reg(&buf, pc->r1); emit_mov_rsi_reg(&buf, pc->r2); emit_mov_rdx_reg(&buf, pc->r0);
                emit_mov_rax_imm64(&buf, (uintptr_t)simd_add_helper);
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
                jit_emit_byte(&buf, 0xe9); jumps[jump_count].instr_idx = pc->arg; jumps[jump_count].patch_pos = buf.size; jit_emit_u32(&buf, 0); jump_count++;
                break;
            }
            case OP_RETURN: { 
                emit_mov_rax_reg(&buf, pc->r0); 
                jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3); 
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
    if (fe->jit_tier == JIT_TIER_INTERP && !fe->body->baseline_jit_code && !fe->body->jit_failed) {
        if (fe->exec_count > 10) {
            jit_compile_baseline(fe->body);
        }
    }
    
    // Tier 2: Optimizing JIT after 100 executions
    if (fe->jit_tier == JIT_TIER_BASELINE && !fe->body->jit_code && !fe->body->jit_failed) {
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
    jit_emit_byte(buf, 0x55);
    jit_emit_byte(buf, 0x48); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xe5);
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x54);
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x55);
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x56);
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xfc);
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0xf5);
}

static void jit_baseline_emit_epilogue(JITBuffer *buf) {
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x5e);
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x5d);
    jit_emit_byte(buf, 0x41); jit_emit_byte(buf, 0x5c);
    jit_emit_byte(buf, 0x5d);
    jit_emit_byte(buf, 0xc3);
}

void jit_compile_baseline_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) {
    (void)current_sig;
    if (bc->baseline_jit_code || bc->jit_failed) return;

    for (int i = 0; i < bc->count; i++) {
        Instruction *pc = &bc->code[i];
        switch (pc->op) {
            case OP_CONST_INT: case OP_CONST_FLOAT: case OP_CONST_BOOL:
            case OP_LOAD_VAR: case OP_STORE_VAR: case OP_STORE_LET:
            case OP_MOVE: case OP_NOP:
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
            case OP_GET_PROPERTY: case OP_LT: case OP_GT: case OP_LTE:
            case OP_GTE: case OP_EQ: case OP_NEQ: case OP_JUMP:
            case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_RETURN:
            case OP_CALL: case OP_CALL_VALUE: case OP_CALL_METHOD:
            case OP_INDEX: case OP_STORE_INDEX:
            case OP_SIMD_ADD: case OP_TAIL_CALL:
            case OP_ADD_INT: case OP_SUB_INT: case OP_MUL_INT:
            case OP_LT_INT: case OP_GT_INT: case OP_LTE_INT: case OP_GTE_INT: case OP_EQ_INT:
            case OP_LOAD_LOCAL: case OP_STORE_LOCAL:
                break;
            default: bc->jit_failed = 1; return;
        }
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
                    emit_mov_rax_imm64(&buf, MASK_NAN | (15ULL << 48)); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3);
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
                if (pc->cache[0].key) {
                    emit_mov_rax_reg(&buf, pc->r1);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0xd0); emit_mov_rsi_imm64(&buf, MASK_NAN | (6ULL << 48)); emit_mov_rdi_imm64(&buf, MASK_NAN | (7ULL << 48)); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xfa); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xf2);
                    uint32_t p1 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
                    emit_mov_rsi_imm64(&buf, 0x0000FFFFFFFFFFFFULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xf0); emit_mov_rsi_imm64(&buf, (uintptr_t)pc->cache[0].key); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x3b); jit_emit_byte(&buf, 0x70); jit_emit_byte(&buf, 40);
                    uint32_t p2 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x40); jit_emit_byte(&buf, 48); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x40); jit_emit_byte(&buf, pc->cache[0].val * 8);
                    uint32_t pd = buf.size; jit_emit_byte(&buf, 0xe9); jit_emit_u32(&buf, 0);
                    uint32_t bailout = buf.size; *(uint32_t*)(buf.code + p1 + 2) = (uint32_t)(bailout - (p1 + 6)); *(uint32_t*)(buf.code + p2 + 2) = (uint32_t)(bailout - (p2 + 6));
                    emit_mov_rax_imm64(&buf, (uint64_t)i); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x45); jit_emit_byte(&buf, 0x00); emit_mov_rax_imm64(&buf, MASK_NAN | (15ULL << 48)); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3);
                    uint32_t done = buf.size; *(uint32_t*)(buf.code + pd + 1) = (uint32_t)(done - (pd + 5));
                    emit_mov_reg_rax(&buf, pc->r0);
                } else { bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return; }
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
            case OP_CALL:
            case OP_CALL_VALUE: {
                FuncEntry *target_fe = (pc->op == OP_CALL) ? func_get((const char*)pc->ptr) : NULL;
                jit_emit_byte(&buf, 0x4d); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2);
                jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8);
                jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd7);
                emit_mov_rsi_imm64(&buf, (uint64_t)pc->arg);
                emit_mov_rax_imm64(&buf, (uintptr_t)compute_signature);
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
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
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
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
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
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
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
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
                    jit_baseline_emit_epilogue(&buf);
                    jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xe0);
                }
                break;
            }
            case OP_INDEX: {
                emit_mov_rdi_reg(&buf, pc->r1); emit_mov_rsi_reg(&buf, pc->r2);
                emit_mov_rax_imm64(&buf, (uintptr_t)glow_index_jit);
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_STORE_INDEX: {
                emit_mov_rdi_reg(&buf, pc->r0); emit_mov_rsi_reg(&buf, pc->r1); emit_mov_rdx_reg(&buf, pc->r2);
                emit_mov_rax_imm64(&buf, (uintptr_t)glow_store_index_jit);
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
                break;
            }
            case OP_SIMD_ADD: {
                emit_mov_rdi_reg(&buf, pc->r1); emit_mov_rsi_reg(&buf, pc->r2); emit_mov_rdx_reg(&buf, pc->r0);
                emit_mov_rax_imm64(&buf, (uintptr_t)simd_add_helper);
                jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
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
                emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0xc8);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_SUB_INT: {
                emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_MUL_INT: {
                emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0xaf); jit_emit_byte(&buf, 0xc1);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_LT_INT: case OP_GT_INT: case OP_LTE_INT: case OP_GTE_INT: case OP_EQ_INT: {
                emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xc8);
                uint8_t set_op;
                switch(pc->op) { case OP_LT_INT: set_op = 0x9c; break; case OP_GT_INT: set_op = 0x9f; break; case OP_LTE_INT: set_op = 0x9e; break; case OP_GTE_INT: set_op = 0x9d; break; case OP_EQ_INT: set_op = 0x94; break; default: set_op = 0x94; break; }
                jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, set_op); jit_emit_byte(&buf, 0xc1); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0xb6); jit_emit_byte(&buf, 0xc1); emit_mov_rax_imm64(&buf, value_bool(0)); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xc8); emit_mov_reg_rax(&buf, pc->r0);
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

void jit_compile_trace(Trace *t) {
    if (!t || t->jit_code) return;
    size_t size = 16384;
    uint8_t *code = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) return;
    JITBuffer buf = {code, 0, size};
    jit_emit_byte(&buf, 0x55); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe5);
    jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x54); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x55);
    jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x56); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x57);
    jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xfc); jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xf5);
    for (int i = 0; i < t->count; i++) {
        Instruction *pc = &t->instructions[i];
        switch (pc->op) {
            case OP_CONST_INT: emit_mov_rax_imm64(&buf, value_int(pc->arg)); emit_mov_reg_rax(&buf, pc->r0); break;
            case OP_ADD:
            case OP_SUB: {
                emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                if (pc->op == OP_ADD) { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0xc8); emit_mov_rcx_imm64(&buf, 0x7FF8000000000000ULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8); }
                else { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8); emit_mov_rcx_imm64(&buf, 0x7FF8000000000000ULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xc8); }
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_JUMP_IF_FALSE: {
                emit_mov_rax_reg(&buf, pc->r0);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x83); jit_emit_byte(&buf, 0xe0); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x85); jit_emit_byte(&buf, 0xc0);
                uint32_t patch = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x84); jit_emit_u32(&buf, 0);
                uint32_t exit_pos = buf.size; *(uint32_t*)(buf.code + patch + 2) = (uint32_t)(exit_pos - (patch + 6));
                emit_mov_rax_imm64(&buf, (uint64_t)t->original_offsets[i]); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x45); jit_emit_byte(&buf, 0x00); emit_mov_rax_imm64(&buf, MASK_NAN | (16ULL << 48)); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3);
                break;
            }
            case OP_JUMP: { if (pc->arg == (int)(t->start_pc - t->instructions)) { emit_mov_rax_imm64(&buf, 0); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3); } break; }
            default: break;
        }
    }
    jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3);
    mprotect(code, size, PROT_READ | PROT_EXEC);
    t->jit_code = code; t->jit_size = size;
}

#elif defined(__aarch64__) || defined(_M_ARM64)
void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { if (bc) bc->jit_failed = 1; }
void jit_compile(Bytecode *bc) { if (bc) bc->jit_failed = 1; }
void jit_compile_specialized(FuncEntry *fe, TypeSignature sig) { if (fe) func_add_specialization(fe, sig, (void*)1, 0); }
void jit_compile_trace(Trace *t) { if (t) t->jit_code = NULL; }
void jit_compile_baseline_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { if (bc) bc->jit_failed = 1; }
void jit_compile_baseline(Bytecode *bc) { if (bc) bc->jit_failed = 1; }
void jit_maybe_tier_up(Bytecode *bc) { (void)bc; }
void func_maybe_tier_up(FuncEntry *fe) { (void)fe; }
#else
void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { if (bc) bc->jit_failed = 1; }
void jit_compile(Bytecode *bc) { if (bc) bc->jit_failed = 1; }
void jit_compile_specialized(FuncEntry *fe, TypeSignature sig) { if (fe) func_add_specialization(fe, sig, (void*)1, 0); }
void jit_compile_trace(Trace *t) { if (t) t->jit_code = NULL; }
void jit_compile_baseline_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) { if (bc) bc->jit_failed = 1; }
void jit_compile_baseline(Bytecode *bc) { if (bc) bc->jit_failed = 1; }
void jit_maybe_tier_up(Bytecode *bc) { (void)bc; }
void func_maybe_tier_up(FuncEntry *fe) { (void)fe; }
#endif
