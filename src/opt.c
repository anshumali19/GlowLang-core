#include "opt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void optimize_bytecode(Bytecode *bc) {
    if (!bc || bc->count == 0) return;

    // Pass 1: Identify all jump targets to define basic block boundaries
    signed char *is_jump_target = calloc(bc->count, sizeof(signed char));
    for (int i = 0; i < bc->count; i++) {
        Instruction *inst = &bc->code[i];
        if (inst->op == OP_JUMP || inst->op == OP_JUMP_IF_FALSE || inst->op == OP_JUMP_IF_TRUE) {
            if (inst->arg >= 0 && inst->arg < bc->count) {
                is_jump_target[inst->arg] = 1;
            }
        }
    }

    Value const_vals[1024] = {0};
    signed char is_const[1024] = {0};
    unsigned char reg_types[1024] = {0}; // 0=Unknown, 1=Int, 2=Float, 3=String, 4=Bool

    // Pass 2: Type Inference and Constant Folding (Basic Block Local)
    for (int i=0; i < bc->count; i++) {
        Instruction *inst = &bc->code[i];

        // If this instruction is a jump target, it starts a new basic block.
        // We must invalidate all constant tracking because we don't know the state
        // coming from the jump source.
        if (is_jump_target[i]) {
            memset(is_const, 0, sizeof(is_const));
            memset(reg_types, 0, sizeof(reg_types));
        }

        if (inst->op == OP_CONST_INT) {
            if (inst->r0 < 1024) {
                is_const[inst->r0] = 1;
                const_vals[inst->r0] = value_int(inst->arg);
                reg_types[inst->r0] = 1;
            }
            inst->type = 1;
        }
        else if (inst->op == OP_CONST_FLOAT) {
            if (inst->r0 < 1024) {
                reg_types[inst->r0] = 2;
            }
            inst->type = 2;
        }
        else if (inst->op == OP_CONST_STRING) {
            if (inst->r0 < 1024) {
                reg_types[inst->r0] = 3;
            }
            inst->type = 3;
        }
        else if (inst->op == OP_CONST_BOOL) {
            if (inst->r0 < 1024) {
                is_const[inst->r0] = 1;
                const_vals[inst->r0] = value_bool(inst->arg);
                reg_types[inst->r0] = 4;
            }
            inst->type = 4;
        }
        else if (inst->op == OP_ADD || inst->op == OP_SUB || inst->op == OP_MUL || inst->op == OP_DIV) {
            int r0 = inst->r0;
            int r1 = inst->r1;
            int r2 = inst->r2;
            
            if (r1 < 1024 && r2 < 1024) {
                // Type Inference
                if (reg_types[r1] == 1 && reg_types[r2] == 1) {
                    if (r0 < 1024) reg_types[r0] = 1;
                    inst->type = 1;
                    if (inst->op == OP_ADD) inst->op = OP_ADD_INT;
                    else if (inst->op == OP_SUB) inst->op = OP_SUB_INT;
                    else if (inst->op == OP_MUL) inst->op = OP_MUL_INT;
                } else if (reg_types[r1] == 2 || reg_types[r2] == 2) {
                    if (r0 < 1024) reg_types[r0] = 2;
                    inst->type = 2;
                }
                
                // Constant folding
                if (is_const[r1] && is_const[r2]) {
                    Value v1 = const_vals[r1];
                    Value v2 = const_vals[r2];
                    if (GET_TYPE(v1) == VAL_INT && GET_TYPE(v2) == VAL_INT) {
                        int64_t i1 = AS_INT(v1);
                        int64_t i2 = AS_INT(v2);
                        int64_t res = 0;
                        int ok = 1;
                        if (inst->op == OP_ADD || inst->op == OP_ADD_INT) res = i1 + i2;
                        else if (inst->op == OP_SUB || inst->op == OP_SUB_INT) res = i1 - i2;
                        else if (inst->op == OP_MUL || inst->op == OP_MUL_INT) res = i1 * i2;
                        else if (inst->op == OP_DIV) { if (i2 != 0) res = i1 / i2; else ok = 0; }
                        
                        if (ok) {
                            inst->op = OP_CONST_INT;
                            inst->arg = (int)res;
                            if (r0 < 1024) {
                                is_const[r0] = 1;
                                const_vals[r0] = value_int(res);
                                reg_types[r0] = 1;
                            }
                            inst->type = 1;
                        }
                    }
                }
            }
        }
        else if (inst->op >= OP_LT && inst->op <= OP_EQ) {
            int r1 = inst->r1;
            int r2 = inst->r2;
            if (r1 < 1024 && r2 < 1024) {
                if (reg_types[r1] == 1 && reg_types[r2] == 1) {
                    if (inst->op == OP_LT) inst->op = OP_LT_INT;
                    else if (inst->op == OP_GT) inst->op = OP_GT_INT;
                    else if (inst->op == OP_LTE) inst->op = OP_LTE_INT;
                    else if (inst->op == OP_GTE) inst->op = OP_GTE_INT;
                    else if (inst->op == OP_EQ) inst->op = OP_EQ_INT;
                }
            }
        }
        else if (inst->op == OP_MOVE) {
            if (inst->r1 < 1024 && inst->r0 < 1024) {
                is_const[inst->r0] = is_const[inst->r1];
                const_vals[inst->r0] = const_vals[inst->r1];
                reg_types[inst->r0] = reg_types[inst->r1];
                inst->type = reg_types[inst->r1];
            }
        } else {
            if (inst->r0 < 1024) {
                is_const[inst->r0] = 0;
            }
        }
    }

    // Pass 3: Scalar Replacement
    int virtual_bases[1024];
    int virtual_sizes[1024];
    for (int j=0; j<1024; j++) { virtual_bases[j] = -1; virtual_sizes[j] = 0; }

    for (int i=0; i < bc->count; i++) {
        Instruction *inst = &bc->code[i];

        if (is_jump_target[i]) {
            for (int j=0; j<1024; j++) { virtual_bases[j] = -1; virtual_sizes[j] = 0; }
        }

        if (inst->op == OP_LIST_LITERAL && inst->is_stack_alloc && inst->arg <= 4) {
            if (inst->r0 < 1024) {
                virtual_bases[inst->r0] = inst->r1;
                virtual_sizes[inst->r0] = inst->arg;
                inst->op = OP_NOP;
            }
        }
        else if (inst->op == OP_INDEX) {
            int coll_reg = inst->r1;
            int idx_reg = inst->r2;
            if (coll_reg < 1024 && virtual_bases[coll_reg] != -1) {
                if (idx_reg < 1024 && is_const[idx_reg]) {
                    int k = (int)AS_INT(const_vals[idx_reg]);
                    if (k >= 0 && k < virtual_sizes[coll_reg]) {
                        inst->op = OP_MOVE;
                        inst->r1 = virtual_bases[coll_reg] + k;
                    }
                }
            }
        }
        else if (inst->op == OP_LEN) {
             int coll_reg = inst->r1;
             if (coll_reg < 1024 && virtual_bases[coll_reg] != -1) {
                 inst->op = OP_CONST_INT;
                 inst->arg = virtual_sizes[coll_reg];
             }
        }
        else if (inst->op == OP_STORE_INDEX) {
            int coll_reg = inst->r0;
            int idx_reg = inst->r1;
            int src_reg = inst->r2;
            if (coll_reg < 1024 && virtual_bases[coll_reg] != -1) {
                if (idx_reg < 1024 && is_const[idx_reg]) {
                    int k = (int)AS_INT(const_vals[idx_reg]);
                    if (k >= 0 && k < virtual_sizes[coll_reg]) {
                        inst->op = OP_MOVE;
                        inst->r0 = virtual_bases[coll_reg] + k;
                        inst->r1 = src_reg;
                    }
                }
            }
        }
    }
    
    // Pass 4: Loop Auto-Vectorization
    for (int i = 0; i < bc->count - 12; i++) {
        Instruction *lt = &bc->code[i];
        if (lt->op == OP_LTE || lt->op == OP_LT) {
            Instruction *jf = &bc->code[i+1];
            if (jf->op == OP_JUMP_IF_FALSE) {
                int exit_pc = jf->arg;
                // Pattern for res[i] = a[i] + b[i]
                // 4: LOAD_VAR (i) -> r_idx
                // 34: INDEX (a, r_idx) -> r_a
                // 4: LOAD_VAR (i) -> r_idx2
                // 34: INDEX (b, r_idx2) -> r_b
                // 6: ADD (r_a, r_b) -> r_res
                // 36: STORE_INDEX (res, r_idx, r_res)
                
                if (bc->code[i+2].op == OP_LOAD_VAR && 
                    bc->code[i+3].op == OP_INDEX &&
                    bc->code[i+4].op == OP_LOAD_VAR &&
                    bc->code[i+5].op == OP_INDEX &&
                    bc->code[i+6].op == OP_ADD &&
                    bc->code[i+7].op == OP_STORE_INDEX) {
                    
                    // Match!
                    jf->op = OP_SIMD_ADD;
                    jf->r0 = bc->code[i+7].r0; // target collection
                    jf->r1 = bc->code[i+3].r1; // a collection
                    jf->r2 = bc->code[i+5].r1; // b collection
                    
                    // Cleanup loop body
                    for (int k = i+2; k < exit_pc && k < bc->count - 1; k++) {
                        if (bc->code[k].op != OP_JUMP) bc->code[k].op = OP_NOP;
                    }
                }
            }
        }
    }
    // Pass 4.5: Devirtualize CALL_VALUE
    for (int i = 0; i < bc->count - 1; i++) {
        Instruction *load = &bc->code[i];
        Instruction *call = &bc->code[i+1];
        if (load->op == OP_LOAD_VAR && call->op == OP_CALL_VALUE && load->r0 == call->r1) {
            const char *name = global_table.names[load->arg];
            FuncEntry *fe = func_get(name);
            if (fe && fe->body) {
                call->op = OP_CALL;
                call->ptr = (intptr_t)name;
                load->op = OP_NOP;
            }
        }
    }
    
    // Pass 5: Tail Call & Self-Call Fast Optimization (Early Decision)
    for (int i = 0; i < bc->count; i++) {
        Instruction *pc = &bc->code[i];

        if (pc->op == OP_CALL || pc->op == OP_CALL_VALUE || pc->op == OP_CALL_SELF_FAST) {
            // Look ahead for RETURN, skipping NOPs
            int next_idx = i + 1;
            while (next_idx < bc->count && bc->code[next_idx].op == OP_NOP) next_idx++;
            
            if (next_idx < bc->count && bc->code[next_idx].op == OP_RETURN && pc->r0 == bc->code[next_idx].r0) {
                pc->op = OP_TAIL_CALL;
                bc->code[next_idx].op = OP_NOP;
                continue;
            }
        }

        // 2. Self-Call Fast Path (if not already TCO'd)
        if (pc->op == OP_CALL) {
             const char *name = (const char*)pc->ptr;
             if (name && bc->owner && bc->owner->name && strcmp(name, bc->owner->name) == 0) {
                 pc->op = OP_CALL_SELF_FAST;
                 
                 // Re-check for TCO after patching to SELF_FAST
                 int next_idx = i + 1;
                 while (next_idx < bc->count && bc->code[next_idx].op == OP_NOP) next_idx++;
                 if (next_idx < bc->count && bc->code[next_idx].op == OP_RETURN && pc->r0 == bc->code[next_idx].r0) {
                     pc->op = OP_TAIL_CALL;
                     bc->code[next_idx].op = OP_NOP;
                 }
             }
        }
        }
    // Pass 6: Method Devirtualization
    for (int i = 0; i < bc->count - 1; i++) {
        Instruction *pc = &bc->code[i];
        if (pc->op == OP_GET_PROPERTY) {
            int next_idx = i + 1;
            while (next_idx < bc->count && bc->code[next_idx].op == OP_NOP) next_idx++;
            if (next_idx < bc->count && bc->code[next_idx].op == OP_CALL_VALUE) {
                Instruction *call_pc = &bc->code[next_idx];
                if (pc->r0 == call_pc->r1) {
                    call_pc->op = OP_CALL_METHOD;
                    call_pc->ptr = pc->ptr; // name of method
                    // r1 of OP_CALL_METHOD needs the object
                    call_pc->r1 = pc->r1;
                    
                    // We can eliminate OP_GET_PROPERTY
                    pc->op = OP_NOP;
                }
            }
        }
    }

    free(is_jump_target);
}
