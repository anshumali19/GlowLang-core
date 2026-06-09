#ifndef GLOWLANG_IR_H
#define GLOWLANG_IR_H

#include "glowlang.h"

typedef enum {
    IR_NOP,
    IR_CONST_INT,
    IR_CONST_FLOAT,
    IR_CONST_BOOL,
    IR_CONST_STRING,
    IR_LOAD_VAR,
    IR_STORE_VAR,
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_DIV,
    IR_MOD,
    IR_LT,
    IR_GT,
    IR_LTE,
    IR_GTE,
    IR_EQ,
    IR_NEQ,
    IR_JUMP,
    IR_JUMP_IF_FALSE,
    IR_CALL,
    IR_RETURN,
    IR_PHI
} IROp;

typedef enum {
    TYPE_UNKNOWN = 0,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_OBJECT
} IRType;

typedef struct IRInst {
    IROp op;
    int dst;   // SSA variable ID
    int src1;  // SSA variable ID
    int src2;  // SSA variable ID
    Value const_val;
    const char *name;
    IRType type;
} IRInst;

typedef struct IRBlock {
    int id;
    IRInst *insts;
    int count;
    int capacity;
    struct IRBlock *true_branch;
    struct IRBlock *false_branch;
} IRBlock;

typedef struct IRFunction {
    const char *name;
    IRBlock **blocks;
    int block_count;
    int block_capacity;
    int next_ssa_var;
} IRFunction;

IRFunction* ir_func_new(const char *name);
void ir_func_free(IRFunction *f);
IRBlock* ir_block_new(IRFunction *f);
int ir_emit(IRBlock *b, IROp op, int src1, int src2);
void ir_build_from_ast(IRFunction *f, struct Node *n);
void ir_optimize(IRFunction *f);
void ir_lower_to_bytecode(IRFunction *f, Bytecode *bc);

#endif
