#ifndef GLOWLANG_SSA_H
#define GLOWLANG_SSA_H

#include "glowlang.h"

typedef enum {
    SSA_OP_CONST_INT,
    SSA_OP_CONST_FLOAT,
    SSA_OP_CONST_BOOL,
    SSA_OP_PARAM,
    SSA_OP_LOAD_LOCAL,
    SSA_OP_STORE_LOCAL,
    SSA_OP_ADD,
    SSA_OP_SUB,
    SSA_OP_MUL,
    SSA_OP_DIV,
    SSA_OP_LT,
    SSA_OP_GT,
    SSA_OP_LTE,
    SSA_OP_GTE,
    SSA_OP_EQ,
    SSA_OP_NEQ,
    SSA_OP_PHI,
    SSA_OP_CALL,
    SSA_OP_RETURN,
    SSA_OP_BRANCH,
    SSA_OP_JUMP,
    SSA_OP_INDEX,
    SSA_OP_STORE_INDEX
} SSAOpcode;

typedef struct SSAValue {
    int id;
    SSAOpcode op;
    int type; // 1=int, 2=float, 3=string, 4=bool, 0=any
    int64_t const_val;
    struct SSAValue *src1;
    struct SSAValue *src2;
    struct SSAValue *src3;
    int phys_reg; // Assigned x86-64 register (-1 if unassigned/spilled)
    int start_live;
    int end_live;
} SSAValue;

typedef struct SSABasicBlock {
    int id;
    SSAValue **instructions;
    int count;
    int capacity;
    struct SSABasicBlock **preds;
    int pred_count;
    struct SSABasicBlock **succs;
    int succ_count;
} SSABasicBlock;

typedef struct {
    SSABasicBlock **blocks;
    int block_count;
    int block_capacity;
    SSAValue **values;
    int value_count;
    int value_capacity;
} SSAFunc;

SSAFunc *ssa_build_from_bytecode(Bytecode *bc);
void ssa_optimize_dce(SSAFunc *f);
void ssa_optimize_gvn(SSAFunc *f);
void ssa_linear_scan_regalloc(SSAFunc *f);
void ssa_free(SSAFunc *f);

#endif
