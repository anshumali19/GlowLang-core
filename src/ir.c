#include "ir.h"
#include "arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

IRFunction* ir_func_new(const char *name) {
    IRFunction *f = arena_alloc(sizeof(IRFunction));
    f->name = name;
    f->block_capacity = 8;
    f->blocks = arena_alloc(sizeof(IRBlock*) * f->block_capacity);
    f->block_count = 0;
    f->next_ssa_var = 1;
    return f;
}

IRBlock* ir_block_new(IRFunction *f) {
    IRBlock *b = arena_alloc(sizeof(IRBlock));
    b->id = f->block_count;
    b->capacity = 16;
    b->insts = arena_alloc(sizeof(IRInst) * b->capacity);
    b->count = 0;
    b->true_branch = NULL;
    b->false_branch = NULL;
    
    if (f->block_count >= f->block_capacity) {
        f->block_capacity *= 2;
        f->blocks = arena_realloc(f->blocks, sizeof(IRBlock*) * (f->block_capacity / 2), sizeof(IRBlock*) * f->block_capacity);
    }
    f->blocks[f->block_count++] = b;
    return b;
}

int ir_emit(IRBlock *b, IROp op, int src1, int src2) {
    if (b->count >= b->capacity) {
        b->capacity *= 2;
        b->insts = arena_realloc(b->insts, sizeof(IRInst) * (b->capacity / 2), sizeof(IRInst) * b->capacity);
    }
    int idx = b->count++;
    b->insts[idx].op = op;
    b->insts[idx].src1 = src1;
    b->insts[idx].src2 = src2;
    b->insts[idx].type = TYPE_UNKNOWN;
    return idx;
}

void ir_optimize(IRFunction *f) {
    // Pass 1: Constant Folding & Type Inference
    for (int i=0; i<f->block_count; i++) {
        IRBlock *b = f->blocks[i];
        for (int j=0; j<b->count; j++) {
            IRInst *inst = &b->insts[j];
            if (inst->op == IR_CONST_INT) {
                inst->type = TYPE_INT;
            } else if (inst->op == IR_ADD) {
                // If we know types of src1 and src2, we can infer type of ADD
                // Find definitions
                IRInst *def1 = NULL, *def2 = NULL;
                for (int k=0; k<j; k++) {
                    if (b->insts[k].dst == inst->src1) def1 = &b->insts[k];
                    if (b->insts[k].dst == inst->src2) def2 = &b->insts[k];
                }
                if (def1 && def2 && def1->type == TYPE_INT && def2->type == TYPE_INT) {
                    inst->type = TYPE_INT;
                    if (def1->op == IR_CONST_INT && def2->op == IR_CONST_INT) {
                        // Fold!
                        inst->op = IR_CONST_INT;
                        inst->const_val = value_int(AS_INT(def1->const_val) + AS_INT(def2->const_val));
                    }
                }
            }
        }
    }
}

// Convert SSA variable IDs to Register IDs
void ir_lower_to_bytecode(IRFunction *f, Bytecode *bc) {
    // For now, map each SSA variable exactly to a register to simulate basic SSA.
    // In a real backend, we'd do Graph Coloring Register Allocation.
    for (int i=0; i<f->block_count; i++) {
        IRBlock *b = f->blocks[i];
        for (int j=0; j<b->count; j++) {
            IRInst *inst = &b->insts[j];
            switch (inst->op) {
                // Mapping IR -> Bytecode
                // ... This is just a stub for now. The real generation happens in glowlang.y.
                default: break;
            }
        }
    }
}
