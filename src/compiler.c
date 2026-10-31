#include "glowlang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#ifndef _WIN32
#include <sys/mman.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define munmap(addr, size) VirtualFree(addr, 0, MEM_RELEASE)
#endif

#define MAX_LOOP_DEPTH 64
typedef struct {
    int start_pc;
    int end_patches[256];
    int end_patch_count;
    int skip_patches[256];
    int skip_patch_count;
} LoopScope;

static THREAD_LOCAL LoopScope loop_stack[MAX_LOOP_DEPTH];
static THREAD_LOCAL int loop_stack_ptr = 0;

int global_lambda_count = 0;
static int compiling_line = 0;

Bytecode *compiling_bc = NULL;
Class *last_compiling_class = NULL;
Trait *last_compiling_trait = NULL;
static const char *compiling_class_name = NULL;
static const char *compiling_trait_name = NULL;

Node* node_new(NodeType t) { Node *n = arena_alloc(sizeof(Node)); memset(n, 0, sizeof(Node)); n->type = t; n->line = line_num; return n; }
/* Build an integer literal Value via the numeric tower: inline 48-bit int ->
   double (exact to 2^53) -> heap big-int (exact beyond 2^53). */
static inline Value int_literal_value(int64_t v) {
    if (fits_int48(v)) return value_int(v);
    if (v >= -INT53_MAX && v <= INT53_MAX) return value_float((double)v);
    return value_bigint(bigint_from_int64(v));
}
Node* newIntNode(int64_t v) { Node *n = node_new(NODE_INT_LIT); n->value = int_literal_value(v); return n; }
Node* newIntNodeStr(const char *s) {
    errno = 0;
    char *end;
    long long v = strtoll(s, &end, 10);
    /* Exact when the literal fits an int64 and is parsed without overflow. */
    if (errno == 0 && *end == '\0') return newIntNode((int64_t)v);
    /* Otherwise (literal beyond 2^63) keep it exact as a heap big-int. */
    BigInt *b = bigint_from_string(s);
    Node *n = node_new(NODE_INT_LIT);
    n->value = value_bigint(b);
    return n;
}
Node* newFloatNode(double v) { Node *n = node_new(NODE_FLOAT_LIT); n->value = value_float(v); return n; }
Node* newBoolNode(int v) { Node *n = node_new(NODE_BOOL_LIT); n->value = value_bool(v); return n; }
Node* newStringNode(const char *s) { Node *n = node_new(NODE_STRING_LIT); n->name = s; return n; }
Node* newVarNode(const char *s) { Node *n = node_new(NODE_VAR); n->name = s; return n; }
Node* newAssignNode(const char *s, Node *e) { Node *n = node_new(NODE_ASSIGN); n->name = s; n->left = e; return n; }
Node* newBinOp(NodeType t, Node *l, Node *r) { Node *n = node_new(t); n->left = l; n->right = r; return n; }
/* `for each a, b in coll:` iterates pairs(coll) — [index, item] for lists
   and strings, [key, value] for dicts — and unpacks each pair. */
Node* newPairLoopNode(const char *a, const char *b, Node *coll, Node *body) {
    static int pair_count = 0;
    char buf[48];
    snprintf(buf, sizeof(buf), "__pair_%d", pair_count++);
    const char *pv = intern_string(buf);
    Node **args = malloc(sizeof(Node*));
    args[0] = coll;
    Node *loop = node_new(NODE_FOR_EACH);
    loop->name = pv;
    loop->left = newCallExprNode(newVarNode(intern_string("pairs")), args, 1);
    Node *first = node_new(NODE_INDEX); first->left = newVarNode(pv); first->right = newIntNode(0);
    Node *second = node_new(NODE_INDEX); second->left = newVarNode(pv); second->right = newIntNode(1);
    loop->right = newSeqNode(newAssignNode(a, first), newSeqNode(newAssignNode(b, second), body));
    return loop;
}
Node* newSeqNode(Node *l, Node *r) { if(!l) return r; if(!r) return l; Node *n = node_new(NODE_SEQ); n->left = l; n->right = r; return n; }
Node* newFuncNode(const char *n, int pc, char **ps, Node *b) { Node *nd = node_new(NODE_FUNC_DEF); nd->name = n; nd->func_param_count = pc; nd->func_params = ps; nd->left = b; return nd; }
Node* newReturnNode(Node *e) { Node *n = node_new(NODE_RETURN); n->left = e; return n; }
Node* newIfNode(Node *c, Node *t, Node *e) { Node *n = node_new(NODE_IF); n->left = c; n->right = t; n->third = e; return n; }
Node* newWhileNode(Node *c, Node *b) { Node *n = node_new(NODE_WHILE); n->left = c; n->right = b; return n; }
Node* newUntilNode(Node *c, Node *b) { Node *n = node_new(NODE_UNTIL); n->left = c; n->right = b; return n; }
Node* newRepeatNode(Node *c, Node *b) { Node *n = node_new(NODE_REPEAT); n->left = c; n->right = b; return n; }
Node* newCompoundAssignNode(NodeType t, const char *s, Node *e) { Node *n = node_new(t); n->name = s; n->left = e; return n; }
Node* newShowNode(Node **is, int c) { Node *n = node_new(NODE_SHOW); n->print_items = is; n->print_count = c; return n; }
Node* newWriteNode(Node **is, int c) { Node *n = node_new(NODE_WRITE); n->print_items = is; n->print_count = c; return n; }
Node* newExprStmtNode(Node *e) { Node *n = node_new(NODE_EXPR_STMT); n->left = e; return n; }
Node* newCallExprNode(Node *e, Node **as, int c) {
    Node *n = node_new(NODE_CALL_EXPR);
    n->left = e;
    n->call_args = as;
    n->call_arg_count = c;
    return n;
}
#if 0
static Node* newIndexAssignNode(Node *c, Node *i, Node *v) { Node *n = node_new(NODE_INDEX_ASSIGN); n->left = c; n->right = i; n->third = v; return n; }
#endif



void visualize_stack(int base_reg, int local_count) {
    printf("   Stack: [");
    for (int i = 0; i < local_count; i++) {
        Value v = vm_stack[base_reg + i];
        if (i > 0) printf(", ");
        if (v == 0) printf("nil");
        else if (IS_FLOAT_BITS(v)) printf("%g", AS_FLOAT(v));
        else {
            uint64_t type = ((v) & MASK_TYPE) >> 48;
            if (type == VAL_INT) printf("%lld", (long long)AS_INT(v));
            else if (type == VAL_BOOL) printf(AS_BOOL(v) ? "true" : "false");
            else if (type == VAL_STRING) {
                const char *s = AS_STRING(v);
                if (strlen(s) > 10) printf("\"%.7s...\"", s);
                else printf("\"%s\"", s);
            } else printf("<obj>");
        }
    }
    printf("]\n");
}

/* Highest register count the function being compiled has used. Temporaries
   are released with reg_restore, so next_free_reg at the end of a body can be
   lower than what some instruction needed; the frame size must cover the peak. */
static int reg_peak = 0;
static inline void note_reg_peak(void) { if (next_free_reg > reg_peak) reg_peak = next_free_reg; }
int compiler_reg_peak(void) { note_reg_peak(); return reg_peak; }
void compiler_set_reg_peak(int peak) { reg_peak = peak; }

int bc_emit(Bytecode *bc, OpCode op, int arg, intptr_t ptr) {
    note_reg_peak();
    if (bc->count >= bc->capacity) { bc->capacity = (bc->capacity == 0) ? 1024 : bc->capacity * 2; bc->code = realloc(bc->code, sizeof(Instruction)*bc->capacity); }
    int idx = bc->count;
    bc->code[idx].op = op;
    bc->code[idx].original_op = op;
    bc->code[idx].arg = arg;
    bc->code[idx].ptr = ptr;
    for (int i=0; i<4; i++) {
        bc->code[idx].cache[i].key = (void*)-1;
        bc->code[idx].cache[i].val = 0;
    }
    bc->code[idx].cache_next = 0;
    bc->code[idx].exec_count = 0;
    bc->code[idx].r0 = 0;
    bc->code[idx].r1 = 0;
    bc->code[idx].r2 = 0;
    bc->code[idx].type = 0;
    bc->code[idx].is_stack_alloc = 0;
    bc->code[idx].is_method = 0;
    bc->code[idx].line = compiling_line;
    bc->code[idx].file = compiling_file;
    bc->code[idx].trace = NULL;
    bc->count++;
    return idx;
}

static int bc_emit_reg(Bytecode *bc, OpCode op, int r0, int r1, int r2) {
    if (r0 < 0 || r1 < 0 || r2 < 0) {
        // fprintf(stderr, "Warning: Emitting register < 0 (r0=%d, r1=%d, r2=%d) for op %d\n", r0, r1, r2, op);
    }
    int idx = bc_emit(bc, op, 0, 0);
    bc->code[idx].r0 = (unsigned short)r0;
    bc->code[idx].r1 = (unsigned short)r1;
    bc->code[idx].r2 = (unsigned short)r2;
    return idx;
}
void bc_free(Bytecode *bc) {
    if (!bc) return;
    if (bc->code) free(bc->code);
    if (bc->exception_table) free(bc->exception_table);
    if (bc->jit_code) munmap(bc->jit_code, bc->jit_size);
    if (bc->baseline_jit_code) munmap(bc->baseline_jit_code, bc->baseline_jit_size);
    if (bc->jit_offsets) free(bc->jit_offsets);
    if (bc->baseline_jit_offsets) free(bc->baseline_jit_offsets);
    if (bc->osr_entries) free(bc->osr_entries);
    if (bc->osr_code) munmap(bc->osr_code, bc->osr_code_size);
    free(bc);
}

void func_entry_cleanup(FuncEntry *fe) {
    if (!fe || !fe->used) return;
    // fe->params is arena-allocated and will be reclaimed by arena_free_all(), do not free it.
    fe->params = NULL;
    if (fe->local_names) {
        free(fe->local_names);
        fe->local_names = NULL;
    }
    if (fe->local_regs) {
        free(fe->local_regs);
        fe->local_regs = NULL;
    }
    if (fe->body) {
        bc_free(fe->body);
        fe->body = NULL;
    }
    if (fe->specs) {
        for (int i = 0; i < fe->spec_count; i++) {
            if (fe->specs[i].jit_code && fe->specs[i].jit_code != (void*)1) {
                munmap(fe->specs[i].jit_code, fe->specs[i].jit_size);
            }
        }
        free(fe->specs);
        fe->specs = NULL;
    }
    fe->used = 0;
}


Bytecode* bc_new(FuncEntry *owner) {
    Bytecode *bc = calloc(1, sizeof(Bytecode));
    bc->capacity = 1024;
    bc->code = calloc(bc->capacity, sizeof(Instruction));
    bc->owner = owner;
    bc->exception_table = NULL;
    bc->exception_count = 0;
    bc->preserved_locals = owner ? owner->param_count : 0;
    return bc;
}

static void bc_add_handler(Bytecode *bc, int start, int end, int handler, int exc_reg) {
    bc->exception_table = realloc(bc->exception_table, sizeof(ExceptionEntry) * (bc->exception_count + 1));
    bc->exception_table[bc->exception_count].start_pc = start;
    bc->exception_table[bc->exception_count].end_pc = end;
    bc->exception_table[bc->exception_count].handler_pc = handler;
    bc->exception_table[bc->exception_count].exc_reg = exc_reg;
    bc->exception_count++;
}

/* Each local gets the next free register (not its index): temporaries are
   allocated above every existing local, so a local declared mid-function
   must not land on a register an enclosing loop is still using for its
   iterator. reg_floor keeps pool restores from handing local registers back
   out as temporaries. */
static void func_add_local(FuncEntry *fe, const char *name) {
    if (!fe || !name) return;
    for (int i=0; i<fe->local_count; i++) if (strcmp(fe->local_names[i], name) == 0) return;
    if (fe->local_count >= fe->local_capacity) {
        fe->local_capacity = (fe->local_capacity == 0) ? 8 : fe->local_capacity * 2;
        fe->local_names = realloc(fe->local_names, sizeof(char*) * fe->local_capacity);
        fe->local_regs = realloc(fe->local_regs, sizeof(int) * fe->local_capacity);
    }
    if (next_free_reg < fe->reg_floor) next_free_reg = fe->reg_floor;
    int reg = next_free_reg++;
    fe->local_names[fe->local_count] = (char*)name;
    fe->local_regs[fe->local_count] = reg;
    fe->local_count++;
    if (reg + 1 > fe->reg_floor) fe->reg_floor = reg + 1;
}
FuncEntry *compiling_func = NULL;

/* Functions enclosing the one being compiled (innermost last; NULL for a
   method, which captures nothing). Assigning to one of their variables from a
   closure updates the closure's captured copy rather than making a new local,
   so `count = count + 1` in a returned lambda counts up across calls. */
static FuncEntry *outer_funcs[64];
static int outer_func_count = 0;
static void push_outer(FuncEntry *f) { if (outer_func_count < 64) outer_funcs[outer_func_count] = f; outer_func_count++; }
static void pop_outer(void) { if (outer_func_count > 0) outer_func_count--; }
static int is_outer_local(const char *name) {
    int top = outer_func_count < 64 ? outer_func_count : 64;
    for (int k = top - 1; k >= 0; k--) {
        FuncEntry *o = outer_funcs[k];
        if (!o) return 0;   /* a method boundary: nothing further out is captured */
        for (int i = 0; i < o->local_count; i++)
            if (strcmp(o->local_names[i], name) == 0) return 1;
    }
    return 0;
}

static int get_local_reg(const char *name) {
    if (!compiling_func) return -1;
    for (int i=0; i<compiling_func->local_count; i++) {
        if (strcmp(compiling_func->local_names[i], name) == 0) return compiling_func->local_regs[i];
    }
    return -1;
}

/* Return a temporary-register pool position, never below the locals. */
static int reg_restore(int base) {
    note_reg_peak();
    if (compiling_func && base < compiling_func->reg_floor) return compiling_func->reg_floor;
    return base;
}

static void analyze_escapes(Bytecode *bc) {
    if (!bc) return;
    unsigned char *escapes = calloc(bc->local_count + 256, 1);

    // Pass 1: Identify all registers that escape directly
    for (int i = 0; i < bc->count; i++) {
        Instruction *pc = &bc->code[i];
        switch (pc->op) {
            case OP_RETURN: escapes[pc->r0] = 1; break;
            case OP_STORE_VAR: escapes[pc->r0] = 1; break;
            case OP_CALL:
            case OP_CALL_VALUE:
            case OP_SPAWN: {
                for (int j = 0; j < pc->arg; j++) escapes[pc->r2 + j] = 1;
                escapes[pc->r1] = 1; // Callee also escapes if it's an object
                break;
            }
            case OP_SET_PROPERTY: escapes[pc->r1] = 1; break;
            case OP_STORE_INDEX: escapes[pc->r2] = 1; break;
            case OP_LIST_PUSH: escapes[pc->r1] = 1; break;
            case OP_LAMBDA: {
                // Heuristic: objects captured by lambda escape
                for (int j = 0; j < bc->local_count; j++) escapes[j] = 1;
                break;
            }
            default: break;
        }
    }

    // Pass 1.5: Backward propagation of escapes (run multiple iterations to handle dependencies)
    for (int iter = 0; iter < 10; iter++) {
        for (int i = bc->count - 1; i >= 0; i--) {
            Instruction *pc = &bc->code[i];
            switch (pc->op) {
                case OP_MOVE:
                    if (escapes[pc->r0]) escapes[pc->r1] = 1;
                    break;
                case OP_LIST_LITERAL:
                    if (escapes[pc->r0]) {
                        for (int j = 0; j < pc->arg; j++) {
                            escapes[pc->r1 + j] = 1;
                        }
                    }
                    break;
                case OP_DICT_LITERAL:
                    if (escapes[pc->r0]) {
                        for (int j = 0; j < 2 * pc->arg; j++) {
                            escapes[pc->r1 + j] = 1;
                        }
                    }
                    break;
                case OP_STORE_INDEX:
                    // If the collection object escapes, then the value stored inside it also escapes.
                    if (escapes[pc->r0]) escapes[pc->r2] = 1;
                    break;
                case OP_SET_PROPERTY:
                    // If the object escapes, the value assigned to its property escapes.
                    if (escapes[pc->r0]) escapes[pc->r1] = 1;
                    break;
                case OP_LIST_PUSH:
                    // If the list escapes, the pushed item escapes.
                    if (escapes[pc->r0]) escapes[pc->r1] = 1;
                    break;
                default:
                    break;
            }
        }
    }

    // Pass 2: Mark non-escaping allocations
    for (int i = 0; i < bc->count; i++) {
        Instruction *pc = &bc->code[i];
        if (pc->op == OP_LIST_LITERAL || pc->op == OP_DICT_LITERAL || pc->op == OP_LAMBDA) {
            if (!escapes[pc->r0]) pc->is_stack_alloc = 1;
        }
    }
    free(escapes);
}

static int compile_expr(Node *n, int target_reg);

#if 0
static bool try_inline(FuncEntry *fe, int target_reg, int arg_count, int base_arg_reg) {
    if (!fe || !fe->body || fe->body->count > 20 || fe->body->count == 0) return false;

    // Safety check: recursion and complex ops
    for (int i = 0; i < fe->body->count; i++) {
        Instruction *pc = &fe->body->code[i];
        if (pc->op == OP_CALL || pc->op == OP_CALL_VALUE) {
             if (pc->op == OP_CALL && strcmp((const char*)pc->ptr, fe->name) == 0) return false;
        }
        if (pc->op == OP_SPAWN || pc->op == OP_TRY_START || pc->op == OP_LAMBDA) return false;
    }

    int caller_base = base_arg_reg;
    int needed_regs = caller_base + fe->body->local_count;
    if (needed_regs > next_free_reg) next_free_reg = needed_regs;

    // Splice bytecode with register remapping
    for (int i = 0; i < fe->body->count; i++) {
        Instruction pc = fe->body->code[i];
        if (pc.op == OP_RETURN) {
            bc_emit_reg(compiling_bc, OP_MOVE, target_reg, caller_base + pc.r0, 0);
            continue;
        }

        int idx = bc_emit(compiling_bc, pc.op, pc.arg, pc.ptr);
        compiling_bc->code[idx].r0 = caller_base + pc.r0;
        compiling_bc->code[idx].r1 = caller_base + pc.r1;
        compiling_bc->code[idx].r2 = caller_base + pc.r2;
        // Copy PIC cache
        for(int c=0; c<4; c++) compiling_bc->code[idx].cache[c] = pc.cache[c];
    }

    return true;
}
#endif

/* Python-style function scope: every name a function body assigns is a local
   for the whole body, including where it is read before the assignment (in a
   loop, an earlier statement sees the previous iteration's value). Declaring
   them up front also gives every local its register before any temporary is
   allocated, so a temporary from earlier in a loop can never share a register
   with a local declared later. Nested function, lambda, class and `whenever`
   bodies are scopes of their own and are not entered. */
static void hoist_locals(FuncEntry *fe, Node *n) {
    if (!n) return;
    switch (n->type) {
        case NODE_FUNC_DEF:
            /* A nested function is a closure held in a local of this name. */
            if (n->name && get_local_reg(n->name) == -1) func_add_local(fe, n->name);
            return;
        case NODE_LAMBDA: case NODE_CLASS: case NODE_TRAIT: case NODE_WHENEVER:
            return;
        case NODE_LET:
            if (n->name && get_local_reg(n->name) == -1) func_add_local(fe, n->name);
            break;
        case NODE_ASSIGN: case NODE_COMPOUND_ADD: case NODE_COMPOUND_SUB: case NODE_COMPOUND_MUL:
        case NODE_COMPOUND_DIV: case NODE_COMPOUND_MOD: case NODE_FOR_EACH: case NODE_FOR_RANGE:
        case NODE_TRY_CATCH:
            if (n->name && get_local_reg(n->name) == -1 && !global_is_declared(n->name) && !is_outer_local(n->name)) func_add_local(fe, n->name);
            break;
        case NODE_LIST_COMP:
            if (n->comp_var && get_local_reg(n->comp_var) == -1) func_add_local(fe, n->comp_var);
            break;
        default: break;
    }
    hoist_locals(fe, n->left); hoist_locals(fe, n->right); hoist_locals(fe, n->third);
    hoist_locals(fe, n->comp_expr); hoist_locals(fe, n->comp_filter);
    for (int i = 0; i < n->element_count; i++) hoist_locals(fe, n->elements[i]);
    for (int i = 0; i < n->print_count; i++) hoist_locals(fe, n->print_items[i]);
    for (int i = 0; i < n->dict_count; i++) { hoist_locals(fe, n->dict_keys[i]); hoist_locals(fe, n->dict_values[i]); }
    for (int i = 0; i < n->call_arg_count; i++) hoist_locals(fe, n->call_args[i]);
    for (int i = 0; i < n->fstring_part_count; i++) hoist_locals(fe, n->fstring_parts[i]);
}

/* Parse the expression inside an f-string's braces with the language's own
   parser (wrapped as an assignment), so any expression works there. */
extern Node *root;
int yyparse(void);
static Node *parse_fstring_expr(const char *src) {
    size_t n = strlen(src);
    char *code = malloc(n + 32);
    snprintf(code, n + 32, "__fexpr = (%s)\n", src);
    Node *saved_root = root;
    int saved_line = line_num;
    YY_BUFFER_STATE old = get_current_buffer();
    YY_BUFFER_STATE b = yy_scan_string(code);
    root = NULL;
    int rc = yyparse();
    yy_delete_buffer(b);
    if (old) yy_switch_to_buffer(old);
    Node *r = root;
    root = saved_root;
    line_num = saved_line;
    free(code);
    if (rc != 0) return NULL;
    while (r && r->type == NODE_SEQ) r = r->left ? r->left : r->right;
    return (r && r->type == NODE_ASSIGN) ? r->left : NULL;
}

static void compile_stmt(Node *n) {
    if (!n) return;
    compiling_line = n->line;
    int binds_name = n->name && (n->type == NODE_LET || n->type == NODE_ASSIGN ||
        n->type == NODE_COMPOUND_ADD || n->type == NODE_COMPOUND_SUB || n->type == NODE_COMPOUND_MUL ||
        n->type == NODE_COMPOUND_DIV || n->type == NODE_COMPOUND_MOD ||
        n->type == NODE_FOR_EACH || n->type == NODE_FOR_RANGE);
    if (compiling_func && n->name) {
        if (n->type == NODE_LET) {
            if (get_local_reg(n->name) == -1) {
                func_add_local(compiling_func, n->name);
            }
        } else if (binds_name) {
            /* Inside a function, assigning a name that is not already a
               declared global creates a local (like Python). Before this used
               global_get_slot(), which creates the slot it looks up, so every
               such assignment silently wrote a global: recursive functions
               clobbered each other's variables and threads raced on them. */
            if (get_local_reg(n->name) == -1 && !global_is_declared(n->name) && !is_outer_local(n->name)) {
                func_add_local(compiling_func, n->name);
            }
        }
    } else if (binds_name) {
        global_mark_declared(global_get_slot(n->name));
    }

    switch (n->type) {
        case NODE_SEQ: compile_stmt(n->left); compile_stmt(n->right); break;
        case NODE_ASSIGN: {
            int src = compile_expr(n->left, -1);
            int dst = get_local_reg(n->name);
            if (src == -1) src = 0; // Fallback for invalid expression
            if (dst != -1) {
                bc_emit_reg(compiling_bc, OP_MOVE, dst, src, 0);
            } else {
                int slot = global_get_slot(n->name);
                int idx = bc_emit(compiling_bc, OP_STORE_VAR, slot, 0);
                compiling_bc->code[idx].r0 = (unsigned short)src;
            }
            break;
        }
        case NODE_COMPOUND_ADD:
        case NODE_COMPOUND_SUB:
        case NODE_COMPOUND_MUL:
        case NODE_COMPOUND_DIV:
        case NODE_COMPOUND_MOD: {
            OpCode op;
            switch(n->type) {
                case NODE_COMPOUND_ADD: op = OP_ADD; break;
                case NODE_COMPOUND_SUB: op = OP_SUB; break;
                case NODE_COMPOUND_MUL: op = OP_MUL; break;
                case NODE_COMPOUND_DIV: op = OP_DIV; break;
                case NODE_COMPOUND_MOD: op = OP_MOD; break;
                default: op = OP_ADD; break;
            }
            int src = compile_expr(n->left, -1);
            if (src == -1) src = 0;
            int dst = get_local_reg(n->name);
            if (dst != -1) {
                bc_emit_reg(compiling_bc, op, dst, dst, src);
            } else {
                int slot = global_get_slot(n->name);
                int val_reg = next_free_reg++;
                int idx = bc_emit(compiling_bc, OP_LOAD_VAR, slot, 0);
                compiling_bc->code[idx].r0 = val_reg;
                int res_reg = next_free_reg++;
                bc_emit_reg(compiling_bc, op, res_reg, val_reg, src);
                idx = bc_emit(compiling_bc, OP_STORE_VAR, slot, 0);
                compiling_bc->code[idx].r0 = res_reg;
            }
            break;
        }
        case NODE_LET: {
            if (compiling_func) {
                if (get_local_reg(n->name) == -1) {
                    func_add_local(compiling_func, n->name);
                }
            }
            int src = compile_expr(n->left, -1);
            int dst = get_local_reg(n->name);
            if (src == -1) src = 0;
            if (dst != -1) {
                bc_emit_reg(compiling_bc, OP_MOVE, dst, src, 0);
            } else {
                int slot = global_get_slot(n->name);
                int idx = bc_emit(compiling_bc, OP_STORE_LET, slot, 0);
                compiling_bc->code[idx].r0 = (unsigned short)src;
            }
            break;
        }
        case NODE_IF: {
            int cond = compile_expr(n->left, -1);
            int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            compiling_bc->code[jf].r0 = cond;
            compile_stmt(n->right);
            int j = bc_emit(compiling_bc, OP_JUMP, 0, 0);
            compiling_bc->code[jf].arg = compiling_bc->count;
            if (n->third) compile_stmt(n->third);
            compiling_bc->code[j].arg = compiling_bc->count;
            break;
        }
        case NODE_WHILE: {
            int start = compiling_bc->count;

            if (loop_stack_ptr >= MAX_LOOP_DEPTH) { fprintf(stderr, "Loop nesting too deep\n"); exit(1); }
            LoopScope *loop = &loop_stack[loop_stack_ptr++];
            loop->start_pc = start;
            loop->end_patch_count = 0;
            loop->skip_patch_count = 0;

            int cond = compile_expr(n->left, -1);
            int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            compiling_bc->code[jf].r0 = cond;
            compile_stmt(n->right);
            bc_emit(compiling_bc, OP_JUMP, start, 0);
            compiling_bc->code[jf].arg = compiling_bc->count;

            // Backpatch breaks
            for (int i = 0; i < loop->end_patch_count; i++) {
                compiling_bc->code[loop->end_patches[i]].arg = compiling_bc->count;
            }
            // Backpatch skips to start of loop condition check
            for (int i = 0; i < loop->skip_patch_count; i++) {
                compiling_bc->code[loop->skip_patches[i]].arg = start;
            }
            loop_stack_ptr--;
            break;
        }
        case NODE_UNTIL: {
            int start = compiling_bc->count;

            if (loop_stack_ptr >= MAX_LOOP_DEPTH) { fprintf(stderr, "Loop nesting too deep\n"); exit(1); }
            LoopScope *loop = &loop_stack[loop_stack_ptr++];
            loop->start_pc = start;
            loop->end_patch_count = 0;
            loop->skip_patch_count = 0;

            int cond = compile_expr(n->left, -1);
            int jt = bc_emit(compiling_bc, OP_JUMP_IF_TRUE, 0, 0);
            compiling_bc->code[jt].r0 = cond;
            compile_stmt(n->right);
            bc_emit(compiling_bc, OP_JUMP, start, 0);
            compiling_bc->code[jt].arg = compiling_bc->count;

            // Backpatch breaks
            for (int i = 0; i < loop->end_patch_count; i++) {
                compiling_bc->code[loop->end_patches[i]].arg = compiling_bc->count;
            }
            // Backpatch skips to start of loop condition check
            for (int i = 0; i < loop->skip_patch_count; i++) {
                compiling_bc->code[loop->skip_patches[i]].arg = start;
            }
            loop_stack_ptr--;
            break;
        }
        case NODE_REPEAT: {
            int do_unroll = 0;
            int unroll_limit = 0;
            if (n->left && n->left->type == NODE_INT_LIT) {
                unroll_limit = (int)AS_INT(n->left->value);
                if (unroll_limit > 0 && unroll_limit <= 16) {
                    do_unroll = 1;
                }
            }

            if (do_unroll) {
                if (loop_stack_ptr >= MAX_LOOP_DEPTH) { fprintf(stderr, "Loop nesting too deep\n"); exit(1); }
                LoopScope *loop = &loop_stack[loop_stack_ptr++];
                loop->start_pc = -1; // skip jumps handled per-iteration
                loop->end_patch_count = 0;
                loop->skip_patch_count = 0;

                for (int i=0; i<unroll_limit; i++) {
                    compile_stmt(n->right);
                    // Next iteration starts here; patch all skips in this iteration
                    for (int s=0; s<loop->skip_patch_count; s++) {
                        compiling_bc->code[loop->skip_patches[s]].arg = compiling_bc->count;
                    }
                    loop->skip_patch_count = 0; // reset for next iteration
                }

                // End of loop; patch all breaks
                for (int i = 0; i < loop->end_patch_count; i++) {
                    compiling_bc->code[loop->end_patches[i]].arg = compiling_bc->count;
                }
                loop_stack_ptr--;
            } else {
                int limit_reg = compile_expr(n->left, -1);
                int counter_reg = next_free_reg++;
                int idx = bc_emit(compiling_bc, OP_CONST_INT, 0, 0);
                compiling_bc->code[idx].r0 = counter_reg;

                int loop_start = compiling_bc->count;

                if (loop_stack_ptr >= MAX_LOOP_DEPTH) { fprintf(stderr, "Loop nesting too deep\n"); exit(1); }
                LoopScope *loop = &loop_stack[loop_stack_ptr++];
                loop->start_pc = -1; // skip jumps to loop increment
                loop->end_patch_count = 0;
                loop->skip_patch_count = 0;

                int cond_reg = next_free_reg++;
                bc_emit_reg(compiling_bc, OP_LT, cond_reg, counter_reg, limit_reg);
                int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
                compiling_bc->code[jf].r0 = cond_reg;

                compile_stmt(n->right);

                // Increment phase PC
                int incr_start = compiling_bc->count;
                for (int i = 0; i < loop->skip_patch_count; i++) {
                    compiling_bc->code[loop->skip_patches[i]].arg = incr_start;
                }

                int one_reg = next_free_reg++;
                idx = bc_emit(compiling_bc, OP_CONST_INT, 1, 0);
                compiling_bc->code[idx].r0 = one_reg;
                bc_emit_reg(compiling_bc, OP_ADD, counter_reg, counter_reg, one_reg);

                bc_emit(compiling_bc, OP_JUMP, loop_start, 0);
                compiling_bc->code[jf].arg = compiling_bc->count;

                // Backpatch breaks
                for (int i = 0; i < loop->end_patch_count; i++) {
                    compiling_bc->code[loop->end_patches[i]].arg = compiling_bc->count;
                }
                loop_stack_ptr--;
            }
            break;
        }
        case NODE_FOR_RANGE: {
            if (compiling_func && get_local_reg(n->name) == -1) {
                func_add_local(compiling_func, n->name);
            }
            int var_reg = get_local_reg(n->name);

            int do_unroll = 0;
            int start_lit = 0, end_lit = 0;
            if (n->left && n->left->type == NODE_INT_LIT && n->right && n->right->type == NODE_INT_LIT) {
                start_lit = (int)AS_INT(n->left->value);
                end_lit = (int)AS_INT(n->right->value);
                if (end_lit >= start_lit && (end_lit - start_lit + 1) <= 16) {
                    do_unroll = 1;
                }
            }

            if (do_unroll) {
                if (loop_stack_ptr >= MAX_LOOP_DEPTH) { fprintf(stderr, "Loop nesting too deep\n"); exit(1); }
                LoopScope *loop = &loop_stack[loop_stack_ptr++];
                loop->start_pc = -1;
                loop->end_patch_count = 0;
                loop->skip_patch_count = 0;

                for (int i = start_lit; i <= end_lit; i++) {
                    int i_val = bc_emit(compiling_bc, OP_CONST_INT, i, 0);
                    int val_reg = next_free_reg++;
                    compiling_bc->code[i_val].r0 = val_reg;

                    if (var_reg != -1) {
                        bc_emit_reg(compiling_bc, OP_MOVE, var_reg, val_reg, 0);
                    } else {
                        int slot = global_get_slot(n->name);
                        int idx = bc_emit(compiling_bc, OP_STORE_VAR, slot, 0);
                        compiling_bc->code[idx].r0 = val_reg;
                    }

                    compile_stmt(n->third);

                    for (int s = 0; s < loop->skip_patch_count; s++) {
                        compiling_bc->code[loop->skip_patches[s]].arg = compiling_bc->count;
                    }
                    loop->skip_patch_count = 0;
                }

                for (int i = 0; i < loop->end_patch_count; i++) {
                    compiling_bc->code[loop->end_patches[i]].arg = compiling_bc->count;
                }
                loop_stack_ptr--;
            } else {
                int start_val = compile_expr(n->left, -1);
                int end_val = compile_expr(n->right, -1);
                if (var_reg != -1) {
                    bc_emit_reg(compiling_bc, OP_MOVE, var_reg, start_val, 0);
                } else {
                    int slot = global_get_slot(n->name);
                    int idx = bc_emit(compiling_bc, OP_STORE_VAR, slot, 0);
                    compiling_bc->code[idx].r0 = start_val;
                }
                int loop_start = compiling_bc->count;

                if (loop_stack_ptr >= MAX_LOOP_DEPTH) { fprintf(stderr, "Loop nesting too deep\n"); exit(1); }
                LoopScope *loop = &loop_stack[loop_stack_ptr++];
                loop->start_pc = -1; // skip jumps to loop increment
                loop->end_patch_count = 0;
                loop->skip_patch_count = 0;

                int i_reg = var_reg != -1 ? var_reg : next_free_reg++;
                if (var_reg == -1) {
                    int slot = global_get_slot(n->name);
                    int idx = bc_emit(compiling_bc, OP_LOAD_VAR, slot, 0);
                    compiling_bc->code[idx].r0 = i_reg;
                }
                int cond_reg = next_free_reg++;
                bc_emit_reg(compiling_bc, OP_LTE, cond_reg, i_reg, end_val);
                int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
                compiling_bc->code[jf].r0 = cond_reg;
                compile_stmt(n->third);

                // Increment phase PC
                int incr_start = compiling_bc->count;
                for (int i = 0; i < loop->skip_patch_count; i++) {
                    compiling_bc->code[loop->skip_patches[i]].arg = incr_start;
                }

                if (var_reg == -1) {
                    int slot = global_get_slot(n->name);
                    int idx = bc_emit(compiling_bc, OP_LOAD_VAR, slot, 0);
                    compiling_bc->code[idx].r0 = i_reg;
                }
                int one_reg = next_free_reg++;
                int idx = bc_emit(compiling_bc, OP_CONST_INT, 1, 0);
                compiling_bc->code[idx].r0 = one_reg;
                bc_emit_reg(compiling_bc, OP_ADD, i_reg, i_reg, one_reg);
                if (var_reg == -1) {
                    int slot = global_get_slot(n->name);
                    int idx2 = bc_emit(compiling_bc, OP_STORE_VAR, slot, 0);
                    compiling_bc->code[idx2].r0 = i_reg;
                }
                bc_emit(compiling_bc, OP_JUMP, loop_start, 0);
                compiling_bc->code[jf].arg = compiling_bc->count;

                // Backpatch breaks
                for (int i = 0; i < loop->end_patch_count; i++) {
                    compiling_bc->code[loop->end_patches[i]].arg = compiling_bc->count;
                }
                loop_stack_ptr--;
            }
            break;
        }
        case NODE_FOR_EACH: {
            int coll_reg = compile_expr(n->left, -1);

            // 1. Ensure loop variable is local and get its register
            if (compiling_func && get_local_reg(n->name) == -1) {
                func_add_local(compiling_func, n->name);
                next_free_reg++;
            }
            int var_reg = get_local_reg(n->name);

            // 2. Allocate internal iterator state register
            int iter_reg = next_free_reg++;

            bc_emit_reg(compiling_bc, OP_ITER_START, iter_reg, coll_reg, 0);
            int loop_start = compiling_bc->count;

            if (loop_stack_ptr >= MAX_LOOP_DEPTH) { fprintf(stderr, "Loop nesting too deep\n"); exit(1); }
            LoopScope *loop = &loop_stack[loop_stack_ptr++];
            loop->start_pc = loop_start;
            loop->end_patch_count = 0;
            loop->skip_patch_count = 0;

            // 3. Directly load the next value into the loop variable's register
            int val_reg = var_reg != -1 ? var_reg : next_free_reg++;
            int jf_idx = bc_emit_reg(compiling_bc, OP_ITER_NEXT, val_reg, iter_reg, 0);

            if (var_reg == -1) {
                int g_slot = global_get_slot(n->name);
                int s_idx = bc_emit(compiling_bc, OP_STORE_VAR, g_slot, 0);
                compiling_bc->code[s_idx].r0 = val_reg;
            }

            compile_stmt(n->right);
            bc_emit(compiling_bc, OP_JUMP, loop_start, 0);
            compiling_bc->code[jf_idx].arg = compiling_bc->count;

            // Backpatch breaks
            for (int i = 0; i < loop->end_patch_count; i++) {
                compiling_bc->code[loop->end_patches[i]].arg = compiling_bc->count;
            }
            // Backpatch skips to loop iteration head
            for (int i = 0; i < loop->skip_patch_count; i++) {
                compiling_bc->code[loop->skip_patches[i]].arg = loop_start;
            }
            loop_stack_ptr--;
            break;
        }
        case NODE_RETURN: {
            int r = compile_expr(n->left, -1);
            int idx = bc_emit(compiling_bc, OP_RETURN, 0, 0);
            compiling_bc->code[idx].r0 = r;
            break;
        }
        case NODE_FUNC_DEF: {
            /* Only functions written directly in a class/trait body are
               methods; a function nested inside a method body is an ordinary
               function (it used to become Class::name with a `self` slot, so
               calling or spawning it by name found nothing). */
            int is_method = (last_compiling_class || last_compiling_trait) ? 1 : 0;
            const char *func_name = n->name;
            if (compiling_class_name) {
                char buf[512];
                snprintf(buf, sizeof(buf), "%s::%s", compiling_class_name, n->name);
                func_name = intern_string(buf);
            } else if (compiling_trait_name) {
                char buf[512];
                snprintf(buf, sizeof(buf), "%s::%s", compiling_trait_name, n->name);
                func_name = intern_string(buf);
            }
            FuncEntry *fe = func_table_add(func_name);
            if (fe->body) bc_free(fe->body);
            if (fe->specs) {
                for (int i=0; i<fe->spec_count; i++) {
                    if (fe->specs[i].jit_code && fe->specs[i].jit_code != (void*)1) {
                        munmap(fe->specs[i].jit_code, fe->specs[i].jit_size);
                    }
                }
                free(fe->specs);
                fe->specs = NULL;
                fe->spec_count = 0;
                fe->spec_capacity = 0;
            }
            if (fe->local_names) free(fe->local_names);
            if (fe->local_regs) free(fe->local_regs);
            fe->local_names = NULL; fe->local_regs = NULL; fe->local_count = 0; fe->local_capacity = 0; fe->reg_floor = 0;

            fe->param_count = n->func_param_count; fe->params = n->func_params; fe->body = bc_new(fe);
            Bytecode *p = compiling_bc; compiling_bc = fe->body;
            FuncEntry *pf = compiling_func; compiling_func = fe;
            push_outer(is_method ? NULL : pf);
            int old_free = next_free_reg;
            next_free_reg = 0; /* self and parameters occupy registers 0..n-1 */

            if (is_method) {
                func_add_local(fe, "self");
            }
            for (int i=0; i<fe->param_count; i++) {
                func_add_local(fe, n->func_params[i]);
            }
            fe->body->preserved_locals = fe->local_count;
            hoist_locals(fe, n->left);

            next_free_reg = fe->local_count;
            int saved_peak = reg_peak; reg_peak = next_free_reg;
            const char *saved_class_name = compiling_class_name, *saved_trait_name = compiling_trait_name;
            Class *saved_class = last_compiling_class; Trait *saved_trait = last_compiling_trait;
            compiling_class_name = NULL; compiling_trait_name = NULL;
            last_compiling_class = NULL; last_compiling_trait = NULL;
            compile_stmt(n->left);
            compiling_class_name = saved_class_name; compiling_trait_name = saved_trait_name;
            last_compiling_class = saved_class; last_compiling_trait = saved_trait;
            note_reg_peak();
            fe->body->local_count = reg_peak > fe->reg_floor ? reg_peak : fe->reg_floor;
            bc_emit(compiling_bc, OP_RETURN, 0, 0);
            optimize_bytecode(compiling_bc);
            analyze_escapes(compiling_bc);
            compiling_bc = p; compiling_func = pf; next_free_reg = old_free; reg_peak = saved_peak;
            pop_outer();
            bc_emit(compiling_bc, is_method ? OP_METHOD_DEF : OP_FUNC_DEF, 0, (intptr_t)func_name);
            /* A function defined inside another function is a closure, like a
               lambda: it sees the enclosing function's variables as they are
               when the definition runs, and the enclosing code calls it
               through the local of the same name. */
            if (pf && !is_method) {
                int lreg = get_local_reg(func_name);
                if (lreg != -1) {
                    int li = bc_emit(compiling_bc, OP_LAMBDA, 0, (intptr_t)func_name);
                    compiling_bc->code[li].r0 = lreg;
                    compiling_bc->code[li].cache[0].key = pf;
                }
            }
            break;
        }
        case NODE_SHOW: {
            int start_reg = next_free_reg;
            next_free_reg += n->print_count;
            for (int i=0; i<n->print_count; i++) {
                compile_expr(n->print_items[i], start_reg + i);
            }
            int idx = bc_emit(compiling_bc, OP_SHOW, n->print_count, 0);
            compiling_bc->code[idx].r0 = start_reg;
            break;
        }
        case NODE_WRITE: {
            int start_reg = next_free_reg;
            next_free_reg += n->print_count;
            for (int i=0; i<n->print_count; i++) {
                compile_expr(n->print_items[i], start_reg + i);
            }
            int idx = bc_emit(compiling_bc, OP_WRITE, n->print_count, 0);
            compiling_bc->code[idx].r0 = start_reg;
            break;
        }
        case NODE_EXPR_STMT: compile_expr(n->left, -1); break;
        case NODE_STOP: {
            int idx = bc_emit(compiling_bc, OP_STOP, 0, 0);
            if (loop_stack_ptr > 0) {
                LoopScope *loop = &loop_stack[loop_stack_ptr - 1];
                if (loop->end_patch_count < 256) {
                    loop->end_patches[loop->end_patch_count++] = idx;
                } else {
                    fprintf(stderr, "Too many breaks in loop\n");
                    exit(1);
                }
            }
            break;
        }
        case NODE_SKIP: {
            int idx = bc_emit(compiling_bc, OP_SKIP, 0, 0);
            if (loop_stack_ptr > 0) {
                LoopScope *loop = &loop_stack[loop_stack_ptr - 1];
                if (loop->skip_patch_count < 256) {
                    loop->skip_patches[loop->skip_patch_count++] = idx;
                } else {
                    fprintf(stderr, "Too many continues in loop\n");
                    exit(1);
                }
            }
            break;
        }
        case NODE_CLASS: {
            const char *old_class = compiling_class_name;
            compiling_class_name = n->name;
            Class *pc = last_compiling_class; last_compiling_class = (Class*)1;
            if (n->parent_name) {
                int r = next_free_reg++;
                int slot = global_get_slot(n->parent_name);
                int idx = bc_emit(compiling_bc, OP_LOAD_VAR, slot, 0);
                compiling_bc->code[idx].r0 = r;
            }
            bc_emit(compiling_bc, OP_CLASS_DEF, n->parent_name ? 1 : 0, (intptr_t)n->name);
            compile_stmt(n->left);
            last_compiling_class = pc;
            compiling_class_name = old_class;
            break;
        }
        case NODE_TRAIT: {
            const char *old_trait = compiling_trait_name;
            compiling_trait_name = n->name;
            Trait *pt = last_compiling_trait; last_compiling_trait = (Trait*)1;
            bc_emit(compiling_bc, OP_TRAIT_DEF, 0, (intptr_t)n->name);
            compile_stmt(n->left);
            last_compiling_trait = pt;
            compiling_trait_name = old_trait;
            break;
        }
        case NODE_USE_TRAIT: bc_emit(compiling_bc, OP_USE_TRAIT, 0, (intptr_t)n->name); break;
        case NODE_TRY_CATCH: {
            /* The VM delivers the caught exception into a dedicated register
               (it used to overwrite register 0, i.e. `self` in methods or the
               first local in functions). */
            if (next_free_reg < (compiling_func ? compiling_func->reg_floor : 0)) next_free_reg = compiling_func->reg_floor;
            int exc_reg = next_free_reg++;
            int start_pc = compiling_bc->count;
            compile_stmt(n->left);
            int end_pc = compiling_bc->count;
            int j = bc_emit(compiling_bc, OP_JUMP, 0, 0);
            int h = compiling_bc->count;
            /* The range includes the jump over the handler: an error a native
               call raises is noticed when the next instruction dispatches, so
               a call that ends the try body would otherwise escape it. */
            bc_add_handler(compiling_bc, start_pc, end_pc + 1, h, exc_reg);
            if (n->name) {
                int var_reg = get_local_reg(n->name);
                if (compiling_func && var_reg == -1 && !global_is_declared(n->name)) {
                    func_add_local(compiling_func, n->name);
                    var_reg = get_local_reg(n->name);
                }
                if (var_reg != -1) {
                    bc_emit_reg(compiling_bc, OP_MOVE, var_reg, exc_reg, 0);
                } else {
                    int slot = global_get_slot(n->name);
                    if (!compiling_func) global_mark_declared(slot);
                    int idx = bc_emit(compiling_bc, OP_STORE_VAR, slot, 0);
                    compiling_bc->code[idx].r0 = exc_reg;
                }
            }
            compile_stmt(n->right); compiling_bc->code[j].arg = compiling_bc->count; break;
        }
        case NODE_THROW: { int r = compile_expr(n->left, -1); int idx = bc_emit(compiling_bc, OP_THROW, 0, 0); compiling_bc->code[idx].r0 = r; break; }
        case NODE_SPAWN: {
             Node *call = n->left; int ac = call->call_arg_count;
             int base = next_free_reg;
             next_free_reg += ac + 1;
             for (int i=0; i<ac; i++) compile_expr(call->call_args[i], base + i);
             int callee_reg = base + ac;
             if (call->type == NODE_CALL) {
                 int slot = global_get_slot(call->name);
                 int idx = bc_emit(compiling_bc, OP_LOAD_VAR, slot, 0);
                 compiling_bc->code[idx].r0 = callee_reg;
             } else {
                 compile_expr(call->left, callee_reg);
             }
             const char *fn_name = (call->type == NODE_CALL && call->name) ? call->name : ((call->left && call->left->name) ? call->left->name : NULL);
             int idx = bc_emit(compiling_bc, OP_SPAWN, ac, (intptr_t)fn_name);
             compiling_bc->code[idx].r0 = callee_reg;
             compiling_bc->code[idx].r1 = base;
             break;
        }
        case NODE_IMPORT: {
            bc_emit(compiling_bc, OP_USE_FILE, 0, (intptr_t)n->name);
            break;
        }
        case NODE_WHENEVER: {
            // Refined edge-triggered semantic: spawn a background monitor
            const char *last_name = intern_string("_whenever_last");
            const char *curr_name = intern_string("_whenever_curr");

            Node *init_last = node_new(NODE_LET);
            init_last->name = last_name;
            init_last->left = newBoolNode(0);

            Node *get_curr = node_new(NODE_LET);
            get_curr->name = curr_name;
            get_curr->left = n->left;

            Node *not_last = node_new(NODE_NOT);
            not_last->left = newVarNode(last_name);
            Node *trigger_cond = node_new(NODE_AND);
            trigger_cond->left = newVarNode(curr_name);
            trigger_cond->right = not_last;

            Node *action = newIfNode(trigger_cond, n->right, NULL);
            Node *update_last = newAssignNode(last_name, newVarNode(curr_name));

            Node *os_var = newVarNode(intern_string("os"));
            Node *sleep_var = node_new(NODE_INDEX);
            sleep_var->left = os_var;
            sleep_var->name = intern_string("sleep");

            Node **sleep_args = malloc(sizeof(Node*));
            sleep_args[0] = newFloatNode(1.0); /* poll every millisecond */
            Node *sleep_call = newCallExprNode(sleep_var, sleep_args, 1);

            Node *loop_content = newSeqNode(get_curr, newSeqNode(action, newSeqNode(update_last, sleep_call)));
            Node *while_loop = newWhileNode(newBoolNode(1), loop_content);
            Node *lambda_body = newSeqNode(init_last, while_loop);

            char buf[64]; snprintf(buf, sizeof(buf), "whenever_%d", global_lambda_count++);
            Node *lambda = newFuncNode(intern_string(buf), 0, NULL, lambda_body);
            lambda->type = NODE_LAMBDA;

            Node *spawn_node = node_new(NODE_SPAWN);
            spawn_node->left = newCallExprNode(lambda, NULL, 0);

            compile_stmt(spawn_node);
            /* The monitor loops forever, so it must not keep the program
               alive: mark the spawn as a daemon thread (r2 = 1). */
            if (compiling_bc->count > 0 && compiling_bc->code[compiling_bc->count - 1].op == OP_SPAWN)
                compiling_bc->code[compiling_bc->count - 1].r2 = 1;
            break;
        }
        case NODE_INDEX_ASSIGN: {
            int val = compile_expr(n->third, -1);
            int obj = compile_expr(n->left, -1);
            if (n->name) {
                int idx = bc_emit(compiling_bc, OP_SET_PROPERTY, 0, (intptr_t)n->name);
                compiling_bc->code[idx].r0 = obj;
                compiling_bc->code[idx].r1 = val;
            } else {
                int key = compile_expr(n->right, -1);
                int idx = bc_emit(compiling_bc, OP_STORE_INDEX, 0, 0);
                compiling_bc->code[idx].r0 = obj;
                compiling_bc->code[idx].r1 = key;
                compiling_bc->code[idx].r2 = val;
            }
            break;
        }
        default: compile_expr(n, -1); break;
    }
}

static int compile_expr(Node *n, int target_reg) {
    if (!n) {
        return -1;
    }
    compiling_line = n->line;
    int reg = target_reg;
    if (reg == -1) reg = next_free_reg++;

    switch (n->type) {
        case NODE_INT_LIT: {
            Value lit = n->value;
            if (GET_TYPE(lit) == VAL_INT && AS_INT(lit) >= INT32_MIN && AS_INT(lit) <= INT32_MAX) {
                /* Fits the 32-bit OP_CONST_INT immediate. */
                int idx = bc_emit(compiling_bc, OP_CONST_INT, (int)AS_INT(lit), 0);
                compiling_bc->code[idx].r0 = reg;
            } else {
                /* Wider-than-32-bit int, double, or heap big-int: carry it as a
                   generic Value constant (like FLOAT_LIT) so the value is not
                   truncated by the 32-bit immediate, and root big-ints so the GC
                   never frees a literal still referenced by the bytecode. */
                Value *vp = arena_alloc(sizeof(Value));
                *vp = lit;
                int idx = bc_emit(compiling_bc, OP_CONST_FLOAT, 0, (intptr_t)vp);
                compiling_bc->code[idx].r0 = reg;
                if (is_bigint(lit)) gc_register_constant_root(lit);
            }
            break;
        }
        case NODE_FLOAT_LIT: {
            Value *v = arena_alloc(sizeof(Value)); *v = n->value;
            int idx = bc_emit(compiling_bc, OP_CONST_FLOAT, 0, (intptr_t)v);
            compiling_bc->code[idx].r0 = reg;
            break;
        }
        case NODE_BOOL_LIT: {
            int idx = bc_emit(compiling_bc, OP_CONST_BOOL, AS_BOOL(n->value), 0);
            compiling_bc->code[idx].r0 = reg;
            break;
        }
        case NODE_STRING_LIT: {
            int idx = bc_emit(compiling_bc, OP_CONST_STRING, 0, (intptr_t)n->name);
            compiling_bc->code[idx].r0 = reg;
            break;
        }
        case NODE_VAR: {
            int src = get_local_reg(n->name);
            if (src != -1) {
                if (reg != src) bc_emit_reg(compiling_bc, OP_MOVE, reg, src, 0);
            } else {
                int slot = global_get_slot(n->name);
                int idx = bc_emit(compiling_bc, OP_LOAD_VAR, slot, 0);
                compiling_bc->code[idx].r0 = (unsigned short)reg;
            }
            break;
        }
        case NODE_AND: {
            // Short-circuit: evaluate the left operand into the result register and
            // branch on that SAME register. Keeping the branch condition and the
            // result in one register (instead of a separate temp joined by a MOVE)
            // stays coherent through the optimizer's copy-propagation/regalloc.
            int left = compile_expr(n->left, reg);
            if (left != reg) bc_emit_reg(compiling_bc, OP_MOVE, reg, left, 0);
            int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            compiling_bc->code[jf].r0 = reg;               // left falsy -> keep reg (=left)
            int right = compile_expr(n->right, reg);
            if (right != reg) bc_emit_reg(compiling_bc, OP_MOVE, reg, right, 0);
            compiling_bc->code[jf].arg = compiling_bc->count;
            break;
        }
        case NODE_COND: {
            int cond = compile_expr(n->left, -1);
            int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            compiling_bc->code[jf].r0 = cond;
            int yes = compile_expr(n->right, reg);
            if (yes != reg) bc_emit_reg(compiling_bc, OP_MOVE, reg, yes, 0);
            int j = bc_emit(compiling_bc, OP_JUMP, 0, 0);
            compiling_bc->code[jf].arg = compiling_bc->count;
            int no = compile_expr(n->third, reg);
            if (no != reg) bc_emit_reg(compiling_bc, OP_MOVE, reg, no, 0);
            compiling_bc->code[j].arg = compiling_bc->count;
            break;
        }
        case NODE_OR: {
            int left = compile_expr(n->left, reg);
            if (left != reg) bc_emit_reg(compiling_bc, OP_MOVE, reg, left, 0);
            int jt = bc_emit(compiling_bc, OP_JUMP_IF_TRUE, 0, 0);
            compiling_bc->code[jt].r0 = reg;               // left truthy -> keep reg (=left)
            int right = compile_expr(n->right, reg);
            if (right != reg) bc_emit_reg(compiling_bc, OP_MOVE, reg, right, 0);
            compiling_bc->code[jt].arg = compiling_bc->count;
            break;
        }
        case NODE_ADD:
        case NODE_SUB:
        case NODE_MUL:
        case NODE_DIV:
        case NODE_MOD:
        case NODE_POW:
        case NODE_LT:
        case NODE_GT:
        case NODE_LTE:
        case NODE_GTE:
        case NODE_EQ:
        case NODE_NEQ: {
            int left = compile_expr(n->left, -1);
            int right = compile_expr(n->right, -1);
            if (target_reg == -1) reg = next_free_reg++;
            OpCode op;
            switch(n->type) {
                case NODE_ADD: op = OP_ADD; break;
                case NODE_SUB: op = OP_SUB; break;
                case NODE_MUL: op = OP_MUL; break;
                case NODE_DIV: op = OP_DIV; break;
                case NODE_MOD: op = OP_MOD; break;
                case NODE_POW: op = OP_POW; break;
                case NODE_LT: op = OP_LT; break;
                case NODE_GT: op = OP_GT; break;
                case NODE_LTE: op = OP_LTE; break;
                case NODE_GTE: op = OP_GTE; break;
                case NODE_EQ: op = OP_EQ; break;
                case NODE_NEQ: op = OP_NEQ; break;
                default: op = OP_ADD; break;
            }
            bc_emit_reg(compiling_bc, op, reg, left, right);
            break;
        }
        case NODE_NOT: {
            int src = compile_expr(n->left, -1);
            bc_emit_reg(compiling_bc, OP_NOT, reg, src, 0);
            break;
        }
        case NODE_IN: {
            int elem = compile_expr(n->left, -1);
            int coll = compile_expr(n->right, -1);
            int idx = bc_emit(compiling_bc, OP_IN, 0, 0);
            compiling_bc->code[idx].r0 = reg;
            compiling_bc->code[idx].r1 = elem;
            compiling_bc->code[idx].r2 = coll;
            break;
        }
        case NODE_NEG: {
            int src = compile_expr(n->left, -1);
            bc_emit_reg(compiling_bc, OP_NEG, reg, src, 0);
            break;
        }
        case NODE_CALL_EXPR:
        case NODE_CALL: {
            int ac = n->call_arg_count;
            int start_reg = next_free_reg;

            // 1. Identify method status
            bool is_method = (n->type == NODE_CALL_EXPR && n->left->type == NODE_INDEX && n->left->name != NULL);

            // 2. Reserve the entire callee frame block immediately
            int callee_frame_base = start_reg;
            if (is_method) {
                next_free_reg += (ac + 1);
                // Compile the instance (receiver) directly into Local 0 of the new frame
                compile_expr(n->left->left, callee_frame_base);
                // Compile arguments starting at Local 1
                for (int i=0; i<ac; i++) {
                    compile_expr(n->call_args[i], callee_frame_base + i + 1);
                }
            } else {
                next_free_reg += ac;
                // Compile arguments starting at Local 0
                for (int i=0; i<ac; i++) {
                    compile_expr(n->call_args[i], callee_frame_base + i);
                }
            }

            // 3. Resolve the callee into a register outside the frame block
            int callee_obj_reg = next_free_reg++;
            if (is_method) {
                callee_obj_reg = callee_frame_base;
            } else if (n->type == NODE_CALL && n->name) {
                int slot = global_get_slot(n->name);
                int idx = bc_emit(compiling_bc, OP_LOAD_VAR, slot, 0);
                compiling_bc->code[idx].r0 = callee_obj_reg;
            } else {
                compile_expr(n->left, callee_obj_reg);
            }

            // 4. Emit the call
            int idx = bc_emit(compiling_bc, is_method ? OP_CALL_METHOD : ((n->type == NODE_CALL) ? OP_CALL : OP_CALL_VALUE), ac, is_method ? (intptr_t)intern_string(n->left->name) : ((intptr_t)n->name));
            compiling_bc->code[idx].r0 = reg;
            compiling_bc->code[idx].r1 = callee_obj_reg;
            compiling_bc->code[idx].r2 = callee_frame_base;
            compiling_bc->code[idx].is_method = is_method;

            // 5. Restore the register pool
            next_free_reg = reg_restore(start_reg);
            break;
        }
        case NODE_INDEX: {
            int obj = compile_expr(n->left, -1);
            if (n->name) {
                int idx = bc_emit(compiling_bc, OP_GET_PROPERTY, 0, (intptr_t)n->name);
                compiling_bc->code[idx].r0 = reg;
                compiling_bc->code[idx].r1 = obj;
            } else {
                int key = compile_expr(n->right, -1);
                int idx = bc_emit(compiling_bc, OP_INDEX, 0, 0);
                compiling_bc->code[idx].r0 = reg;
                compiling_bc->code[idx].r1 = obj;
                compiling_bc->code[idx].r2 = key;
            }
            break;
        }
        case NODE_SLICE: {
            int obj = compile_expr(n->left, -1);
            int base = next_free_reg;
            next_free_reg += 2;

            if (n->right) {
                compile_expr(n->right, base);
            } else {
                int idx = bc_emit(compiling_bc, OP_CONST_INT, -999999, 0);
                compiling_bc->code[idx].r0 = base;
            }

            if (n->third) {
                compile_expr(n->third, base + 1);
            } else {
                int idx = bc_emit(compiling_bc, OP_CONST_INT, -999999, 0);
                compiling_bc->code[idx].r0 = base + 1;
            }

            int idx = bc_emit(compiling_bc, OP_SLICE, 0, 0);
            compiling_bc->code[idx].r0 = reg;
            compiling_bc->code[idx].r1 = obj;
            compiling_bc->code[idx].r2 = base;

            next_free_reg = reg_restore(base);
            break;
        }
        case NODE_FSTRING: {
            /* f"text {expression} text": each {...} is a full expression,
               {{ and }} are literal braces, and the usual escapes apply.
               The pieces are joined as text, starting from "" so numbers
               are converted, not added. */
            const char *str = n->name;
            size_t len = strlen(str);
            char *lit = malloc(len + 1);
            size_t lit_len = 0;
            int acc = compile_expr(newStringNode(intern_string("")), -1);
            size_t i = 0;
            while (i <= len) {
                int flush = (i == len);
                if (!flush && str[i] == '{' && i + 1 < len && str[i + 1] == '{') { lit[lit_len++] = '{'; i += 2; continue; }
                if (!flush && str[i] == '}' && i + 1 < len && str[i + 1] == '}') { lit[lit_len++] = '}'; i += 2; continue; }
                if (!flush && str[i] == '\\' && i + 1 < len) {
                    char e = str[i + 1];
                    lit[lit_len++] = e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e;
                    i += 2;
                    continue;
                }
                if (!flush && str[i] != '{') { lit[lit_len++] = str[i++]; continue; }
                /* A literal run ends here. */
                if (lit_len > 0) {
                    lit[lit_len] = '\0';
                    int lr = compile_expr(newStringNode(intern_string(lit)), -1);
                    int idx = bc_emit_reg(compiling_bc, OP_ADD, acc, acc, lr);
                    (void)idx;
                    lit_len = 0;
                }
                if (flush) break;
                /* An expression: up to the matching closing brace. */
                size_t start = ++i;
                int depth = 1;
                while (i < len && depth > 0) {
                    if (str[i] == '{') depth++;
                    else if (str[i] == '}') depth--;
                    if (depth > 0) i++;
                }
                size_t elen = i - start;
                char *src = malloc(elen + 1);
                memcpy(src, str + start, elen);
                src[elen] = '\0';
                Node *e = parse_fstring_expr(src);
                free(src);
                if (i < len) i++; /* skip '}' */
                int er = compile_expr(e ? e : newVarNode(intern_string("none")), -1);
                bc_emit_reg(compiling_bc, OP_ADD, acc, acc, er);
            }
            free(lit);
            bc_emit_reg(compiling_bc, OP_MOVE, reg, acc, 0);
            break;
        }
        case NODE_LIST_LITERAL: {
            int base = next_free_reg;
            next_free_reg += n->element_count;
            for (int i=0; i<n->element_count; i++) compile_expr(n->elements[i], base + i);
            int idx = bc_emit(compiling_bc, OP_LIST_LITERAL, n->element_count, 0);
            compiling_bc->code[idx].r0 = reg;
            compiling_bc->code[idx].r1 = base;
            next_free_reg = reg_restore(base); // Restore pool
            break;
        }
        case NODE_LIST_COMP: {
            int start_reg = next_free_reg;

            // 1. Compile the collection/iterable first (e.g. n->left)
            int coll_reg = compile_expr(n->left, -1);

            // 2. Allocate the destination list literal register (which is `reg`)
            int idx_list = bc_emit(compiling_bc, OP_LIST_LITERAL, 0, 0);
            compiling_bc->code[idx_list].r0 = reg;
            compiling_bc->code[idx_list].r1 = 0;

            // 3. Handle loop variable (e.g. n->comp_var)
            if (compiling_func && get_local_reg(n->comp_var) == -1) {
                func_add_local(compiling_func, n->comp_var);
                next_free_reg++;
            }
            int var_reg = get_local_reg(n->comp_var);

            // 4. Allocate iterator register and value register
            int iter_reg = next_free_reg++;
            int val_reg = var_reg != -1 ? var_reg : next_free_reg++;

            // 5. Emit iterator start
            bc_emit_reg(compiling_bc, OP_ITER_START, iter_reg, coll_reg, 0);
            int loop_start = compiling_bc->count;

            // 6. Emit iterator next
            int jf_idx = bc_emit_reg(compiling_bc, OP_ITER_NEXT, val_reg, iter_reg, 0);

            // 7. Store in global variable if it is global
            if (var_reg == -1) {
                int g_slot = global_get_slot(n->comp_var);
                int s_idx = bc_emit(compiling_bc, OP_STORE_VAR, g_slot, 0);
                compiling_bc->code[s_idx].r0 = val_reg;
            }

            // 8. Compile filter condition if present
            int filter_jump_idx = -1;
            if (n->comp_filter) {
                int cond_reg = compile_expr(n->comp_filter, -1);
                filter_jump_idx = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
                compiling_bc->code[filter_jump_idx].r0 = cond_reg;
            }

            // 9. Evaluate the item expression
            int val_item_reg = compile_expr(n->comp_expr, -1);

            // 10. Append to the destination list
            bc_emit_reg(compiling_bc, OP_APPEND, reg, val_item_reg, 0);

            // 11. Patch filter jump destination
            if (filter_jump_idx != -1) {
                compiling_bc->code[filter_jump_idx].arg = compiling_bc->count;
            }

            // 12. Jump back to loop start
            bc_emit(compiling_bc, OP_JUMP, loop_start, 0);

            // 13. Patch iterator exit jump destination
            compiling_bc->code[jf_idx].arg = compiling_bc->count;

            // 14. Restore the register pool (except the returned `reg`)
            next_free_reg = reg_restore(start_reg);
            break;
        }
        case NODE_DICT_LITERAL: {
            int base = next_free_reg;
            next_free_reg += n->dict_count * 2;
            for (int i=0; i<n->dict_count; i++) {
                compile_expr(n->dict_keys[i], base + i * 2);
                compile_expr(n->dict_values[i], base + i * 2 + 1);
            }
            int idx = bc_emit(compiling_bc, OP_DICT_LITERAL, n->dict_count, 0);
            compiling_bc->code[idx].r0 = reg;
            compiling_bc->code[idx].r1 = base;
            next_free_reg = reg_restore(base); // Restore pool
            break;
        }
        case NODE_LAMBDA: {
            FuncEntry *fe = func_table_add(n->name);
            if (fe->body) bc_free(fe->body);
            if (fe->specs) {
                for (int i=0; i<fe->spec_count; i++) {
                    if (fe->specs[i].jit_code && fe->specs[i].jit_code != (void*)1) {
                        munmap(fe->specs[i].jit_code, fe->specs[i].jit_size);
                    }
                }
                free(fe->specs);
                fe->specs = NULL;
                fe->spec_count = 0;
                fe->spec_capacity = 0;
            }
            if (fe->local_names) free(fe->local_names);
            if (fe->local_regs) free(fe->local_regs);
            fe->local_names = NULL; fe->local_regs = NULL; fe->local_count = 0; fe->local_capacity = 0; fe->reg_floor = 0;
            fe->param_count = n->func_param_count;
            fe->params = n->func_params;
            fe->body = bc_new(fe);
            Bytecode *p = compiling_bc; compiling_bc = fe->body; FuncEntry *pf = compiling_func; compiling_func = fe;
            push_outer(pf);
            int old_free = next_free_reg;
            next_free_reg = 0; /* parameters occupy registers 0..n-1 */
            for (int i = 0; i < fe->param_count; i++) {
                func_add_local(fe, n->func_params[i]);
            }
            fe->body->preserved_locals = fe->local_count;
            hoist_locals(fe, n->left);
            next_free_reg = fe->local_count;
            int saved_peak = reg_peak; reg_peak = next_free_reg;
            compile_stmt(n->left);
            bc_emit(compiling_bc, OP_RETURN, 0, 0);
            note_reg_peak();
            compiling_bc->local_count = reg_peak > fe->reg_floor ? reg_peak : fe->reg_floor;
            compiling_bc = p; compiling_func = pf; next_free_reg = old_free; reg_peak = saved_peak;
            pop_outer();

            int idx = bc_emit(compiling_bc, OP_LAMBDA, 0, (intptr_t)n->name);
            compiling_bc->code[idx].r0 = reg;
            compiling_bc->code[idx].cache[0].key = compiling_func;
            break;
        }
        default: break;
    }
    return reg;
}

void compile(Node *n) {
    compile_stmt(n);
}

void yyerror(const char *s) {
    /* Recorded too, for the LSP's diagnostics and the REPL (they used to
       see no errors at all). */
    glow_add_error(line_num, s);
    fprintf(stderr, "\033[31mSyntaxError:\033[0m %s\n\n", s);
    if (compiling_file) {
        print_error_line(compiling_file, line_num);
        fprintf(stderr, "\n  in %s:%d\n\n", compiling_file, line_num);
    } else {
        fprintf(stderr, "  at line %d\n\n", line_num);
    }
}

