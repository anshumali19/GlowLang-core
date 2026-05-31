#include "jit.h"
#include <sys/mman.h>
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

Value add_floats_jit(Value a, Value b) { return value_float(val_to_double(a) + val_to_double(b)); }
Value sub_floats_jit(Value a, Value b) { return value_float(val_to_double(a) - val_to_double(b)); }
Value mul_floats_jit(Value a, Value b) { return value_float(val_to_double(a) * val_to_double(b)); }
Value div_floats_jit(Value a, Value b) { return value_float(val_to_double(a) / val_to_double(b)); }

static void emit_mov_rax_reg(JITBuffer *buf, int reg) {
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x84); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
}
static void emit_mov_reg_rax(JITBuffer *buf, int reg) {
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x89); jit_emit_byte(buf, 0x84); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
}
static void emit_mov_rcx_reg(JITBuffer *buf, int reg) {
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0x8c); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
}
static void emit_mov_rdi_reg(JITBuffer *buf, int reg) {
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0xbc); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
}
static void emit_mov_rsi_reg(JITBuffer *buf, int reg) {
    jit_emit_byte(buf, 0x49); jit_emit_byte(buf, 0x8b); jit_emit_byte(buf, 0xb4); jit_emit_byte(buf, 0x24); jit_emit_u32(buf, reg * 8);
}

void jit_compile_ext(Bytecode *bc, FuncEntry *fe, TypeSignature current_sig) {
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
            case OP_CALL:
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

    uint32_t *offsets = calloc(bc->count + 1, sizeof(uint32_t));
    struct { int instr_idx; uint32_t patch_pos; } *jumps = calloc(bc->count, sizeof(*jumps));
    int jump_count = 0;

    for (int i = 0; i < bc->count; i++) {
        offsets[i] = buf.size;
        Instruction *pc = &bc->code[i];
        switch (pc->op) {
            case OP_CONST_INT: emit_mov_rax_imm64(&buf, value_int(pc->arg)); emit_mov_reg_rax(&buf, pc->r0); break;
            case OP_CONST_FLOAT: emit_mov_rax_imm64(&buf, *(uint64_t*)pc->ptr); emit_mov_reg_rax(&buf, pc->r0); break;
            case OP_CONST_BOOL: emit_mov_rax_imm64(&buf, value_bool(pc->arg)); emit_mov_reg_rax(&buf, pc->r0); break;
            case OP_LOAD_VAR: {
                // Load address of global_table.values (pointer to the array)
                emit_mov_rcx_imm64(&buf, (uintptr_t)&global_table.values);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x09); // mov rcx, [rcx] (get actual array pointer)
                // Now rcx points to the array, load the value at slot
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x81); // mov rax, [rcx + imm32]
                jit_emit_u32(&buf, pc->arg * 8);
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_STORE_VAR:
            case OP_STORE_LET: {
                emit_mov_rax_reg(&buf, pc->r0);
                // Load address of global_table.values
                emit_mov_rcx_imm64(&buf, (uintptr_t)&global_table.values);
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x8b); jit_emit_byte(&buf, 0x09); // mov rcx, [rcx]
                // Store rax into [rcx + slot * 8]
                jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x81); // mov [rcx + imm32], rax
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
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xc6); // mov rsi, rax
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd6); // and rsi, r10
                    emit_mov_r11_imm64(&buf, 0x7FF8000000000000ULL);
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xde); // cmp rsi, r11
                    uint32_t p1 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
                    
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xce); // mov rsi, rcx
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd6); // and rsi, r10
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x39); jit_emit_byte(&buf, 0xde); // cmp rsi, r11
                    uint32_t p2 = buf.size; jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x85); jit_emit_u32(&buf, 0);
                    
                    if (pc->op == OP_ADD) { 
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0xc8); // add rax, rcx
                        jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xd8); // sub rax, r11
                    }
                    else if (pc->op == OP_SUB) { 
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8); // sub rax, rcx
                        jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xd8); // or rax, r11
                    }
                    
                    uint32_t pd = buf.size; jit_emit_byte(&buf, 0xe9); jit_emit_u32(&buf, 0);
                    uint32_t bailout = buf.size; *(uint32_t*)(buf.code + p1 + 2) = (uint32_t)(bailout - (p1 + 6)); *(uint32_t*)(buf.code + p2 + 2) = (uint32_t)(bailout - (p2 + 6));
                    emit_mov_rax_imm64(&buf, (uint64_t)i); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0x45); jit_emit_byte(&buf, 0x00);
                    emit_mov_rax_imm64(&buf, MASK_NAN | (15ULL << 48)); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3);
                    uint32_t done = buf.size; *(uint32_t*)(buf.code + pd + 1) = (uint32_t)(done - (pd + 5));
                    emit_mov_reg_rax(&buf, pc->r0);
                } else if (pc->type == 2) {
                    emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                    jit_emit_byte(&buf, 0x66); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x6e); jit_emit_byte(&buf, 0xc0); // movq xmm0, rax
                    jit_emit_byte(&buf, 0x66); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x6e); jit_emit_byte(&buf, 0xc1); // movq xmm1, rcx
                    if (pc->op == OP_ADD) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x58); jit_emit_byte(&buf, 0xc1); }
                    else if (pc->op == OP_SUB) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0xc1); }
                    else if (pc->op == OP_MUL) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x59); jit_emit_byte(&buf, 0xc1); }
                    else if (pc->op == OP_DIV) { jit_emit_byte(&buf, 0xf2); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0xc1); }
                    else { bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return; }
                    jit_emit_byte(&buf, 0x66); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0x7e); jit_emit_byte(&buf, 0xc0); // movq rax, xmm0
                    emit_mov_reg_rax(&buf, pc->r0);
                } else {
                    emit_mov_rax_reg(&buf, pc->r1); emit_mov_rcx_reg(&buf, pc->r2);
                    if (pc->op == OP_ADD) { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x01); jit_emit_byte(&buf, 0xc8); emit_mov_rcx_imm64(&buf, 0x7FF8000000000000ULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8); }
                    else if (pc->op == OP_SUB) { jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x29); jit_emit_byte(&buf, 0xc8); emit_mov_rcx_imm64(&buf, 0x7FF8000000000000ULL); jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xc8); }
                    else if (pc->op == OP_MUL) {
                        emit_mov_rdx_imm64(&buf, 0x0000FFFFFFFFFFFFULL);
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd0); // and rax, rdx
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x21); jit_emit_byte(&buf, 0xd1); // and rcx, rdx
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x0f); jit_emit_byte(&buf, 0xaf); jit_emit_byte(&buf, 0xc1); // imul rax, rcx
                        emit_mov_rdx_imm64(&buf, 0x7FF8000000000000ULL);
                        jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x09); jit_emit_byte(&buf, 0xd0); // or rax, rdx
                    }
                    else { bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return; }
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
            case OP_LT:
            case OP_GT:
            case OP_LTE:
            case OP_GTE:
            case OP_EQ:
            case OP_NEQ: {
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
            case OP_CALL: {
                FuncEntry *target_fe = func_get((const char*)pc->ptr);
                if (target_fe && target_fe == fe && fe->param_count == 1 && current_sig == 1) {
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe7); // mov rdi, r12
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc7); jit_emit_u32(&buf, pc->r2 * 8); // add rdi, r2*8
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xee); // mov rsi, r13
                    jit_emit_byte(&buf, 0xe8); jit_emit_u32(&buf, 0 - (buf.size + 4)); // call start (offset 0)
                } else if (target_fe) {
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2); // mov r10, r12
                    jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8); // add r10, r2*8
                    emit_mov_rdi_imm64(&buf, (uintptr_t)target_fe);
                    emit_mov_rsi_imm64(&buf, (uint64_t)pc->arg);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd2); // mov rdx, r10
                    emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper);
                    jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
                } else {
                    jit_emit_byte(&buf, 0x4c); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xe2); // mov r10, r12
                    jit_emit_byte(&buf, 0x49); jit_emit_byte(&buf, 0x81); jit_emit_byte(&buf, 0xc2); jit_emit_u32(&buf, pc->r2 * 8); // add r10, r2*8
                    emit_mov_rdi_imm64(&buf, (uintptr_t)pc->ptr);
                    emit_mov_rsi_imm64(&buf, (uint64_t)pc->arg);
                    jit_emit_byte(&buf, 0x48); jit_emit_byte(&buf, 0x89); jit_emit_byte(&buf, 0xd2); // mov rdx, r10
                    emit_mov_rax_imm64(&buf, (uintptr_t)jit_call_helper_name);
                    jit_emit_byte(&buf, 0xff); jit_emit_byte(&buf, 0xd0);
                }
                emit_mov_reg_rax(&buf, pc->r0);
                break;
            }
            case OP_RETURN: { emit_mov_rax_reg(&buf, pc->r0); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5f); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5e); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0x41); jit_emit_byte(&buf, 0x5c); jit_emit_byte(&buf, 0x5d); jit_emit_byte(&buf, 0xc3); break; }
            default: bc->jit_failed = 1; munmap(code, size); free(offsets); free(jumps); return;
        }
    }
    offsets[bc->count] = buf.size;
    for (int i = 0; i < jump_count; i++) { uint32_t target_offset = offsets[jumps[i].instr_idx]; uint32_t patch_pos = jumps[i].patch_pos; int32_t rel = (int32_t)target_offset - (int32_t)(patch_pos + 4); memcpy(code + patch_pos, &rel, 4); }
    mprotect(code, size, PROT_READ | PROT_EXEC);
    bc->jit_code = code; bc->jit_size = size; free(offsets); free(jumps);
}

void jit_compile(Bytecode *bc) {
    jit_compile_ext(bc, NULL, 0);
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

static void propagate_types(Instruction *code, int count, int param_count, TypeSignature sig, FuncEntry *fe) {
    unsigned char reg_types[256]; memset(reg_types, 0, 256);
    for (int i = 0; i < param_count && i < 16; i++) {
        reg_types[i] = (sig >> (i * 4)) & 0xF;
    }
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
                if (target_fe && target_fe == fe && sig == 1) {
                    reg_types[pc->r0] = 1; // Assume self-recursion returns INT if input is INT
                } else {
                    reg_types[pc->r0] = 0;
                }
                break;
            }
            case OP_LOAD_VAR: case OP_GET_PROPERTY: reg_types[pc->r0] = 0; break; 
            default: break;
        }
    }
}

void jit_compile_specialized(FuncEntry *fe, TypeSignature sig) {
    if (!fe || !fe->body) return;
    Bytecode *sbc = malloc(sizeof(Bytecode));
    sbc->count = fe->body->count;
    sbc->capacity = sbc->count;
    sbc->code = malloc(sizeof(Instruction) * sbc->count);
    memcpy(sbc->code, fe->body->code, sizeof(Instruction) * sbc->count);
    for (int i=0; i<sbc->count; i++) sbc->code[i].trace = NULL;
    sbc->jit_code = NULL; sbc->jit_failed = 0; sbc->local_count = fe->body->local_count;
    sbc->exception_table = fe->body->exception_table;
    sbc->exception_count = fe->body->exception_count;

    propagate_types(sbc->code, sbc->count, fe->param_count, sig, fe);
    jit_compile_ext(sbc, fe, sig);

    if (sbc->jit_code) {
        func_add_specialization(fe, sig, sbc->jit_code, sbc->jit_size);
    } else {
        // Mark as failed so we don't keep retrying
        func_add_specialization(fe, sig, (void*)1, 0);
    }
    free(sbc->code);
    free(sbc);
}
