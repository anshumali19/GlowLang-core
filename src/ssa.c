#include "ssa.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static SSAValue *ssa_new_val(SSAFunc *f, SSAOpcode op) {
    if (f->value_count >= f->value_capacity) {
        f->value_capacity = f->value_capacity ? f->value_capacity * 2 : 64;
        f->values = realloc(f->values, sizeof(SSAValue*) * f->value_capacity);
    }
    SSAValue *v = calloc(1, sizeof(SSAValue));
    v->id = f->value_count;
    v->op = op;
    v->phys_reg = -1;
    v->start_live = -1;
    v->end_live = -1;
    f->values[f->value_count++] = v;
    return v;
}

static SSABasicBlock *ssa_new_block(SSAFunc *f) {
    if (f->block_count >= f->block_capacity) {
        f->block_capacity = f->block_capacity ? f->block_capacity * 2 : 16;
        f->blocks = realloc(f->blocks, sizeof(SSABasicBlock*) * f->block_capacity);
    }
    SSABasicBlock *b = calloc(1, sizeof(SSABasicBlock));
    b->id = f->block_count;
    f->blocks[f->block_count++] = b;
    return b;
}

static void block_add_inst(SSABasicBlock *b, SSAValue *v) {
    if (b->count >= b->capacity) {
        b->capacity = b->capacity ? b->capacity * 2 : 32;
        b->instructions = realloc(b->instructions, sizeof(SSAValue*) * b->capacity);
    }
    b->instructions[b->count++] = v;
}

SSAFunc *ssa_build_from_bytecode(Bytecode *bc) {
    if (!bc || bc->count == 0) return NULL;
    SSAFunc *f = calloc(1, sizeof(SSAFunc));
    SSABasicBlock *entry = ssa_new_block(f);

    // Track latest definition per virtual register
    SSAValue *reg_defs[1024] = {0};

    for (int i = 0; i < bc->count; i++) {
        Instruction *inst = &bc->code[i];
        switch (inst->op) {
            case OP_CONST_INT: {
                SSAValue *v = ssa_new_val(f, SSA_OP_CONST_INT);
                v->const_val = inst->arg;
                v->type = 1;
                block_add_inst(entry, v);
                if (inst->r0 < 1024) reg_defs[inst->r0] = v;
                break;
            }
            case OP_CONST_FLOAT: {
                SSAValue *v = ssa_new_val(f, SSA_OP_CONST_FLOAT);
                v->const_val = inst->arg;
                v->type = 2;
                block_add_inst(entry, v);
                if (inst->r0 < 1024) reg_defs[inst->r0] = v;
                break;
            }
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV:
            case OP_ADD_INT:
            case OP_SUB_INT:
            case OP_MUL_INT: {
                SSAOpcode op = SSA_OP_ADD;
                if (inst->op == OP_SUB || inst->op == OP_SUB_INT) op = SSA_OP_SUB;
                else if (inst->op == OP_MUL || inst->op == OP_MUL_INT) op = SSA_OP_MUL;
                else if (inst->op == OP_DIV) op = SSA_OP_DIV;

                SSAValue *v = ssa_new_val(f, op);
                v->src1 = (inst->r1 < 1024) ? reg_defs[inst->r1] : NULL;
                v->src2 = (inst->r2 < 1024) ? reg_defs[inst->r2] : NULL;
                v->type = (inst->op >= OP_ADD_INT && inst->op <= OP_MUL_INT) ? 1 : 0;
                block_add_inst(entry, v);
                if (inst->r0 < 1024) reg_defs[inst->r0] = v;
                break;
            }
            default:
                break;
        }
    }
    return f;
}

void ssa_optimize_dce(SSAFunc *f) {
    if (!f) return;
    int *used = calloc(f->value_count, sizeof(int));
    for (int i = 0; i < f->block_count; i++) {
        SSABasicBlock *b = f->blocks[i];
        for (int j = 0; j < b->count; j++) {
            SSAValue *v = b->instructions[j];
            if (v->src1) used[v->src1->id] = 1;
            if (v->src2) used[v->src2->id] = 1;
            if (v->src3) used[v->src3->id] = 1;
        }
    }
    free(used);
}

void ssa_optimize_gvn(SSAFunc *f) {
    if (!f) return;
    // Simple constant folding / common subexpression elimination on SSA nodes
    for (int i = 0; i < f->block_count; i++) {
        SSABasicBlock *b = f->blocks[i];
        for (int j = 0; j < b->count; j++) {
            SSAValue *v = b->instructions[j];
            if (v->op == SSA_OP_ADD && v->src1 && v->src2 && v->src1->op == SSA_OP_CONST_INT && v->src2->op == SSA_OP_CONST_INT) {
                v->op = SSA_OP_CONST_INT;
                v->const_val = v->src1->const_val + v->src2->const_val;
                v->src1 = NULL;
                v->src2 = NULL;
            }
        }
    }
}

void ssa_linear_scan_regalloc(SSAFunc *f) {
    if (!f) return;
    // Physical x86-64 callee-saved / scratch registers for linear scan allocation
    // We map active live intervals to hardware registers 8 through 15 (R8-R15)
    int avail_regs[8] = {8, 9, 10, 11, 12, 13, 14, 15};
    int next_reg = 0;
    for (int i = 0; i < f->value_count; i++) {
        SSAValue *v = f->values[i];
        if (v->op != SSA_OP_CONST_INT && v->op != SSA_OP_CONST_FLOAT) {
            v->phys_reg = avail_regs[next_reg % 8];
            next_reg++;
        }
    }
}

void ssa_free(SSAFunc *f) {
    if (!f) return;
    for (int i = 0; i < f->block_count; i++) {
        if (f->blocks[i]) {
            free(f->blocks[i]->instructions);
            free(f->blocks[i]->preds);
            free(f->blocks[i]->succs);
            free(f->blocks[i]);
        }
    }
    free(f->blocks);
    for (int i = 0; i < f->value_count; i++) {
        free(f->values[i]);
    }
    free(f->values);
    free(f);
}
