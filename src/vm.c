#include "glowlang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif
#include <math.h>
#include <sqlite3.h>
#include "jit.h"
#include "opt.h"

#define MAX_CALL_STACK 65536

// VM Thread local Stack and frames
__thread Value *vm_stack = NULL;
__thread int vm_stack_size = 0;
__thread int sp = 0;
__thread int vm_stack_top = 0;

__thread CallFrame call_stack[65536];
__thread int call_stack_ptr = 0;

pthread_mutex_t glow_gil = PTHREAD_MUTEX_INITIALIZER;
__thread Value pending_exception = 0;
__thread int has_exception = 0;
__thread int runtime_error_thrown = 0;

__thread int profiling_active = 0;
__thread int trace_mode = 0;
__thread int call_depth = 0;
int next_free_reg = 0;
__thread char *exception_stack_trace = NULL;
__thread const char *error_file = NULL;
__thread int error_line = 0;



void call_stack_push(FuncEntry *fe, Instruction *pc) {
    if (call_stack_ptr < MAX_CALL_STACK) {
        call_stack[call_stack_ptr].fe = fe;
        call_stack[call_stack_ptr].pc = pc;
        call_stack_ptr++;
    }
}

void call_stack_pop(void) {
    if (call_stack_ptr > 0) call_stack_ptr--;
}

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>

static int string_compare_simd(const char *s1, const char *s2) {
    if (s1 == s2) return 1;
    if (!s1 || !s2) return 0;
    
    // For simplicity, we compare in 32-byte chunks if they are long enough
    size_t l1 = strlen(s1);
    size_t l2 = strlen(s2);
    if (l1 != l2) return 0;
    
    size_t i = 0;
    for (; i + 32 <= l1; i += 32) {
        __m256i v1 = _mm256_loadu_si256((__m256i*)&s1[i]);
        __m256i v2 = _mm256_loadu_si256((__m256i*)&s2[i]);
        __m256i cmp = _mm256_cmpeq_epi8(v1, v2);
        unsigned int mask = _mm256_movemask_epi8(cmp);
        if (mask != 0xFFFFFFFF) return 0;
    }
    return strcmp(s1 + i, s2 + i) == 0;
}
#else
static int string_compare_simd(const char *s1, const char *s2) {
    if (s1 == s2) return 1;
    if (!s1 || !s2) return 0;
    return strcmp(s1, s2) == 0;
}
#endif

int val_eq(Value a, Value b) {
    if (a == b) return 1;
    if (((a) & MASK_NAN) != MASK_NAN && ((b) & MASK_NAN) != MASK_NAN) return AS_FLOAT(a) == AS_FLOAT(b);
    uint64_t ta = ((a) & MASK_NAN) != MASK_NAN ? VAL_FLOAT : ((a) & MASK_TYPE) >> 48;
    uint64_t tb = ((b) & MASK_NAN) != MASK_NAN ? VAL_FLOAT : ((b) & MASK_TYPE) >> 48;
    if (ta == VAL_INT && tb == VAL_FLOAT) return (double)AS_INT(a) == AS_FLOAT(b);
    if (ta == VAL_FLOAT && tb == VAL_INT) return AS_FLOAT(a) == (double)AS_INT(b);
    if (ta == VAL_STRING && tb == VAL_STRING) return string_compare_simd(AS_STRING(a), AS_STRING(b));
    return 0;
}



int value_truthy(Value v) {
    if ((v & MASK_NAN) != MASK_NAN) return AS_FLOAT(v) != 0.0;
    uint64_t type = (v & MASK_TYPE) >> 48;
    if (type == VAL_INT) return AS_INT(v) != 0;
    if (type == VAL_BOOL) return AS_BOOL(v);
    if (type == VAL_STRING) return AS_STRING(v) && AS_STRING(v)[0] != '\0';
    if (type == VAL_LIST) return AS_LIST(v) && AS_LIST(v)->size > 0;
    return 0;
}


pthread_mutex_t gc_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t threads_mutex = PTHREAD_MUTEX_INITIALIZER;
ThreadStack active_stacks[128];
int active_stack_count = 0;

void print_error_trace(Value exception);
void vm_stack_init(void) {
    vm_stack_size = 2 * 1024 * 1024; // 16MB if Value is 8 bytes
    vm_stack = calloc(vm_stack_size, sizeof(Value));
    sp = 0;
    vm_stack_top = 0;
    pthread_mutex_lock(&threads_mutex);
    if (active_stack_count < 128) {
        active_stacks[active_stack_count].vm_stack_ptr = &vm_stack;
        active_stacks[active_stack_count].sp_ptr = &sp;
        active_stacks[active_stack_count].top_ptr = &vm_stack_top;
        active_stacks[active_stack_count].pending_exc_ptr = &pending_exception;
        active_stack_count++;
    }
    pthread_mutex_unlock(&threads_mutex);
}

void vm_stack_cleanup(void) {
    pthread_mutex_lock(&threads_mutex);
    for (int i = 0; i < active_stack_count; i++) {
        if (active_stacks[i].vm_stack_ptr == &vm_stack) {
            active_stacks[i] = active_stacks[active_stack_count - 1];
            active_stack_count--;
            break;
        }
    }
    pthread_mutex_unlock(&threads_mutex);
    free(vm_stack);
    vm_stack = NULL;
}

static void* thread_wrapper(void *arg) {
    ThreadData *td = (ThreadData*)arg;
    pthread_mutex_lock(&glow_gil);
    vm_stack_init();
    
    // Push arguments onto the new thread's vm_stack
    if (GET_TYPE(td->callee) == VAL_OBJECT && (td->callee & MASK_PAYLOAD) && GET_OBJ_TYPE(td->callee) == OBJ_BOUND_METHOD) {
        BoundMethod *bm = AS_BOUND_METHOD(td->callee);
        vm_stack[0] = bm->instance;
        for (int i = 0; i < td->argc; i++) {
            vm_stack[i + 1] = td->args[i];
        }
    } else {
        for (int i = 0; i < td->argc; i++) {
            vm_stack[i] = td->args[i];
        }
    }

    if (GET_TYPE(td->callee) == VAL_OBJECT && (td->callee & MASK_PAYLOAD)) {
        ObjType ot = GET_OBJ_TYPE(td->callee);
        if (ot == OBJ_NATIVE_FUNC) { 
            AS_NATIVE_FUNC(td->callee)->func(td->argc, td->args); 
        } else if (ot == OBJ_BOUND_METHOD) { 
            BoundMethod *bm = AS_BOUND_METHOD(td->callee); 
            var_set("self", bm->instance);
            if (bm->method && bm->method->body) {
                run_bytecode(bm->method->body, NULL, NULL, 0);
            }
        } else if (ot == OBJ_CLOSURE) {
            Closure *c = AS_CLOSURE(td->callee);
            if (c->body) {
                run_bytecode(c->body, NULL, c, 0);
            }
        }
    }
    
    if (has_exception) {
        print_error_trace(pending_exception);
        runtime_error_thrown = 1;
        has_exception = 0;
    }
    
    free(td->args); free(td); 
    vm_stack_cleanup();
    pthread_mutex_unlock(&glow_gil);
    return NULL;
}


char* format_stack_trace(void) {
    int max_frames = 30;
    int frames_to_show = call_stack_ptr;
    if (frames_to_show > max_frames) frames_to_show = max_frames;
    
    char *buf = malloc((frames_to_show + 3) * 256);
    buf[0] = '\0';
    strcat(buf, "Stack Trace:\n");
    for (int i = call_stack_ptr - 1; i >= call_stack_ptr - frames_to_show; i--) {
        FuncEntry *fe = call_stack[i].fe;
        Instruction *pc = call_stack[i].pc;
        int line = pc ? pc->line : 0;
        const char *file = pc ? pc->file : "<unknown>";
        char frame[256];
        snprintf(frame, 256, "  at %s (%s:%d)\n", fe ? fe->name : "<unknown>", file, line);
        strcat(buf, frame);
    }
    if (call_stack_ptr > max_frames) {
        char extra[128];
        snprintf(extra, 128, "  ... and %d more frames\n", call_stack_ptr - max_frames);
        strcat(buf, extra);
    }
    return buf;
}

void print_stack_trace(void) {
    char *s = format_stack_trace();
    fprintf(stderr, "%s", s);
    free(s);
}

void print_error_line(const char *file, int line) {
    if (!file || line <= 0) return;
    FILE *f = fopen(file, "r");
    if (!f) return;
    char buf[1024];
    int curr = 1;
    while (fgets(buf, 1024, f)) {
        if (curr == line) {
            fprintf(stderr, "  %4d | %s", line, buf);
            break;
        }
        curr++;
    }
    fclose(f);
}

void print_error_trace(Value exception) {
    fprintf(stderr, "Uncaught Exception: ");
    if (exception == 0) { fprintf(stderr, "none"); }
    else if (((exception) & MASK_NAN) != MASK_NAN) { fprintf(stderr, "%g", AS_FLOAT(exception)); }
    else {
        uint64_t type = ((exception) & MASK_TYPE) >> 48;
        if (type == VAL_INT) fprintf(stderr, "%ld", (long)AS_INT(exception));
        else if (type == VAL_BOOL) fprintf(stderr, AS_BOOL(exception) ? "true" : "false");
        else if (type == VAL_STRING) fprintf(stderr, "%s", AS_STRING(exception));
        else if (type == VAL_OBJECT && (exception & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(exception);
            if (ot == OBJ_CLASS) fprintf(stderr, "<class %s>", AS_CLASS(exception)->name);
            else if (ot == OBJ_INSTANCE) fprintf(stderr, "<instance of %s>", AS_INSTANCE(exception)->klass->name);
            else fprintf(stderr, "<object>");
        } else {
            fprintf(stderr, "<unknown value type>");
        }
    }
    fprintf(stderr, "\n\n");
    
    // Print source context for the actual error location
    if (error_file && error_line > 0) {
        print_error_line(error_file, error_line);
    }
    fprintf(stderr, "\n");

    if (exception_stack_trace) {
        fprintf(stderr, "%s", exception_stack_trace);
        free(exception_stack_trace);
        exception_stack_trace = NULL;
    } else {
        print_stack_trace();
    }
}

static const char *op_names[] = {
    "CONST_INT", "CONST_FLOAT", "CONST_STRING", "CONST_BOOL", "LOAD_VAR", "STORE_VAR",
    "ADD", "SUB", "MUL", "DIV", "MOD", "POW", "NEG", "LT", "GT", "LTE", "GTE", "EQ", "NEQ",
    "AND", "OR", "NOT", "JUMP", "JUMP_IF_FALSE", "JUMP_IF_TRUE", "LEN", "RANGE", "TYPE",
    "APPEND", "INPUT", "SHOW", "WRITE", "LIST_LITERAL", "DICT_LITERAL", "INDEX", "SLICE",
    "STORE_INDEX", "FUNC_DEF", "CALL", "CALL_SELF_FAST", "TAIL_CALL", "RETURN",
    "FOR_EACH", "FOR_EACH_PAIR", "STOP", "SKIP", "POP", "PUSH_SCOPE", "POP_SCOPE",
    "STORE_LET", "LOAD_LOCAL", "STORE_LOCAL", "DICT_KEYS", "DICT_VALUES", "DICT_HAS",
    "DICT_REMOVE", "DICT_MERGE", "MATH_ABS", "MATH_MIN", "MATH_MAX", "MATH_ROUND",
    "MATH_FLOOR", "MATH_CEIL", "MATH_SQRT", "STR_UPPER", "STR_LOWER", "STR_TRIM",
    "STR_FIND", "STR_REPLACE", "STR_STARTSWITH", "STR_ENDSWITH", "STR_SPLIT",
    "LIST_PUSH", "LIST_POP", "LIST_INSERT", "LIST_REMOVE", "LIST_CONTAINS", "LIST_SORT",
    "LIST_REVERSE", "FILE_READ", "FILE_WRITE", "FILE_APPEND", "FSTRING", "LIST_COMP",
    "THROW", "TRY_START", "TRY_END", "LAMBDA", "CALL_VALUE", "CLASS_DEF", "METHOD_DEF",
    "TRAIT_DEF", "USE_TRAIT", "GET_PROPERTY", "SET_PROPERTY", "SPAWN", "MOVE", "NOP",
    "TRACE_ENTER", "PACKED_ADD", "PACKED_SUB", "PACKED_MUL", "PACKED_LOAD", "PACKED_STORE",
    "PACKED_FUSED_MUL_ADD", "MATRIX_MUL", "SIMD_ADD", "USE_FILE", "ITER_START", "ITER_NEXT",
    "IN", "ADD_INT", "SUB_INT", "MUL_INT", "LT_INT", "GT_INT", "LTE_INT", "GTE_INT", "EQ_INT",
    "CALL_METHOD"
};

void print_bytecode(Bytecode *bc) {
    if (!bc) return;
    for (int i = 0; i < bc->count; i++) {
        Instruction *pc = &bc->code[i];
        const char *name = (pc->op < sizeof(op_names)/sizeof(op_names[0])) ? op_names[pc->op] : "UNKNOWN";
        printf("%04d: %-15s r0:%d r1:%d r2:%d arg:%d ptr:%p\n", i, name, pc->r0, pc->r1, pc->r2, pc->arg, (void*)pc->ptr);
    }
}

Value run_bytecode(Bytecode *bc, Instruction *start_pc, Closure *current_closure, int base_reg) {
    if (++call_depth > 65000) {
        pending_exception = value_string_copy("Maximum recursion depth exceeded");
        has_exception = 1;
        call_depth--;
        return value_none();
    }

    call_stack_push(bc->owner, start_pc ? start_pc : bc->code);
    int my_frame_idx = call_stack_ptr - 1;

    size_t old_arena_used = stack_arena_used;
    int old_top = vm_stack_top;
    
    // Ensure the entire call frame and its incoming arguments are rooted for the GC
    if (base_reg + bc->local_count + 16 > vm_stack_top) {
        vm_stack_top = base_reg + bc->local_count + 16;
    }

    // static __thread long total_instructions = 0;
    // static __thread Trace *recording_trace = NULL;

    Instruction *pc = start_pc ? start_pc : bc->code;

    if (!start_pc && bc->count > 0 && !bc->jit_code && !bc->jit_failed) {
        bc->code[0].exec_count++;
        if (0) jit_compile(bc);
    }
    if (!start_pc && bc->jit_code) {
        typedef Value (*JITFunc)(Value *R, int *pc_idx);
        int deopt_pc = 0;
        Value res = ((JITFunc)bc->jit_code)(&vm_stack[base_reg], &deopt_pc);
        if (IS_DEOPT(res)) {
            pc = &bc->code[deopt_pc];
            goto l_RESUME_INTERP;
        }
        call_stack_pop();
        vm_stack_top = old_top;
        gc_free_stack_arena_range(old_arena_used, stack_arena_used);
        stack_arena_used = old_arena_used;
        call_depth--;
        return res;
    }

    l_RESUME_INTERP:;
    if (base_reg + bc->local_count >= vm_stack_size) {
        int old_size = vm_stack_size;
        vm_stack_size = (vm_stack_size == 0) ? 4096 : vm_stack_size * 2;
        while (base_reg + bc->local_count >= vm_stack_size) vm_stack_size *= 2;
        vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
        for (int i=old_size; i<vm_stack_size; i++) vm_stack[i] = 0;
    }
    if (base_reg + bc->local_count > vm_stack_top) vm_stack_top = base_reg + bc->local_count;

    // Clear locals but preserve parameters and 'self' (which are the first local_count registers)
    int param_count = (bc->owner) ? bc->owner->param_count : 0;
    bool is_action = (bc->owner && bc->owner->local_count > 0 && bc->owner->local_names && strcmp(bc->owner->local_names[0], "self") == 0);
    int preserved = is_action ? param_count + 1 : param_count;
    
    for (int i = preserved; i < bc->local_count; i++) {
        vm_stack[base_reg + i] = value_none();
    }

    static void* dt[] = {
        &&l_OP_CONST_INT, &&l_OP_CONST_FLOAT, &&l_OP_CONST_STRING, &&l_OP_CONST_BOOL,
        &&l_OP_LOAD_VAR, &&l_OP_STORE_VAR,
        &&l_OP_ADD, &&l_OP_SUB, &&l_OP_MUL, &&l_OP_DIV, &&l_OP_MOD, &&l_OP_POW, &&l_OP_NEG,
        &&l_OP_LT, &&l_OP_GT, &&l_OP_LTE, &&l_OP_GTE, &&l_OP_EQ, &&l_OP_NEQ,
        &&l_OP_AND, &&l_OP_OR, &&l_OP_NOT,
        &&l_OP_JUMP, &&l_OP_JUMP_IF_FALSE, &&l_OP_JUMP_IF_TRUE,
        &&l_OP_LEN, &&l_OP_RANGE, &&l_OP_TYPE, &&l_OP_APPEND, &&l_OP_INPUT, &&l_OP_SHOW, &&l_OP_WRITE,
        &&l_OP_LIST_LITERAL, &&l_OP_DICT_LITERAL,
        &&l_OP_INDEX, &&l_OP_SLICE, &&l_OP_STORE_INDEX,
        &&l_OP_FUNC_DEF, &&l_OP_CALL, &&l_OP_CALL_SELF_FAST, &&l_OP_TAIL_CALL, &&l_OP_RETURN,
        &&l_OP_FOR_EACH, &&l_OP_FOR_EACH_PAIR,
        &&l_OP_STOP, &&l_OP_SKIP,
        &&l_OP_POP,
        &&l_OP_PUSH_SCOPE, &&l_OP_POP_SCOPE, &&l_OP_STORE_LET,
        &&l_OP_LOAD_LOCAL, &&l_OP_STORE_LOCAL,
        &&l_OP_DICT_KEYS, &&l_OP_DICT_VALUES, &&l_OP_DICT_HAS, &&l_OP_DICT_REMOVE, &&l_OP_DICT_MERGE,
        &&l_OP_MATH_ABS, &&l_OP_MATH_MIN, &&l_OP_MATH_MAX, &&l_OP_MATH_ROUND, &&l_OP_MATH_FLOOR, &&l_OP_MATH_CEIL, &&l_OP_MATH_SQRT,
        &&l_OP_STR_UPPER, &&l_OP_STR_LOWER, &&l_OP_STR_TRIM, &&l_OP_STR_FIND, &&l_OP_STR_REPLACE, &&l_OP_STR_STARTSWITH, &&l_OP_STR_ENDSWITH, &&l_OP_STR_SPLIT,
        &&l_OP_LIST_PUSH, &&l_OP_LIST_POP, &&l_OP_LIST_INSERT, &&l_OP_LIST_REMOVE, &&l_OP_LIST_CONTAINS, &&l_OP_LIST_SORT, &&l_OP_LIST_REVERSE,
        &&l_OP_FILE_READ, &&l_OP_FILE_WRITE, &&l_OP_FILE_APPEND,
        &&l_OP_FSTRING,
        &&l_OP_LIST_COMP,
        &&l_OP_THROW,
        &&l_OP_TRY_START, &&l_OP_TRY_END,
        &&l_OP_LAMBDA,
        &&l_OP_CALL_VALUE,
        &&l_OP_CLASS_DEF,
        &&l_OP_METHOD_DEF,
        &&l_OP_TRAIT_DEF,
        &&l_OP_USE_TRAIT,
        &&l_OP_GET_PROPERTY,
        &&l_OP_SET_PROPERTY,
        &&l_OP_SPAWN,
        &&l_OP_MOVE,
        &&l_OP_NOP,
        &&l_OP_TRACE_ENTER,
        &&l_OP_PACKED_ADD,
        &&l_OP_PACKED_SUB,
        &&l_OP_PACKED_MUL,
        &&l_OP_PACKED_LOAD,
        &&l_OP_PACKED_STORE,
        &&l_OP_PACKED_FUSED_MUL_ADD,
        &&l_OP_MATRIX_MUL,
        &&l_OP_SIMD_ADD,
        &&l_OP_USE_FILE,
        &&l_OP_ITER_START,
        &&l_OP_ITER_NEXT,
        &&l_OP_IN,
        &&l_OP_ADD_INT, &&l_OP_SUB_INT, &&l_OP_MUL_INT,
        &&l_OP_LT_INT, &&l_OP_GT_INT, &&l_OP_LTE_INT, &&l_OP_GTE_INT, &&l_OP_EQ_INT,
        &&l_OP_CALL_METHOD
    };

#define R(idx) vm_stack[base_reg + (idx)]
#define DISPATCH() { \
    if (profiling_active) pc->exec_count++; \
    if (trace_mode) { \
        printf("DEBUG %04d: %-15s r0:%d r1:%d r2:%d arg:%d\n", (int)(pc - bc->code), op_names[pc->op], pc->r0, pc->r1, pc->r2, pc->arg); \
        visualize_stack(base_reg, bc->local_count); \
    } \
    if (has_exception) goto l_OP_THROW; \
    goto *dt[pc->op]; \
}
    DISPATCH();

    l_OP_CONST_INT: R(pc->r0) = value_int(pc->arg); pc++; DISPATCH();
    l_OP_CONST_FLOAT: R(pc->r0) = *(Value*)pc->ptr; pc++; DISPATCH();
    l_OP_CONST_STRING: R(pc->r0) = value_string_copy((const char*)pc->ptr); pc++; DISPATCH();
    l_OP_CONST_BOOL: R(pc->r0) = value_bool(pc->arg); pc++; DISPATCH();
    l_OP_LOAD_VAR: {
        int slot = pc->arg;
        if (slot >= global_table.count) { fprintf(stderr, "ERROR: LOAD_VAR slot %d out of bounds (%d)\n", slot, global_table.count); exit(1); }
        
        bool found_in_closure = false;
        if (current_closure) {
            const char *var_name = global_table.names[slot];
            if (var_name) {
                for (int i = 0; i < current_closure->captured_count; i++) {
                    if (strcmp(current_closure->captured_names[i], var_name) == 0) {
                        R(pc->r0) = current_closure->captured_values[i];
                        found_in_closure = true;
                        break;
                    }
                }
            }
        }
        
        if (!found_in_closure) {
            Value v = global_table.values[slot];
            if (GET_TYPE(v) == VAL_NONE) {
                const char *var_name = global_table.names[slot];
                if (var_name) {
                    FuncEntry *fe = func_get(var_name);
                    if (fe) {
                        v = bound_method_new(value_none(), fe);
                    }
                }
            }
            R(pc->r0) = v;
        }
        pc++; DISPATCH();
    }
    l_OP_STORE_VAR: {
        int slot = pc->arg;
        if (slot >= global_table.count) { fprintf(stderr, "ERROR: STORE_VAR slot %d out of bounds (%d)\n", slot, global_table.count); exit(1); }
        
        bool found_in_closure = false;
        if (current_closure) {
            const char *var_name = global_table.names[slot];
            if (var_name) {
                for (int i = 0; i < current_closure->captured_count; i++) {
                    if (strcmp(current_closure->captured_names[i], var_name) == 0) {
                        current_closure->captured_values[i] = R(pc->r0);
                        found_in_closure = true;
                        break;
                    }
                }
            }
        }
        
        if (!found_in_closure) {
            global_table.values[slot] = R(pc->r0);
        }
        pc++; DISPATCH();
    }
    l_OP_MOVE: R(pc->r0) = R(pc->r1); pc++; DISPATCH();
    l_OP_ADD: {
        Value a = R(pc->r1), b = R(pc->r2), res;
        if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) {
            pc->type = 1;
            res = value_int(AS_INT(a) + AS_INT(b));
        } else if (GET_TYPE(a) == VAL_STRING || GET_TYPE(b) == VAL_STRING) {
            Value sa, sb;
            if (GET_TYPE(a) == VAL_STRING) sa = a;
            else { char buf[64]; if(GET_TYPE(a)==VAL_INT) snprintf(buf,64,"%ld",(long)AS_INT(a)); else if(GET_TYPE(a)==VAL_FLOAT) snprintf(buf,64,"%g",AS_FLOAT(a)); else if(GET_TYPE(a)==VAL_BOOL) strcpy(buf, AS_BOOL(a) ? "true" : "false"); else if(GET_TYPE(a)==VAL_NONE) strcpy(buf, "none"); else strcpy(buf, ""); sa = value_string_copy(buf); }
            if (GET_TYPE(b) == VAL_STRING) sb = b;
            else { char buf[64]; if(GET_TYPE(b)==VAL_INT) snprintf(buf,64,"%ld",(long)AS_INT(b)); else if(GET_TYPE(b)==VAL_FLOAT) snprintf(buf,64,"%g",AS_FLOAT(b)); else if(GET_TYPE(b)==VAL_BOOL) strcpy(buf, AS_BOOL(b) ? "true" : "false"); else if(GET_TYPE(b)==VAL_NONE) strcpy(buf, "none"); else strcpy(buf, ""); sb = value_string_copy(buf); }
            res = rope_new(sa, sb);
        } else if (GET_TYPE(a) == VAL_LIST && GET_TYPE(b) == VAL_LIST) {
            pc->type = 5;
            List *la = AS_LIST(a), *lb = AS_LIST(b);
            List *rl = list_new(la->size + lb->size);
            for (int i=0; i<la->size; i++) list_push(rl, la->items[i]);
            for (int i=0; i<lb->size; i++) list_push(rl, lb->items[i]);
            res = value_list(rl);
        } else {
            if (GET_TYPE(a) == VAL_FLOAT || GET_TYPE(b) == VAL_FLOAT) pc->type = 2;
            res = value_from_double(val_to_double(a) + val_to_double(b));
        }
        R(pc->r0) = res; pc++; DISPATCH();
    }
    l_OP_SUB: { Value a = R(pc->r1), b = R(pc->r2); if(GET_TYPE(a)==VAL_INT && GET_TYPE(b)==VAL_INT) { pc->type=1; R(pc->r0) = value_int(AS_INT(a) - AS_INT(b)); } else { pc->type=2; R(pc->r0) = value_from_double(val_to_double(a) - val_to_double(b)); } pc++; DISPATCH(); }
    l_OP_MUL: { Value a = R(pc->r1), b = R(pc->r2); if(GET_TYPE(a)==VAL_INT && GET_TYPE(b)==VAL_INT) { pc->type=1; R(pc->r0) = value_int(AS_INT(a) * AS_INT(b)); } else { pc->type=2; R(pc->r0) = value_from_double(val_to_double(a) * val_to_double(b)); } pc++; DISPATCH(); }
    l_OP_DIV: { 
        Value a = R(pc->r1), b = R(pc->r2);
        if (val_to_double(b) == 0.0) {
            pending_exception = value_string_copy("Division by zero");
            has_exception = 1;
            goto l_OP_THROW;
        }
        if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) {
            pc->type = 1;
            R(pc->r0) = value_int(AS_INT(a) / AS_INT(b));
        } else {
            pc->type = 2;
            R(pc->r0) = value_from_double(val_to_double(a) / val_to_double(b));
        }
        pc++; DISPATCH(); 
    }
    l_OP_LT: { Value a = R(pc->r1), b = R(pc->r2); if(GET_TYPE(a)==VAL_INT && GET_TYPE(b)==VAL_INT) { pc->type=1; R(pc->r0) = value_bool(AS_INT(a) < AS_INT(b)); } else { pc->type=2; R(pc->r0) = value_bool(val_to_double(a) < val_to_double(b)); } pc++; DISPATCH(); }
    l_OP_GT: { Value a = R(pc->r1), b = R(pc->r2); if(GET_TYPE(a)==VAL_INT && GET_TYPE(b)==VAL_INT) { pc->type=1; R(pc->r0) = value_bool(AS_INT(a) > AS_INT(b)); } else { pc->type=2; R(pc->r0) = value_bool(val_to_double(a) > val_to_double(b)); } pc++; DISPATCH(); }
    l_OP_LTE: { Value a = R(pc->r1), b = R(pc->r2); if(GET_TYPE(a)==VAL_INT && GET_TYPE(b)==VAL_INT) { pc->type=1; R(pc->r0) = value_bool(AS_INT(a) <= AS_INT(b)); } else { pc->type=2; R(pc->r0) = value_bool(val_to_double(a) <= val_to_double(b)); } pc++; DISPATCH(); }
    l_OP_GTE: { Value a = R(pc->r1), b = R(pc->r2); if(GET_TYPE(a)==VAL_INT && GET_TYPE(b)==VAL_INT) { pc->type=1; R(pc->r0) = value_bool(AS_INT(a) >= AS_INT(b)); } else { pc->type=2; R(pc->r0) = value_bool(val_to_double(a) >= val_to_double(b)); } pc++; DISPATCH(); }
    l_OP_EQ: { pc->type=1; R(pc->r0) = value_bool(val_eq(R(pc->r1), R(pc->r2))); pc++; DISPATCH(); }
    l_OP_NEQ: R(pc->r0) = value_bool(!val_eq(R(pc->r1), R(pc->r2))); pc++; DISPATCH();
    l_OP_AND: R(pc->r0) = value_bool(value_truthy(R(pc->r1)) && value_truthy(R(pc->r2))); pc++; DISPATCH();
    l_OP_OR: R(pc->r0) = value_bool(value_truthy(R(pc->r1)) || value_truthy(R(pc->r2))); pc++; DISPATCH();
    l_OP_NOT: R(pc->r0) = value_bool(!value_truthy(R(pc->r1))); pc++; DISPATCH();
    l_OP_JUMP: {
        if (pc->arg < 0 || pc->arg >= bc->count) { pending_exception = value_string_copy("JUMP out of bounds"); has_exception = 1; goto l_OP_THROW; }
        pc = bc->code + pc->arg; DISPATCH();
    }
    l_OP_JUMP_IF_FALSE: {
        if(!value_truthy(R(pc->r0))) {
            if (pc->arg < 0 || pc->arg >= bc->count) { pending_exception = value_string_copy("JUMP out of bounds"); has_exception = 1; goto l_OP_THROW; }
            pc = bc->code + pc->arg;
        } else {
            pc++;
        }
        DISPATCH();
    }
    l_OP_JUMP_IF_TRUE: {
        if(value_truthy(R(pc->r0))) {
            if (pc->arg < 0 || pc->arg >= bc->count) { pending_exception = value_string_copy("JUMP out of bounds"); has_exception = 1; goto l_OP_THROW; }
            pc = bc->code + pc->arg;
        } else {
            pc++;
        }
        DISPATCH();
    }
    l_OP_FUNC_DEF: pc++; DISPATCH();
    l_OP_CALL: {
        int ac = pc->arg; const char *n = (const char*)pc->ptr;
        if (!n) {
            pending_exception = value_string_copy("Attempted to call a null function name");
            has_exception = 1; goto l_OP_THROW;
        }
        FuncEntry *fe = func_get(n);
        call_stack[my_frame_idx].pc = pc; // Update current frame PC
        if (fe && fe->body) {
            TypeSignature sig = compute_signature(&vm_stack[base_reg + pc->r2], fe->param_count);
            
            // 1. PIC Lookup
            if (pc->cache[0].key == (void*)sig && pc->cache[0].val) {
                bool cache_valid = false;
                for (int i = 0; i < fe->spec_count; i++) {
                    if (fe->specs[i].jit_code == (void*)pc->cache[0].val) {
                        cache_valid = true;
                        break;
                    }
                }
                if (!cache_valid) {
                    pc->cache[0].key = NULL;
                    pc->cache[0].val = 0;
                } else {
                    typedef Value (*JITFunc)(Value *R, int *pc_idx);
                    JITFunc f = (JITFunc)pc->cache[0].val;
                    int deopt_pc = 0;
                    Value res = f(&vm_stack[base_reg + pc->r2], &deopt_pc);
                    if (IS_DEOPT(res)) {
                        res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, base_reg + pc->r2);
                    }
                    if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
                    pc++; DISPATCH();
                }
            }

            void *jit = func_get_specialization(fe, sig);
            if (jit && jit != (void*)1) {
                // Update Cache
                pc->cache[0].key = (void*)sig;
                pc->cache[0].val = (intptr_t)jit;

                typedef Value (*JITFunc)(Value *R, int *pc_idx);
                JITFunc f = (JITFunc)jit;
                int deopt_pc = 0;
                Value res = f(&vm_stack[base_reg + pc->r2], &deopt_pc);
                if (IS_DEOPT(res)) {
                    res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, base_reg + pc->r2);
                }
                if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
            } else {
                fe->used++;
                if (fe->used > 10 && fe->spec_count < 8) {
                    void *jit_check = func_get_specialization(fe, sig);
                    if (!jit_check) {
                        jit_compile_specialized(fe, sig);
                        if (!func_get_specialization(fe, sig)) {
                            func_add_specialization(fe, sig, (void*)1, 0);
                        }
                    }
                }
                Value res = run_bytecode(fe->body, NULL, NULL, base_reg + pc->r2);
                if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
            }
        } else {
            Value v = var_get(n);
            if (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD)) {
                ObjType ot = GET_OBJ_TYPE(v);
                if (ot == OBJ_CLASS) {
                    Value inst = value_instance(instance_new(AS_CLASS(v)));
                    R(pc->r0) = inst;
                    Value init_method = class_get_method(AS_CLASS(v), "init");
                    if (GET_TYPE(init_method) == VAL_OBJECT && (init_method & MASK_PAYLOAD)) {
                        Value temp[256];
                        int count = (ac > 255) ? 255 : ac;
                        for (int i=0; i<count; i++) temp[i] = R(pc->r2 + i);
                        
                        int callee_base = base_reg + pc->r2;
                        if (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 >= vm_stack_size) {
                            int old_size = vm_stack_size;
                            while (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 >= vm_stack_size) vm_stack_size *= 2;
                            vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
                            for (int i=old_size; i<vm_stack_size; i++) vm_stack[i] = 0;
                        }
                        
                        vm_stack[callee_base] = inst; // self
                        for (int i=0; i<count; i++) vm_stack[callee_base + i + 1] = temp[i];

                        Value old_self = var_get("self");
                        var_set("self", inst);
                        FuncEntry *ife = AS_BOUND_METHOD(init_method)->method;
                        if (ife && ife->body) {
                            run_bytecode(ife->body, NULL, NULL, callee_base);
                        }
                        var_set("self", old_self);
                    }
                }
                else if (ot == OBJ_BOUND_METHOD) {
                    BoundMethod *bm = AS_BOUND_METHOD(v);
                    if (bm->native_method) {
                        Value args[256];
                        args[0] = bm->instance;
                        int count = ac;
                        if (count > 254) count = 254;
                        int start_offset = pc->is_method ? 1 : 0;
                        for (int i = 0; i < count; i++) {
                            args[i + 1] = R(pc->r2 + start_offset + i);
                        }
                        R(pc->r0) = bm->native_method(count + 1, args);
                    } else if (bm->method && bm->method->body) {
                        Value old_self = var_get("self");
                        var_set("self", bm->instance);
                        Value res = run_bytecode(bm->method->body, NULL, NULL, base_reg + pc->r2);
                        var_set("self", old_self);
                        if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
                    }
                }
                else if (ot == OBJ_CLOSURE) {
                    Closure *c = AS_CLOSURE(v);
                    if (c->body) {
                        Value res = run_bytecode(c->body, NULL, c, base_reg + pc->r2);
                        if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
                    } else R(pc->r0) = value_none();
                }
                else if (ot == OBJ_NATIVE_FUNC) {
                    Value *args = &R(pc->r2);
                    int argc = ac;
                    if (pc->is_method) {
                        args++;
                    }
                    R(pc->r0) = AS_NATIVE_FUNC(v)->func(argc, args);
                }
                else R(pc->r0) = value_none();
            } else {
                char msg[256]; snprintf(msg, 256, "Function '%s' not found", n);
                pending_exception = value_string_copy(msg);
                has_exception = 1;
                goto l_OP_THROW;
            }
        }
        pc++; DISPATCH();
    }
    l_OP_CALL_SELF_FAST: {
        // int ac = pc->arg; 
        FuncEntry *fe = bc->owner;
        if (fe && fe->body) {
            TypeSignature sig = compute_signature(&vm_stack[base_reg + pc->r2], fe->param_count);
            void *jit = func_get_specialization(fe, sig);
            if (jit && jit != (void*)1) {
                typedef Value (*JITFunc)(Value *R, int *pc_idx);
                JITFunc f = (JITFunc)jit; int deopt_pc = 0;
                Value res = f(&vm_stack[base_reg + pc->r2], &deopt_pc);
                if (IS_DEOPT(res)) res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, base_reg + pc->r2);
                if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
            } else {
                Value res = run_bytecode(fe->body, NULL, NULL, base_reg + pc->r2);
                if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
            }
        }
        pc++; DISPATCH();
    }
    l_OP_TAIL_CALL: {
        int ac = pc->arg; const char *n = (const char*)pc->ptr;
        FuncEntry *fe = NULL;
        Value callee = 0;
        bool is_bound_method = false;
        Value bound_instance = value_none();
        if (n) fe = func_get(n);
        else {
            callee = R(pc->r1);
            if (GET_TYPE(callee) == VAL_OBJECT && (callee & MASK_PAYLOAD)) {
                ObjType ot = GET_OBJ_TYPE(callee);
                if (ot == OBJ_BOUND_METHOD) {
                    BoundMethod *bm = AS_BOUND_METHOD(callee);
                    if (bm->native_method) {
                        Value args[256];
                        args[0] = bm->instance;
                        int count = ac;
                        if (count > 254) count = 254;
                        int start_offset = pc->is_method ? 1 : 0;
                        for (int i = 0; i < count; i++) {
                            args[i + 1] = R(pc->r2 + start_offset + i);
                        }
                        R(0) = bm->native_method(count + 1, args);
                        goto l_FINISH;
                    } else {
                        fe = bm->method;
                        is_bound_method = true;
                        bound_instance = bm->instance;
                    }
                }
                else if (ot == OBJ_NATIVE_FUNC) {
                    Value *args = &R(pc->r2);
                    int argc = ac;
                    if (pc->is_method) {
                        args++;
                    }
                    R(0) = AS_NATIVE_FUNC(callee)->func(argc, args);
                    goto l_FINISH;
                }
                else if (ot == OBJ_CLASS) {
                    Value inst = value_instance(instance_new(AS_CLASS(callee)));
                    Value init_method = class_get_method(AS_CLASS(callee), "init");
                    if (GET_TYPE(init_method) == VAL_OBJECT && (init_method & MASK_PAYLOAD)) {
                        Value temp[256];
                        int count = (ac > 255) ? 255 : ac;
                        for (int i=0; i<count; i++) temp[i] = R(pc->r2 + i);

                        int callee_base = base_reg + pc->r2;
                        if (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 >= vm_stack_size) {
                            int old_size = vm_stack_size;
                            while (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 >= vm_stack_size) vm_stack_size *= 2;
                            vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
                            for (int i=old_size; i<vm_stack_size; i++) vm_stack[i] = 0;
                        }
                        vm_stack[callee_base] = inst; // self at R0
                        for (int i=0; i<count; i++) vm_stack[callee_base + i + 1] = temp[i];

                        Value old_self = var_get("self");
                        var_set("self", inst);
                        FuncEntry *ife = AS_BOUND_METHOD(init_method)->method;
                        if (ife && ife->body) {
                            run_bytecode(ife->body, NULL, NULL, callee_base);
                        }
                        var_set("self", old_self);
                    }
                    R(0) = inst;
                    goto l_FINISH;
                }
            }
        }
        
        if (fe && fe->body) {
            int needed = fe->param_count;
            if (fe->body->local_count > needed) needed = fe->body->local_count;
            if (base_reg + needed >= vm_stack_size) {
                int old_size = vm_stack_size;
                while (base_reg + needed >= vm_stack_size) vm_stack_size *= 2;
                vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
                for (int i=old_size; i<vm_stack_size; i++) vm_stack[i] = 0;
            }
            call_stack[my_frame_idx].fe = fe;
            call_stack[my_frame_idx].pc = fe->body->code;
            int arg_base = base_reg + pc->r2;
            if (arg_base + ac >= vm_stack_size) {
                 goto l_OP_CALL; 
            }
            if (pc->is_method) {
                vm_stack[base_reg] = vm_stack[arg_base];
                for (int i=0; i<ac && i<fe->param_count; i++) vm_stack[base_reg + i + 1] = vm_stack[arg_base + i + 1];
                for (int i=ac; i<fe->param_count; i++) vm_stack[base_reg + i + 1] = value_none();
            } else if (is_bound_method && GET_TYPE(bound_instance) != VAL_NONE) {
                vm_stack[base_reg] = bound_instance;
                for (int i=0; i<ac && i<fe->param_count; i++) vm_stack[base_reg + i + 1] = vm_stack[arg_base + i];
                for (int i=ac; i<fe->param_count; i++) vm_stack[base_reg + i + 1] = value_none();
            } else {
                for (int i=0; i<ac && i<fe->param_count; i++) vm_stack[base_reg + i] = vm_stack[arg_base + i];
                for (int i=ac; i<fe->param_count; i++) vm_stack[base_reg + i] = value_none();
            }
            bc = fe->body; pc = bc->code;
            gc_free_stack_arena_range(old_arena_used, stack_arena_used);
            stack_arena_used = old_arena_used;
            if (bc->jit_code) {
                typedef Value (*JITFunc)(Value *R, int *pc_idx);
                int deopt_pc = 0;
                Value res = ((JITFunc)bc->jit_code)(&vm_stack[base_reg], &deopt_pc);
                if (IS_DEOPT(res)) { pc = &bc->code[deopt_pc]; goto l_RESUME_INTERP; }
                R(0) = res; goto l_FINISH;
            }
            goto l_RESUME_INTERP;
        }
        goto l_OP_CALL;
    }
    l_OP_RETURN: {
        R(0) = R(pc->r0);
        goto l_FINISH;
    }
    l_OP_POP: pc++; DISPATCH();
    l_OP_SHOW: { int n = pc->arg; int base = pc->r0; for (int i=0; i<n; i++) { print_value(R(base+i)); if(i<n-1) printf(" "); } printf("\n"); pc++; DISPATCH(); }
    l_OP_STORE_LET: {
        int slot = pc->arg;
        global_table.values[slot] = R(pc->r0);
        pc++; DISPATCH();
    }
    l_OP_TRY_START: pc++; DISPATCH();
    l_OP_TRY_END: pc++; DISPATCH();
    l_OP_THROW: {
        Value exc = (has_exception) ? pending_exception : R(pc->r0);
        pending_exception = exc; has_exception = 1;
        call_stack[my_frame_idx].pc = pc; // Record error location
        
        if (!exception_stack_trace) {
            exception_stack_trace = format_stack_trace();
            error_file = pc->file;
            error_line = pc->line;
        }

        int current_pc_idx = (int)(pc - bc->code);
        for (int i = 0; i < bc->exception_count; i++) {
            if (current_pc_idx >= bc->exception_table[i].start_pc && 
                current_pc_idx < bc->exception_table[i].end_pc) {
                R(0) = exc;
                has_exception = 0;
                pending_exception = 0;
                if (exception_stack_trace) { free(exception_stack_trace); exception_stack_trace = NULL; }
                pc = bc->code + bc->exception_table[i].handler_pc;
                DISPATCH();
            }
        }
        goto l_FINISH;
    }
    l_OP_CLASS_DEF: {
        const char *n = (const char*)pc->ptr; Class *p = NULL;
        if (pc->arg == 1) { Value pv = R(pc->r1); if (GET_TYPE(pv) == VAL_OBJECT && (pv & MASK_PAYLOAD)) p = AS_CLASS(pv); }
        Value c = value_class(class_new(n, p)); var_set(n, c); vm_last_class = AS_CLASS(c); pc++; DISPATCH();
    }
    l_OP_METHOD_DEF: {
        FuncEntry *fe = func_get((const char*)pc->ptr);
        const char *mname = fe->name;
        const char *colon = strstr(mname, "::");
        if (colon) mname = colon + 2;
        if(vm_last_class) dict_set(vm_last_class->methods, mname, bound_method_new(value_none(), fe));
        else if(vm_last_trait) dict_set(vm_last_trait->methods, mname, bound_method_new(value_none(), fe));
        pc++; DISPATCH();
    }
    l_OP_GET_PROPERTY: {
        const char *n = (const char*)pc->ptr; Value o = R(pc->r1);
        if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(o);
            if(ot==OBJ_INSTANCE) {
                Instance *inst = AS_INSTANCE(o);
                Dict *d = inst->properties;
                intptr_t idx = pc->cache[0].val - 1;
                Value v;
                if (idx >= 0 && idx < d->capacity && GET_TYPE(d->keys[idx]) == VAL_STRING && pc->cache[0].key == (void*)(uintptr_t)(d->keys[idx] & MASK_PAYLOAD)) {
                    v = d->values[idx];
                } else {
                    int new_idx = dict_get_idx(d, n);
                    if (new_idx != -1) {
                        pc->cache[0].val = new_idx + 1;
                        pc->cache[0].key = (void*)(uintptr_t)(d->keys[new_idx] & MASK_PAYLOAD);
                        v = d->values[new_idx];
                    } else {
                        v = class_get_method(inst->klass, n);
                    }
                }
                if (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD) && GET_OBJ_TYPE(v) == OBJ_BOUND_METHOD && GET_TYPE(AS_BOUND_METHOD(v)->instance) == VAL_NONE) {
                    v = bound_method_new(o, AS_BOUND_METHOD(v)->method);
                }
                R(pc->r0) = v;
            } else R(pc->r0) = value_none();
        } else if(GET_TYPE(o)==VAL_DICT) {
            Value v = dict_get(AS_DICT(o), n);
            if (GET_TYPE(v) == VAL_NONE) {
                if (strcmp(n, "has") == 0) {
                    v = bound_native_method_new(o, native_dict_has);
                } else if (strcmp(n, "keys") == 0) {
                    v = bound_native_method_new(o, native_dict_keys);
                } else if (strcmp(n, "values") == 0) {
                    v = bound_native_method_new(o, native_dict_values);
                } else if (strcmp(n, "remove") == 0) {
                    v = bound_native_method_new(o, native_dict_remove);
                } else if (strcmp(n, "merge") == 0) {
                    v = bound_native_method_new(o, native_dict_merge);
                }
            }
            R(pc->r0) = v;
        } else if(GET_TYPE(o)==VAL_STRING) {
            Value v = value_none();
            if (strcmp(n, "contains") == 0) {
                v = bound_native_method_new(o, native_str_contains);
            } else if (strcmp(n, "find") == 0) {
                v = bound_native_method_new(o, native_str_find);
            } else if (strcmp(n, "slice") == 0) {
                v = bound_native_method_new(o, native_str_slice);
            } else if (strcmp(n, "split") == 0) {
                v = bound_native_method_new(o, native_str_split);
            }
            R(pc->r0) = v;
        }
        else R(pc->r0) = value_none(); pc++; DISPATCH();
    }
    l_OP_SET_PROPERTY: {
        const char *n = (const char*)pc->ptr; Value o = R(pc->r0); Value v = R(pc->r1);
        if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD) && GET_OBJ_TYPE(o)==OBJ_INSTANCE) {
            Instance *inst = AS_INSTANCE(o);
            Dict *d = inst->properties;
            intptr_t idx = pc->cache[0].val - 1;
            if (idx >= 0 && idx < d->capacity && GET_TYPE(d->keys[idx]) == VAL_STRING && pc->cache[0].key == (void*)(uintptr_t)(d->keys[idx] & MASK_PAYLOAD)) {
                d->values[idx] = v;
                gc_write_barrier((GCObject*)inst, v);
            } else {
                int new_idx = dict_get_idx(d, n);
                if (new_idx != -1) {
                    pc->cache[0].val = new_idx + 1;
                    pc->cache[0].key = (void*)(uintptr_t)(d->keys[new_idx] & MASK_PAYLOAD);
                    d->values[new_idx] = v;
                    gc_write_barrier((GCObject*)inst, v);
                } else {
                    instance_set(inst, n, v);
                }
            }
        }
        else if(GET_TYPE(o)==VAL_DICT) dict_set(AS_DICT(o), n, v);
        pc++; DISPATCH();
    }
    l_OP_CALL_VALUE: {
        int ac = pc->arg; Value callee = R(pc->r1);
        call_stack[my_frame_idx].pc = pc; // Update current frame PC
        if(GET_TYPE(callee)==VAL_OBJECT && (callee & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(callee);
            if(ot==OBJ_CLASS) {
                Value inst = value_instance(instance_new(AS_CLASS(callee)));
                R(pc->r0) = inst;
                Value init_method = class_get_method(AS_CLASS(callee), "init");
                if (GET_TYPE(init_method) == VAL_OBJECT && (init_method & MASK_PAYLOAD)) {
                    Value temp[256];
                    int count = (ac > 255) ? 255 : ac;
                    for (int i=0; i<count; i++) temp[i] = R(pc->r2 + i);

                    int callee_base = base_reg + pc->r2;
                    if (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 >= vm_stack_size) {
                        int old_size = vm_stack_size;
                        while (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 >= vm_stack_size) vm_stack_size *= 2;
                        vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
                        for (int i=old_size; i<vm_stack_size; i++) vm_stack[i] = 0;
                    }
                    vm_stack[callee_base] = inst; // self at R0
                    for (int i=0; i<count; i++) vm_stack[callee_base + i + 1] = temp[i];

                    Value old_self = var_get("self");
                    var_set("self", inst);
                    FuncEntry *ife = AS_BOUND_METHOD(init_method)->method;
                    if (ife && ife->body) {
                        run_bytecode(ife->body, NULL, NULL, callee_base);
                    }
                    var_set("self", old_self);
                }
            }
            else if(ot==OBJ_BOUND_METHOD) {
                BoundMethod *bm = AS_BOUND_METHOD(callee);
                if (bm->native_method) {
                    Value args[256];
                    args[0] = bm->instance;
                    int count = ac;
                    if (count > 254) count = 254;
                    int start_offset = pc->is_method ? 1 : 0;
                    for (int i = 0; i < count; i++) {
                        args[i + 1] = R(pc->r2 + start_offset + i);
                    }
                    R(pc->r0) = bm->native_method(count + 1, args);
                } else if(bm->method && bm->method->body) {
                    int callee_base = base_reg + pc->r2;
                    if (callee_base + bm->method->body->local_count + 16 >= vm_stack_size) {
                        int old_size = vm_stack_size;
                        while (callee_base + bm->method->body->local_count + 16 >= vm_stack_size) vm_stack_size *= 2;
                        vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
                        for (int i=old_size; i<vm_stack_size; i++) vm_stack[i] = 0;
                    }
                    if (GET_TYPE(bm->instance) != VAL_NONE) {
                        vm_stack[callee_base] = bm->instance; // self at R0
                    }
                    Value res = run_bytecode(bm->method->body, NULL, NULL, callee_base);
                    if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
                } else R(pc->r0) = value_none();
            } else if(ot==OBJ_CLOSURE) {
                Closure *c = AS_CLOSURE(callee);
                if(c->body) {
                    Value res = run_bytecode(c->body, NULL, c, base_reg + pc->r2);
                    if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
                } else R(pc->r0) = value_none();
            } else if (ot == OBJ_NATIVE_FUNC) {
                Value *args = &R(pc->r2);
                int argc = ac;
                if (pc->is_method) {
                    args++;
                }
                R(pc->r0) = AS_NATIVE_FUNC(callee)->func(argc, args);
            } else {
                pending_exception = value_string_copy("Object is not callable");
                has_exception = 1;
                goto l_OP_THROW;
            }
        }
        pc++; DISPATCH();
    }
    l_OP_CALL_METHOD: {
        int ac = pc->arg; 
        Value obj = R(pc->r1);
        
        FuncEntry *fe = NULL;
        NativeFuncPtr native_fe = NULL;
        
        if(GET_TYPE(obj)==VAL_OBJECT && (obj & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(obj);
            if(ot==OBJ_INSTANCE) {
                Instance *inst = AS_INSTANCE(obj);
                if (pc->cache[0].key == inst->klass) {
                    fe = (FuncEntry*)pc->cache[0].val;
                } else {
                    Value m = class_get_method(inst->klass, (const char*)pc->ptr);
                    if (GET_TYPE(m) == VAL_OBJECT && GET_OBJ_TYPE(m) == OBJ_BOUND_METHOD) {
                        fe = AS_BOUND_METHOD(m)->method;
                        pc->cache[0].key = inst->klass;
                        pc->cache[0].val = (intptr_t)fe;
                    }
                }
            } else if(ot==OBJ_DICT) {
                const char *n = (const char*)pc->ptr;
                if (strcmp(n, "has") == 0) native_fe = native_dict_has;
                else if (strcmp(n, "keys") == 0) native_fe = native_dict_keys;
                else if (strcmp(n, "values") == 0) native_fe = native_dict_values;
                else if (strcmp(n, "remove") == 0) native_fe = native_dict_remove;
                else if (strcmp(n, "merge") == 0) native_fe = native_dict_merge;
            } else if(ot==OBJ_STRING) {
                const char *n = (const char*)pc->ptr;
                if (strcmp(n, "contains") == 0) native_fe = native_str_contains;
                else if (strcmp(n, "find") == 0) native_fe = native_str_find;
                else if (strcmp(n, "slice") == 0) native_fe = native_str_slice;
                else if (strcmp(n, "split") == 0) native_fe = native_str_split;
            }
        }
        
        if (fe && fe->body) {
            int callee_base = base_reg + pc->r2;
            if (callee_base + fe->body->local_count + 16 >= vm_stack_size) {
                int old_size = vm_stack_size;
                while (callee_base + fe->body->local_count + 16 >= vm_stack_size) vm_stack_size *= 2;
                vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
                for (int i=old_size; i<vm_stack_size; i++) vm_stack[i] = 0;
            }
            vm_stack[callee_base] = obj; // SET SELF
            
            Value res = run_bytecode(fe->body, NULL, NULL, callee_base);
            if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
        } else if (native_fe) {
            Value args[257];
            args[0] = obj;
            int count = ac;
            if (count > 254) count = 254;
            int start_offset = pc->is_method ? 1 : 0;
            for (int i = 0; i < count; i++) {
                args[i + 1] = R(pc->r2 + start_offset + i);
            }
            R(pc->r0) = native_fe(count + 1, args);
        } else {
            pending_exception = value_string_copy("Attempted to call a non-method value");
            has_exception = 1;
            goto l_OP_THROW;
        }
        pc++; DISPATCH();
    }
    l_OP_LIST_LITERAL: {
        int n = pc->arg; List *l = pc->is_stack_alloc ? gc_alloc_stack(sizeof(List), OBJ_LIST) : gc_alloc(sizeof(List), OBJ_LIST);
        l->size = 0; l->capacity = n; l->items = n > 0 ? calloc(n, sizeof(Value)) : NULL;
        for (int i=0; i<n; i++) list_push(l, R(pc->r1 + i));
        R(pc->r0) = value_list(l); pc++; DISPATCH();
    }
    l_OP_INDEX: {
        Value coll = R(pc->r1), idx_arg = R(pc->r2);
        if (GET_TYPE(coll) == VAL_LIST) {
            int i = (int)AS_INT(idx_arg); if (i < 0) i += AS_LIST(coll)->size;
            if (i >= 0 && i < AS_LIST(coll)->size) R(pc->r0) = AS_LIST(coll)->items[i];
            else R(pc->r0) = value_none();
        } else if (GET_TYPE(coll) == VAL_OBJECT) {
            if ((coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_PACKED_ARRAY) {
                R(pc->r0) = packed_array_get(AS_PACKED_ARRAY(coll), (int)AS_INT(idx_arg));
            } else R(pc->r0) = value_none();
        } else if (GET_TYPE(coll) == VAL_DICT) {
            Value v = dict_get(AS_DICT(coll), AS_STRING(idx_arg));
            if (GET_TYPE(v) == VAL_NONE && GET_TYPE(idx_arg) == VAL_STRING) {
                const char *mname = AS_STRING(idx_arg);
                if (strcmp(mname, "has") == 0) {
                    v = bound_native_method_new(coll, native_dict_has);
                } else if (strcmp(mname, "keys") == 0) {
                    v = bound_native_method_new(coll, native_dict_keys);
                } else if (strcmp(mname, "values") == 0) {
                    v = bound_native_method_new(coll, native_dict_values);
                } else if (strcmp(mname, "remove") == 0) {
                    v = bound_native_method_new(coll, native_dict_remove);
                } else if (strcmp(mname, "merge") == 0) {
                    v = bound_native_method_new(coll, native_dict_merge);
                }
            } else if (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD)) {
                ObjType ot = GET_OBJ_TYPE(v);
                if (ot == OBJ_NATIVE_FUNC || ot == OBJ_BOUND_METHOD) {
                    // For dicts, we don't bind 'self' unless it's explicitly done.
                    // But we must return the callable.
                }
            }
            R(pc->r0) = v;
        } else if (GET_TYPE(coll) == VAL_STRING) {
            Value v = value_none();
            if (GET_TYPE(idx_arg) == VAL_STRING) {
                const char *mname = AS_STRING(idx_arg);
                if (strcmp(mname, "contains") == 0) {
                    v = bound_native_method_new(coll, native_str_contains);
                } else if (strcmp(mname, "find") == 0) {
                    v = bound_native_method_new(coll, native_str_find);
                } else if (strcmp(mname, "slice") == 0) {
                    v = bound_native_method_new(coll, native_str_slice);
                } else if (strcmp(mname, "split") == 0) {
                    v = bound_native_method_new(coll, native_str_split);
                }
            } else if (GET_TYPE(idx_arg) == VAL_INT || GET_TYPE(idx_arg) == VAL_FLOAT) {
                const char *s = AS_STRING(coll);
                int len = strlen(s);
                int i = (GET_TYPE(idx_arg) == VAL_INT) ? (int)AS_INT(idx_arg) : (int)AS_FLOAT(idx_arg);
                if (i < 0) i += len;
                if (i >= 0 && i < len) {
                    char char_str[2] = { s[i], '\0' };
                    v = value_string_copy(char_str);
                }
            }
            R(pc->r0) = v;
        } else R(pc->r0) = value_none();
        pc++; DISPATCH();
    }
    l_OP_STORE_INDEX: {
        Value coll = R(pc->r0), idx = R(pc->r1), val = R(pc->r2);
        if (GET_TYPE(coll) == VAL_LIST) {
            int i = (int)AS_INT(idx); if (i < 0) i += AS_LIST(coll)->size;
            if (i >= 0 && i < AS_LIST(coll)->size) {
                gc_write_barrier((GCObject*)AS_LIST(coll), val);
                AS_LIST(coll)->items[i] = val;
            }
        } else if (GET_TYPE(coll) == VAL_OBJECT) {
            if ((coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_PACKED_ARRAY) {
                packed_array_set(AS_PACKED_ARRAY(coll), (int)AS_INT(idx), val);
            }
        } else if (GET_TYPE(coll) == VAL_DICT) {
            dict_set(AS_DICT(coll), AS_STRING(idx), val);
        }
        pc++; DISPATCH();
    }
    l_OP_SPAWN: {
        int ac = pc->arg; Value callee = R(pc->r0);
        if (ac < 0 || ac > 1024) {
            pending_exception = value_string_copy("Runtime Error: Invalid number of arguments for OP_SPAWN");
            has_exception = 1;
            goto l_OP_THROW;
        }
        ThreadData *td = malloc(sizeof(ThreadData)); td->callee = callee; td->argc = ac;
        td->args = malloc(sizeof(Value)*ac); for(int i=0; i<ac; i++) td->args[i] = R(pc->r1 + i);
        pthread_t t; pthread_create(&t, NULL, thread_wrapper, td); pthread_detach(t); pc++; DISPATCH();
    }
    l_OP_LEN: { Value v = R(pc->r1); if(GET_TYPE(v)==VAL_STRING) R(pc->r0) = value_int(strlen(AS_STRING(v))); else if(GET_TYPE(v)==VAL_LIST) R(pc->r0) = value_int(AS_LIST(v)->size); else R(pc->r0) = value_int(0); pc++; DISPATCH(); }
    l_OP_RANGE: { int n = (int)AS_INT(R(pc->r1)); List *l = list_new(n); for(int i=0; i<n; i++) list_push(l, value_int(i)); R(pc->r0) = value_list(l); pc++; DISPATCH(); }
    l_OP_APPEND: { Value v = R(pc->r1), l = R(pc->r0); if(GET_TYPE(l)==VAL_LIST) list_push(AS_LIST(l), v); pc++; DISPATCH(); }
    l_OP_TYPE: { 
        Value v = R(pc->r1); int t = GET_TYPE(v); 
        if(t == VAL_FLOAT) R(pc->r0) = value_string_copy("float"); 
        else if(t == VAL_INT) R(pc->r0) = value_string_copy("int"); 
        else if(t == VAL_BOOL) R(pc->r0) = value_string_copy("bool"); 
        else if(t == VAL_STRING) R(pc->r0) = value_string_copy("string"); 
        else if(t == VAL_LIST) R(pc->r0) = value_string_copy("list"); 
        else if(t == VAL_DICT) R(pc->r0) = value_string_copy("dict"); 
        else if(t == VAL_OBJECT) R(pc->r0) = value_string_copy("object"); 
        else R(pc->r0) = value_string_copy("none"); 
        pc++; DISPATCH(); 
    }
    l_OP_MOD: R(pc->r0) = value_from_double(fmod(val_to_double(R(pc->r1)), val_to_double(R(pc->r2)))); pc++; DISPATCH();
    l_OP_POW: R(pc->r0) = value_from_double(pow(val_to_double(R(pc->r1)), val_to_double(R(pc->r2)))); pc++; DISPATCH();
    l_OP_NEG: R(pc->r0) = value_from_double(-val_to_double(R(pc->r1))); pc++; DISPATCH();
    l_OP_STOP: {
        if (pc->arg > 0) {
            pc = bc->code + pc->arg;
        } else {
            goto l_FINISH;
        }
        DISPATCH();
    }
    l_OP_SKIP: {
        if (pc->arg > 0) {
            pc = bc->code + pc->arg;
        } else {
            pc++;
        }
        DISPATCH();
    }
    l_OP_TRAIT_DEF: { const char *n = (const char*)pc->ptr; Value t = value_trait(trait_new(n)); var_set(n, t); vm_last_trait = AS_TRAIT(t); pc++; DISPATCH(); }
    l_OP_USE_TRAIT: { 
        Value tv = var_get((const char*)pc->ptr); 
        if (GET_TYPE(tv) == VAL_OBJECT && (tv & MASK_PAYLOAD) && GET_OBJ_TYPE(tv) == OBJ_TRAIT && vm_last_class) { 
            Trait *t = AS_TRAIT(tv); 
            for (int i = 0; i < t->methods->capacity; i++) {
                if (t->methods->keys[i] != 0 && GET_TYPE(t->methods->keys[i]) != VAL_NONE) {
                    dict_set(vm_last_class->methods, (const char*)(uintptr_t)(t->methods->keys[i] & MASK_PAYLOAD), t->methods->values[i]); 
                }
            }
        } 
        pc++; DISPATCH(); 
    }
    l_OP_INPUT: pc++; DISPATCH();
    l_OP_WRITE: { int n = pc->arg; int base = pc->r0; for (int i=0; i<n; i++) { print_value(R(base+i)); if(i<n-1) printf(" "); } pc++; DISPATCH(); }
    l_OP_DICT_LITERAL: {
        int n = pc->arg; Dict *d = pc->is_stack_alloc ? gc_alloc_stack(sizeof(Dict), OBJ_DICT) : gc_alloc(sizeof(Dict), OBJ_DICT);
        d->capacity = 8; while(d->capacity < n*2) d->capacity *= 2;
        d->keys = calloc(d->capacity, sizeof(Value));
        d->values = calloc(d->capacity, sizeof(Value));
        for (int i=0; i<d->capacity; i++) d->keys[i] = value_none();
        d->size = 0;
        for (int i=0; i<n; i++) {
            Value k = R(pc->r1 + i * 2);
            Value v = R(pc->r1 + i * 2 + 1);
            if (GET_TYPE(k) == VAL_STRING) {
                dict_set(d, AS_STRING(k), v);
            }
        }
        R(pc->r0) = value_dict(d); pc++; DISPATCH();
    }
    l_OP_SLICE: {
        Value obj = R(pc->r1);
        Value startV = R(pc->r2);
        Value endV = R(pc->r2 + 1);
        int slen = 0;
        if (GET_TYPE(obj) == VAL_STRING) {
            slen = strlen(AS_STRING(obj));
        } else if (GET_TYPE(obj) == VAL_LIST) {
            slen = AS_LIST(obj)->size;
        }
        int s = 0;
        if (GET_TYPE(startV) == VAL_INT) {
            int val = (int)AS_INT(startV);
            if (val == -999999) s = 0;
            else if (val < 0) s = val + slen;
            else s = val;
        }
        int e = slen;
        if (GET_TYPE(endV) == VAL_INT) {
            int val = (int)AS_INT(endV);
            if (val == -999999) e = slen;
            else if (val < 0) e = val + slen;
            else e = val;
        }
        if (s < 0) s = 0;
        if (e > slen) e = slen;
        if (s > e) s = e;
        if (GET_TYPE(obj) == VAL_STRING) {
            int rlen = e - s;
            char *sub = malloc(rlen + 1);
            strncpy(sub, AS_STRING(obj) + s, rlen);
            sub[rlen] = 0;
            R(pc->r0) = value_string_copy(sub);
            free(sub);
        } else if (GET_TYPE(obj) == VAL_LIST) {
            List *rl = list_new(e - s);
            for (int i = s; i < e; i++) {
                list_push(rl, AS_LIST(obj)->items[i]);
            }
            R(pc->r0) = value_list(rl);
        } else {
            R(pc->r0) = value_none();
        }
        pc++; DISPATCH();
    }
    l_OP_FOR_EACH: pc++; DISPATCH();
    l_OP_FOR_EACH_PAIR: pc++; DISPATCH();
    l_OP_PUSH_SCOPE: pc++; DISPATCH();
    l_OP_POP_SCOPE: pc++; DISPATCH();
    l_OP_LOAD_LOCAL: pc++; DISPATCH();
    l_OP_STORE_LOCAL: pc++; DISPATCH();
    l_OP_DICT_KEYS: pc++; DISPATCH();
    l_OP_DICT_VALUES: pc++; DISPATCH();
    l_OP_DICT_HAS: pc++; DISPATCH();
    l_OP_DICT_REMOVE: pc++; DISPATCH();
    l_OP_DICT_MERGE: pc++; DISPATCH();
    l_OP_MATH_ABS: pc++; DISPATCH();
    l_OP_MATH_MIN: pc++; DISPATCH();
    l_OP_MATH_MAX: pc++; DISPATCH();
    l_OP_MATH_ROUND: pc++; DISPATCH();
    l_OP_MATH_FLOOR: pc++; DISPATCH();
    l_OP_MATH_CEIL: pc++; DISPATCH();
    l_OP_MATH_SQRT: pc++; DISPATCH();
    l_OP_STR_UPPER: pc++; DISPATCH();
    l_OP_STR_LOWER: pc++; DISPATCH();
    l_OP_STR_TRIM: pc++; DISPATCH();
    l_OP_STR_FIND: pc++; DISPATCH();
    l_OP_STR_REPLACE: pc++; DISPATCH();
    l_OP_STR_STARTSWITH: pc++; DISPATCH();
    l_OP_STR_ENDSWITH: pc++; DISPATCH();
    l_OP_STR_SPLIT: pc++; DISPATCH();
    l_OP_LIST_PUSH: pc++; DISPATCH();
    l_OP_LIST_POP: pc++; DISPATCH();
    l_OP_LIST_INSERT: pc++; DISPATCH();
    l_OP_LIST_REMOVE: pc++; DISPATCH();
    l_OP_LIST_CONTAINS: pc++; DISPATCH();
    l_OP_LIST_SORT: pc++; DISPATCH();
    l_OP_LIST_REVERSE: pc++; DISPATCH();
    l_OP_FILE_READ: pc++; DISPATCH();
    l_OP_FILE_WRITE: pc++; DISPATCH();
    l_OP_FILE_APPEND: pc++; DISPATCH();
    l_OP_FSTRING: pc++; DISPATCH();
    l_OP_LIST_COMP: pc++; DISPATCH();
    l_OP_LAMBDA: {
        FuncEntry *fe = func_get((const char*)pc->ptr);
        Closure *c = pc->is_stack_alloc ? gc_alloc_stack(sizeof(Closure), OBJ_CLOSURE) : gc_alloc(sizeof(Closure), OBJ_CLOSURE);
        c->param_count = fe->param_count; c->params = fe->params; c->body = fe->body;
        FuncEntry *parent_fe = (FuncEntry*)pc->cache[0].key;
        int capture_size = 0;
        if (parent_fe) capture_size += parent_fe->local_count;
        if (current_closure) capture_size += current_closure->captured_count;
        c->captured_count = capture_size;
        if (capture_size > 0) {
            c->captured_names = malloc(sizeof(const char*) * capture_size);
            c->captured_values = malloc(sizeof(Value) * capture_size);
            int idx = 0;
            if (current_closure) {
                for (int i=0; i<current_closure->captured_count; i++) {
                    c->captured_names[idx] = current_closure->captured_names[i];
                    c->captured_values[idx] = current_closure->captured_values[i];
                    idx++;
                }
            }
            if (parent_fe) {
                for (int i=0; i<parent_fe->local_count; i++) {
                    c->captured_names[idx] = parent_fe->local_names[i];
                    c->captured_values[idx] = vm_stack[base_reg + i];
                    idx++;
                }
            }
        } else {
            c->captured_names = NULL;
            c->captured_values = NULL;
        }
        R(pc->r0) = value_closure(c);
        pc++; DISPATCH();
    }

    l_OP_TRACE_ENTER: {
        Trace *t = (Trace*)pc->trace;
        typedef Value (*TraceFunc)(Value *R, int *pc_off);
        int off = 0;
        Value res = ((TraceFunc)t->jit_code)(&vm_stack[base_reg], &off);
        if (IS_TRACE_EXIT(res)) { pc = bc->code + off; goto *dt[pc->op]; }
        pc->op = pc->original_op; goto *dt[pc->op];
    }
    l_OP_USE_FILE: {
        const char *path = (const char*)pc->ptr;
        const char *resolved_path = path;
        FILE *lock = fopen("Glow.lock", "r");
        if (lock) {
            char line[512];
            while (fgets(line, sizeof(line), lock)) {
                char url[256], hash[256], local[256];
                if (sscanf(line, "\"%255[^\"]\" \"%255[^\"]\" \"%255[^\"]\"", url, hash, local) == 3) {
                    if (strcmp(url, path) == 0) {
                        resolved_path = intern_string(local);
                        break;
                    }
                }
            }
            fclose(lock);
        }
        if (resolved_path == path) {
            FILE *deps = fopen("Glow.deps", "r");
            if (deps) {
                char line[512];
                while (fgets(line, sizeof(line), deps)) {
                    char url[256], local[256];
                    if (sscanf(line, "\"%255[^\"]\" \"%255[^\"]\"", url, local) == 2) {
                        if (strcmp(url, path) == 0) {
                            resolved_path = intern_string(local);
                            break;
                        }
                    }
                }
                fclose(deps);
            }
        }
        FILE *f = fopen(resolved_path, "r");
        if (!f) {
            char lib_path[512];
            snprintf(lib_path, sizeof(lib_path), "lib/%s", resolved_path);
            f = fopen(lib_path, "r");
            if (f) {
                resolved_path = intern_string(lib_path);
            }
        }
        if (!f) {
            char err[512]; snprintf(err, sizeof(err), "Could not open file: %s", path);
            pending_exception = value_string_copy(err); has_exception = 1; goto l_OP_THROW;
        }
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz < 0) { fclose(f); pc++; DISPATCH(); }
        char *buf = malloc(sz + 1); 
        size_t nread = fread(buf, 1, sz, f); buf[nread] = '\0'; fclose(f);

        Node *old_root = root;
        const char *old_file = compiling_file;
        Bytecode *old_bc = compiling_bc;
        int old_reg = next_free_reg;
        FuncEntry *old_func = compiling_func;
        Class *old_class = last_compiling_class; Trait *old_trait = last_compiling_trait;

        compiling_file = resolved_path;
        int old_line = line_num; line_num = 1;
        YY_BUFFER_STATE old_bp = get_current_buffer();
        YY_BUFFER_STATE bp = yy_scan_string(buf);
        root = NULL; int parse_res = yyparse();
        yy_delete_buffer(bp);
        if (old_bp) yy_switch_to_buffer(old_bp);
        line_num = old_line;
        if (parse_res != 0) {
            char err[512]; snprintf(err, sizeof(err), "Failed to load module: %s (syntax error)", path);
            pending_exception = value_string_copy(err); has_exception = 1;
            free(buf); goto l_OP_THROW;
        }
        if (root) {
            FuncEntry *fe = arena_alloc(sizeof(FuncEntry)); memset(fe, 0, sizeof(FuncEntry));
            fe->name = "use_file";
            Bytecode *use_bc = bc_new(fe); compiling_bc = use_bc; next_free_reg = 0;
            last_compiling_class = NULL; last_compiling_trait = NULL;
            compiling_func = fe;
            compile(root); bc_emit(use_bc, OP_RETURN, 0, 0);
            run_bytecode(use_bc, NULL, NULL, vm_stack_top);
            bc_free(use_bc);
        }
        root = old_root; compiling_file = old_file; compiling_bc = old_bc; 
        next_free_reg = old_reg; compiling_func = old_func;
        last_compiling_class = old_class; last_compiling_trait = old_trait;
        free(buf); 
        if (has_exception) goto l_OP_THROW;
        pc++; DISPATCH();
    }
    l_OP_PACKED_ADD: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (GET_TYPE(a) == VAL_OBJECT && GET_OBJ_TYPE(a) == OBJ_PACKED_ARRAY &&
            GET_TYPE(b) == VAL_OBJECT && GET_OBJ_TYPE(b) == OBJ_PACKED_ARRAY) {
            PackedArray *pa = AS_PACKED_ARRAY(a), *pb = AS_PACKED_ARRAY(b);
            if (pa->type == pb->type && pa->size == pb->size) {
                PackedArray *res = packed_array_new(pa->type, pa->size);
                if (pa->type == PACKED_FLOAT64) {
                    double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
                    for (int i=0; i<pa->size; i++) dr[i] = da[i] + db[i];
                } else {
                    int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
                    for (int i=0; i<pa->size; i++) ir[i] = ia[i] + ib[i];
                }
                R(pc->r0) = value_packed(res);
            } else R(pc->r0) = value_none();
        } else R(pc->r0) = value_none();
        pc++; DISPATCH();
    }
    l_OP_PACKED_SUB: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (GET_TYPE(a) == VAL_OBJECT && GET_OBJ_TYPE(a) == OBJ_PACKED_ARRAY &&
            GET_TYPE(b) == VAL_OBJECT && GET_OBJ_TYPE(b) == OBJ_PACKED_ARRAY) {
            PackedArray *pa = AS_PACKED_ARRAY(a), *pb = AS_PACKED_ARRAY(b);
            if (pa->type == pb->type && pa->size == pb->size) {
                PackedArray *res = packed_array_new(pa->type, pa->size);
                if (pa->type == PACKED_FLOAT64) {
                    double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
                    for (int i=0; i<pa->size; i++) dr[i] = da[i] - db[i];
                } else {
                    int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
                    for (int i=0; i<pa->size; i++) ir[i] = ia[i] - ib[i];
                }
                R(pc->r0) = value_packed(res);
            } else R(pc->r0) = value_none();
        } else R(pc->r0) = value_none();
        pc++; DISPATCH();
    }
    l_OP_SIMD_ADD: {
        Value a = R(pc->r1), b = R(pc->r2), target = R(pc->r0);
        simd_add_helper(a, b, target);
        pc++; DISPATCH();
    }
    l_OP_PACKED_MUL:
    l_OP_PACKED_LOAD:
    l_OP_PACKED_STORE:
    l_OP_PACKED_FUSED_MUL_ADD:
    l_OP_MATRIX_MUL:
        pc++; DISPATCH();
    l_OP_ITER_START: {
        Value coll = R(pc->r1);
        if (GET_TYPE(coll) == VAL_OBJECT && (coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_SQLITE_ITER) {
            R(pc->r0) = coll;
        } else {
            Iterator *it = gc_alloc(sizeof(Iterator), OBJ_ITERATOR);
            it->iterable = coll;
            it->index = 0;
            R(pc->r0) = make_value(VAL_OBJECT, (uintptr_t)it);
        }
        pc++; DISPATCH();
    }
    l_OP_ITER_NEXT: {
        Value vit = R(pc->r1);
        if (GET_TYPE(vit) != VAL_OBJECT || !(vit & MASK_PAYLOAD)) { pc = bc->code + pc->arg; DISPATCH(); }
        GCObject *obj = (GCObject*)(uintptr_t)(vit & MASK_PAYLOAD);
        
        if (obj->type == OBJ_ITERATOR) {
            Iterator *it = (Iterator*)obj;
            Value coll = it->iterable;
            int type = GET_TYPE(coll);
            if (type == VAL_LIST) {
                 List *l = AS_LIST(coll);
                 if (it->index < l->size) {
                     R(pc->r0) = l->items[it->index++];
                     pc++; DISPATCH();
                 }
            } else if (type == VAL_DICT) {
                 Dict *d = AS_DICT(coll);
                 while (it->index < d->capacity && (d->keys[it->index] == 0 || GET_TYPE(d->keys[it->index]) == VAL_NONE)) it->index++;
                 if (it->index < d->capacity) {
                     R(pc->r0) = value_string_copy((const char*)(uintptr_t)(d->keys[it->index++] & MASK_PAYLOAD));
                     pc++; DISPATCH();
                 }
            } else if (type == VAL_STRING) {
                 const char *s = AS_STRING(coll);
                 int len = strlen(s);
                 if (it->index < len) {
                     char buf[2] = { s[it->index++], '\0' };
                     R(pc->r0) = value_string_copy(buf);
                     pc++; DISPATCH();
                 }
            }
        } else if (obj->type == OBJ_SQLITE_ITER) {
            SQLiteIter *it = (SQLiteIter*)obj;
            if (!it->done) {
                int rc = sqlite3_step(it->stmt);
                if (rc == SQLITE_ROW) {
                    int cols = sqlite3_column_count(it->stmt);
                    Dict *row = dict_new(cols);
                    for (int i = 0; i < cols; i++) {
                        const char *name = sqlite3_column_name(it->stmt, i);
                        int type = sqlite3_column_type(it->stmt, i);
                        Value v;
                        if (type == SQLITE_INTEGER) v = value_int(sqlite3_column_int64(it->stmt, i));
                        else if (type == SQLITE_FLOAT) v = value_float(sqlite3_column_double(it->stmt, i));
                        else if (type == SQLITE_TEXT) v = value_string_copy((const char*)sqlite3_column_text(it->stmt, i));
                        else v = value_none();
                        dict_set(row, name, v);
                    }
                    R(pc->r0) = value_dict(row);
                    pc++; DISPATCH();
                } else {
                    it->done = 1;
                    sqlite3_finalize(it->stmt);
                    it->stmt = NULL;
                }
            }
        }
        
        pc = bc->code + pc->arg; DISPATCH();
    }
    l_OP_IN: {
        Value elem = R(pc->r1);
        Value coll = R(pc->r2);
        int found = 0;
        if (GET_TYPE(coll) == VAL_LIST) {
            List *l = AS_LIST(coll);
            for (int i = 0; i < l->size; i++) {
                if (val_eq(l->items[i], elem)) {
                    found = 1;
                    break;
                }
            }
        } else if (GET_TYPE(coll) == VAL_DICT) {
            if (GET_TYPE(elem) == VAL_STRING) {
                Value val = dict_get(AS_DICT(coll), AS_STRING(elem));
                if (GET_TYPE(val) != VAL_NONE) {
                    found = 1;
                }
            }
        } else if (GET_TYPE(coll) == VAL_STRING) {
            if (GET_TYPE(elem) == VAL_STRING) {
                const char *s = AS_STRING(coll);
                const char *sub = AS_STRING(elem);
                if (strstr(s, sub) != NULL) {
                    found = 1;
                }
            }
        }
        R(pc->r0) = value_bool(found);
        pc++; DISPATCH();
    }
    
    l_OP_ADD_INT: R(pc->r0) = value_int(AS_INT(R(pc->r1)) + AS_INT(R(pc->r2))); pc++; DISPATCH();
    l_OP_SUB_INT: R(pc->r0) = value_int(AS_INT(R(pc->r1)) - AS_INT(R(pc->r2))); pc++; DISPATCH();
    l_OP_MUL_INT: R(pc->r0) = value_int(AS_INT(R(pc->r1)) * AS_INT(R(pc->r2))); pc++; DISPATCH();
    l_OP_LT_INT: R(pc->r0) = value_bool(AS_INT(R(pc->r1)) < AS_INT(R(pc->r2))); pc++; DISPATCH();
    l_OP_GT_INT: R(pc->r0) = value_bool(AS_INT(R(pc->r1)) > AS_INT(R(pc->r2))); pc++; DISPATCH();
    l_OP_LTE_INT: R(pc->r0) = value_bool(AS_INT(R(pc->r1)) <= AS_INT(R(pc->r2))); pc++; DISPATCH();
    l_OP_GTE_INT: R(pc->r0) = value_bool(AS_INT(R(pc->r1)) >= AS_INT(R(pc->r2))); pc++; DISPATCH();
    l_OP_EQ_INT: R(pc->r0) = value_bool(AS_INT(R(pc->r1)) == AS_INT(R(pc->r2))); pc++; DISPATCH();
    l_OP_NOP: pc++; DISPATCH();

l_FINISH:
    if (has_exception && call_depth == 1) {
        print_error_trace(pending_exception);
        runtime_error_thrown = 1;
        has_exception = 0; // Prevent double report
    }
    call_stack_pop();
    vm_stack_top = old_top;
    gc_free_stack_arena_range(old_arena_used, stack_arena_used);
    stack_arena_used = old_arena_used;
    call_depth--;
    return (has_exception) ? value_none() : R(0);
}


