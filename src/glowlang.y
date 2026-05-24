%code requires {
    #include "glowlang.h"
}

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include "glowlang.h"

/* Extern from lexer */
extern const char *intern_string(const char *s);
extern void intern_cleanup(void);
extern void intern_table_init(void);
extern int line_num;
extern int yylex(void);
extern FILE *yyin;
void yyerror(const char *s);

/* ================================================================
 *  BYTECODE
 * ================================================================ */
#include <sys/mman.h>
#include <unistd.h>

#define JIT_THRESHOLD 500

static void gc_mark_value(Value v);
static void gc_mark_object(GCObject *obj);
static void gc_collect(void);
static void* gc_alloc(size_t sz, ObjType type);
Value value_string(const char *s);

/* Let shadow: saves old values when let shadows a variable */
typedef struct LetShadow {
    const char *name;
    Value old_value;
    int had_old;  /* was there an old value? */
    struct LetShadow *next;
} LetShadow;

static __thread LetShadow *let_shadow_list = NULL;

pthread_mutex_t glow_gil = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    Value callee;
    int argc;
    Value *args;
} ThreadData;

/* Call frame for iterative VM (no C recursion) */
typedef struct {
    Bytecode *bc;
    int ip;
    int base_sp;
    int old_has_return;
    Value old_return_value;
    FuncEntry *pending_fn;
    Closure *pending_closure;
    LetShadow *old_shadow_list;
    const char *p0, *p1;
    Value v0, v1;
    int param_count;
    Value inline_locals[16];
    Value *local_values;
    int local_count;
} CallFrame;

#define CALL_STACK_SIZE 4194304
static __thread CallFrame call_stack[CALL_STACK_SIZE];
static __thread int csp = 0;

static __thread Value *vm_stack = NULL;
static __thread int vm_stack_size = 0;
static __thread int sp = 0;

typedef struct {
    int catch_ip;
    int csp;
    int sp;
    LetShadow *shadow_list;
} TryHandler;

#define HANDLER_STACK_SIZE 1024
static __thread TryHandler handler_stack[HANDLER_STACK_SIZE];
static __thread int hsp = 0;
static __thread int vm_exception_active = 0;
static __thread Value vm_exception_value;

static long total_calls = 0;
static long total_returns = 0;




/* ================================================================ */
/*                        VM VALUE HELPERS                          */
/* ================================================================ */

static int value_truthy(Value v);
static Dict *dict_grow(Dict *d, int new_cap);

/* Convert any value to int (truthiness) */
static int val_to_int(Value v) {
    switch (GET_TYPE(v)) {
        case VAL_INT:    return AS_INT(v);
        case VAL_FLOAT:  return (int)AS_FLOAT(v);
        case VAL_BOOL:   return AS_BOOL(v);
        case VAL_STRING: return AS_STRING(v) && AS_STRING(v)[0] ? 1 : 0;
        case VAL_LIST:   return AS_LIST(v) && AS_LIST(v)->size > 0 ? 1 : 0;
        case VAL_DICT:   return AS_DICT(v) && AS_DICT(v)->size > 0 ? 1 : 0;
        default: return 0;
    }
}

/* Convert any value to double (for math) */
static double val_to_int_or_float(Value v) {
    if (GET_TYPE(v) == VAL_INT) return (double)AS_INT(v);
    if (GET_TYPE(v) == VAL_FLOAT) return AS_FLOAT(v);
    return 0.0;
}

/* List push helper */
static void list_push(List *l, Value v) {
    if (l->size >= l->capacity) {
        l->capacity = (l->capacity == 0) ? 4 : l->capacity * 2;
        l->items = realloc(l->items, sizeof(Value) * l->capacity);
    }
    l->items[l->size++] = v;
}

/* Dict set helper */
static void dict_set(Dict *d, const char *key, Value val) {
    for (int i = 0; i < d->size; i++) {
        if (strcmp(d->keys[i], key) == 0) {
            d->values[i] = val;
            return;
        }
    }
    if (d->size >= d->capacity) {
        dict_grow(d, d->capacity * 2);
    }
    d->keys[d->size] = key;
    d->values[d->size] = val;
    d->size++;
}

/* Convert any value to bool */
static int val_to_bool(Value v) {
    return value_truthy(v);
}

/* Forward declarations (functions call each other recursively) */

static void print_value(Value v);
static Value run_bytecode(Bytecode *bc, int base_offset);
static void jit_compile(Bytecode *bc);
extern void lsp_run_socket(int port);

static void var_set(const char *name, Value value);
static void bc_free(Bytecode *bc);
static void dict_set(Dict *d, const char *key, Value val);
static Shape *shape_new(Shape *parent, const char *name, int offset);

static Shape *shape_get_next(Shape *current, const char *name) {
    for (int i = 0; i < current->transitions->size; i++) {
        if (strcmp(current->transitions->keys[i], name) == 0) {
            return (Shape*)AS_BOUND_METHOD(current->transitions->values[i]); // AS_BOUND_METHOD is just casting MASK_PAYLOAD
        }
    }
    Shape *next = shape_new(current, name, current->offset + 1);
    dict_set(current->transitions, name, value_obj(next));
    return next;
}

static GCObject *gc_objects = NULL;
static size_t gc_allocated = 0;
static size_t gc_threshold = 1024 * 1024; /* 1MB initial threshold */

/* ================================================================
 *  NODE TYPES
 * ================================================================ */

/* ================================================================
 *  VARIABLE STORAGE WITH LET SHADOW SUPPORT
 * ================================================================ */

/* Simple hash table for variables */
#define VAR_TABLE_SIZE 1024
typedef struct { const char *name; Value value; int used; } VarEntry;
static VarEntry var_table[VAR_TABLE_SIZE];
static int var_count = 0;

/* Let shadow: saves old values when let shadows a variable */
static int let_shadow_count = 0;

static void var_table_init(void) {
    var_count = 0;
    let_shadow_list = NULL;
    let_shadow_count = 0;
    hsp = 0;
    vm_exception_active = 0;
    memset(var_table, 0, sizeof(var_table));
}

static unsigned int hash_str(const char *s) {
    unsigned int h = 5381;
    while (*s) h = ((h << 5) + h) + (unsigned char)*s++;
    return h;
}

/* Set variable (updates existing or creates new) */
static void var_set(const char *name, Value value) {
    unsigned int h = hash_str(name) % VAR_TABLE_SIZE;
    unsigned int idx = h;
    unsigned int start = idx;
    do {
        if (!var_table[idx].used) {
            var_table[idx].name = name;
            var_table[idx].value = value;
            var_table[idx].used = 1;
            var_count++;
            return;
        }
        if (strcmp(var_table[idx].name, name) == 0) {
            var_table[idx].value = value;
            return;
        }
        idx = (idx + 1) % VAR_TABLE_SIZE;
    } while (idx != start);
}

/* let: save old value to shadow list, then set */
static void var_set_let(const char *name, Value value) {
    /* Check if already in shadow list (don't double-save) */
    for (LetShadow *s = let_shadow_list; s; s = s->next) {
        if (strcmp(s->name, name) == 0) {
            var_set(name, value);
            return;
        }
    }
    /* Save old value */
    unsigned int h = hash_str(name) % VAR_TABLE_SIZE;
    unsigned int idx = h;
    unsigned int start = idx;
    int had_old = 0;
    Value old_value = value_int(0);
    do {
        if (var_table[idx].used && strcmp(var_table[idx].name, name) == 0) {
            old_value = var_table[idx].value;
            had_old = 1;
            break;
        }
        idx = (idx + 1) % VAR_TABLE_SIZE;
        if (idx == start) break;
    } while (1);
    /* Add to shadow list */
    LetShadow *s = malloc(sizeof(LetShadow));
    s->name = name;
    s->old_value = old_value;
    s->had_old = had_old;
    s->next = let_shadow_list;
    let_shadow_list = s;
    let_shadow_count++;
    /* Set new value */
    var_set(name, value);
}

/* Get variable */
static Value var_get(const char *name) {
    unsigned int h = hash_str(name) % VAR_TABLE_SIZE;
    unsigned int idx = h;
    unsigned int start = idx;
    do {
        if (var_table[idx].used && strcmp(var_table[idx].name, name) == 0) {
            return var_table[idx].value;
        }
        idx = (idx + 1) % VAR_TABLE_SIZE;
        if (idx == start) break;
    } while (1);
    Value v = value_int(0);
    return v;
}

/* Scope management */
static void var_push_scope(void) {
    /* No-op: scope is handled via let shadow list */
}

static void var_pop_scope(void) {
    /* Pop scope is a no-op now; use var_restore_shadow instead */
}

/* Restore shadow list to a saved state (used on function return) */
static void var_restore_shadow(LetShadow *target_list) {
    if (let_shadow_list == target_list) return;  /* nothing to restore */
    while (let_shadow_list != target_list) {
        LetShadow *s = let_shadow_list;
        if (s->had_old) {
            var_set(s->name, s->old_value);
        } else {
            /* Remove the variable entirely */
            unsigned int h = hash_str(s->name) % VAR_TABLE_SIZE;
            unsigned int idx = h;
            unsigned int start = idx;
            do {
                if (var_table[idx].used && strcmp(var_table[idx].name, s->name) == 0) {
                    var_table[idx].used = 0;
                    var_count--;
                    break;
                }
                idx = (idx + 1) % VAR_TABLE_SIZE;
                if (idx == start) break;
            } while (1);
        }
        let_shadow_list = s->next;
        let_shadow_count--;
        free(s);
    }
}

static void var_table_free(void) {
    /* Clean up any remaining shadow entries */
    var_pop_scope();
    memset(var_table, 0, sizeof(var_table));
    var_count = 0;
}

/* ================================================================
 *  DYNAMIC HASH TABLE: FUNCTIONS
 * ================================================================ */


/* ================================================================
 *  DYNAMIC AST CONSTRUCTORS
 * ================================================================ */

Node *node_new(NodeType type) {
    Node *n = calloc(1, sizeof(Node));
    n->type = type;
    n->line = line_num;
    return n;
}

static int ast_contains_let(Node *n) {
    if (!n) return 0;
    if (n->type == NODE_LET) return 1;
    if (n->type == NODE_FUNC_DEF) return 0;
    if (n->type == NODE_TRY_CATCH) return ast_contains_let(n->left) || ast_contains_let(n->right) || ast_contains_let(n->third);
    int res = ast_contains_let(n->left) || ast_contains_let(n->right) || ast_contains_let(n->third);
    if (!res && n->elements) for (int i = 0; i < n->element_count; i++) if (ast_contains_let(n->elements[i])) return 1;
    if (!res && n->dict_values) for (int i = 0; i < n->dict_count; i++) if (ast_contains_let(n->dict_values[i])) return 1;
    if (!res && n->call_args) for (int i = 0; i < n->call_arg_count; i++) if (ast_contains_let(n->call_args[i])) return 1;
    if (!res && n->print_items) for (int i = 0; i < n->print_count; i++) if (ast_contains_let(n->print_items[i])) return 1;
    return res;
}

static Node *newIntNode(int64_t v) {
    Node *n = node_new(NODE_INT_LIT); n->value = value_int(v); return n;
}
static Node *newFloatNode(double v) {
    Node *n = node_new(NODE_FLOAT_LIT); n->value = value_float(v); return n;
}
static Node *newBoolNode(int v) {
    Node *n = node_new(NODE_BOOL_LIT); n->value = value_bool(v); return n;
}
static Node *newStringNode(const char *s) {
    Node *n = node_new(NODE_STRING_LIT); n->name = s; return n;
}
static Node *newFStringNode(const char *s) {
    Node *n = node_new(NODE_FSTRING); n->name = s; return n;
}
static Node *newVarNode(const char *s) {
    Node *n = node_new(NODE_VAR); n->name = s; return n;
}
static Node *newAssignNode(const char *s, Node *expr) {
    Node *n = node_new(NODE_ASSIGN); n->name = s; n->left = expr; return n;
}
static Node *newIndexAssignNode(Node *coll, Node *idx, Node *val) {
    Node *n = node_new(NODE_INDEX_ASSIGN); n->left = coll; n->right = idx; n->third = val; return n;
}
static Node *newCompoundNode(NodeType t, const char *s, Node *expr) {
    Node *n = node_new(t); n->name = s; n->left = expr; return n;
}
static Node *newBinOp(NodeType t, Node *l, Node *r) {
    Node *n = node_new(t); n->left = l; n->right = r; return n;
}
static Node *newUnary(NodeType t, Node *l) {
    Node *n = node_new(t); n->left = l; return n;
}
static Node *newIfNode(Node *cond, Node *if_true, Node *if_false) {
    Node *n = node_new(NODE_IF); n->left = cond; n->right = if_true; n->third = if_false; return n;
}
static Node *newWhileNode(Node *cond, Node *body) {
    Node *n = node_new(NODE_WHILE); n->left = cond; n->right = body; return n;
}
static Node *newUntilNode(Node *cond, Node *body) {
    Node *n = node_new(NODE_UNTIL); n->left = cond; n->right = body; return n;
}
static Node *newRepeatNode(Node *count, Node *body) {
    Node *n = node_new(NODE_REPEAT); n->left = count; n->right = body; return n;
}
static Node *newForEachPairNode(const char *v1, const char *v2, Node *coll, Node *body) {
    Node *n = node_new(NODE_FOR_EACH); n->name = v1; n->second_name = v2; n->left = coll; n->right = body; return n;
}
static Node *newForEachNode(const char *v1, Node *coll, Node *body) {
    Node *n = node_new(NODE_FOR_EACH); n->name = v1; n->left = coll; n->right = body; return n;
}
static Node *newTryCatchNode(Node *try_body, const char *err_var, Node *catch_body) {
    Node *n = node_new(NODE_TRY_CATCH); n->left = try_body; n->name = err_var; n->right = catch_body; return n;
}
static Node *newThrowNode(Node *expr) {
    Node *n = node_new(NODE_THROW); n->left = expr; return n;
}
static Node *newExprStmtNode(Node *expr) {
    Node *n = node_new(NODE_EXPR_STMT); n->left = expr; return n;
}
static Node *newIndexNode(Node *coll, Node *idx) {
    Node *n = node_new(NODE_INDEX); n->left = coll; n->right = idx; return n;
}
static Node *newSliceNode(Node *coll, Node *start, Node *end) {
    Node *n = node_new(NODE_SLICE); n->left = coll; n->right = start; n->third = end; return n;
}
static Node *newCallNode(const char *name, Node **args, int count) {
    Node *n = node_new(NODE_CALL); n->name = name; n->call_args = args; n->call_arg_count = count; return n;
}
static Node *newCallExprNode(Node *expr, Node **args, int count) {
    Node *n = node_new(NODE_CALL_EXPR); n->left = expr; n->call_args = args; n->call_arg_count = count; return n;
}
static Node *newLambdaNode(int param_count, char **params, Node *body) {
    Node *n = node_new(NODE_LAMBDA); n->func_param_count = param_count; n->func_params = params; n->left = body; return n;
}
static Node *newListLiteral(Node **elements, int count) {
    Node *n = node_new(NODE_LIST_LITERAL); n->elements = elements; n->element_count = count; return n;
}
static Node *newListCompNode(Node *expr, const char *var, Node *coll, Node *filter) {
    Node *n = node_new(NODE_LIST_COMP); n->comp_expr = expr; n->comp_var = var; n->left = coll; n->comp_filter = filter; return n;
}
static Node *newDictLiteral(Node **keys, Node **values, int count) {
    Node *n = node_new(NODE_DICT_LITERAL); n->dict_keys = keys; n->dict_values = values; n->dict_count = count; return n;
}
static Node *newFuncNode(const char *name, int param_count, char **params, Node *body) {
    Node *n = node_new(NODE_FUNC_DEF); n->name = name; n->func_param_count = param_count; n->func_params = params; n->left = body; return n;
}
static Node *newReturnNode(Node *expr) {
    Node *n = node_new(NODE_RETURN); n->left = expr; return n;
}
static Node *newShowNode(Node **items, int count) {
    Node *n = node_new(NODE_SHOW); n->print_items = items; n->print_count = count; return n;
}
static Node *newWriteNode(Node **items, int count) {
    Node *n = node_new(NODE_WRITE); n->print_items = items; n->print_count = count; return n;
}
static Node *newSeqNode(Node *l, Node *r) {
    Node *n = node_new(NODE_SEQ); n->left = l; n->right = r; return n;
}

/* Compiler context to track local variables in current function */
typedef struct {
    FuncEntry *current_func;
    int is_function;
    const char *current_class_name;
    const char *current_trait_name;
} CompilerContext;

static CompilerContext comp_ctx = { NULL, 0, NULL, NULL };

static FuncEntry *func_table = NULL;
static int func_table_size = 0;
static int func_table_count = 0;

static int add_local(FuncEntry *fn, const char *name) {
    if (!fn) return -1;
    for (int i = 0; i < fn->local_count; i++) {
        if (strcmp(fn->local_names[i], name) == 0) return i;
    }
    if (fn->local_count >= fn->local_capacity) {
        fn->local_capacity = fn->local_capacity == 0 ? 8 : fn->local_capacity * 2;
        fn->local_names = realloc(fn->local_names, sizeof(char *) * fn->local_capacity);
    }
    fn->local_names[fn->local_count] = strdup(name);
    return fn->local_count++;
}

static void func_table_init(void) {
    func_table_size = 256;
    func_table_count = 0;
    func_table = calloc(func_table_size, sizeof(FuncEntry));
}

static void func_table_resize(void) {
    FuncEntry *old = func_table;
    int old_size = func_table_size;
    func_table_size *= 2;
    func_table = calloc(func_table_size, sizeof(FuncEntry));
    func_table_count = 0;
    for (int i = 0; i < old_size; i++) {
        if (old[i].used) {
            unsigned int h = hash_str(old[i].name) % func_table_size;
            unsigned int idx = h;
            do {
                if (!func_table[idx].used) {
                    func_table[idx] = old[i];
                    func_table_count++;
                    break;
                }
                idx = (idx + 1) % func_table_size;
            } while (idx != h);
        }
    }
    free(old);
}

static void func_set(const char *name, int param_count, char **params, Bytecode *body, int uses_let) {
    if (func_table_count > func_table_size * 3 / 4) func_table_resize();
    unsigned int h = hash_str(name) % func_table_size;
    unsigned int idx = h;
    do {
        if (!func_table[idx].used) {
            func_table[idx].name = name;
            func_table[idx].param_count = param_count;
            func_table[idx].params = params;
            func_table[idx].body = body;
            func_table[idx].used = 1;
            func_table[idx].uses_let = uses_let;
            func_table_count++;
            return;
        }
        if (strcmp(func_table[idx].name, name) == 0) {
            func_table[idx].param_count = param_count;
            func_table[idx].params = params;
            func_table[idx].body = body;
            func_table[idx].uses_let = uses_let;
            return;
        }
        idx = (idx + 1) % func_table_size;
    } while (idx != h);
}

static FuncEntry *func_get(const char *name) {
    unsigned int h = hash_str(name) % func_table_size;
    unsigned int idx = h;
    do {
        if (!func_table[idx].used) break;
        if (strcmp(func_table[idx].name, name) == 0) return &func_table[idx];
        idx = (idx + 1) % func_table_size;
    } while (idx != h);
    return NULL;
}

static void func_table_free(void) {
    if (func_table) {
        for (int i = 0; i < func_table_size; i++) {
            if (func_table[i].used && func_table[i].params) {
                for (int j = 0; j < func_table[i].param_count; j++)
                    free(func_table[i].params[j]);
                free(func_table[i].params);
                if (func_table[i].body) bc_free(func_table[i].body);
            }
        }
        free(func_table);
        func_table = NULL;
    }
}

/* ================================================================
 *  DYNAMIC VM STACK
 * ================================================================ */

static void vm_stack_init(void) {
    vm_stack_size = 16384;
    vm_stack = malloc(sizeof(Value) * vm_stack_size);
}

#define PUSH(v) do { \
    if (sp >= vm_stack_size) { \
        vm_stack_size *= 2; \
        vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size); \
    } \
    vm_stack[sp++] = (v); \
} while(0)
#define POP() (vm_stack[--sp])

/* ================================================================
 *  VALUE HELPERS + REF COUNTING
 * ================================================================ */


static int val_eq(Value a, Value b) {
    if (GET_TYPE(a) != GET_TYPE(b)) return 0;
    switch (GET_TYPE(a)) {
        case VAL_INT: return AS_INT(a) == AS_INT(b);
        case VAL_FLOAT: return AS_FLOAT(a) == AS_FLOAT(b);
        case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
        case VAL_STRING: return strcmp(AS_STRING(a), AS_STRING(b)) == 0;
        default: return 0;
    }
}

static void print_value(Value v) {
    if (v == 0) { printf("none"); return; }
    ValueType t = GET_TYPE(v);
    if (t == VAL_INT) printf("%ld", (long)AS_INT(v));
    else if (t == VAL_FLOAT) printf("%g", AS_FLOAT(v));
    else if (t == VAL_BOOL) printf("%s", AS_BOOL(v) ? "true" : "false");
    else if (t == VAL_STRING) printf("%s", AS_STRING(v));
    else if (t == VAL_OBJECT) {
        if (!(v & MASK_PAYLOAD)) { printf("none"); return; }
        ObjType ot = GET_OBJ_TYPE(v);
        if (ot == OBJ_CLASS) printf("<class %s>", AS_CLASS(v)->name);
        else if (ot == OBJ_TRAIT) printf("<trait %s>", AS_TRAIT(v)->name);
        else if (ot == OBJ_INSTANCE) printf("<instance of %s>", AS_INSTANCE(v)->klass->name);
        else if (ot == OBJ_BOUND_METHOD) printf("<bound method %s>", AS_BOUND_METHOD(v)->method->name);
        else if (ot == OBJ_NATIVE_FUNC) printf("<native function %s>", AS_NATIVE_FUNC(v)->name);
        else printf("<object>");
    }
    else if (t == VAL_LIST) {
        printf("[");
        for (int j = 0; j < AS_LIST(v)->size; j++) {
            print_value(AS_LIST(v)->items[j]);
            if (j < AS_LIST(v)->size - 1) printf(", ");
        }
        printf("]");
    }
}

static char *val_to_str(Value v) {
    char buf[1024];
    switch (GET_TYPE(v)) {
        case VAL_INT: snprintf(buf, sizeof(buf), "%ld", (long)AS_INT(v)); break;
        case VAL_FLOAT: snprintf(buf, sizeof(buf), "%g", AS_FLOAT(v)); break;
        case VAL_BOOL: snprintf(buf, sizeof(buf), "%s", AS_BOOL(v) ? "true" : "false"); break;
        case VAL_STRING: return strdup(AS_STRING(v));
        case VAL_LIST: return strdup("[list]");
        case VAL_DICT: return strdup("{dict}");
        case VAL_CLOSURE: return strdup("<function>");
        case VAL_OBJECT: {
            if (!(v & MASK_PAYLOAD)) return strdup("none");
            ObjType ot = GET_OBJ_TYPE(v);
            if (ot == OBJ_CLASS) {
                char b[128]; snprintf(b, sizeof(b), "<class %s>", AS_CLASS(v)->name);
                return strdup(b);
            }
            if (ot == OBJ_TRAIT) {
                char b[128]; snprintf(b, sizeof(b), "<trait %s>", AS_TRAIT(v)->name);
                return strdup(b);
            }
            if (ot == OBJ_INSTANCE) {
                char b[128]; snprintf(b, sizeof(b), "<instance of %s>", AS_INSTANCE(v)->klass->name);
                return strdup(b);
            }
            if (ot == OBJ_BOUND_METHOD) {
                char b[128]; snprintf(b, sizeof(b), "<bound method %s>", AS_BOUND_METHOD(v)->method->name);
                return strdup(b);
            }
            if (ot == OBJ_NATIVE_FUNC) {
                char b[128]; snprintf(b, sizeof(b), "<native function %s>", AS_NATIVE_FUNC(v)->name);
                return strdup(b);
            }
            return strdup("<object>");
        }
        default: return strdup("none");
    }
    return strdup(buf);
}

static char *str_replace(const char *s, const char *old, const char *new_s) {
    char *result;
    int i, count = 0;
    int newlen = strlen(new_s);
    int oldlen = strlen(old);
    for (i = 0; s[i] != '\0'; i++) {
        if (strstr(&s[i], old) == &s[i]) { count++; i += oldlen - 1; }
    }
    result = (char *)malloc(i + count * (newlen - oldlen) + 1);
    i = 0;
    while (*s) {
        if (strstr(s, old) == s) {
            strcpy(&result[i], new_s);
            i += newlen;
            s += oldlen;
        } else result[i++] = *s++;
    }
    result[i] = '\0';
    return result;
}

static int val_compare(const void *a, const void *b) {
    Value va = *(Value *)a;
    Value vb = *(Value *)b;
    if (GET_TYPE(va) == VAL_INT && GET_TYPE(vb) == VAL_INT) return AS_INT(va) - AS_INT(vb);
    if (GET_TYPE(va) == VAL_FLOAT && GET_TYPE(vb) == VAL_FLOAT) return (AS_FLOAT(va) > AS_FLOAT(vb)) - (AS_FLOAT(va) < AS_FLOAT(vb));
    return 0;
}

static Dict *dict_new(int sz);

static Shape *shape_new(Shape *parent, const char *name, int offset) {
    Shape *s = gc_alloc(sizeof(Shape), OBJ_SHAPE);
    s->parent = parent;
    s->name = name;
    s->offset = offset;
    s->transitions = dict_new(4);
    return s;
}

static Class *class_new(const char *name, Class *parent) {
    Class *c = gc_alloc(sizeof(Class), OBJ_CLASS);
    c->name = strdup(name);
    c->parent = parent;
    c->methods = dict_new(8);
    c->root_shape = shape_new(NULL, "__root__", -1);
    return c;
}

static Trait *trait_new(const char *name) {
    Trait *t = gc_alloc(sizeof(Trait), OBJ_TRAIT);
    t->name = strdup(name);
    t->methods = dict_new(8);
    return t;
}

static Instance *instance_new(Class *klass) {
    Instance *i = gc_alloc(sizeof(Instance), OBJ_INSTANCE);
    i->klass = klass;
    i->shape = klass->root_shape;
    i->fields = NULL;
    i->field_capacity = 0;
    return i;
}

static BoundMethod *bound_method_new(Value instance, FuncEntry *method) {
    BoundMethod *bm = gc_alloc(sizeof(BoundMethod), OBJ_BOUND_METHOD);
    bm->instance = instance;
    bm->method = method;
    return bm;
}

static List *list_new(int sz) {
    List *l = gc_alloc(sizeof(List), OBJ_LIST);
    l->size = sz; l->capacity = sz;
    l->items = sz > 0 ? calloc(sz, sizeof(Value)) : NULL;
    return l;
}

static List *list_grow(List *l, int new_cap) {
    if (new_cap <= l->capacity) return l;
    l->capacity = new_cap;
    l->items = realloc(l->items, sizeof(Value) * new_cap);
    return l;
}

static Closure *closure_new(int param_count, char **params, Bytecode *body) {
    Closure *c = gc_alloc(sizeof(Closure), OBJ_CLOSURE);
    c->param_count = param_count;
    c->params = malloc(sizeof(char *) * param_count);
    for (int i = 0; i < param_count; i++) c->params[i] = strdup(params[i]);
    c->body = body;
    c->captured_count = 0;
    c->captured_names = NULL;
    c->captured_values = NULL;
    return c;
}

static void closure_decref(Closure *c) {
    /* No-op with GC */
}

static void list_incref(List *l) { /* No-op with GC */ }
static void list_decref(List *l) { /* No-op with GC */ }
static void dict_decref(Dict *d) { /* No-op with GC */ }

static Dict *dict_new(int sz) {
    Dict *d = gc_alloc(sizeof(Dict), OBJ_DICT);
    d->size = 0; d->capacity = sz;
    d->keys   = sz > 0 ? calloc(sz, sizeof(const char *)) : NULL;
    d->values = sz > 0 ? calloc(sz, sizeof(Value)) : NULL;
    return d;
}

static Dict *dict_grow(Dict *d, int new_cap) {
    if (new_cap <= d->capacity) return d;
    d->capacity = new_cap;
    d->keys   = realloc(d->keys,   sizeof(const char *) * new_cap);
    d->values = realloc(d->values, sizeof(Value) * new_cap);
    return d;
}

static void dict_incref(Dict *d) { /* No-op with GC */ }


/* Truthiness: GlowLang truth rules */
static int value_truthy(Value v) {
    switch (GET_TYPE(v)) {
        case VAL_INT:    return AS_INT(v) != 0;
        case VAL_FLOAT:  return AS_FLOAT(v) != 0.0;
        case VAL_BOOL:   return AS_BOOL(v);
        case VAL_STRING: return AS_STRING(v) && AS_STRING(v)[0] != '\0';
        case VAL_LIST:   return AS_LIST(v) && AS_LIST(v)->size > 0;
        case VAL_DICT:   return AS_DICT(v) && AS_DICT(v)->size > 0;
        default: return 0;
    }
}

/* Type name helper */
static const char *type_name(Value v) {
    ValueType t = GET_TYPE(v);
    switch (t) {
        case VAL_INT:    return "int";
        case VAL_FLOAT:  return "float";
        case VAL_BOOL:   return "bool";
        case VAL_STRING: return "string";
        case VAL_LIST:   return "list";
        case VAL_DICT:   return "dict";
        case VAL_CLOSURE: return "function";
        case VAL_OBJECT: {
            if (!(v & MASK_PAYLOAD)) return "none";
            ObjType ot = GET_OBJ_TYPE(v);
            if (ot == OBJ_CLASS) return "class";
            if (ot == OBJ_INSTANCE) return "instance";
            if (ot == OBJ_BOUND_METHOD) return "method";
            return "object";
        }
        default:         return "none";
    }
}

/* ================================================================
 *  BYTECODE UTILITIES
 * ================================================================ */
static Bytecode *bc_new(void) {
    Bytecode *bc = calloc(1, sizeof(Bytecode));
    bc->capacity = 512; bc->count = 0;
    bc->code = calloc(bc->capacity, sizeof(Instruction));
    bc->jit_code = NULL;
    bc->jit_size = 0;
    return bc;
}

static void bc_free(Bytecode *bc) {
    if (!bc) return;
    for (int i = 0; i < bc->count; i++) {
        if (bc->code[i].op == OP_CONST_FLOAT) {
            free((void *)bc->code[i].ptr);
        }
    }
    if (bc->jit_code) {
        munmap(bc->jit_code, bc->jit_size);
    }
    free(bc->code);
    free(bc);
}

static int bc_emit(Bytecode *bc, OpCode op, int arg, intptr_t ptr) {
    if (bc->count >= bc->capacity) {
        int old_cap = bc->capacity;
        bc->capacity *= 2;
        bc->code = realloc(bc->code, sizeof(Instruction) * bc->capacity);
        memset(bc->code + old_cap, 0, sizeof(Instruction) * (bc->capacity - old_cap));
    }
    int idx = bc->count;
    bc->code[idx].op  = op;
    bc->code[idx].arg = arg;
    bc->code[idx].ptr = ptr;
    bc->code[idx].exec_count = 0;
    bc->count++;
    return idx;
}

static void bc_patch(Bytecode *bc, int offset, int target) {
    bc->code[offset].arg = target;
}

/* ================================================================
 *  FORWARD DECLARATIONS
 * ================================================================ */
static Value run_bytecode(Bytecode *bc, int base_offset);
void compile(Node *n);

static void* thread_worker(void *arg) {
    ThreadData *td = (ThreadData*)arg;
    Value callee = td->callee;
    int argc = td->argc;
    Value *args = td->args;

    vm_stack_init();
    
    pthread_mutex_lock(&glow_gil);

    if (GET_TYPE(callee) == VAL_CLOSURE) {
        Closure *c = AS_CLOSURE(callee);
        if (csp < CALL_STACK_SIZE - 1) {
            CallFrame *cf = &call_stack[csp++];
            cf->bc = NULL;
            cf->ip = 0;
            cf->base_sp = 0;
            cf->old_has_return = 0;
            cf->old_return_value = value_none();
            cf->pending_fn = NULL;
            cf->pending_closure = c;
            cf->old_shadow_list = let_shadow_list;
            cf->local_count = 16;
            cf->local_values = cf->inline_locals;
            for (int i = 0; i < 16; i++) cf->local_values[i] = value_none();
            for (int i = 0; i < c->captured_count; i++) var_set_let(c->captured_names[i], c->captured_values[i]);
            
            for (int i = 0; i < argc; i++) {
                if (i < c->param_count) {
                    var_set_let(c->params[i], args[i]);
                    if (i < 16) cf->local_values[i] = args[i];
                }
            }
            run_bytecode(c->body, 0);
            csp--;
            var_restore_shadow(cf->old_shadow_list);
        }
    } else {
        const char *fname = (GET_TYPE(callee) == VAL_STRING) ? AS_STRING(callee) : NULL;
        FuncEntry *fn = fname ? func_get(fname) : NULL;
        if (fn) {
            if (csp < CALL_STACK_SIZE - 1) {
                CallFrame *cf = &call_stack[csp++];
                cf->bc = NULL; cf->ip = 0; cf->base_sp = 0;
                cf->old_has_return = 0; cf->old_return_value = value_none();
                cf->pending_fn = fn; cf->pending_closure = NULL;
                cf->local_count = fn->local_count;
                if (cf->local_count > 0) {
                    if (cf->local_count <= 16) {
                        cf->local_values = cf->inline_locals;
                        for (int i = 0; i < cf->local_count; i++) cf->local_values[i] = value_none();
                    } else {
                        cf->local_values = calloc(cf->local_count, sizeof(Value));
                    }
                } else cf->local_values = NULL;

                if (fn->uses_let) cf->old_shadow_list = let_shadow_list;
                else cf->old_shadow_list = NULL;

                for (int i = 0; i < argc; i++) {
                    if (i < fn->param_count) {
                        if (cf->local_values && i < cf->local_count) cf->local_values[i] = args[i];
                        else var_set(fn->params[i], args[i]);
                    }
                }
                run_bytecode(fn->body, 0);
                csp--;
                if (fn->uses_let) var_restore_shadow(cf->old_shadow_list);
                if (cf->local_values && cf->local_count > 16) free(cf->local_values);
            }
        }
    }

    pthread_mutex_unlock(&glow_gil);
    free(td->args);
    free(td);
    if (vm_stack) free(vm_stack);
    return NULL;
}

/* ================================================================
 *  BYTECODE OPTIMIZER — Peephole + Constant Folding
 * ================================================================ */

/* Constant-fold: if top two stack items are const, fold into one */
static void bc_optimize_const_folding(Bytecode *bc) {
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 1; i < bc->count; i++) {
            OpCode op = bc->code[i].op;
            if (op != OP_ADD && op != OP_SUB && op != OP_MUL && op != OP_DIV &&
                op != OP_MOD && op != OP_POW && op != OP_LT && op != OP_GT &&
                op != OP_LTE && op != OP_GTE && op != OP_EQ && op != OP_NEQ &&
                op != OP_NEG) continue;

            int left_is_const = 0, right_is_const = 0;
            if (op == OP_NEG) {
                right_is_const = 0;
                left_is_const = (i >= 1) && (bc->code[i-1].op == OP_CONST_INT || bc->code[i-1].op == OP_CONST_FLOAT);
            } else {
                left_is_const  = (i >= 2) && (bc->code[i-2].op == OP_CONST_INT || bc->code[i-2].op == OP_CONST_FLOAT);
                right_is_const = (i >= 1) && (bc->code[i-1].op == OP_CONST_INT || bc->code[i-1].op == OP_CONST_FLOAT);
            }

            if ((op == OP_NEG && left_is_const) || (left_is_const && right_is_const)) {
                int lpos, rpos;
                if (op == OP_NEG) {
                    lpos = i - 1;
                    rpos = -1;
                } else {
                    lpos = i - 2;
                    rpos = i - 1;
                }

                int l_int = bc->code[lpos].op == OP_CONST_INT;
                int r_int = (rpos >= 0) && bc->code[rpos].op == OP_CONST_INT;

                int remove_count = 0;
                if (op == OP_NEG && l_int) {
                    int v = -bc->code[lpos].arg;
                    bc->code[lpos].op = OP_CONST_INT;
                    bc->code[lpos].arg = v;
                    bc->code[lpos].ptr = 0;
                    /* Remove OP_NEG */
                    remove_count = 1;
                    int start_remove = i;
                    for (int j = start_remove; j < bc->count - remove_count; j++)
                        bc->code[j] = bc->code[j+remove_count];
                    bc->count -= remove_count;
                    /* Update jump targets */
                    for (int j = 0; j < bc->count; j++) {
                        if (bc->code[j].op == OP_JUMP || bc->code[j].op == OP_JUMP_IF_FALSE || bc->code[j].op == OP_JUMP_IF_TRUE) {
                            if (bc->code[j].arg > start_remove)
                                bc->code[j].arg -= remove_count;
                        }
                    }
                    changed = 1;
                    break;
                } else if (op == OP_NEG) {
                    /* float negation — leave as is */
                } else if (l_int && r_int) {
                    int a = bc->code[lpos].arg, b = bc->code[rpos].arg;
                    int result = 0;
                    int ok = 1;
                    switch (op) {
                        case OP_ADD: result = a + b; break;
                        case OP_SUB: result = a - b; break;
                        case OP_MUL: result = a * b; break;
                        case OP_DIV: if (b == 0) ok = 0; else result = a / b; break;
                        case OP_MOD: if (b == 0) ok = 0; else result = a % b; break;
                        case OP_POW: result = (int)pow(a, b); break;
                        case OP_LT:  result = a < b; break;
                        case OP_GT:  result = a > b; break;
                        case OP_LTE: result = a <= b; break;
                        case OP_GTE: result = a >= b; break;
                        case OP_EQ:  result = a == b; break;
                        case OP_NEQ: result = a != b; break;
                        default: break;
                    }
                    if (ok) {
                        bc->code[lpos].op = OP_CONST_INT;
                        bc->code[lpos].arg = result;
                        bc->code[lpos].ptr = 0;
                        /* Remove right operand and operator */
                        remove_count = 2;
                        int start_remove = rpos;
                        for (int j = start_remove; j < bc->count - remove_count; j++)
                            bc->code[j] = bc->code[j+remove_count];
                        bc->count -= remove_count;
                        /* Update jump targets */
                        for (int j = 0; j < bc->count; j++) {
                            if (bc->code[j].op == OP_JUMP || bc->code[j].op == OP_JUMP_IF_FALSE || bc->code[j].op == OP_JUMP_IF_TRUE) {
                                if (bc->code[j].arg > start_remove)
                                    bc->code[j].arg -= remove_count;
                            }
                        }
                        changed = 1;
                        break;
                    }
                }
            }
        }
    }
}

/* Peephole: remove POP immediately followed by LOAD_VAR (dead expr) */
static void bc_optimize_peephole(Bytecode *bc) {
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < bc->count - 1; i++) {
            if (bc->code[i].op == OP_POP &&
                bc->code[i+1].op == OP_JUMP &&
                bc->code[i+1].arg == i) {
                /* Remove useless self-jump */
                for (int j = i + 1; j < bc->count - 1; j++) bc->code[j] = bc->code[j+1];
                bc->count--;
                changed = 1;
                break;
            }
        }
    }
}

static void bc_optimize(Bytecode *bc) {
    bc_optimize_const_folding(bc);
    bc_optimize_peephole(bc);
}

/* ================================================================
 *  GLOBALS
 * ================================================================ */
Node *root = NULL;
static Bytecode *main_bc = NULL;
static Bytecode *compiling_bc = NULL;

/* Exception handling */

static __thread int vm_loop_stop = 0, vm_loop_skip = 0;
static __thread Value vm_return_value;
static __thread int vm_has_return = 0;
%}

/* ====== Bison declarations ====== */

%union {
    int         int_val;
    double      float_val;
    const char *str_val;
    Node       *node;
    struct { Node **nodes; int count; } node_list;
    struct { Node **keys; Node **values; int count; } dict_list;
    struct { char **names; int count; } str_list;
}

/* Tokens with semantic values */
%token <int_val>   INT_LIT
%token <float_val> FLOAT_LIT
%token <str_val>   ID STRING FSTRING

/* Keywords */
%token SHOW WRITE WHEN EIF OTHERWISE WHILE UNTIL REPEAT
%token FOR EACH IN FUNC LAMBDA RETURN STOP SKIP END LET TRY CATCH THROW IMPORT CLASS TRAIT EXTENDS USE SPAWN
%token TRUE_KW FALSE_KW
%token TIMES_KW
%token INPUT_KW
%token IS IS_NOT FROM TO

/* Operators */
%token ASSIGN
%token PLUS MINUS MUL DIV MOD
%token PLUS_ASSIGN MINUS_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN
%token LT GT EQ NEQ LTE GTE
%token POW
%token AND_OP OR_OP NOT_OP

/* Punctuation */
%token COLON COMMA NEWLINE

/* Precedence (lowest to highest) */
%right OR_OP
%right AND_OP
%right NOT_OP
%left EQ NEQ IS IS_NOT IN
%left LT GT LTE GTE
%left PLUS MINUS
%left MUL DIV MOD
%right POW
%right UMINUS

/* Types */
%type <node> program lines function_body stmt expr term power postfix primary dict_literal call_expr
%type <node> condition eif_chain otherwise_part
%type <node_list> expr_list arg_list
%type <dict_list> dict_pairs
%type <str_list> param_list

%%

/* ================================================================
 *  GRAMMAR
 * ================================================================ */

program:
    lines
    {
        root = $1;
    }
;

lines:
      lines NEWLINE stmt   { $$ = newSeqNode($1, $3); }
    | lines NEWLINE        { $$ = $1; }
    | stmt                 { $$ = $1; }
    | /* empty */          { $$ = NULL; }
;

function_body:
      lines                { $$ = $1; }
;

/* ---- Statements ---- */
stmt:
    /* --- Assignment --- */
    postfix ASSIGN expr
      { 
        if ($1->type == NODE_VAR) {
            $$ = newAssignNode($1->name, $3);
        } else if ($1->type == NODE_INDEX) {
            if ($1->name) {
                /* Property assignment: obj.prop = expr */
                $$ = node_new(NODE_INDEX_ASSIGN); $$->left = $1->left; $$->name = $1->name; $$->right = $3;
            } else {
                /* List index assignment: obj[index] = expr */
                $$ = newIndexAssignNode($1->left, $1->right, $3);
            }
        } else {
            yyerror("Invalid assignment target");
            $$ = newAssignNode("error", $3);
        }
      }

    /* --- Multi-variable assignment: a, b = b, a --- */
    | ID COMMA ID ASSIGN expr COMMA expr
      { 
          Node *n = node_new(NODE_SEQ);
          /* Create temporary to hold first value */
          n->left = newAssignNode("__tmp_multi", $5);
          /* Create sequence: assign first, then second, then copy temp to first */
          Node *n2 = node_new(NODE_SEQ);
          n2->left = newAssignNode($3, $7);
          n2->right = newAssignNode($1, newVarNode("__tmp_multi"));
          n->right = n2;
          $$ = n;
      }

    /* --- Compound assignment --- */
    | ID PLUS_ASSIGN expr  { $$ = newCompoundNode(NODE_COMPOUND_ADD,  $1, $3); }
    | ID MINUS_ASSIGN expr { $$ = newCompoundNode(NODE_COMPOUND_SUB,  $1, $3); }
    | ID MUL_ASSIGN expr   { $$ = newCompoundNode(NODE_COMPOUND_MUL,  $1, $3); }
    | ID DIV_ASSIGN expr   { $$ = newCompoundNode(NODE_COMPOUND_DIV,  $1, $3); }
    | ID MOD_ASSIGN expr   { $$ = newCompoundNode(NODE_COMPOUND_MOD,  $1, $3); }

    /* --- Function definition --- */
    | FUNC ID '(' param_list ')' COLON NEWLINE function_body END
      { $$ = newFuncNode($2, $4.count, $4.names, $8); }

    | FUNC ID '(' ')' COLON NEWLINE function_body END
      { $$ = newFuncNode($2, 0, NULL, $7); }

    /* --- Import --- */
    | IMPORT STRING
      { 
          Node *n = node_new(NODE_IMPORT);
          n->name = $2;
          $$ = n;
      }

    /* --- Return --- */
    | RETURN expr          { $$ = newReturnNode($2); }

    /* --- Show / Print --- */
    | SHOW                 { $$ = newShowNode(NULL, 0); }
    | SHOW expr_list       { $$ = newShowNode($2.nodes, $2.count); }

    /* --- Write (no trailing newline) --- */
    | WRITE                { $$ = newWriteNode(NULL, 0); }
    | WRITE expr_list      { $$ = newWriteNode($2.nodes, $2.count); }

    /* --- When / If --- */
    | WHEN condition COLON NEWLINE lines eif_chain otherwise_part END
      {
          Node *else_part = $7;
          if ($6) {
              /* eif_chain is an IF_NODE; attach otherwise at the end */
              Node *chain = $6;
              while (chain->third) chain = chain->third;
              chain->third = $7;
              else_part = $6;
          }
          $$ = newIfNode($2, $5, else_part);
      }

    /* --- While --- */
    | WHILE condition COLON NEWLINE lines END
      { $$ = newWhileNode($2, $5); }

    /* --- Until (inverse of while) --- */
    | UNTIL condition COLON NEWLINE lines END
      { $$ = newUntilNode($2, $5); }

    /* --- Repeat N times --- */
    | REPEAT expr TIMES_LOOP COLON NEWLINE lines END
      { $$ = newRepeatNode($2, $6); }

    /* --- For-each --- */
    | FOR EACH ID COMMA ID IN expr COLON NEWLINE lines END
      { $$ = newForEachPairNode($3, $5, $7, $10); }

    | FOR EACH ID IN expr COLON NEWLINE lines END
      { $$ = newForEachNode($3, $5, $8); }

    | FOR ID IN expr COLON NEWLINE lines END
      { $$ = newForEachNode($2, $4, $7); }

    /* --- Stop / Break --- */
    | STOP                 { $$ = node_new(NODE_STOP); }

    /* --- Skip / Continue --- */
    | SKIP                 { $$ = node_new(NODE_SKIP); }

    /* --- Spawn (Concurrency) --- */
    | SPAWN call_expr      { $$ = node_new(NODE_SPAWN); $$->left = $2; }

    /* --- Use Trait --- */
    | USE ID               { $$ = node_new(NODE_USE_TRAIT); $$->name = $2; }

    /* --- Let (local variable) --- */
    | LET ID ASSIGN expr   { $$ = node_new(NODE_LET); $$->name = $2; $$->left = $4; }

    /* --- Class Definition --- */
    | CLASS ID COLON NEWLINE lines END
      { 
          $$ = node_new(NODE_CLASS); 
          $$->name = $2; 
          $$->left = $5; 
      }
    | CLASS ID EXTENDS ID COLON NEWLINE lines END
      {
          $$ = node_new(NODE_CLASS);
          $$->name = $2;
          $$->parent_name = $4;
          $$->left = $7;
      }

    /* --- Trait Definition --- */
    | TRAIT ID COLON NEWLINE lines END
      {
          $$ = node_new(NODE_TRAIT);
          $$->name = $2;
          $$->left = $5;
      }

    /* --- Try / Catch --- */
    | TRY COLON NEWLINE lines CATCH ID COLON NEWLINE lines END
      { $$ = newTryCatchNode($4, $6, $9); }

    /* --- Throw --- */
    | THROW expr
      { $$ = newThrowNode($2); }

    /* --- Expression statement (function call, etc.) --- */
    | expr                 { $$ = newExprStmtNode($1); }
;

TIMES_LOOP:
      TIMES_KW             { /* just a token */ }
;

eif_chain:
      /* empty */          { $$ = NULL; }
    | EIF condition COLON NEWLINE lines eif_chain
      { $$ = newIfNode($2, $5, $6); }
;

otherwise_part:
      /* empty */          { $$ = NULL; }
    | OTHERWISE COLON NEWLINE lines
      { $$ = $4; }
;

/* ---- Condition (comparison / logical) ---- */
condition:
      expr                 { $$ = $1; }
;

/* ---- Expressions (full precedence via %left/%right) ---- */
expr:
      expr OR_OP expr      { $$ = newBinOp(NODE_OR, $1, $3); }
    | expr AND_OP expr     { $$ = newBinOp(NODE_AND, $1, $3); }
    | NOT_OP expr          { $$ = newUnary(NODE_NOT, $2); }
    | expr EQ expr         { $$ = newBinOp(NODE_EQ, $1, $3); }
    | expr NEQ expr        { $$ = newBinOp(NODE_NEQ, $1, $3); }
    | expr IS expr         { $$ = newBinOp(NODE_EQ, $1, $3); }
    | expr IS_NOT expr     { $$ = newBinOp(NODE_NEQ, $1, $3); }
    | expr IN expr         { $$ = newBinOp(NODE_IN, $1, $3); }
    | expr IS IN expr      { $$ = newBinOp(NODE_IN, $1, $4); }
    | expr IS_NOT IN expr  { $$ = newUnary(NODE_NOT, newBinOp(NODE_IN, $1, $4)); }
    | expr LT expr         { $$ = newBinOp(NODE_LT, $1, $3); }
    | expr GT expr         { $$ = newBinOp(NODE_GT, $1, $3); }
    | expr LTE expr        { $$ = newBinOp(NODE_LTE, $1, $3); }
    | expr GTE expr        { $$ = newBinOp(NODE_GTE, $1, $3); }
    | expr PLUS term       { $$ = newBinOp(NODE_ADD, $1, $3); }
    | expr MINUS term      { $$ = newBinOp(NODE_SUB, $1, $3); }
    | term                 { $$ = $1; }
;

term:
      term MUL power       { $$ = newBinOp(NODE_MUL, $1, $3); }
    | term DIV power       { $$ = newBinOp(NODE_DIV, $1, $3); }
    | term MOD power       { $$ = newBinOp(NODE_MOD, $1, $3); }
    | power                { $$ = $1; }
;

power:
      postfix POW power    { $$ = newBinOp(NODE_POW, $1, $3); }
    | postfix              { $$ = $1; }
;

postfix:
      primary              { $$ = $1; }
    | postfix '.' ID       { $$ = node_new(NODE_INDEX); $$->left = $1; $$->name = $3; }
    | postfix '[' expr ']' { $$ = newIndexNode($1, $3); }
    | postfix '[' expr COLON expr ']'
                           { $$ = newSliceNode($1, $3, $5); }
    | postfix '[' COLON expr ']'
                           { $$ = newSliceNode($1, newIntNode(-1), $4); }
    | postfix '[' expr COLON ']'
                           { $$ = newSliceNode($1, $3, newIntNode(-1)); }
    | postfix '[' COLON ']'
                           { $$ = newSliceNode($1, newIntNode(-1), newIntNode(-1)); }
    | call_expr            { $$ = $1; }
;

call_expr:
      postfix '(' arg_list ')'
      { if ($1->type == NODE_VAR) $$ = newCallNode($1->name, $3.nodes, $3.count);
        else $$ = newCallExprNode($1, $3.nodes, $3.count); }
    | postfix '(' ')'
      { if ($1->type == NODE_VAR) $$ = newCallNode($1->name, NULL, 0);
        else $$ = newCallExprNode($1, NULL, 0); }
;

primary:
      '(' expr ')'         { $$ = $2; }
    | MINUS primary %prec UMINUS
                           { $$ = newUnary(NODE_NEG, $2); }
    | INT_LIT              { $$ = newIntNode($1); }
    | FLOAT_LIT            { $$ = newFloatNode($1); }
    | TRUE_KW              { $$ = newBoolNode(1); }
    | FALSE_KW             { $$ = newBoolNode(0); }
    | STRING               { $$ = newStringNode($1); }
    | FSTRING              { $$ = newFStringNode($1); }

    | INPUT_KW '(' arg_list ')'
      { if ($3.count == 1) { $$ = node_new(NODE_INPUT); $$->left = $3.nodes[0]; }
        else { $$ = node_new(NODE_INPUT); }
      }
    | INPUT_KW '(' ')'
      { $$ = node_new(NODE_INPUT); }

    | ID                  { $$ = newVarNode($1); }
    | LAMBDA '(' param_list ')' COLON expr
      { $$ = newLambdaNode($3.count, $3.names, $6); }
    | LAMBDA '(' ')' COLON expr
      { $$ = newLambdaNode(0, NULL, $5); }
    | LAMBDA '(' param_list ')' COLON NEWLINE lines END
      { $$ = newLambdaNode($3.count, $3.names, $7); }
    | LAMBDA '(' ')' COLON NEWLINE lines END
      { $$ = newLambdaNode(0, NULL, $6); }
    | '[' expr_list ']'   { $$ = newListLiteral($2.nodes, $2.count); }
    | '[' ']'             { $$ = newListLiteral(NULL, 0); }
    | '[' expr FOR EACH ID IN expr ']'
                          { $$ = newListCompNode($2, $5, $7, NULL); }
    | '[' expr FOR EACH ID IN expr WHEN expr ']'
                          { $$ = newListCompNode($2, $5, $7, $9); }
    | FROM expr TO expr
      { 
        /* desugar: from start to end -> range(start, end + 1) */
        Node *one = newIntNode(1);
        Node *end_plus_one = newBinOp(NODE_ADD, $4, one);
        Node **args = malloc(sizeof(Node *) * 2);
        args[0] = $2; args[1] = end_plus_one;
        $$ = newCallNode("range", args, 2);
      }
    | dict_literal        { $$ = $1; }

expr_list:
      expr                { $$.nodes = malloc(sizeof(Node *)); $$.nodes[0] = $1; $$.count = 1; }
    | expr_list COMMA expr  { $1.nodes = realloc($1.nodes, sizeof(Node *) * ($1.count + 1));
                            $1.nodes[$1.count] = $3; $$.nodes = $1.nodes; $$.count = $1.count + 1; }
;

arg_list:
      expr                { $$.nodes = malloc(sizeof(Node *)); $$.nodes[0] = $1; $$.count = 1; }
    | arg_list COMMA expr   { $1.nodes = realloc($1.nodes, sizeof(Node *) * ($1.count + 1));
                            $1.nodes[$1.count] = $3; $$.nodes = $1.nodes; $$.count = $1.count + 1; }
;

dict_literal:
      '{' dict_pairs '}'  { $$ = newDictLiteral($2.keys, $2.values, $2.count); }
    | '{' '}'             { $$ = newDictLiteral(NULL, NULL, 0); }
;

dict_pairs:
      STRING COLON expr   { $$.keys = malloc(sizeof(Node *)); $$.values = malloc(sizeof(Node *));
                            $$.keys[0] = newStringNode($1); $$.values[0] = $3; $$.count = 1; }
    | dict_pairs COMMA STRING COLON expr
      { $1.keys = realloc($1.keys, sizeof(Node *) * ($1.count + 1));
        $1.values = realloc($1.values, sizeof(Node *) * ($1.count + 1));
        $1.keys[$1.count] = newStringNode($3); $1.values[$1.count] = $5;
        $$.keys = $1.keys; $$.values = $1.values; $$.count = $1.count + 1; }
;

param_list:
      ID                  { $$.names = malloc(sizeof(char *)); $$.names[0] = strdup($1); $$.count = 1; }
    | param_list COMMA ID   { $1.names = realloc($1.names, sizeof(char *) * ($1.count + 1));
                            $1.names[$1.count] = strdup($3); $$.names = $1.names; $$.count = $1.count + 1; }
;

%%

/* ================================================================ */
/*                      BYTECODE COMPILER                           */
/* ================================================================ */

void compile(Node *n) {
    if (!n) return;
    switch (n->type) {
        /* ---- Literals ---- */
        case NODE_INT_LIT:
            bc_emit(compiling_bc, OP_CONST_INT, AS_INT(n->value), 0);
            break;

        case NODE_FLOAT_LIT: {
            double *dp = malloc(sizeof(double));
            *dp = AS_FLOAT(n->value);
            bc_emit(compiling_bc, OP_CONST_FLOAT, 0, (intptr_t)dp);
            break;
        }

        case NODE_BOOL_LIT:
            bc_emit(compiling_bc, OP_CONST_BOOL, AS_BOOL(n->value), 0);
            break;

        case NODE_STRING_LIT:
            bc_emit(compiling_bc, OP_CONST_STRING, 0, (intptr_t)n->value);
            break;

        case NODE_FSTRING: {
            for (int i = 0; i < n->fstring_part_count; i++) {
                compile(n->fstring_parts[i]);
            }
            bc_emit(compiling_bc, OP_FSTRING, n->fstring_part_count, 0);
            break;
        }

        case NODE_VAR: {
            int idx = -1;
            if (comp_ctx.is_function && comp_ctx.current_func) {
                for (int i = 0; i < comp_ctx.current_func->local_count; i++) {
                    if (strcmp(comp_ctx.current_func->local_names[i], n->name) == 0) { idx = i; break; }
                }
            }
            if (idx >= 0) bc_emit(compiling_bc, OP_LOAD_LOCAL, idx, (intptr_t)n->name);
            else bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)n->name);
            break;
        }

        /* ---- Assignment ---- */
        case NODE_ASSIGN: {
            int idx = -1;
            if (comp_ctx.is_function && comp_ctx.current_func) {
                for (int i = 0; i < comp_ctx.current_func->local_count; i++) {
                    if (strcmp(comp_ctx.current_func->local_names[i], n->name) == 0) { idx = i; break; }
                }
            }
            compile(n->left);
            if (idx >= 0) bc_emit(compiling_bc, OP_STORE_LOCAL, idx, (intptr_t)n->name);
            else bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->name);
            break;
        }

        case NODE_INDEX_ASSIGN:
            compile(n->left);  /* collection/object */
            if (n->name) {
                compile(n->right); /* value */
                bc_emit(compiling_bc, OP_SET_PROPERTY, 0, (intptr_t)n->name);
            } else {
                compile(n->right); /* index */
                compile(n->third); /* value */
                bc_emit(compiling_bc, OP_STORE_INDEX, 0, 0);
            }
            break;

        /* ---- Compound assignment ---- */
        case NODE_COMPOUND_ADD: {
            int idx = -1;
            if (comp_ctx.is_function && comp_ctx.current_func) {
                for (int i = 0; i < comp_ctx.current_func->local_count; i++) {
                    if (strcmp(comp_ctx.current_func->local_names[i], n->name) == 0) { idx = i; break; }
                }
            }
            if (idx >= 0) bc_emit(compiling_bc, OP_LOAD_LOCAL, idx, (intptr_t)n->name);
            else bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)n->name);
            compile(n->left);
            bc_emit(compiling_bc, OP_ADD, 0, 0);
            if (idx >= 0) bc_emit(compiling_bc, OP_STORE_LOCAL, idx, (intptr_t)n->name);
            else bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->name);
            break;
        }
        case NODE_COMPOUND_SUB: {
            int idx = -1;
            if (comp_ctx.is_function && comp_ctx.current_func) {
                for (int i = 0; i < comp_ctx.current_func->local_count; i++) {
                    if (strcmp(comp_ctx.current_func->local_names[i], n->name) == 0) { idx = i; break; }
                }
            }
            if (idx >= 0) bc_emit(compiling_bc, OP_LOAD_LOCAL, idx, (intptr_t)n->name);
            else bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)n->name);
            compile(n->left);
            bc_emit(compiling_bc, OP_SUB, 0, 0);
            if (idx >= 0) bc_emit(compiling_bc, OP_STORE_LOCAL, idx, (intptr_t)n->name);
            else bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->name);
            break;
        }
        case NODE_COMPOUND_MUL: {
            int idx = -1;
            if (comp_ctx.is_function && comp_ctx.current_func) {
                for (int i = 0; i < comp_ctx.current_func->local_count; i++) {
                    if (strcmp(comp_ctx.current_func->local_names[i], n->name) == 0) { idx = i; break; }
                }
            }
            if (idx >= 0) bc_emit(compiling_bc, OP_LOAD_LOCAL, idx, (intptr_t)n->name);
            else bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)n->name);
            compile(n->left);
            bc_emit(compiling_bc, OP_MUL, 0, 0);
            if (idx >= 0) bc_emit(compiling_bc, OP_STORE_LOCAL, idx, (intptr_t)n->name);
            else bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->name);
            break;
        }
        case NODE_COMPOUND_DIV: {
            int idx = -1;
            if (comp_ctx.is_function && comp_ctx.current_func) {
                for (int i = 0; i < comp_ctx.current_func->local_count; i++) {
                    if (strcmp(comp_ctx.current_func->local_names[i], n->name) == 0) { idx = i; break; }
                }
            }
            if (idx >= 0) bc_emit(compiling_bc, OP_LOAD_LOCAL, idx, (intptr_t)n->name);
            else bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)n->name);
            compile(n->left);
            bc_emit(compiling_bc, OP_DIV, 0, 0);
            if (idx >= 0) bc_emit(compiling_bc, OP_STORE_LOCAL, idx, (intptr_t)n->name);
            else bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->name);
            break;
        }
        case NODE_COMPOUND_MOD: {
            int idx = -1;
            if (comp_ctx.is_function && comp_ctx.current_func) {
                for (int i = 0; i < comp_ctx.current_func->local_count; i++) {
                    if (strcmp(comp_ctx.current_func->local_names[i], n->name) == 0) { idx = i; break; }
                }
            }
            if (idx >= 0) bc_emit(compiling_bc, OP_LOAD_LOCAL, idx, (intptr_t)n->name);
            else bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)n->name);
            compile(n->left);
            bc_emit(compiling_bc, OP_MOD, 0, 0);
            if (idx >= 0) bc_emit(compiling_bc, OP_STORE_LOCAL, idx, (intptr_t)n->name);
            else bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->name);
            break;
        }

        /* ---- Arithmetic ---- */
        case NODE_IN:
            compile(n->right); /* collection */
            compile(n->left);  /* element */
            bc_emit(compiling_bc, OP_LIST_CONTAINS, 0, 0);
            break;

        case NODE_ADD:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_ADD, 0, 0);
            break;
        case NODE_SUB:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_SUB, 0, 0);
            break;
        case NODE_MUL:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_MUL, 0, 0);
            break;
        case NODE_DIV:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_DIV, 0, 0);
            break;
        case NODE_MOD:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_MOD, 0, 0);
            break;
        case NODE_POW:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_POW, 0, 0);
            break;
        case NODE_NEG:
            compile(n->left);
            bc_emit(compiling_bc, OP_NEG, 0, 0);
            break;

        /* ---- Comparison ---- */
        case NODE_LT:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_LT, 0, 0);
            break;
        case NODE_GT:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_GT, 0, 0);
            break;
        case NODE_LTE:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_LTE, 0, 0);
            break;
        case NODE_GTE:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_GTE, 0, 0);
            break;
        case NODE_EQ:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_EQ, 0, 0);
            break;
        case NODE_NEQ:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_NEQ, 0, 0);
            break;

        /* ---- Logical ---- */
        case NODE_AND: {
            compile(n->left);
            int jmp = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            bc_emit(compiling_bc, OP_POP, 0, 0);
            compile(n->right);
            bc_patch(compiling_bc, jmp, compiling_bc->count);
            break;
        }
        case NODE_OR: {
            compile(n->left);
            int jmp = bc_emit(compiling_bc, OP_JUMP_IF_TRUE, 0, 0);
            bc_emit(compiling_bc, OP_POP, 0, 0);
            compile(n->right);
            bc_patch(compiling_bc, jmp, compiling_bc->count);
            break;
        }
        case NODE_NOT:
            compile(n->left);
            bc_emit(compiling_bc, OP_NOT, 0, 0);
            break;

        /* ---- Builtin calls ---- */
        case NODE_LEN:
            compile(n->left);
            bc_emit(compiling_bc, OP_CALL_BUILTIN_LEN, 0, 0);
            break;
        case NODE_RANGE:
            compile(n->left);
            bc_emit(compiling_bc, OP_CALL_BUILTIN_RANGE, 0, 0);
            break;
        case NODE_TYPE:
            compile(n->left);
            bc_emit(compiling_bc, OP_CALL_BUILTIN_TYPE, 0, 0);
            break;
        case NODE_APPEND:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_BUILTIN_APPEND, 0, 0);
            break;

        case NODE_INPUT:
            if (n->left) compile(n->left);
            bc_emit(compiling_bc, OP_BUILTIN_INPUT, n->left ? 1 : 0, 0);
            break;

        /* ---- File I/O ---- */
        case NODE_FILE_READ:
            compile(n->left);
            bc_emit(compiling_bc, OP_FILE_READ, 0, 0);
            break;
        case NODE_FILE_WRITE:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_FILE_WRITE, 0, 0);
            break;
        case NODE_FILE_APPEND:
            compile(n->left); compile(n->right);
            bc_emit(compiling_bc, OP_FILE_APPEND, 0, 0);
            break;

        /* ---- Index / Slice ---- */
        case NODE_INDEX:
            compile(n->left);
            if (n->name) {
                bc_emit(compiling_bc, OP_GET_PROPERTY, 0, (intptr_t)n->name);
            } else {
                compile(n->right);
                bc_emit(compiling_bc, OP_INDEX, 0, 0);
            }
            break;
        case NODE_SLICE:
            compile(n->left);
            compile(n->right ? n->right : newIntNode(0));
            compile(n->third ? n->third : newIntNode(-1));
            bc_emit(compiling_bc, OP_SLICE, 0, 0);
            break;

        /* ---- Literals ---- */
        case NODE_LIST_LITERAL:
            for (int i = 0; i < n->element_count; i++) compile(n->elements[i]);
            bc_emit(compiling_bc, OP_LIST_LITERAL, n->element_count, 0);
            break;

        case NODE_LIST_COMP: {
            /* Iterative compilation of list comprehension */
            const char *tmp_res = "__cr";
            const char *tmp_iter = "__ci";
            const char *tmp_i = "__cj";

            bc_emit(compiling_bc, OP_LIST_LITERAL, 0, 0); 
            bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)tmp_res);
            compile(n->left);
            bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)tmp_iter);
            bc_emit(compiling_bc, OP_CONST_INT, 0, 0);
            bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)tmp_i);

            int start = compiling_bc->count;
            bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)tmp_i);
            bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)tmp_iter);
            bc_emit(compiling_bc, OP_CALL_BUILTIN_LEN, 0, 0);
            bc_emit(compiling_bc, OP_LT, 0, 0);
            int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            bc_emit(compiling_bc, OP_POP, 0, 0);

            bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)tmp_iter);
            bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)tmp_i);
            bc_emit(compiling_bc, OP_INDEX, 0, 0);
            bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->comp_var);

            int skip_push = -1;
            if (n->comp_filter) {
                compile(n->comp_filter);
                skip_push = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
                bc_emit(compiling_bc, OP_POP, 0, 0);
            }

            bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)tmp_res);
            compile(n->comp_expr);
            bc_emit(compiling_bc, OP_LIST_PUSH, 0, 0);
            bc_emit(compiling_bc, OP_POP, 0, 0);

            if (skip_push >= 0) {
                int jump_next = bc_emit(compiling_bc, OP_JUMP, 0, 0);
                bc_patch(compiling_bc, skip_push, compiling_bc->count);
                bc_emit(compiling_bc, OP_POP, 0, 0);
                bc_patch(compiling_bc, jump_next, compiling_bc->count);
            }

            bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)tmp_i);
            bc_emit(compiling_bc, OP_CONST_INT, 1, 0);
            bc_emit(compiling_bc, OP_ADD, 0, 0);
            bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)tmp_i);
            bc_emit(compiling_bc, OP_JUMP, start, 0);

            int end = compiling_bc->count;
            bc_patch(compiling_bc, jf, end);
            bc_emit(compiling_bc, OP_POP, 0, 0);
            bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)tmp_res);
            break;
        }

        case NODE_DICT_LITERAL:
            for (int i = 0; i < n->dict_count; i++) {
                compile(n->dict_keys[i]);
                compile(n->dict_values[i]);
            }
            bc_emit(compiling_bc, OP_DICT_LITERAL, n->dict_count, 0);
            break;

        /* ---- Output ---- */
        case NODE_SHOW:
            for (int i = n->print_count - 1; i >= 0; i--) compile(n->print_items[i]);
            bc_emit(compiling_bc, OP_BUILTIN_SHOW, n->print_count, 0);
            break;
        case NODE_WRITE:
            for (int i = n->print_count - 1; i >= 0; i--) compile(n->print_items[i]);
            bc_emit(compiling_bc, OP_BUILTIN_WRITE, n->print_count, 0);
            break;

        /* ---- Control Flow ---- */
        case NODE_IF: {
            compile(n->left);
            int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            bc_emit(compiling_bc, OP_POP, 0, 0);
            compile(n->right);
            int jmp = bc_emit(compiling_bc, OP_JUMP, 0, 0);
            int false_target = compiling_bc->count;
            bc_emit(compiling_bc, OP_POP, 0, 0);
            bc_patch(compiling_bc, jf, false_target);
            if (n->third) compile(n->third);
            bc_patch(compiling_bc, jmp, compiling_bc->count);
            break;
        }

        case NODE_WHILE: {
            int loop_start = compiling_bc->count;
            compile(n->left);
            int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            bc_emit(compiling_bc, OP_POP, 0, 0);
            compile(n->right);
            bc_emit(compiling_bc, OP_JUMP, loop_start, 0);
            bc_patch(compiling_bc, jf, compiling_bc->count);
            bc_emit(compiling_bc, OP_POP, 0, 0);
            break;
        }

        case NODE_UNTIL: {
            int loop_start = compiling_bc->count;
            compile(n->left);
            int jt = bc_emit(compiling_bc, OP_JUMP_IF_TRUE, 0, 0);
            bc_emit(compiling_bc, OP_POP, 0, 0);
            compile(n->right);
            bc_emit(compiling_bc, OP_JUMP, loop_start, 0);
            bc_patch(compiling_bc, jt, compiling_bc->count);
            bc_emit(compiling_bc, OP_POP, 0, 0);
            break;
        }

        case NODE_REPEAT: {
            compile(n->left); 
            int loop_start = compiling_bc->count;
            bc_emit(compiling_bc, OP_CONST_INT, 0, 0);
            bc_emit(compiling_bc, OP_GT, 0, 0);
            int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            bc_emit(compiling_bc, OP_POP, 0, 0);
            compile(n->right);
            bc_emit(compiling_bc, OP_CONST_INT, 1, 0);
            bc_emit(compiling_bc, OP_SUB, 0, 0);
            bc_emit(compiling_bc, OP_JUMP, loop_start, 0);
            bc_patch(compiling_bc, jf, compiling_bc->count);
            bc_emit(compiling_bc, OP_POP, 0, 0);
            bc_emit(compiling_bc, OP_POP, 0, 0);
            break;
        }

        case NODE_FOR_EACH: {
            compile(n->left);
            int idx = -1;
            if (comp_ctx.is_function && comp_ctx.current_func) {
                idx = add_local(comp_ctx.current_func, n->name);
            }
            int loop_ip = bc_emit(compiling_bc, OP_FOR_EACH, 0, (intptr_t)n->name);
            int jump_to_end = bc_emit(compiling_bc, OP_JUMP, 0, 0);
            int body_start = compiling_bc->count;
            compile(n->right);
            bc_emit(compiling_bc, OP_SKIP, 0, 0);
            int body_end = compiling_bc->count;
            
            /* Patch OP_FOR_EACH with body_end */
            bc_patch(compiling_bc, loop_ip, body_end);
            /* Patch JUMP to also go to body_end (for when list is empty) */
            bc_patch(compiling_bc, jump_to_end, body_end);
            break;
        }

        case NODE_STOP: bc_emit(compiling_bc, OP_STOP, 0, 0); break;
        case NODE_SKIP: bc_emit(compiling_bc, OP_SKIP, 0, 0); break;

        case NODE_LET: {
            int idx = -1;
            if (comp_ctx.is_function && comp_ctx.current_func) {
                idx = add_local(comp_ctx.current_func, n->name);
            }
            compile(n->left);
            if (idx >= 0) bc_emit(compiling_bc, OP_STORE_LOCAL, idx, 0);
            else bc_emit(compiling_bc, OP_STORE_LET, 0, (intptr_t)n->name);
            break;
        }

        case NODE_TRY_CATCH: {
            int try_start = bc_emit(compiling_bc, OP_TRY_START, 0, 0);
            compile(n->left);
            bc_emit(compiling_bc, OP_TRY_END, 0, 0);
            int jump_over_catch = bc_emit(compiling_bc, OP_JUMP, 0, 0);
            int catch_start = compiling_bc->count;
            bc_patch(compiling_bc, try_start, catch_start);
            bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->name);
            compile(n->right);
            bc_patch(compiling_bc, jump_over_catch, compiling_bc->count);
            break;
        }

        case NODE_THROW:
            compile(n->left);
            bc_emit(compiling_bc, OP_THROW, 0, 0);
            break;

        case NODE_FUNC_DEF: {
            FuncEntry *fn = malloc(sizeof(FuncEntry));
            fn->name = n->name;
            fn->param_count = n->func_param_count;
            fn->params = n->func_params;
            fn->used = 1;
            fn->uses_let = ast_contains_let(n->left);
            fn->local_names = NULL;
            fn->local_count = 0;
            fn->local_capacity = 0;
            for (int i = 0; i < fn->param_count; i++) add_local(fn, fn->params[i]);

            Bytecode *fn_bc = bc_new();
            Bytecode *save_bc = compiling_bc;
            CompilerContext save_ctx = comp_ctx;
            compiling_bc = fn_bc;
            comp_ctx.current_func = fn;
            comp_ctx.is_function = 1;

            compile(n->left);
            bc_emit(compiling_bc, OP_RETURN, 0, 0);
            bc_optimize(fn_bc);
            fn->body = fn_bc;
            compiling_bc = save_bc;
            comp_ctx = save_ctx;

            unsigned int h = hash_str(fn->name) % func_table_size;
            unsigned int idx = h;
            do {
                if (!func_table[idx].used) {
                    func_table[idx] = *fn;
                    func_table_count++;
                    break;
                }
                idx = (idx + 1) % func_table_size;
            } while (idx != h);
            free(fn);
            
            if (comp_ctx.current_class_name || comp_ctx.current_trait_name) {
                bc_emit(compiling_bc, OP_METHOD_DEF, 0, (intptr_t)n->name);
            }
            break;
        }

        case NODE_LAMBDA: {
            FuncEntry *fn = calloc(1, sizeof(FuncEntry));
            fn->name = "__lambda";
            fn->param_count = n->func_param_count;
            fn->params = n->func_params;
            for (int i = 0; i < fn->param_count; i++) add_local(fn, fn->params[i]);

            Bytecode *fn_bc = bc_new();
            Bytecode *save_bc = compiling_bc;
            CompilerContext save_ctx = comp_ctx;
            compiling_bc = fn_bc;
            comp_ctx.current_func = fn;
            comp_ctx.is_function = 1;

            compile(n->left);
            bc_emit(compiling_bc, OP_RETURN, 0, 0);
            bc_optimize(fn_bc);
            compiling_bc = save_bc;
            comp_ctx = save_ctx;

            for (int i = 0; i < n->func_param_count; i++) {
                bc_emit(compiling_bc, OP_CONST_STRING, 0, (intptr_t)value_string(n->func_params[i]));
            }
            bc_emit(compiling_bc, OP_LAMBDA, n->func_param_count, (intptr_t)fn_bc);
            break;
        }

        case NODE_CALL: {
            const char *fname = n->name;
            int emitted = 0;

            if (strcmp(fname, "len") == 0 && n->call_arg_count == 1) {
                compile(n->call_args[0]); bc_emit(compiling_bc, OP_CALL_BUILTIN_LEN, 0, 0); emitted = 1;
            } else if (strcmp(fname, "range") == 0 && (n->call_arg_count == 1 || n->call_arg_count == 2)) {
                for (int i = 0; i < n->call_arg_count; i++) compile(n->call_args[i]);
                bc_emit(compiling_bc, OP_CALL_BUILTIN_RANGE, n->call_arg_count, 0); emitted = 1;
            } else if (strcmp(fname, "type") == 0 && n->call_arg_count == 1) {
                compile(n->call_args[0]); bc_emit(compiling_bc, OP_CALL_BUILTIN_TYPE, 0, 0); emitted = 1;
            } else if (strcmp(fname, "append") == 0 && n->call_arg_count == 2) {
                compile(n->call_args[0]); compile(n->call_args[1]); bc_emit(compiling_bc, OP_BUILTIN_APPEND, 0, 0); emitted = 1;
            } else if (strcmp(fname, "input") == 0) {
                if (n->call_arg_count == 1) compile(n->call_args[0]);
                bc_emit(compiling_bc, OP_BUILTIN_INPUT, n->call_arg_count == 1 ? 1 : 0, 0); emitted = 1;
            } else if (strcmp(fname, "keys") == 0 && n->call_arg_count == 1) {
                compile(n->call_args[0]); bc_emit(compiling_bc, OP_DICT_KEYS, 0, 0); emitted = 1;
            } else if (strcmp(fname, "values") == 0 && n->call_arg_count == 1) {
                compile(n->call_args[0]); bc_emit(compiling_bc, OP_DICT_VALUES, 0, 0); emitted = 1;
            } else if (strcmp(fname, "has") == 0 && n->call_arg_count == 2) {
                compile(n->call_args[0]); compile(n->call_args[1]); bc_emit(compiling_bc, OP_DICT_HAS, 0, 0); emitted = 1;
            } else if (strcmp(fname, "remove") == 0 && n->call_arg_count == 2) {
                compile(n->call_args[0]); compile(n->call_args[1]); bc_emit(compiling_bc, OP_DICT_REMOVE, 0, 0); emitted = 1;
            } else if (strcmp(fname, "merge") == 0 && n->call_arg_count == 2) {
                compile(n->call_args[0]); compile(n->call_args[1]); bc_emit(compiling_bc, OP_DICT_MERGE, 0, 0); emitted = 1;
            /* Math functions */
            } else if (strcmp(fname, "abs") == 0 && n->call_arg_count == 1) {
                compile(n->call_args[0]); bc_emit(compiling_bc, OP_MATH_ABS, 0, 0); emitted = 1;
            } else if (strcmp(fname, "sqrt") == 0 && n->call_arg_count == 1) {
                compile(n->call_args[0]); bc_emit(compiling_bc, OP_MATH_SQRT, 0, 0); emitted = 1;
            } else if (strcmp(fname, "round") == 0 && n->call_arg_count == 1) {
                compile(n->call_args[0]); bc_emit(compiling_bc, OP_MATH_ROUND, 0, 0); emitted = 1;
            } else if (strcmp(fname, "floor") == 0 && n->call_arg_count == 1) {
                compile(n->call_args[0]); bc_emit(compiling_bc, OP_MATH_FLOOR, 0, 0); emitted = 1;
            } else if (strcmp(fname, "ceil") == 0 && n->call_arg_count == 1) {
                compile(n->call_args[0]); bc_emit(compiling_bc, OP_MATH_CEIL, 0, 0); emitted = 1;
            } else if (strcmp(fname, "min") == 0 && n->call_arg_count >= 1) {
                for (int i = 0; i < n->call_arg_count; i++) compile(n->call_args[i]);
                bc_emit(compiling_bc, OP_MATH_MIN, n->call_arg_count, 0); emitted = 1;
            } else if (strcmp(fname, "max") == 0 && n->call_arg_count >= 1) {
                for (int i = 0; i < n->call_arg_count; i++) compile(n->call_args[i]);
                bc_emit(compiling_bc, OP_MATH_MAX, n->call_arg_count, 0); emitted = 1;
            /* List methods */
            } else if (strcmp(fname, "push") == 0 && n->call_arg_count == 2) {
                compile(n->call_args[0]); compile(n->call_args[1]); bc_emit(compiling_bc, OP_LIST_PUSH, 0, 0); emitted = 1;
            } else if (strcmp(fname, "pop") == 0 && n->call_arg_count == 1) {
                compile(n->call_args[0]); bc_emit(compiling_bc, OP_LIST_POP, 0, 0); emitted = 1;
            } else if (strcmp(fname, "sort") == 0 && n->call_arg_count == 1) {
                compile(n->call_args[0]); bc_emit(compiling_bc, OP_LIST_SORT, 0, 0); emitted = 1;
            } else if (strcmp(fname, "reverse") == 0 && n->call_arg_count == 1) {
                compile(n->call_args[0]); bc_emit(compiling_bc, OP_LIST_REVERSE, 0, 0); emitted = 1;
            }

            if (!emitted) {
                for (int i = 0; i < n->call_arg_count; i++) compile(n->call_args[i]);
                /* Check if it's a local closure variable first */
                int idx = -1;
                if (comp_ctx.is_function && comp_ctx.current_func) {
                    for (int i = 0; i < comp_ctx.current_func->local_count; i++) {
                        if (strcmp(comp_ctx.current_func->local_names[i], fname) == 0) { idx = i; break; }
                    }
                }
                if (idx >= 0) {
                    bc_emit(compiling_bc, OP_LOAD_LOCAL, idx, (intptr_t)fname);
                    bc_emit(compiling_bc, OP_CALL_VALUE, n->call_arg_count, 0);
                } else {
                    bc_emit(compiling_bc, OP_CALL, n->call_arg_count, (intptr_t)fname);
                }
            }
            break;
        }

        case NODE_CALL_EXPR: {
            if (n->left->type == NODE_VAR) {
                const char *name = n->left->name;
                if (strcmp(name, "len") == 0 && n->call_arg_count == 1) { compile(n->call_args[0]); bc_emit(compiling_bc, OP_CALL_BUILTIN_LEN, 0, 0); return; }
                if (strcmp(name, "range") == 0 && (n->call_arg_count == 1 || n->call_arg_count == 2)) {
                    for (int i = 0; i < n->call_arg_count; i++) compile(n->call_args[i]);
                    bc_emit(compiling_bc, OP_CALL_BUILTIN_RANGE, n->call_arg_count, 0); return; 
                }
                if (strcmp(name, "type") == 0 && n->call_arg_count == 1) { compile(n->call_args[0]); bc_emit(compiling_bc, OP_CALL_BUILTIN_TYPE, 0, 0); return; }
                if (strcmp(name, "append") == 0 && n->call_arg_count == 2) { compile(n->call_args[0]); compile(n->call_args[1]); bc_emit(compiling_bc, OP_BUILTIN_APPEND, 0, 0); return; }
                if (strcmp(name, "input") == 0) { if (n->call_arg_count == 1) compile(n->call_args[0]); bc_emit(compiling_bc, OP_BUILTIN_INPUT, n->call_arg_count == 1 ? 1 : 0, 0); return; }
            }
            for (int i = 0; i < n->call_arg_count; i++) compile(n->call_args[i]);
            if (n->left->type == NODE_VAR && func_get(n->left->name)) {
                bc_emit(compiling_bc, OP_CALL, n->call_arg_count, (intptr_t)n->left->name);
            } else {
                compile(n->left);
                bc_emit(compiling_bc, OP_CALL_VALUE, n->call_arg_count, 0);
            }
            break;
        }

        case NODE_RETURN:
            compile(n->left);
            bc_emit(compiling_bc, OP_RETURN, 0, 0);
            break;

        case NODE_EXPR_STMT:
            compile(n->left);
            bc_emit(compiling_bc, OP_POP, 0, 0);
            break;

        case NODE_SEQ:
            compile(n->left); compile(n->right);
            break;

        case NODE_IMPORT: {
            /* Handled by Module Loader in main() */
            break;
        }

        case NODE_SPAWN: {
            /* Compile the call expression's children (callee and args) */
            Node *call = n->left;
            for (int i = 0; i < call->call_arg_count; i++) compile(call->call_args[i]);
            compile(call->left);
            bc_emit(compiling_bc, OP_SPAWN, call->call_arg_count, 0);
            break;
        }

        case NODE_CLASS: {
            const char *prev_class = comp_ctx.current_class_name;
            comp_ctx.current_class_name = n->name;
            
            if (n->parent_name) {
                bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)n->parent_name);
            } else {
                bc_emit(compiling_bc, OP_CONST_BOOL, 0, 0); /* false for no parent */
            }
            bc_emit(compiling_bc, OP_CLASS_DEF, 0, (intptr_t)n->name);
            
            /* Pushes the class onto the stack for method registration */
            if (n->left) compile(n->left);
            
            bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->name);
            comp_ctx.current_class_name = prev_class;
            break;
        }

        case NODE_TRAIT: {
            const char *prev_trait = comp_ctx.current_trait_name;
            comp_ctx.current_trait_name = n->name;
            
            bc_emit(compiling_bc, OP_TRAIT_DEF, 0, (intptr_t)n->name);
            
            /* Pushes the trait onto the stack for method registration */
            if (n->left) compile(n->left);
            
            bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->name);
            comp_ctx.current_trait_name = prev_trait;
            break;
        }

        case NODE_USE_TRAIT: {
            bc_emit(compiling_bc, OP_USE_TRAIT, 0, (intptr_t)n->name);
            break;
        }

        default:
            fprintf(stderr, "Unknown node type: %d\n", n->type);
            break;
    }
}

/* ================================================================ */
/*                        VM VALUE HELPERS                          */
typedef struct {
    uint8_t *code;
    size_t size;
    size_t capacity;
} JitBuffer;

static void jit_buf_init(JitBuffer *b) {
    b->capacity = 4096;
    b->size = 0;
    b->code = malloc(b->capacity);
}

static void jit_emit_byte(JitBuffer *b, uint8_t byte) {
    if (b->size >= b->capacity) {
        b->capacity *= 2;
        b->code = realloc(b->code, b->capacity);
    }
    b->code[b->size++] = byte;
}

static void jit_emit_bytes(JitBuffer *b, uint8_t *bytes, int len) {
    for (int i = 0; i < len; i++) jit_emit_byte(b, bytes[i]);
}

static void jit_emit_uint32(JitBuffer *b, uint32_t val) {
    jit_emit_byte(b, val & 0xFF);
    jit_emit_byte(b, (val >> 8) & 0xFF);
    jit_emit_byte(b, (val >> 16) & 0xFF);
    jit_emit_byte(b, (val >> 24) & 0xFF);
}

static void jit_emit_uint64(JitBuffer *b, uint64_t val) {
    jit_emit_uint32(b, val & 0xFFFFFFFF);
    jit_emit_uint32(b, (val >> 32) & 0xFFFFFFFF);
}

/* x86-64 Encoders */
static void x64_push_rbp(JitBuffer *b) { jit_emit_byte(b, 0x55); }
static void x64_mov_rbp_rsp(JitBuffer *b) { 
    uint8_t op[] = {0x48, 0x89, 0xe5}; jit_emit_bytes(b, op, 3); 
}
static void x64_pop_rbp(JitBuffer *b) { jit_emit_byte(b, 0x5d); }
static void x64_ret(JitBuffer *b) { jit_emit_byte(b, 0xc3); }

static void x64_push_rax(JitBuffer *b) { jit_emit_byte(b, 0x50); }
static void x64_push_rbx(JitBuffer *b) { jit_emit_byte(b, 0x53); }
static void x64_pop_rax(JitBuffer *b) { jit_emit_byte(b, 0x58); }
static void x64_pop_rbx(JitBuffer *b) { jit_emit_byte(b, 0x5b); }

static void x64_push_rcx(JitBuffer *b) { jit_emit_byte(b, 0x51); }
static void x64_pop_rcx(JitBuffer *b) { jit_emit_byte(b, 0x59); }

static void x64_mov_rax_imm64(JitBuffer *b, uint64_t imm) {
    uint8_t op[] = {0x48, 0xb8}; jit_emit_bytes(b, op, 2);
    jit_emit_uint64(b, imm);
}

static void x64_add_rax_rcx(JitBuffer *b) {
    /* rax = rax + rcx (32-bit add) */
    uint8_t op[] = {0x01, 0xc8}; jit_emit_bytes(b, op, 2);
}

static void x64_sub_rax_rcx(JitBuffer *b) {
    uint8_t op[] = {0x29, 0xc8}; jit_emit_bytes(b, op, 2);
}

static void x64_cmp_rax_rcx(JitBuffer *b) {
    /* cmp rax, rcx (64-bit) */
    uint8_t op[] = {0x48, 0x39, 0xc8}; jit_emit_bytes(b, op, 3);
}

static void x64_mov_rax_rdi_off(JitBuffer *b, int idx) {
    int offset = idx * 8;
    if (offset < 128) {
        uint8_t op[] = {0x48, 0x8b, 0x47, (uint8_t)offset};
        jit_emit_bytes(b, op, 4);
    } else {
        uint8_t op[] = {0x48, 0x8b, 0x87};
        jit_emit_bytes(b, op, 3);
        jit_emit_uint32(b, offset);
    }
}

static void x64_mov_rdi_off_rax(JitBuffer *b, int idx) {
    int offset = idx * 8;
    if (offset < 128) {
        uint8_t op[] = {0x48, 0x89, 0x47, (uint8_t)offset};
        jit_emit_bytes(b, op, 4);
    } else {
        uint8_t op[] = {0x48, 0x89, 0x87};
        jit_emit_bytes(b, op, 3);
        jit_emit_uint32(b, offset);
    }
}

static void x64_mov_rcx_imm64(JitBuffer *b, uint64_t imm) {
    uint8_t op[] = {0x48, 0xb9}; jit_emit_bytes(b, op, 2);
    jit_emit_uint64(b, imm);
}

static void x64_mov_rbx_imm64(JitBuffer *b, uint64_t imm) {
    uint8_t op[] = {0x48, 0xbb}; jit_emit_bytes(b, op, 2);
    jit_emit_uint64(b, imm);
}

static void x64_setl_rax(JitBuffer *b) {
    uint8_t op[] = {0x0f, 0x9c, 0xc0, 0x48, 0x0f, 0xb6, 0xc0};
    jit_emit_bytes(b, op, 7);
}

/* x86-64 Register Mapping for Phase 8 JIT:
   %rdi: Pointer to local_values array (CallFrame->local_values)
   %rbx: Local Variable 0
   %r12: Local Variable 1
   %r13: Local Variable 2
   %r14: Local Variable 3
   %r15: Local Variable 4
   (Higher indices fall back to memory access via %rdi)
*/
#define JIT_REG_LOCALS 5
static const int jit_regs[] = { 3, 12, 13, 14, 15 }; /* rbx, r12, r13, r14, r15 */

static void x64_mov_reg_rdi_off(JitBuffer *b, int reg_idx, int local_idx) {
    int offset = local_idx * 8;
    int reg = jit_regs[reg_idx];
    uint8_t rex = 0x48;
    if (reg >= 8) rex |= 0x04; /* REX.R for destination register */
    jit_emit_byte(b, rex);
    jit_emit_byte(b, 0x8B);
    /* ModRM: [rdi + disp32] -> Mod=10 (disp32), Reg=reg&7, RM=111 (rdi) */
    uint8_t modrm = 0x80 | ((reg & 7) << 3) | 0x07;
    jit_emit_byte(b, modrm);
    jit_emit_uint32(b, offset);
}

static void x64_mov_rdi_off_reg(JitBuffer *b, int local_idx, int reg_idx) {
    int offset = local_idx * 8;
    int reg = jit_regs[reg_idx];
    uint8_t rex = 0x48;
    if (reg >= 8) rex |= 0x04; /* REX.R for source register */
    jit_emit_byte(b, rex);
    jit_emit_byte(b, 0x89);
    uint8_t modrm = 0x80 | ((reg & 7) << 3) | 0x07;
    jit_emit_byte(b, modrm);
    jit_emit_uint32(b, offset);
}

static void jit_compile(Bytecode *bc) {
    if (bc->jit_code || bc->jit_failed) return;
    JitBuffer b; jit_buf_init(&b);
    
    /* Prologue: push rbp; mov rbp, rsp; push rbx, r12, r13, r14, r15 */
    x64_push_rbp(&b); x64_mov_rbp_rsp(&b);
    jit_emit_byte(&b, 0x53); /* push rbx */
    uint8_t push_ext[] = {0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57}; /* push r12-r15 */
    jit_emit_bytes(&b, push_ext, 8);

    /* Load hot locals 0-4 into registers from memory (%rdi is base) */
    for (int i = 0; i < JIT_REG_LOCALS; i++) {
        x64_mov_reg_rdi_off(&b, i, i);
    }

    /* Default return value: VAL_NONE */
    x64_mov_rax_imm64(&b, MASK_NAN | ((uint64_t)VAL_NONE << 48));
    x64_push_rax(&b);

    int *target_offsets = malloc(sizeof(int) * (bc->count + 1));
    typedef struct { int target_idx; int patch_offset; } Reloc;
    Reloc *relocs = malloc(sizeof(Reloc) * bc->count);
    int reloc_count = 0;

    for (int i = 0; i < bc->count; i++) {
        target_offsets[i] = b.size;
        Instruction *inst = &bc->code[i];
        switch (inst->op) {
            case OP_CONST_INT: x64_mov_rax_imm64(&b, value_int(inst->arg)); x64_push_rax(&b); break;
            
            case OP_LOAD_LOCAL:
                if (inst->arg < JIT_REG_LOCALS) {
                    /* mov rax, <reg> 
                       Using 0x89 (mov r/m, r): RM=rax(0), Reg=reg. 
                       REX.R = reg >> 3, REX.B = 0.
                    */
                    int reg = jit_regs[inst->arg];
                    uint8_t rex = 0x48;
                    if (reg >= 8) rex |= 0x04; /* REX.R for source reg */
                    jit_emit_byte(&b, rex);
                    jit_emit_byte(&b, 0x89);
                    jit_emit_byte(&b, 0xc0 | ((reg & 7) << 3));
                    x64_push_rax(&b);
                } else {
                    x64_mov_rax_rdi_off(&b, inst->arg);
                    x64_push_rax(&b);
                }
                break;

            case OP_STORE_LOCAL:
                x64_pop_rax(&b);
                if (inst->arg < JIT_REG_LOCALS) {
                    /* mov <reg>, rax 
                       Using 0x89 (mov r/m, r): RM=reg, Reg=rax(0).
                       REX.R = 0, REX.B = reg >> 3.
                    */
                    int reg = jit_regs[inst->arg];
                    uint8_t rex = 0x48;
                    if (reg >= 8) rex |= 0x01; /* REX.B for dest reg */
                    jit_emit_byte(&b, rex);
                    jit_emit_byte(&b, 0x89);
                    jit_emit_byte(&b, 0xc0 | (reg & 7));
                } else {
                    x64_mov_rdi_off_rax(&b, inst->arg);
                }
                break;

            case OP_POP: x64_pop_rax(&b); break;
            
            case OP_ADD:
                x64_pop_rcx(&b); x64_pop_rax(&b);
                /* Optimized Integer Add: rax = rax + rcx (keeping low 32 bits and tag) */
                x64_add_rax_rcx(&b);
                {
                    /* move mask to rcx */
                    x64_mov_rcx_imm64(&b, 0xFFFFFFFF);
                    /* and rax, rcx */
                    uint8_t and_op[] = {0x48, 0x21, 0xc8}; jit_emit_bytes(&b, and_op, 3);
                    x64_mov_rcx_imm64(&b, MASK_NAN | ((uint64_t)VAL_INT << 48));
                    /* or rax, rcx */
                    uint8_t or_op[] = {0x48, 0x09, 0xc8}; jit_emit_bytes(&b, or_op, 3);
                }
                x64_push_rax(&b);
                break;

            case OP_LT:
                x64_pop_rcx(&b); x64_pop_rax(&b);
                uint8_t cmp_op[] = {0x39, 0xc8}; jit_emit_bytes(&b, cmp_op, 2);
                x64_setl_rax(&b);
                {
                    x64_mov_rcx_imm64(&b, MASK_NAN | ((uint64_t)VAL_BOOL << 48));
                    uint8_t or_op[] = {0x48, 0x09, 0xc8}; jit_emit_bytes(&b, or_op, 3);
                }
                x64_push_rax(&b);
                break;

            case OP_JUMP:
                jit_emit_byte(&b, 0xe9);
                if (inst->arg < i) {
                    int joff = target_offsets[inst->arg] - (b.size + 4);
                    jit_emit_uint32(&b, (uint32_t)joff);
                } else {
                    relocs[reloc_count++] = (Reloc){inst->arg, b.size};
                    jit_emit_uint32(&b, 0);
                }
                break;

            case OP_JUMP_IF_FALSE:
                uint8_t peek_op[] = {0x48, 0x8b, 0x04, 0x24}; jit_emit_bytes(&b, peek_op, 4);
                uint8_t test_op[] = {0x48, 0x83, 0xe0, 0x01, 0x48, 0x83, 0xf8, 0x00}; jit_emit_bytes(&b, test_op, 8);
                jit_emit_byte(&b, 0x0f); jit_emit_byte(&b, 0x84);
                if (inst->arg < i) {
                    int joff = target_offsets[inst->arg] - (b.size + 4);
                    jit_emit_uint32(&b, (uint32_t)joff);
                } else {
                    relocs[reloc_count++] = (Reloc){inst->arg, b.size};
                    jit_emit_uint32(&b, 0);
                }
                break;

            case OP_RETURN:
                x64_pop_rax(&b);
                /* Jump to epilogue instead of C goto, so we can compile multiple returns */
                jit_emit_byte(&b, 0xe9);
                relocs[reloc_count++] = (Reloc){bc->count, b.size};
                jit_emit_uint32(&b, 0);
                break;

            default:
                bc->jit_failed = 1; free(target_offsets); free(relocs); free(b.code); return;
        }
    }

    target_offsets[bc->count] = b.size;

label_JIT_EPILOGUE:
    /* Restore registers back to memory before exiting JIT */
    for (int j = 0; j < JIT_REG_LOCALS; j++) {
        x64_mov_rdi_off_reg(&b, j, j);
    }
    
    /* Safely reset stack pointer before popping callee-saved registers.
       We pushed rbx (1) and r12-r15 (4) = 5 registers = 40 bytes.
       So rsp must be rbp - 40.
       lea rsp, [rbp - 40]
    */
    uint8_t reset_rsp[] = {0x48, 0x8d, 0x65, 0xd8}; /* d8 is -40 in two's complement 8-bit */
    jit_emit_bytes(&b, reset_rsp, 4);

    /* Epilogue: pop r15-r12, rbx, rbp */
    uint8_t pop_ext[] = {0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c};
    jit_emit_bytes(&b, pop_ext, 8);
    jit_emit_byte(&b, 0x5b); /* pop rbx */
    x64_pop_rbp(&b);
    x64_ret(&b);

label_FINISH_JIT:
    for (int j = 0; j < reloc_count; j++) {
        int tidx = relocs[j].target_idx; int poff = relocs[j].patch_offset;
        if (tidx <= bc->count && target_offsets[tidx] != -1) {
            int joff = target_offsets[tidx] - (poff + 4); uint8_t *p = b.code + poff;
            p[0] = joff & 0xFF; p[1] = (joff >> 8) & 0xFF; p[2] = (joff >> 16) & 0xFF; p[3] = (joff >> 24) & 0xFF;
        }
    }
    bc->jit_size = b.size;
    bc->jit_code = mmap(NULL, bc->jit_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memcpy(bc->jit_code, b.code, bc->jit_size);
    free(target_offsets); free(relocs); free(b.code);
}

static Value run_bytecode(Bytecode *bc, int base_offset) {
    if (bc->jit_code && base_offset == 0 && csp > 0) {
        CallFrame *cf = &call_stack[csp-1];
        if (cf->local_values) {
            return ((Value (*)(Value*))bc->jit_code)(cf->local_values);
        }
    }
    static void* dispatch_table[] = {
        &&label_OP_CONST_INT, &&label_OP_CONST_FLOAT, &&label_OP_CONST_STRING, &&label_OP_CONST_BOOL,
        &&label_OP_LOAD_VAR, &&label_OP_STORE_VAR,
        &&label_OP_ADD, &&label_OP_SUB, &&label_OP_MUL, &&label_OP_DIV, &&label_OP_MOD, &&label_OP_POW, &&label_OP_NEG,
        &&label_OP_LT, &&label_OP_GT, &&label_OP_LTE, &&label_OP_GTE, &&label_OP_EQ, &&label_OP_NEQ,
        &&label_OP_AND, &&label_OP_OR, &&label_OP_NOT,
        &&label_OP_STRING_CONCAT,
        &&label_OP_JUMP, &&label_OP_JUMP_IF_FALSE, &&label_OP_JUMP_IF_TRUE,
        &&label_OP_CALL_BUILTIN_LEN, &&label_OP_CALL_BUILTIN_RANGE, &&label_OP_CALL_BUILTIN_TYPE,
        &&label_OP_BUILTIN_APPEND, &&label_OP_BUILTIN_INPUT, &&label_OP_BUILTIN_SHOW, &&label_OP_BUILTIN_WRITE,
        &&label_OP_LIST_LITERAL, &&label_OP_DICT_LITERAL,
        &&label_OP_INDEX, &&label_OP_SLICE, &&label_OP_STORE_INDEX,
        &&label_OP_FUNC_DEF, &&label_OP_CALL, &&label_OP_RETURN,
        &&label_OP_FOR_EACH, &&label_OP_FOR_EACH_PAIR,
        &&label_OP_STOP, &&label_OP_SKIP,
        &&label_OP_POP,
        &&label_OP_PUSH_SCOPE, &&label_OP_POP_SCOPE, &&label_OP_STORE_LET,
        &&label_OP_LOAD_LOCAL, &&label_OP_STORE_LOCAL,
        &&label_OP_DICT_KEYS, &&label_OP_DICT_VALUES, &&label_OP_DICT_HAS, &&label_OP_DICT_REMOVE, &&label_OP_DICT_MERGE,
        &&label_OP_MATH_ABS, &&label_OP_MATH_MIN, &&label_OP_MATH_MAX, &&label_OP_MATH_ROUND, &&label_OP_MATH_FLOOR, &&label_OP_MATH_CEIL, &&label_OP_MATH_SQRT,
        &&label_OP_STR_UPPER, &&label_OP_STR_LOWER, &&label_OP_STR_TRIM, &&label_OP_STR_FIND, &&label_OP_STR_REPLACE, &&label_OP_STR_STARTSWITH, &&label_OP_STR_ENDSWITH, &&label_OP_STR_SPLIT,
        &&label_OP_LIST_PUSH, &&label_OP_LIST_POP, &&label_OP_LIST_INSERT, &&label_OP_LIST_REMOVE, &&label_OP_LIST_CONTAINS, &&label_OP_LIST_SORT, &&label_OP_LIST_REVERSE,
        &&label_OP_FILE_READ, &&label_OP_FILE_WRITE, &&label_OP_FILE_APPEND,
        &&label_OP_FSTRING,
        &&label_OP_LIST_COMP,
        &&label_OP_THROW, &&label_OP_TRY_START, &&label_OP_TRY_END,
        &&label_OP_LAMBDA,
        &&label_OP_CALL_VALUE,
        &&label_OP_CLASS_DEF,
        &&label_OP_METHOD_DEF,
        &&label_OP_TRAIT_DEF,
        &&label_OP_USE_TRAIT,
        &&label_OP_GET_PROPERTY,
        &&label_OP_SET_PROPERTY,
        &&label_OP_SPAWN
    };

    int ip = 0;
    int base_sp = sp;
    vm_loop_stop = 0; vm_loop_skip = 0; vm_has_return = 0;

    #define DISPATCH() \
        do { \
            static __thread int yield_ctr = 0; \
            if (++yield_ctr > 100) { \
                yield_ctr = 0; \
                pthread_mutex_unlock(&glow_gil); \
                sched_yield(); \
                pthread_mutex_lock(&glow_gil); \
            } \
            if (ip >= bc->count) goto label_EXIT_VM; \
            if (vm_exception_active) goto label_HANDLE_EXCEPTION; \
            if (base_offset > 0 && (vm_loop_stop || vm_loop_skip)) goto label_EXIT_VM; \
            goto *dispatch_table[bc->code[ip].op]; \
        } while (0)

    DISPATCH();

label_HANDLE_EXCEPTION:
    if (hsp > 0) {
        if (handler_stack[hsp-1].csp == csp) {
            TryHandler h = handler_stack[--hsp];
            vm_exception_active = 0;
            ip = h.catch_ip; sp = h.sp;
            var_restore_shadow(h.shadow_list);
            PUSH(vm_exception_value);
            DISPATCH();
        } else {
            if (csp > 0) {
                csp--; CallFrame *cf = &call_stack[csp];
                if ((cf->pending_fn && cf->pending_fn->uses_let) || cf->pending_closure) var_restore_shadow(cf->old_shadow_list);
                if (cf->param_count > 0) var_set(cf->p0, cf->v0);
                if (cf->param_count > 1) var_set(cf->p1, cf->v1);
                if (cf->local_values && cf->local_count > 16) free(cf->local_values);
                bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
                vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
                DISPATCH();
            } else {
                printf("Unhandled exception: ");
                if (GET_TYPE(vm_exception_value) == VAL_STRING) printf("%s\n", AS_STRING(vm_exception_value));
                else printf("type %s\n", type_name(GET_TYPE(vm_exception_value)));
                exit(1);
            }
        }
    } else {
        printf("Unhandled exception at top level\n");
        exit(1);
    }

label_OP_CONST_INT:
    PUSH(value_int(bc->code[ip].arg)); ip++; DISPATCH();

label_OP_CONST_FLOAT:
    PUSH(value_float(*(double*)bc->code[ip].ptr)); ip++; DISPATCH();

label_OP_CONST_STRING:
    PUSH((Value)bc->code[ip].ptr); ip++; DISPATCH();

label_OP_CONST_BOOL:
    PUSH(value_bool(bc->code[ip].arg)); ip++; DISPATCH();

label_OP_LOAD_VAR:
    PUSH(var_get((const char*)bc->code[ip].ptr)); ip++; DISPATCH();

label_OP_STORE_VAR:
    var_set((const char*)bc->code[ip].ptr, POP()); ip++; DISPATCH();

label_OP_LOAD_LOCAL: {
    int idx = bc->code[ip].arg;
    if (csp > 0) {
        CallFrame *cf = &call_stack[csp-1];
        if (cf->local_values && idx < cf->local_count) PUSH(cf->local_values[idx]);
        else PUSH(value_int(0));
    } else PUSH(var_get((const char*)bc->code[ip].ptr));
    ip++; DISPATCH();
}

label_OP_STORE_LOCAL: {
    int idx = bc->code[ip].arg; Value v = POP();
    if (csp > 0) {
        CallFrame *cf = &call_stack[csp-1];
        if (cf->local_values && idx < cf->local_count) cf->local_values[idx] = v;
    } else var_set((const char*)bc->code[ip].ptr, v);
    ip++; DISPATCH();
}

label_OP_ADD: {
    Value b = vm_stack[sp-1];
    Value a = vm_stack[sp-2];
    if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) {
        Value res = value_int(AS_INT(a) + AS_INT(b));
        sp -= 2; PUSH(res);
    } else if (GET_TYPE(a) == VAL_FLOAT && GET_TYPE(b) == VAL_FLOAT) {
        Value res = value_float(AS_FLOAT(a) + AS_FLOAT(b));
        sp -= 2; PUSH(res);
    } else if (GET_TYPE(a) == VAL_STRING && GET_TYPE(b) == VAL_STRING) {
        char *s = malloc(strlen(AS_STRING(a)) + strlen(AS_STRING(b)) + 1);
        strcpy(s, AS_STRING(a)); strcat(s, AS_STRING(b));
        Value res = value_string(s);
        free(s);
        sp -= 2; PUSH(res);
    } else if (GET_TYPE(a) == VAL_LIST && GET_TYPE(b) == VAL_LIST) {
        List *l1 = AS_LIST(a), *l2 = AS_LIST(b);
        List *res_l = list_new(l1->size + l2->size);
        /* Re-fetch l1, l2 as GC might have moved them (though list_new doesn't move objects yet, it's good practice) */
        l1 = AS_LIST(vm_stack[sp-2]);
        l2 = AS_LIST(vm_stack[sp-1]);
        for (int i = 0; i < l1->size; i++) res_l->items[i] = l1->items[i];
        for (int i = 0; i < l2->size; i++) res_l->items[l1->size + i] = l2->items[i];
        res_l->size = l1->size + l2->size;
        Value res = value_list(res_l);
        sp -= 2; PUSH(res);
    } else {
        double da = val_to_int_or_float(a); double db = val_to_int_or_float(b);
        Value res = value_float(da + db);
        sp -= 2; PUSH(res);
    }
    ip++; DISPATCH();
}

label_OP_SUB: {
    Value b = POP(), a = POP();
    if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) PUSH(value_int(AS_INT(a) - AS_INT(b)));
    else PUSH(value_float(val_to_int_or_float(a) - val_to_int_or_float(b)));
    ip++; DISPATCH();
}

label_OP_MUL: {
    Value b = POP(), a = POP();
    if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) PUSH(value_int(AS_INT(a) * AS_INT(b)));
    else PUSH(value_float(val_to_int_or_float(a) * val_to_int_or_float(b)));
    ip++; DISPATCH();
}

label_OP_DIV: {
    Value b = POP(), a = POP();
    double da = val_to_int_or_float(a); double db = val_to_int_or_float(b);
    if (db == 0.0) { printf("Error: Division by zero\n"); PUSH(value_float(0)); }
    else PUSH(value_float(da / db));
    ip++; DISPATCH();
}

label_OP_MOD: {
    Value b = POP(), a = POP();
    if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) {
        if (AS_INT(b) == 0) { printf("Error: Modulo by zero\n"); PUSH(value_int(0)); }
        else PUSH(value_int(AS_INT(a) % AS_INT(b)));
    } else {
        double da = val_to_int_or_float(a); double db = val_to_int_or_float(b);
        if (db == 0.0) { printf("Error: Modulo by zero\n"); PUSH(value_float(0)); }
        else PUSH(value_float(fmod(da, db)));
    }
    ip++; DISPATCH();
}

label_OP_POW: {
    Value b = POP(), a = POP();
    PUSH(value_float(pow(val_to_int_or_float(a), val_to_int_or_float(b))));
    ip++; DISPATCH();
}

label_OP_NEG: {
    Value a = POP();
    if (GET_TYPE(a) == VAL_INT) PUSH(value_int(-AS_INT(a)));
    else PUSH(value_float(-val_to_int_or_float(a)));
    ip++; DISPATCH();
}

label_OP_LT: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_int_or_float(a) < val_to_int_or_float(b))); ip++; DISPATCH(); }
label_OP_GT: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_int_or_float(a) > val_to_int_or_float(b))); ip++; DISPATCH(); }
label_OP_LTE: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_int_or_float(a) <= val_to_int_or_float(b))); ip++; DISPATCH(); }
label_OP_GTE: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_int_or_float(a) >= val_to_int_or_float(b))); ip++; DISPATCH(); }

label_OP_EQ: {
    Value b = POP(), a = POP(); int res = 0;
    if (GET_TYPE(a) == GET_TYPE(b)) {
        switch (GET_TYPE(a)) {
            case VAL_INT: res = AS_INT(a) == AS_INT(b); break;
            case VAL_FLOAT: res = AS_FLOAT(a) == AS_FLOAT(b); break;
            case VAL_BOOL: res = AS_BOOL(a) == AS_BOOL(b); break;
            case VAL_STRING: res = strcmp(AS_STRING(a), AS_STRING(b)) == 0; break;
            default: res = (a == b); break;
        }
    } else res = (val_to_int_or_float(a) == val_to_int_or_float(b));
    PUSH(value_bool(res)); ip++; DISPATCH();
}

label_OP_NEQ: {
    Value b = POP(), a = POP(); PUSH(value_bool(!val_eq(a, b))); ip++; DISPATCH();
}

label_OP_AND: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_bool(a) && val_to_bool(b))); ip++; DISPATCH(); }
label_OP_OR: { Value b = POP(), a = POP(); PUSH(value_bool(val_to_bool(a) || val_to_bool(b))); ip++; DISPATCH(); }
label_OP_NOT: PUSH(value_bool(!val_to_bool(POP()))); ip++; DISPATCH();

label_OP_STRING_CONCAT: {
    Value b = vm_stack[sp-1];
    Value a = vm_stack[sp-2];
    /* Keep a and b on stack while allocating to protect them from GC */
    int len_a = strlen(AS_STRING(a));
    int len_b = strlen(AS_STRING(b));
    char *s = malloc(len_a + len_b + 1);
    strcpy(s, AS_STRING(a)); strcat(s, AS_STRING(b));
    Value res = value_string(s);
    free(s);
    sp -= 2;
    PUSH(res);
    ip++; DISPATCH();
}

label_OP_JUMP: {
    int target = bc->code[ip].arg - base_offset;
    if (target <= ip && !bc->jit_code && !bc->jit_failed) {
        if (++bc->code[ip].exec_count > JIT_THRESHOLD) jit_compile(bc);
    }
    ip = target; DISPATCH();
}

label_OP_JUMP_IF_FALSE: {
    if (!val_to_bool(vm_stack[sp-1])) {
        int target = bc->code[ip].arg - base_offset;
        if (target <= ip && !bc->jit_code && !bc->jit_failed) {
            if (++bc->code[ip].exec_count > JIT_THRESHOLD) jit_compile(bc);
        }
        ip = target;
    } else ip++;
    DISPATCH();
}

label_OP_JUMP_IF_TRUE:
    if (val_to_bool(vm_stack[sp-1])) ip = bc->code[ip].arg - base_offset; else ip++;
    DISPATCH();

label_OP_CALL_BUILTIN_LEN: {
    Value v = vm_stack[sp-1];
    int len = 0;
    if (GET_TYPE(v) == VAL_LIST) len = AS_LIST(v)->size;
    else if (GET_TYPE(v) == VAL_STRING) len = (int)strlen(AS_STRING(v));
    else if (GET_TYPE(v) == VAL_DICT) len = AS_DICT(v)->size;
    sp--; PUSH(value_int(len)); ip++; DISPATCH();
}

label_OP_CALL_BUILTIN_RANGE: {
    int argc = bc->code[ip].arg;
    int start = 0, end = 0;
    if (argc == 2) {
        end = val_to_int(POP());
        start = val_to_int(POP());
    } else {
        end = val_to_int(POP());
    }
    int size = end - start;
    if (size < 0) size = 0;
    List *l = list_new(size);
    for (int i = 0; i < size; i++) l->items[i] = value_int(start + i);
    l->size = size;
    PUSH(value_list(l));
    ip++; DISPATCH();
}

label_OP_CALL_BUILTIN_TYPE: {
    Value v = vm_stack[sp-1];
    Value res = value_string(type_name(GET_TYPE(v)));
    sp--; PUSH(res);
    ip++; DISPATCH();
}

label_OP_BUILTIN_APPEND: {
    Value item = vm_stack[sp-1];
    Value lv = vm_stack[sp-2];
    if (GET_TYPE(lv) == VAL_LIST) list_push(AS_LIST(lv), item);
    sp -= 2; PUSH(lv); ip++; DISPATCH();
}

label_OP_BUILTIN_INPUT: {
    if (bc->code[ip].arg == 1) {
        Value p = POP();
        if (GET_TYPE(p) == VAL_STRING) {
            printf("%s", AS_STRING(p));
            fflush(stdout);
        }
    }
    char buf[4096]; if (fgets(buf, sizeof(buf), stdin)) {
        int len = (int)strlen(buf); while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
        PUSH(value_string(buf));
    } else PUSH(value_string(""));
    ip++; DISPATCH();
}

label_OP_BUILTIN_SHOW: {
    int n = bc->code[ip].arg; if (n == 0) printf("\n");
    else { for (int i = 0; i < n; i++) { print_value(POP()); if (i < n-1) printf(" "); } printf("\n"); }
    ip++; DISPATCH();
}

label_OP_BUILTIN_WRITE: {
    int n = bc->code[ip].arg; for (int i = 0; i < n; i++) { print_value(POP()); if (i < n-1) printf(" "); }
}

label_OP_LIST_LITERAL: {
    int n = bc->code[ip].arg; List *l = list_new(n);
    PUSH(value_list(l));
    for (int i = n - 1; i >= 0; i--) {
        AS_LIST(vm_stack[sp-1])->items[i] = vm_stack[sp - n - 1 + i];
    }
    AS_LIST(vm_stack[sp-1])->size = n;
    Value res = vm_stack[sp-1];
    sp -= (n + 1);
    PUSH(res);
    ip++; DISPATCH();
}

label_OP_DICT_LITERAL: {
    int n = bc->code[ip].arg; 
    Dict *d = dict_new(n);
    /* Items are on stack: [k1, v1, k2, v2, ..., kn, vn] */
    /* We need to pop them in reverse order */
    for (int i = 0; i < n; i++) {
        Value v = POP();
        Value k = POP();
        if (GET_TYPE(k) == VAL_STRING) {
            dict_set(d, AS_STRING(k), v);
        }
    }
    PUSH(value_dict(d));
    ip++; DISPATCH();
}

label_OP_INDEX: {
    Value idx = vm_stack[sp-1], coll = vm_stack[sp-2];
    if (GET_TYPE(coll) == VAL_LIST) {
        int i = val_to_int(idx); if (i < 0) i += AS_LIST(coll)->size;
        if (i < 0 || i >= AS_LIST(coll)->size) { sp -= 2; PUSH(value_int(0)); }
        else { Value val = AS_LIST(coll)->items[i]; sp -= 2; PUSH(val); }
    } else if (GET_TYPE(coll) == VAL_STRING) {
        int i = val_to_int(idx); int slen = (int)strlen(AS_STRING(coll)); if (i < 0) i += slen;
        if (i < 0 || i >= slen) { sp -= 2; PUSH(value_int(0)); }
        else { 
            char b[2] = {AS_STRING(coll)[i], 0}; 
            Value res = value_string(b);
            sp -= 2; PUSH(res);
        }
    } else if (GET_TYPE(coll) == VAL_DICT) {
        int found = 0; for (int i = 0; i < AS_DICT(coll)->size; i++) {
            if (strcmp(AS_DICT(coll)->keys[i], AS_STRING(idx)) == 0) { 
                Value val = AS_DICT(coll)->values[i];
                sp -= 2; PUSH(val); found = 1; break; 
            }
        }
        if (!found) { sp -= 2; PUSH(value_int(0)); }
    } else { sp -= 2; PUSH(value_int(0)); }
    ip++; DISPATCH();
}

label_OP_SLICE: {
    Value eV = vm_stack[sp-1], sV = vm_stack[sp-2], src = vm_stack[sp-3];
    int slen = (GET_TYPE(src) == VAL_STRING) ? (int)strlen(AS_STRING(src)) : ((GET_TYPE(src) == VAL_LIST) ? AS_LIST(src)->size : 0);
    int s = (GET_TYPE(sV) == VAL_INT && AS_INT(sV) == -1) ? 0 : val_to_int(sV);
    int e = (GET_TYPE(eV) == VAL_INT && AS_INT(eV) == -1) ? slen : val_to_int(eV);
    if (s < 0) s += slen; if (e < 0) e += slen; if (s < 0) s = 0; if (e > slen) e = slen; if (s > e) s = e;
    
    if (GET_TYPE(src) == VAL_STRING) { 
        char *sub = malloc(e - s + 1); strncpy(sub, AS_STRING(src) + s, e - s); sub[e-s] = 0; 
        Value res = value_string(sub);
        free(sub);
        sp -= 3; PUSH(res); 
    }
    else if (GET_TYPE(src) == VAL_LIST) {
        List *rl = list_new(e - s); 
        src = vm_stack[sp-3]; // re-fetch as list_new could trigger GC
        for (int i = 0; i < e - s; i++) rl->items[i] = AS_LIST(src)->items[s + i];
        rl->size = e - s;
        sp -= 3; PUSH(value_list(rl));
    } else { sp -= 3; PUSH(value_int(0)); }
    ip++; DISPATCH();
}

label_OP_STORE_INDEX: {
    Value v = POP(), idx = POP(), coll = POP();
    if (GET_TYPE(coll) == VAL_LIST) {
        int i = val_to_int(idx); if (i < 0) i += AS_LIST(coll)->size;
        if (i >= 0 && i < AS_LIST(coll)->size) AS_LIST(coll)->items[i] = v;
    } else if (GET_TYPE(coll) == VAL_DICT && GET_TYPE(idx) == VAL_STRING) dict_set(AS_DICT(coll), AS_STRING(idx), v);
    ip++; DISPATCH();
}

label_OP_FUNC_DEF: ip++; DISPATCH();

label_OP_CALL: {
    FuncEntry *fn = func_get((const char*)bc->code[ip].ptr);
    if (fn) {
        if (csp >= CALL_STACK_SIZE-1) exit(1);
        CallFrame *cf = &call_stack[csp++]; cf->bc = bc; cf->ip = ip + 1; cf->base_sp = base_sp;
        cf->old_has_return = vm_has_return; cf->old_return_value = vm_return_value;
        cf->pending_fn = fn; cf->pending_closure = NULL; cf->local_count = fn->local_count;
        if (cf->local_count > 0) {
            if (cf->local_count <= 16) { cf->local_values = cf->inline_locals; for (int i=0; i<cf->local_count; i++) cf->local_values[i] = value_none(); }
            else cf->local_values = calloc(cf->local_count, sizeof(Value));
        } else cf->local_values = NULL;
        if (fn->uses_let) { cf->old_shadow_list = let_shadow_list; var_push_scope(); } else cf->old_shadow_list = NULL;
        for (int i=fn->param_count-1; i>=0; i--) { Value arg = POP(); if (cf->local_values && i < cf->local_count) cf->local_values[i] = arg; else var_set(fn->params[i], arg); }
        
        Value rv = run_bytecode(fn->body, 0);

        /* Restore caller state */
        csp--;
        cf = &call_stack[csp];
        if (fn->uses_let) var_restore_shadow(cf->old_shadow_list);
        if (cf->param_count > 0) var_set(cf->p0, cf->v0); if (cf->param_count > 1) var_set(cf->p1, cf->v1);
        if (cf->local_values && cf->local_count > 16) free(cf->local_values);
        bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
        vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
        PUSH(rv); DISPATCH();
    } else {
        Value v = var_get((const char*)bc->code[ip].ptr);
        if (GET_TYPE(v) == VAL_CLOSURE) {
            Closure *c = AS_CLOSURE(v); if (csp >= CALL_STACK_SIZE-1) exit(1);
            CallFrame *cf = &call_stack[csp++]; cf->bc = bc; cf->ip = ip + 1; cf->base_sp = base_sp;
            cf->old_has_return = vm_has_return; cf->old_return_value = vm_return_value;
            cf->pending_fn = NULL; cf->pending_closure = c; cf->old_shadow_list = let_shadow_list;
            for (int i=0; i<c->captured_count; i++) var_set_let(c->captured_names[i], c->captured_values[i]);
            Value inline_args[8]; Value *ab = (c->param_count > 8) ? malloc(sizeof(Value)*c->param_count) : inline_args;
            for (int i=c->param_count-1; i>=0; i--) ab[i] = POP();
            cf->param_count = c->param_count < 2 ? c->param_count : 2;
            if (cf->param_count > 0) { cf->p0 = c->params[0]; cf->v0 = var_get(c->params[0]); }
            if (cf->param_count > 1) { cf->p1 = c->params[1]; cf->v1 = var_get(c->params[1]); }
            for (int i=0; i<c->param_count; i++) var_set_let(c->params[i], ab[i]);
            if (c->param_count > 8) free(ab);
            Value rv = run_bytecode(c->body, 0);
            csp--; cf = &call_stack[csp]; var_restore_shadow(cf->old_shadow_list);
            if (cf->param_count > 0) var_set(cf->p0, cf->v0); if (cf->param_count > 1) var_set(cf->p1, cf->v1);
            bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
            vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
            PUSH(rv); DISPATCH();
        } else if (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD) && GET_OBJ_TYPE(v) == OBJ_CLASS) {
            Class *c = AS_CLASS(v);
            Instance *i = instance_new(c);
            PUSH(value_instance(i)); ip++; DISPATCH();
        }
        ip++; DISPATCH();
    }
}

label_OP_RETURN: {
    total_returns++; Value rv = (sp > base_sp) ? POP() : value_none(); 
    return rv;
}

label_OP_FOR_EACH: {
    Value iter = POP(); const char *vn = (const char*)bc->code[ip].ptr;
    int bs = ip + 2; int be = bc->code[ip].arg;
    if (GET_TYPE(iter) == VAL_LIST) {
        for (int i=0; i<AS_LIST(iter)->size; i++) {
            vm_loop_skip = 0; 
            int found_local = -1;
            if (csp > 0) {
                CallFrame *outer = &call_stack[csp-1];
                if (outer->pending_fn) {
                    for (int j=0; j<outer->pending_fn->local_count; j++) {
                        if (strcmp(outer->pending_fn->local_names[j], vn) == 0) { found_local = j; break; }
                    }
                }
            }
            if (found_local >= 0) {
                call_stack[csp-1].local_values[found_local] = AS_LIST(iter)->items[i];
            } else {
                var_set(vn, AS_LIST(iter)->items[i]);
            } 

            int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
            Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
            Value was_rv = run_bytecode(&bbc, bs);
            if (vm_exception_active) break;
            int was_stop = vm_loop_stop, was_skip = vm_loop_skip;
            sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
            if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
            if (was_skip) continue; if (was_stop) break; 
        }
    } else if (GET_TYPE(iter) == VAL_STRING) {
        int len = strlen(AS_STRING(iter)); for (int i=0; i<len; i++) {
            char b[2] = {AS_STRING(iter)[i], 0}; var_set(vn, value_string(strdup(b))); 
            int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
            Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
            Value was_rv = run_bytecode(&bbc, bs);
            if (vm_exception_active) break;
            int was_stop = vm_loop_stop, was_skip = vm_loop_skip;
            sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
            if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
            if (was_skip) continue; if (was_stop) break; 
        }
    } else if (GET_TYPE(iter) == VAL_DICT) {
        for (int i=0; i<AS_DICT(iter)->size; i++) {
            var_set(vn, value_string(AS_DICT(iter)->keys[i])); 
            int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
            Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
            Value was_rv = run_bytecode(&bbc, bs);
            if (vm_exception_active) break;
            int was_stop = vm_loop_stop, was_skip = vm_loop_skip;
            sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
            if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
            if (was_skip) continue; if (was_stop) break; 
        }
    }
    ip = be - base_offset; DISPATCH();
}

label_OP_FOR_EACH_PAIR: {
    Value iter = POP(); const char *kn = (const char*)bc->code[ip].ptr; const char *vn = (const char*)bc->code[ip+1].ptr;
    int bs = ip + 2; int be = bc->code[ip].arg;
    if (GET_TYPE(iter) == VAL_LIST) {
        for (int i=0; i<AS_LIST(iter)->size; i++) {
            var_set(kn, value_int(i)); var_set(vn, AS_LIST(iter)->items[i]); 
            int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
            Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
            Value was_rv = run_bytecode(&bbc, bs);
            if (vm_exception_active) break;
            int was_stop = vm_loop_stop, was_skip = vm_loop_skip;
            sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
            if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
            if (was_skip) continue; if (was_stop) break; 
        }
    } else if (GET_TYPE(iter) == VAL_DICT) {
        for (int i=0; i<AS_DICT(iter)->size; i++) {
            var_set(kn, value_string(AS_DICT(iter)->keys[i])); var_set(vn, AS_DICT(iter)->values[i]); 
            int ssp = sp; int old_stop = vm_loop_stop, old_skip = vm_loop_skip, old_ret = vm_has_return; Value old_rv = vm_return_value;
            Bytecode bbc; bbc.code = &bc->code[bs]; bbc.count = be - bs; 
            Value was_rv = run_bytecode(&bbc, bs);
            if (vm_exception_active) break;
            int was_stop = vm_loop_stop, was_skip = vm_loop_skip;
            sp = ssp; vm_loop_stop = old_stop; vm_loop_skip = old_skip; vm_has_return = old_ret; vm_return_value = old_rv;
            if (GET_TYPE(was_rv) != VAL_NONE) { vm_has_return = 1; vm_return_value = was_rv; return was_rv; }
            if (was_skip) continue; if (was_stop) break; 
        }
    }
    ip = be - base_offset; DISPATCH();
}

label_OP_STOP: vm_loop_stop = 1; ip++; DISPATCH();
label_OP_SKIP: vm_loop_skip = 1; ip++; DISPATCH();
label_OP_POP: POP(); ip++; DISPATCH();
label_OP_PUSH_SCOPE: var_push_scope(); ip++; DISPATCH();
label_OP_POP_SCOPE: var_pop_scope(); ip++; DISPATCH();
label_OP_STORE_LET: var_set_let((const char*)bc->code[ip].ptr, POP()); ip++; DISPATCH();

label_OP_DICT_KEYS: {
    Value d = vm_stack[sp-1];
    if (GET_TYPE(d) != VAL_DICT) { sp--; PUSH(value_int(0)); }
    else {
        List *l = list_new(AS_DICT(d)->size);
        PUSH(value_list(l));
        for (int i = 0; i < AS_LIST(vm_stack[sp-1])->capacity; i++) {
            AS_LIST(vm_stack[sp-1])->items[i] = value_string(AS_DICT(vm_stack[sp-2])->keys[i]);
        }
        AS_LIST(vm_stack[sp-1])->size = AS_LIST(vm_stack[sp-1])->capacity;
        Value res = vm_stack[sp-1];
        sp -= 2;
        PUSH(res);
    }
    ip++; DISPATCH();
}
label_OP_DICT_VALUES: {
    Value d = vm_stack[sp-1];
    if (GET_TYPE(d) != VAL_DICT) { sp--; PUSH(value_int(0)); }
    else {
        List *l = list_new(AS_DICT(d)->size);
        PUSH(value_list(l));
        for (int i = 0; i < AS_LIST(vm_stack[sp-1])->capacity; i++) {
            AS_LIST(vm_stack[sp-1])->items[i] = AS_DICT(vm_stack[sp-2])->values[i];
        }
        AS_LIST(vm_stack[sp-1])->size = AS_LIST(vm_stack[sp-1])->capacity;
        Value res = vm_stack[sp-1];
        sp -= 2;
        PUSH(res);
    }
    ip++; DISPATCH();
}
label_OP_DICT_HAS: {
    Value k = POP(), d = POP(); int f = 0; if (GET_TYPE(d) == VAL_DICT && GET_TYPE(k) == VAL_STRING) { for (int i=0; i<AS_DICT(d)->size; i++) if (strcmp(AS_DICT(d)->keys[i], AS_STRING(k)) == 0) { f = 1; break; } }
    PUSH(value_bool(f)); ip++; DISPATCH();
}
label_OP_DICT_REMOVE: {
    Value k = POP(), d = POP(); if (GET_TYPE(d) == VAL_DICT && GET_TYPE(k) == VAL_STRING) { for (int i=0; i<AS_DICT(d)->size; i++) if (strcmp(AS_DICT(d)->keys[i], AS_STRING(k)) == 0) { for (int j=i; j<AS_DICT(d)->size-1; j++) { AS_DICT(d)->keys[j] = AS_DICT(d)->keys[j+1]; AS_DICT(d)->values[j] = AS_DICT(d)->values[j+1]; } AS_DICT(d)->size--; break; } }
    PUSH(d); ip++; DISPATCH();
}
label_OP_DICT_MERGE: {
    Value d2 = POP(), d1 = POP(); if (GET_TYPE(d1) == VAL_DICT && GET_TYPE(d2) == VAL_DICT) { for (int i=0; i<AS_DICT(d2)->size; i++) dict_set(AS_DICT(d1), AS_DICT(d2)->keys[i], AS_DICT(d2)->values[i]); }
    PUSH(d1); ip++; DISPATCH();
}

label_OP_MATH_ABS: {
    Value v = POP(); if (GET_TYPE(v) == VAL_INT) PUSH(value_int(abs(AS_INT(v)))); else if (GET_TYPE(v) == VAL_FLOAT) PUSH(value_float(fabs(AS_FLOAT(v)))); else PUSH(value_int(0));
    ip++; DISPATCH();
}
label_OP_MATH_MIN: {
    int n = bc->code[ip].arg; Value best = POP(); for (int i=1; i<n; i++) { Value v = POP(); if (val_to_int_or_float(v) < val_to_int_or_float(best)) best = v; }
    PUSH(best); ip++; DISPATCH();
}
label_OP_MATH_MAX: {
    int n = bc->code[ip].arg; Value best = POP(); for (int i=1; i<n; i++) { Value v = POP(); if (val_to_int_or_float(v) > val_to_int_or_float(best)) best = v; }
    PUSH(best); ip++; DISPATCH();
}
label_OP_MATH_ROUND: PUSH(value_int((int)round(val_to_int_or_float(POP())))); ip++; DISPATCH();
label_OP_MATH_FLOOR: PUSH(value_int((int)floor(val_to_int_or_float(POP())))); ip++; DISPATCH();
label_OP_MATH_CEIL: PUSH(value_int((int)ceil(val_to_int_or_float(POP())))); ip++; DISPATCH();
label_OP_MATH_SQRT: PUSH(value_float(sqrt(val_to_int_or_float(POP())))); ip++; DISPATCH();

label_OP_STR_UPPER: {
    Value s = vm_stack[sp-1];
    if (GET_TYPE(s) == VAL_STRING) {
        char *u = strdup(AS_STRING(s));
        for (char *p = u; *p; p++) *p = toupper(*p);
        Value res = value_string(u);
        free(u);
        sp--; PUSH(res);
    }
    ip++; DISPATCH();
}
label_OP_STR_LOWER: {
    Value s = vm_stack[sp-1];
    if (GET_TYPE(s) == VAL_STRING) {
        char *l = strdup(AS_STRING(s));
        for (char *p = l; *p; p++) *p = tolower(*p);
        Value res = value_string(l);
        free(l);
        sp--; PUSH(res);
    }
    ip++; DISPATCH();
}
label_OP_STR_TRIM: {
    Value s = vm_stack[sp-1];
    if (GET_TYPE(s) == VAL_STRING) {
        const char *st = AS_STRING(s);
        while (*st && isspace(*st)) st++;
        char *ed = (char*)st + strlen(st) - 1;
        while (ed > st && isspace(*ed)) ed--;
        int len = ed - st + 1;
        if (len < 0) len = 0;
        char *res_s = malloc(len + 1);
        strncpy(res_s, st, len);
        res_s[len] = 0;
        Value res = value_string(res_s);
        free(res_s);
        sp--; PUSH(res);
    }
    ip++; DISPATCH();
}
label_OP_STR_FIND: {
    Value sub = vm_stack[sp-1], s = vm_stack[sp-2];
    if (GET_TYPE(s) == VAL_STRING && GET_TYPE(sub) == VAL_STRING) {
        char *p = strstr(AS_STRING(s), AS_STRING(sub));
        sp -= 2; PUSH(value_int(p ? (int)(p - AS_STRING(s)) : -1));
    } else { sp -= 2; PUSH(value_int(-1)); }
    ip++; DISPATCH();
}
label_OP_STR_REPLACE: {
    Value r = vm_stack[sp-1], sub = vm_stack[sp-2], s = vm_stack[sp-3];
    if (GET_TYPE(s) == VAL_STRING && GET_TYPE(sub) == VAL_STRING && GET_TYPE(r) == VAL_STRING) {
        char *replaced = str_replace(AS_STRING(s), AS_STRING(sub), AS_STRING(r));
        Value res = value_string(replaced);
        free(replaced);
        sp -= 3; PUSH(res);
    }
    ip++; DISPATCH();
}
label_OP_STR_STARTSWITH: {
    Value p = vm_stack[sp-1], s = vm_stack[sp-2];
    if (GET_TYPE(s) == VAL_STRING && GET_TYPE(p) == VAL_STRING) {
        int res = strncmp(AS_STRING(s), AS_STRING(p), strlen(AS_STRING(p))) == 0;
        sp -= 2; PUSH(value_bool(res));
    } else { sp -= 2; PUSH(value_bool(0)); }
    ip++; DISPATCH();
}
label_OP_STR_ENDSWITH: {
    Value su = vm_stack[sp-1], s = vm_stack[sp-2];
    if (GET_TYPE(s) == VAL_STRING && GET_TYPE(su) == VAL_STRING) {
        int sl = strlen(AS_STRING(s)), sul = strlen(AS_STRING(su));
        int res = (sl >= sul && strcmp(AS_STRING(s) + sl - sul, AS_STRING(su)) == 0);
        sp -= 2; PUSH(value_bool(res));
    } else { sp -= 2; PUSH(value_bool(0)); }
    ip++; DISPATCH();
}
label_OP_STR_SPLIT: {
    Value sep = vm_stack[sp-1], s = vm_stack[sp-2];
    if (GET_TYPE(s) == VAL_STRING && GET_TYPE(sep) == VAL_STRING) {
        List *l = list_new(8);
        PUSH(value_list(l)); // Protect l
        char *c = strdup(AS_STRING(vm_stack[sp-3])); // s is at sp-3 now
        char *t = strtok(c, AS_STRING(vm_stack[sp-2])); // sep is at sp-2
        while (t) {
            Value part = value_string(t);
            list_push(AS_LIST(vm_stack[sp-1]), part);
            t = strtok(NULL, AS_STRING(vm_stack[sp-2]));
        }
        free(c);
        Value res = vm_stack[sp-1];
        sp -= 3; PUSH(res);
    } else {
        sp -= 2; PUSH(value_list(list_new(0)));
    }
    ip++; DISPATCH();
}

label_OP_LIST_PUSH: { Value v = POP(), l = POP(); if (GET_TYPE(l) == VAL_LIST) list_push(AS_LIST(l), v); PUSH(l); ip++; DISPATCH(); }
label_OP_LIST_POP: { Value l = POP(); if (GET_TYPE(l) == VAL_LIST && AS_LIST(l)->size > 0) PUSH(AS_LIST(l)->items[--AS_LIST(l)->size]); else PUSH(value_int(0)); ip++; DISPATCH(); }
label_OP_LIST_INSERT: {
    Value v = POP(), idx = POP(), l = POP(); if (GET_TYPE(l) == VAL_LIST) {
        int i = val_to_int(idx); if (i < 0) i += AS_LIST(l)->size; if (i >= 0 && i <= AS_LIST(l)->size) {
            list_push(AS_LIST(l), v); for (int j=AS_LIST(l)->size-1; j>i; j--) AS_LIST(l)->items[j] = AS_LIST(l)->items[j-1]; AS_LIST(l)->items[i] = v;
        }
    } PUSH(l); ip++; DISPATCH();
}
label_OP_LIST_REMOVE: {
    Value v = POP(), l = POP(); if (GET_TYPE(l) == VAL_LIST) {
        int idx = -1; for (int i=0; i<AS_LIST(l)->size; i++) if (val_eq(AS_LIST(l)->items[i], v)) { idx = i; break; }
        if (idx >= 0) { for (int i=idx; i<AS_LIST(l)->size-1; i++) AS_LIST(l)->items[i] = AS_LIST(l)->items[i+1]; AS_LIST(l)->size--; }
    } PUSH(l); ip++; DISPATCH();
}
label_OP_LIST_CONTAINS: {
    Value v = POP(), l = POP(); int f = 0;
    if (GET_TYPE(l) == VAL_LIST) {
        for (int i=0; i<AS_LIST(l)->size; i++) if (val_eq(AS_LIST(l)->items[i], v)) { f = 1; break; }
    } else if (GET_TYPE(l) == VAL_STRING && GET_TYPE(v) == VAL_STRING) {
        if (strstr(AS_STRING(l), AS_STRING(v))) f = 1;
    } else if (GET_TYPE(l) == VAL_DICT && GET_TYPE(v) == VAL_STRING) {
        for (int i=0; i<AS_DICT(l)->size; i++) if (strcmp(AS_DICT(l)->keys[i], AS_STRING(v)) == 0) { f = 1; break; }
    }
    PUSH(value_bool(f)); ip++; DISPATCH();
}
label_OP_LIST_SORT: { Value l = POP(); if (GET_TYPE(l) == VAL_LIST) qsort(AS_LIST(l)->items, AS_LIST(l)->size, sizeof(Value), val_compare); PUSH(l); ip++; DISPATCH(); }
label_OP_LIST_REVERSE: {
    Value l = POP(); if (GET_TYPE(l) == VAL_LIST) { int n = AS_LIST(l)->size; for (int i=0; i<n/2; i++) { Value t = AS_LIST(l)->items[i]; AS_LIST(l)->items[i] = AS_LIST(l)->items[n-1-i]; AS_LIST(l)->items[n-1-i] = t; } }
    PUSH(l); ip++; DISPATCH();
}

label_OP_FILE_READ: {
    Value p = vm_stack[sp-1];
    if (GET_TYPE(p) == VAL_STRING) {
        FILE *f = fopen(AS_STRING(p), "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *b = malloc(sz + 1);
            fread(b, 1, sz, f);
            b[sz] = 0;
            fclose(f);
            Value res = value_string(b);
            free(b);
            sp--;
            PUSH(res);
        } else {
            sp--;
            PUSH(value_string(""));
        }
    } else {
        sp--;
        PUSH(value_string(""));
    }
    ip++; DISPATCH();
}
label_OP_FILE_WRITE: { Value t = POP(), p = POP(); if (GET_TYPE(p) == VAL_STRING && GET_TYPE(t) == VAL_STRING) { FILE *f = fopen(AS_STRING(p), "w"); if (f) { fputs(AS_STRING(t), f); fclose(f); PUSH(value_int(1)); } else PUSH(value_int(0)); } else PUSH(value_int(0)); ip++; DISPATCH(); }
label_OP_FILE_APPEND: { Value t = POP(), p = POP(); if (GET_TYPE(p) == VAL_STRING && GET_TYPE(t) == VAL_STRING) { FILE *f = fopen(AS_STRING(p), "a"); if (f) { fputs(AS_STRING(t), f); fclose(f); PUSH(value_int(1)); } else PUSH(value_int(0)); } else PUSH(value_int(0)); ip++; DISPATCH(); }

label_OP_FSTRING: {
    int n = bc->code[ip].arg;
    char *res = strdup("");
    for (int i=0; i<n; i++) {
        Value v = vm_stack[sp - n + i];
        char *vs = val_to_str(v);
        char *next = malloc(strlen(res) + strlen(vs) + 1);
        strcpy(next, res); strcat(next, vs);
        free(res); free(vs); res = next;
    }
    Value final = value_string(res);
    free(res);
    sp -= n;
    PUSH(final);
    ip++; DISPATCH();
}

label_OP_LIST_COMP: ip++; DISPATCH();

label_OP_THROW: vm_exception_value = POP(); vm_exception_active = 1; DISPATCH();

label_OP_TRY_START: {
    if (hsp >= HANDLER_STACK_SIZE) exit(1);
    handler_stack[hsp].catch_ip = bc->code[ip].arg; handler_stack[hsp].csp = csp; handler_stack[hsp].sp = sp; handler_stack[hsp].shadow_list = let_shadow_list;
    hsp++; ip++; DISPATCH();
}
label_OP_TRY_END: if (hsp > 0) hsp--; ip++; DISPATCH();

label_OP_LAMBDA: {
    int pc = bc->code[ip].arg;
    Bytecode *body = (Bytecode*)bc->code[ip].ptr;
    char **pnames = malloc(sizeof(char*)*(pc > 0 ? pc : 1));
    for (int i = 0; i < pc; i++) {
        pnames[i] = (char*)AS_STRING(vm_stack[sp - pc + i]);
    }
    Closure *c = closure_new(pc, pnames, body);
    free(pnames);
    sp -= pc;
    int cnt = 0; for (LetShadow *s = let_shadow_list; s; s = s->next) cnt++;
    if (cnt > 0) {
        c->captured_count = cnt;
        c->captured_names = malloc(sizeof(const char*)*cnt);
        c->captured_values = malloc(sizeof(Value)*cnt);
        int i=0;
        for (LetShadow *s = let_shadow_list; s; s = s->next) {
            c->captured_names[i] = s->name;
            c->captured_values[i] = var_get(s->name);
            i++;
        }
    }
    PUSH(value_closure(c)); ip++; DISPATCH();
}

label_OP_CALL_VALUE: {
    Value callee = POP(); 
    if (GET_TYPE(callee) == VAL_OBJECT && (callee & MASK_PAYLOAD)) {
        ObjType ot = GET_OBJ_TYPE(callee);
        if (ot == OBJ_BOUND_METHOD) {
            BoundMethod *bm = AS_BOUND_METHOD(callee);
            FuncEntry *fn = bm->method;
            if (csp >= CALL_STACK_SIZE-1) exit(1);
            CallFrame *cf = &call_stack[csp++]; cf->bc = bc; cf->ip = ip + 1; cf->base_sp = base_sp;
            cf->old_has_return = vm_has_return; cf->old_return_value = vm_return_value;
            cf->pending_fn = fn; cf->pending_closure = NULL;
            cf->local_count = fn->local_count; if (cf->local_count > 0) {
                if (cf->local_count <= 16) { cf->local_values = cf->inline_locals; for (int i=0; i<cf->local_count; i++) { cf->local_values[i] = value_none(); } }
                else cf->local_values = calloc(cf->local_count, sizeof(Value));
            } else cf->local_values = NULL;
            if (fn->uses_let) { cf->old_shadow_list = let_shadow_list; var_push_scope(); } else cf->old_shadow_list = NULL;
            
            /* Set 'self' in the scope */
            var_set("self", bm->instance);
            
            for (int i=fn->param_count-1; i>=0; i--) { Value arg = POP(); if (cf->local_values && i < cf->local_count) cf->local_values[i] = arg; else var_set(fn->params[i], arg); }
            Value rv = run_bytecode(fn->body, 0);
            csp--; cf = &call_stack[csp]; if (fn->uses_let) var_restore_shadow(cf->old_shadow_list);
            if (cf->param_count > 0) var_set(cf->p0, cf->v0); if (cf->param_count > 1) var_set(cf->p1, cf->v1);
            if (cf->local_values && cf->local_count > 16) free(cf->local_values);
            bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
            vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
            PUSH(rv); DISPATCH();
        } else if (ot == OBJ_NATIVE_FUNC) {
            NativeFunc *nf = AS_NATIVE_FUNC(callee);
            int argc = bc->code[ip].arg;
            Value *args = malloc(sizeof(Value) * (argc > 0 ? argc : 1));
            for (int j = argc - 1; j >= 0; j--) args[j] = POP();
            Value res = nf->func(argc, args);
            free(args);
            PUSH(res); ip++; DISPATCH();
        } else if (ot == OBJ_CLASS) {
            Class *c = AS_CLASS(callee);
            Instance *i = instance_new(c);
            PUSH(value_instance(i)); ip++; DISPATCH();
        } else { PUSH(value_none()); ip++; DISPATCH(); }
    } else if (GET_TYPE(callee) == VAL_CLOSURE) {
        Closure *c = AS_CLOSURE(callee); if (csp >= CALL_STACK_SIZE-1) exit(1);
        CallFrame *cf = &call_stack[csp++]; cf->bc = bc; cf->ip = ip + 1; cf->base_sp = base_sp;
        cf->old_has_return = vm_has_return; cf->old_return_value = vm_return_value;
        cf->pending_fn = NULL; cf->pending_closure = c; cf->old_shadow_list = let_shadow_list;
        cf->local_count = 16; cf->local_values = cf->inline_locals;
        for (int i=0; i<16; i++) cf->local_values[i] = value_none();
        for (int i=0; i<c->captured_count; i++) var_set_let(c->captured_names[i], c->captured_values[i]);
        Value inline_args[8]; Value *ab = (c->param_count > 8) ? malloc(sizeof(Value)*c->param_count) : inline_args;
        for (int i=c->param_count-1; i>=0; i--) ab[i] = POP();
        cf->param_count = c->param_count < 2 ? c->param_count : 2;
        if (cf->param_count > 0) { cf->p0 = c->params[0]; cf->v0 = var_get(c->params[0]); }
        if (cf->param_count > 1) { cf->p1 = c->params[1]; cf->v1 = var_get(c->params[1]); }
        for (int i=0; i<c->param_count; i++) { if (i < 16) cf->local_values[i] = ab[i]; var_set_let(c->params[i], ab[i]); }
        if (c->param_count > 8) free(ab);
        Value rv = run_bytecode(c->body, 0);
        csp--; cf = &call_stack[csp]; var_restore_shadow(cf->old_shadow_list);
        if (cf->param_count > 0) var_set(cf->p0, cf->v0); if (cf->param_count > 1) var_set(cf->p1, cf->v1);
        bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
        vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
        PUSH(rv); DISPATCH();
    } else if (GET_TYPE(callee) == VAL_STRING) {
        FuncEntry *fn = func_get(AS_STRING(callee)); if (!fn) { PUSH(value_none()); ip++; DISPATCH(); }
        if (csp >= CALL_STACK_SIZE-1) exit(1);
        CallFrame *cf = &call_stack[csp++]; cf->bc = bc; cf->ip = ip + 1; cf->base_sp = base_sp;
        cf->old_has_return = vm_has_return; cf->old_return_value = vm_return_value;
        cf->pending_fn = fn; cf->pending_closure = NULL;
        cf->local_count = fn->local_count; if (cf->local_count > 0) {
            if (cf->local_count <= 16) { cf->local_values = cf->inline_locals; for (int i=0; i<cf->local_count; i++) { cf->local_values[i] = value_none(); } }
            else cf->local_values = calloc(cf->local_count, sizeof(Value));
        } else cf->local_values = NULL;
        if (fn->uses_let) { cf->old_shadow_list = let_shadow_list; var_push_scope(); } else cf->old_shadow_list = NULL;
        for (int i=fn->param_count-1; i>=0; i--) { Value arg = POP(); if (cf->local_values && i < cf->local_count) cf->local_values[i] = arg; else var_set(fn->params[i], arg); }
        Value rv = run_bytecode(fn->body, 0);
        csp--; cf = &call_stack[csp]; if (fn->uses_let) var_restore_shadow(cf->old_shadow_list);
        if (cf->param_count > 0) var_set(cf->p0, cf->v0); if (cf->param_count > 1) var_set(cf->p1, cf->v1);
        if (cf->local_values && cf->local_count > 16) free(cf->local_values);
        bc = cf->bc; ip = cf->ip; base_sp = cf->base_sp;
        vm_has_return = cf->old_has_return; vm_return_value = cf->old_return_value;
        PUSH(rv); DISPATCH();
    } else { PUSH(value_none()); ip++; DISPATCH(); }
}

label_OP_CLASS_DEF: {
    const char *name = (const char*)bc->code[ip].ptr;
    Value parent_val = POP();
    Class *parent = (GET_TYPE(parent_val) == VAL_OBJECT && (parent_val & MASK_PAYLOAD) && GET_OBJ_TYPE(parent_val) == OBJ_CLASS) ? AS_CLASS(parent_val) : NULL;
    Class *c = class_new(name, parent);
    PUSH(value_class(c)); ip++; DISPATCH();
}

label_OP_METHOD_DEF: {
    const char *name = (const char*)bc->code[ip].ptr;
    /* Methods are defined by name, but we need the function from the table */
    FuncEntry *fn = func_get(name);
    if (fn) {
        /* Pop the class or trait from stack and add method to it */
        Value c_val = vm_stack[sp-1]; /* peek class/trait */
        if (GET_TYPE(c_val) == VAL_OBJECT && (c_val & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(c_val);
            if (ot == OBJ_CLASS) {
                dict_set(AS_CLASS(c_val)->methods, name, value_string(name));
            } else if (ot == OBJ_TRAIT) {
                dict_set(AS_TRAIT(c_val)->methods, name, value_string(name));
            }
        }
    }
    ip++; DISPATCH();
}

label_OP_TRAIT_DEF: {
    const char *name = (const char*)bc->code[ip].ptr;
    Trait *t = trait_new(name);
    PUSH(value_trait(t)); ip++; DISPATCH();
}

label_OP_USE_TRAIT: {
    const char *name = (const char*)bc->code[ip].ptr;
    Value trait_val = var_get(name);
    if (GET_TYPE(trait_val) == VAL_OBJECT && (trait_val & MASK_PAYLOAD) && GET_OBJ_TYPE(trait_val) == OBJ_TRAIT) {
        Trait *t = AS_TRAIT(trait_val);
        Value c_val = vm_stack[sp-1]; /* peek class */
        if (GET_TYPE(c_val) == VAL_OBJECT && (c_val & MASK_PAYLOAD) && GET_OBJ_TYPE(c_val) == OBJ_CLASS) {
            Class *c = AS_CLASS(c_val);
            /* Copy methods from trait to class */
            for (int i = 0; i < t->methods->capacity; i++) {
                if (t->methods->keys[i]) {
                    dict_set(c->methods, t->methods->keys[i], t->methods->values[i]);
                }
            }
        }
    }
    ip++; DISPATCH();
}
label_OP_SET_PROPERTY: {
    const char *name = (const char*)bc->code[ip].ptr;
    Value val = POP();
    Value obj = POP();
    if (GET_TYPE(obj) == VAL_OBJECT && (obj & MASK_PAYLOAD) && GET_OBJ_TYPE(obj) == OBJ_INSTANCE) {
        Instance *i = AS_INSTANCE(obj);
        if (bc->code[ip].cache_key == i->shape) {
            intptr_t cv = bc->code[ip].cache_val;
            if ((cv & 1) == 0) {
                i->fields[cv >> 1] = val;
                PUSH(val); ip++; DISPATCH();
            }
        }

        Shape *s = i->shape;
        int offset = -1;
        while (s && s->parent) {
            if (strcmp(s->name, name) == 0) { offset = s->offset; break; }
            s = s->parent;
        }
        if (offset >= 0) {
            bc->code[ip].cache_key = i->shape;
            bc->code[ip].cache_val = (intptr_t)(offset << 1);
            i->fields[offset] = val;
        } else {
            i->shape = shape_get_next(i->shape, name);
            if (i->shape->offset >= i->field_capacity) {
                i->field_capacity = (i->field_capacity == 0) ? 4 : i->field_capacity * 2;
                i->fields = realloc(i->fields, sizeof(Value) * i->field_capacity);
            }
            i->fields[i->shape->offset] = val;
            bc->code[ip].cache_key = i->shape;
            bc->code[ip].cache_val = (intptr_t)(i->shape->offset << 1);
        }
    }
    PUSH(val); ip++; DISPATCH();
}

label_OP_GET_PROPERTY: {
    const char *name = (const char*)bc->code[ip].ptr;
    Value val = POP();
    if (GET_TYPE(val) == VAL_OBJECT && (val & MASK_PAYLOAD)) {
        ObjType ot = GET_OBJ_TYPE(val);
        if (ot == OBJ_INSTANCE) {
            Instance *i = AS_INSTANCE(val);
            if (bc->code[ip].cache_key == i->shape) {
                intptr_t cv = bc->code[ip].cache_val;
                if ((cv & 1) == 0) {
                    PUSH(i->fields[cv >> 1]);
                    ip++; DISPATCH();
                } else {
                    FuncEntry *fn = (FuncEntry*)(cv & ~1);
                    PUSH(value_bound_method(bound_method_new(val, fn)));
                    ip++; DISPATCH();
                }
            }

            Shape *s = i->shape;
            int offset = -1;
            while (s && s->parent) {
                if (strcmp(s->name, name) == 0) { offset = s->offset; break; }
                s = s->parent;
            }
            if (offset >= 0) { 
                bc->code[ip].cache_key = i->shape;
                bc->code[ip].cache_val = (intptr_t)(offset << 1);
                PUSH(i->fields[offset]); 
                ip++; DISPATCH(); 
            }
            
            Class *k = i->klass;
            while (k) {
                for (int j = 0; j < k->methods->size; j++) {
                    if (strcmp(k->methods->keys[j], name) == 0) {
                        FuncEntry *fn = func_get(name);
                        if (fn) {
                            bc->code[ip].cache_key = i->shape;
                            bc->code[ip].cache_val = ((intptr_t)fn | 1);
                            PUSH(value_bound_method(bound_method_new(val, fn)));
                            ip++; DISPATCH();
                        }
                    }
                }
                k = k->parent;
            }
        } else if (ot == OBJ_CLASS) {
            Class *c = AS_CLASS(val);
            for (int j = 0; j < c->methods->size; j++) {
                if (strcmp(c->methods->keys[j], name) == 0) {
                    FuncEntry *fn = func_get(name);
                    if (fn) {
                        PUSH(value_bound_method(bound_method_new(val, fn)));
                        ip++; DISPATCH();
                    }
                }
            }
        }
    } else if (GET_TYPE(val) == VAL_DICT) {
        Dict *d = AS_DICT(val);
        for (int j = 0; j < d->size; j++) {
            if (strcmp(d->keys[j], name) == 0) {
                PUSH(d->values[j]); ip++; DISPATCH();
            }
        }
    }
    PUSH(value_none()); ip++; DISPATCH();
}

label_OP_SPAWN: {
    int argc = bc->code[ip].arg;
    Value callee = POP();
    Value *args = malloc(sizeof(Value) * (argc > 0 ? argc : 1));
    for (int i = argc - 1; i >= 0; i--) args[i] = POP();
    
    ThreadData *td = malloc(sizeof(ThreadData));
    td->callee = callee;
    td->argc = argc;
    td->args = args;
    
    pthread_t thread;
    if (pthread_create(&thread, NULL, thread_worker, td) == 0) {
        pthread_detach(thread);
    }
    PUSH(value_none());
    ip++; DISPATCH();
}

label_EXIT_VM:
    return value_none();
}

GlowError *glow_errors = NULL;
int glow_error_count = 0;
static int glow_error_capacity = 0;

void glow_clear_errors(void) {
    for (int i = 0; i < glow_error_count; i++) {
        free(glow_errors[i].message);
    }
    glow_error_count = 0;
}

void glow_add_error(int line, const char *msg) {
    if (glow_error_count >= glow_error_capacity) {
        glow_error_capacity = glow_error_capacity == 0 ? 8 : glow_error_capacity * 2;
        glow_errors = realloc(glow_errors, sizeof(GlowError) * glow_error_capacity);
    }
    glow_errors[glow_error_count].line = line;
    glow_errors[glow_error_count].message = strdup(msg);
    glow_error_count++;
}

void yyerror(const char *s) {
    glow_add_error(line_num, s);
    fprintf(stderr, "GlowLang Error (line %d): %s\n", line_num, s);
}

static void bc_dump(Bytecode *bc) {
    if (!bc) return;
    printf("Bytecode dump (%d instrs):\n", bc->count);
    for (int i = 0; i < bc->count; i++) {
        printf("%3d: op=%d arg=%d ptr=%p\n", i, bc->code[i].op, bc->code[i].arg, (void *)bc->code[i].ptr);
    }
}

/* Module loader state */
static const char *module_worklist[64];
static int module_worklist_count = 0;
static const char *loaded_modules[64];
static int loaded_modules_count = 0;

typedef struct { const char *url; const char *local_path; } DepEntry;
static DepEntry dep_table[64];
static int dep_count = 0;

static void load_deps(void) {
    FILE *f = fopen("Glow.lock", "r");
    if (f) {
        char url[512], hash[64], path[512];
        while (fscanf(f, " \"%511[^\"]\" \"%63[^\"]\" \"%511[^\"]\"", url, hash, path) == 3) {
            if (dep_count < 64) {
                dep_table[dep_count].url = strdup(url);
                dep_table[dep_count].local_path = strdup(path);
                dep_count++;
            }
        }
        fclose(f);
        return;
    }

    f = fopen("Glow.deps", "r");
    if (!f) return;
    char url[512], path[512];
    while (fscanf(f, " \"%511[^\"]\" \"%511[^\"]\"", url, path) == 2) {
        if (dep_count < 64) {
            dep_table[dep_count].url = strdup(url);
            dep_table[dep_count].local_path = strdup(path);
            dep_count++;
        }
    }
    fclose(f);
}

static const char* resolve_path(const char *filename) {
    for (int i = 0; i < dep_count; i++) {
        if (strcmp(dep_table[i].url, filename) == 0) return dep_table[i].local_path;
    }
    return filename;
}

static void add_to_worklist(const char *filename) {
    for (int i = 0; i < loaded_modules_count; i++) {
        if (strcmp(loaded_modules[i], filename) == 0) return;
    }
    for (int i = 0; i < module_worklist_count; i++) {
        if (strcmp(module_worklist[i], filename) == 0) return;
    }
    if (module_worklist_count < 64) module_worklist[module_worklist_count++] = strdup(filename);
}

static void find_imports(Node *n) {
    if (!n) return;
    if (n->type == NODE_IMPORT) {
        add_to_worklist(n->name);
    }
    find_imports(n->left);
    find_imports(n->right);
    find_imports(n->third);
    /* For other node types with arrays, we could iterate but 
       top-level imports are usually enough for now. 
    */
}

static void gc_mark_object(GCObject *obj) {
    if (!obj || obj->marked) return;
    obj->marked = 1;
    switch (obj->type) {
        case OBJ_LIST: {
            List *l = (List*)obj;
            for (int i = 0; i < l->size; i++) gc_mark_value(l->items[i]);
            break;
        }
        case OBJ_DICT: {
            Dict *d = (Dict*)obj;
            for (int i = 0; i < d->size; i++) gc_mark_value(d->values[i]);
            break;
        }
        case OBJ_CLOSURE: {
            Closure *c = (Closure*)obj;
            for (int i = 0; i < c->captured_count; i++) gc_mark_value(c->captured_values[i]);
            break;
        }
        case OBJ_CLASS: {
            Class *c = (Class*)obj;
            gc_mark_object((GCObject*)c->methods);
            gc_mark_object((GCObject*)c->root_shape);
            break;
        }
        case OBJ_TRAIT: {
            Trait *t = (Trait*)obj;
            gc_mark_object((GCObject*)t->methods);
            break;
        }
        case OBJ_INSTANCE: {
            Instance *i = (Instance*)obj;
            gc_mark_object((GCObject*)i->klass);
            gc_mark_object((GCObject*)i->shape);
            if (i->fields) {
                for (int j = 0; j <= i->shape->offset; j++) gc_mark_value(i->fields[j]);
            }
            break;
        }
        case OBJ_SHAPE: {
            Shape *s = (Shape*)obj;
            gc_mark_object((GCObject*)s->parent);
            gc_mark_object((GCObject*)s->transitions);
            break;
        }
        case OBJ_BOUND_METHOD: {
            BoundMethod *bm = (BoundMethod*)obj;
            gc_mark_value(bm->instance);
            break;
        }
        case OBJ_NATIVE_FUNC: break;
        case OBJ_STRING: break;
    }
}

static void gc_mark_value(Value v) {
    ValueType t = GET_TYPE(v);
    if (t == VAL_LIST) gc_mark_object((GCObject*)AS_LIST(v));
    else if (t == VAL_DICT) gc_mark_object((GCObject*)AS_DICT(v));
    else if (t == VAL_CLOSURE) gc_mark_object((GCObject*)AS_CLOSURE(v));
    else if (t == VAL_OBJECT) {
        if (v & MASK_PAYLOAD) {
            ObjType ot = GET_OBJ_TYPE(v);
            if (ot == OBJ_CLASS) gc_mark_object((GCObject*)AS_CLASS(v));
            else if (ot == OBJ_TRAIT) gc_mark_object((GCObject*)AS_TRAIT(v));
            else if (ot == OBJ_INSTANCE) gc_mark_object((GCObject*)AS_INSTANCE(v));
            else if (ot == OBJ_BOUND_METHOD) gc_mark_object((GCObject*)AS_BOUND_METHOD(v));
            else if (ot == OBJ_NATIVE_FUNC) gc_mark_object((GCObject*)AS_NATIVE_FUNC(v));
        }
    }
    else if (t == VAL_STRING) gc_mark_object((GCObject*)(uintptr_t)(v & MASK_PAYLOAD));
}

static void gc_mark_bytecode(Bytecode *bc) {
    if (!bc) return;
    for (int i = 0; i < bc->count; i++) {
        if (bc->code[i].op == OP_CONST_STRING) {
            gc_mark_value((Value)bc->code[i].ptr);
        }
    }
}

static void gc_collect(void) {
    size_t before = gc_allocated;
    /* Mark */
    for (int i = 0; i < VAR_TABLE_SIZE; i++) {
        if (var_table[i].used) gc_mark_value(var_table[i].value);
    }
    for (int i = 0; i < sp; i++) gc_mark_value(vm_stack[i]);
    for (int i = 0; i < csp; i++) {
        CallFrame *cf = &call_stack[i];
        if (cf->pending_closure) gc_mark_object((GCObject*)cf->pending_closure);
        gc_mark_value(cf->v0); gc_mark_value(cf->v1);
        for (int j = 0; j < 16; j++) gc_mark_value(cf->inline_locals[j]);
        if (cf->local_values) {
            for (int j = 0; j < cf->local_count; j++) gc_mark_value(cf->local_values[j]);
        }
    }
    for (LetShadow *ls = let_shadow_list; ls; ls = ls->next) {
        if (ls->had_old) gc_mark_value(ls->old_value);
    }
    if (vm_exception_active) gc_mark_value(vm_exception_value);
    if (vm_has_return) gc_mark_value(vm_return_value);

    for (int i = 0; i < func_table_size; i++) {
        if (func_table[i].used) gc_mark_bytecode(func_table[i].body);
    }
    if (main_bc) gc_mark_bytecode(main_bc);

    /* Sweep */
    GCObject **ptr = &gc_objects;
    while (*ptr) {
        if (!(*ptr)->marked) {
            GCObject *unmarked = *ptr;
            *ptr = unmarked->next;
            if (unmarked->type == OBJ_LIST) {
                free(((List*)unmarked)->items);
            } else if (unmarked->type == OBJ_DICT) {
                free(((Dict*)unmarked)->keys);
                free(((Dict*)unmarked)->values);
            } else if (unmarked->type == OBJ_CLOSURE) {
                Closure *c = (Closure*)unmarked;
                for (int j = 0; j < c->param_count; j++) free(c->params[j]);
                free(c->params);
                free(c->captured_names);
                free(c->captured_values);
            } else if (unmarked->type == OBJ_CLASS) {
                // Methods/Root shape handled by GC
            } else if (unmarked->type == OBJ_INSTANCE) {
                Instance *i = (Instance*)unmarked;
                if (i->fields) free(i->fields);
            } else if (unmarked->type == OBJ_SHAPE) {
                // Transitions handled by GC
            }
            // OBJ_STRING is just a single allocation
            gc_allocated -= unmarked->sz;
            free(unmarked);
        } else {
            (*ptr)->marked = 0;
            ptr = &(*ptr)->next;
        }
    }
    gc_threshold = gc_allocated > 1024*1024 ? gc_allocated * 2 : 1024*1024;
}

static void* gc_alloc(size_t sz, ObjType type) {
    if (gc_allocated > gc_threshold) gc_collect();
    GCObject *obj = malloc(sz);
    obj->type = type;
    obj->marked = 0;
    obj->sz = sz;
    obj->next = gc_objects;
    gc_objects = obj;
    gc_allocated += sz;
    return obj;
}

Value value_string(const char *s) {
    if (!s) return make_value(VAL_STRING, 0);
    size_t len = strlen(s);
    GCString *gs = gc_alloc(sizeof(GCString) + len + 1, OBJ_STRING);
    strcpy(gs->chars, s);
    return make_value(VAL_STRING, (uintptr_t)gs);
}

static NativeFunc *native_func_new(const char *name, NativeFuncPtr ptr) {
    NativeFunc *nf = gc_alloc(sizeof(NativeFunc), OBJ_NATIVE_FUNC);
    nf->name = name;
    nf->func = ptr;
    return nf;
}

static Value os_exit(int argc, Value *args) {
    if (argc > 0 && GET_TYPE(args[0]) == VAL_INT) {
        exit((int)AS_INT(args[0]));
    }
    exit(0);
    return value_none();
}

static Value os_time(int argc, Value *args) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return value_float(tv.tv_sec + tv.tv_usec / 1000000.0);
}

static Value os_system(int argc, Value *args) {
    if (argc > 0 && GET_TYPE(args[0]) == VAL_STRING) {
        int res = system(AS_STRING(args[0]));
        return value_int(res);
    }
    return value_int(-1);
}

static char* json_stringify_recursive(Value v) {
    char buf[2048];
    switch (GET_TYPE(v)) {
        case VAL_INT:
            snprintf(buf, sizeof(buf), "%ld", (long)AS_INT(v));
            return strdup(buf);
        case VAL_FLOAT:
            snprintf(buf, sizeof(buf), "%g", AS_FLOAT(v));
            return strdup(buf);
        case VAL_BOOL:
            return strdup(AS_BOOL(v) ? "true" : "false");
        case VAL_STRING: {
            const char *s = AS_STRING(v);
            char *escaped = malloc(strlen(s) * 2 + 3);
            char *d = escaped;
            *d++ = '"';
            while (*s) {
                if (*s == '"' || *s == '\\') *d++ = '\\';
                *d++ = *s++;
            }
            *d++ = '"';
            *d = '\0';
            return escaped;
        }
        case VAL_LIST: {
            List *l = AS_LIST(v);
            char *res = strdup("[");
            for (int i = 0; i < l->size; i++) {
                char *item = json_stringify_recursive(l->items[i]);
                char *old = res;
                res = malloc(strlen(old) + strlen(item) + 4);
                sprintf(res, "%s%s%s", old, item, (i == l->size - 1) ? "" : ", ");
                free(old);
                free(item);
            }
            char *old = res;
            res = malloc(strlen(old) + 2);
            sprintf(res, "%s]", old);
            free(old);
            return res;
        }
        case VAL_DICT: {
            Dict *d = AS_DICT(v);
            char *res = strdup("{");
            for (int i = 0; i < d->size; i++) {
                char *val = json_stringify_recursive(d->values[i]);
                char *old = res;
                res = malloc(strlen(old) + strlen(d->keys[i]) + strlen(val) + 10);
                sprintf(res, "%s\"%s\": %s%s", old, d->keys[i], val, (i == d->size - 1) ? "" : ", ");
                free(old);
                free(val);
            }
            char *old = res;
            res = malloc(strlen(old) + 2);
            sprintf(res, "%s}", old);
            free(old);
            return res;
        }
        default:
            return strdup("null");
    }
}

static Value json_stringify_native(int argc, Value *args) {
    if (argc < 1) return value_string("null");
    char *s = json_stringify_recursive(args[0]);
    Value res = value_string(s);
    free(s);
    return res;
}

static Value json_parse_value(const char **p);

static Value json_parse_object(const char **p) {
    (*p)++; /* skip { */
    Dict *d = dict_new(8);
    while (**p && **p != '}') {
        while (isspace(**p)) (*p)++;
        if (**p == '"') {
            (*p)++;
            const char *start = *p;
            while (**p && **p != '"') (*p)++;
            int len = *p - start;
            char *key = malloc(len + 1);
            strncpy(key, start, len);
            key[len] = '\0';
            (*p)++; /* skip " */
            while (isspace(**p)) (*p)++;
            if (**p == ':') (*p)++;
            while (isspace(**p)) (*p)++;
            Value val = json_parse_value(p);
            dict_set(d, intern_string(key), val);
            free(key);
        }
        while (isspace(**p)) (*p)++;
        if (**p == ',') (*p)++;
        while (isspace(**p)) (*p)++;
    }
    if (**p == '}') (*p)++;
    return value_dict(d);
}

static Value json_parse_list(const char **p) {
    (*p)++; /* skip [ */
    List *l = list_new(0);
    while (**p && **p != ']') {
        while (isspace(**p)) (*p)++;
        if (**p == ']') break;
        Value val = json_parse_value(p);
        l = list_grow(l, l->size + 1);
        l->items[l->size++] = val;
        while (isspace(**p)) (*p)++;
        if (**p == ',') (*p)++;
        while (isspace(**p)) (*p)++;
    }
    if (**p == ']') (*p)++;
    return value_list(l);
}

static Value json_parse_value(const char **p) {
    while (isspace(**p)) (*p)++;
    if (**p == '{') return json_parse_object(p);
    if (**p == '[') return json_parse_list(p);
    if (**p == '"') {
        (*p)++;
        const char *start = *p;
        while (**p && **p != '"') {
            if (**p == '\\' && *(*p + 1)) (*p)++;
            (*p)++;
        }
        int len = *p - start;
        char *s = malloc(len + 1);
        strncpy(s, start, len);
        s[len] = '\0';
        if (**p == '"') (*p)++;
        Value res = value_string(s);
        free(s);
        return res;
    }
    if (isdigit(**p) || **p == '-') {
        char *end;
        double d = strtod(*p, &end);
        *p = end;
        if (d == (long)d) return value_int((long)d);
        return value_float(d);
    }
    if (strncmp(*p, "true", 4) == 0) { *p += 4; return value_bool(1); }
    if (strncmp(*p, "false", 5) == 0) { *p += 5; return value_bool(0); }
    if (strncmp(*p, "null", 4) == 0) { *p += 4; return value_none(); }
    return value_none();
}

static Value json_parse_native(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_none();
    const char *p = AS_STRING(args[0]);
    return json_parse_value(&p);
}

static Value net_listen(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT) return value_int(-1);
    int port = (int)AS_INT(args[0]);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return value_int(-1);

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sockfd);
        return value_int(-1);
    }

    if (listen(sockfd, 5) < 0) {
        close(sockfd);
        return value_int(-1);
    }

    return value_int(sockfd);
}

static Value net_accept(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT) return value_int(-1);
    int sockfd = (int)AS_INT(args[0]);

    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    int newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
    if (newsockfd < 0) return value_int(-1);

    return value_int(newsockfd);
}

static Value net_send(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_INT || GET_TYPE(args[1]) != VAL_STRING) return value_int(-1);
    int sockfd = (int)AS_INT(args[0]);
    const char *msg = AS_STRING(args[1]);

    ssize_t n = send(sockfd, msg, strlen(msg), 0);
    return value_int((int64_t)n);
}

static Value net_recv(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT) return value_string("");
    int sockfd = (int)AS_INT(args[0]);

    char buffer[4096];
    ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) return value_string("");

    buffer[n] = '\0';
    return value_string(buffer);
}

static Value net_close(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT) return value_none();
    int sockfd = (int)AS_INT(args[0]);
    close(sockfd);
    return value_none();
}

static Value fs_exists(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_bool(0);
    const char *path = AS_STRING(args[0]);
    struct stat st;
    return value_bool(stat(path, &st) == 0);
}

static Value fs_mkdir(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_bool(0);
    const char *path = AS_STRING(args[0]);
    return value_bool(mkdir(path, 0777) == 0);
}

static Value fs_list(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_list(list_new(0));
    const char *path = AS_STRING(args[0]);
    DIR *d = opendir(path);
    List *l = list_new(0);
    if (!d) return value_list(l);
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
        list_push(l, value_string(dir->d_name));
    }
    closedir(d);
    return value_list(l);
}

static Value fs_remove(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_bool(0);
    const char *path = AS_STRING(args[0]);
    return value_bool(remove(path) == 0);
}

static Value fs_size(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_int(-1);
    const char *path = AS_STRING(args[0]);
    struct stat st;
    if (stat(path, &st) == 0) {
        return value_int((int64_t)st.st_size);
    }
    return value_int(-1);
}

static void std_init() {
    Dict *os = dict_new(4);
    dict_set(os, "exit", value_native_func(native_func_new("exit", os_exit)));
    dict_set(os, "time", value_native_func(native_func_new("time", os_time)));
    dict_set(os, "system", value_native_func(native_func_new("system", os_system)));
    var_set("os", value_dict(os));

    Dict *json = dict_new(4);
    dict_set(json, "stringify", value_native_func(native_func_new("stringify", json_stringify_native)));
    dict_set(json, "parse", value_native_func(native_func_new("parse", json_parse_native)));
    var_set("json", value_dict(json));

    Dict *net = dict_new(8);
    dict_set(net, "listen", value_native_func(native_func_new("listen", net_listen)));
    dict_set(net, "accept", value_native_func(native_func_new("accept", net_accept)));
    dict_set(net, "send", value_native_func(native_func_new("send", net_send)));
    dict_set(net, "recv", value_native_func(native_func_new("recv", net_recv)));
    dict_set(net, "close", value_native_func(native_func_new("close", net_close)));
    var_set("net", value_dict(net));

    Dict *fs = dict_new(8);
    dict_set(fs, "exists", value_native_func(native_func_new("exists", fs_exists)));
    dict_set(fs, "mkdir", value_native_func(native_func_new("mkdir", fs_mkdir)));
    dict_set(fs, "list", value_native_func(native_func_new("list", fs_list)));
    dict_set(fs, "remove", value_native_func(native_func_new("remove", fs_remove)));
    dict_set(fs, "size", value_native_func(native_func_new("size", fs_size)));
    var_set("fs", value_dict(fs));

    /* Also register globally to match example */
    var_set("net_accept", value_native_func(native_func_new("net_accept", net_accept)));
    var_set("net_recv", value_native_func(native_func_new("net_recv", net_recv)));
    var_set("net_send", value_native_func(native_func_new("net_send", net_send)));
    var_set("net_close", value_native_func(native_func_new("net_close", net_close)));
}

#include "lsp.h"
extern void lsp_run_socket(int port);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.glow> [--lsp]\n", argv[0]);
        return 1;
    }

    if (argc >= 2 && strcmp(argv[1], "--lsp") == 0) {
        var_table_init();
        func_table_init();
        vm_stack_init();
        intern_table_init();
        lsp_run();
        return 0;
    }

    if (argc >= 3 && strcmp(argv[1], "--lsp-port") == 0) {
        var_table_init();
        func_table_init();
        vm_stack_init();
        intern_table_init();
        lsp_run_socket(atoi(argv[2]));
        return 0;
    }

    var_table_init();
    func_table_init();
    vm_stack_init();
    intern_table_init();
    std_init();
    load_deps();

    main_bc = bc_new();
    compiling_bc = main_bc;

    /* Start with the main file */
    add_to_worklist(argv[1]);

    Node *ast_roots[64];
    int ast_count = 0;

    while (module_worklist_count > 0) {
        const char *orig_filename = module_worklist[--module_worklist_count];
        const char *filename = resolve_path(orig_filename);
        
        yyin = fopen(filename, "r");
        if (!yyin) {
            fprintf(stderr, "Error: cannot open '%s' (resolved from '%s')\n", filename, orig_filename);
            continue;
        }

        line_num = 1;
        yyparse();
        
        if (root) {
            find_imports(root);
            ast_roots[ast_count++] = root;
            root = NULL;
        }

        fclose(yyin);
        loaded_modules[loaded_modules_count++] = orig_filename;
    }

    /* Compile ASTs in reverse order (dependencies first) */
    for (int i = ast_count - 1; i >= 0; i--) {
        compile(ast_roots[i]);
    }

    if (main_bc && main_bc->count > 0) {
        var_push_scope();
        bc_optimize(main_bc);
        pthread_mutex_lock(&glow_gil);
        run_bytecode(main_bc, 0);
        pthread_mutex_unlock(&glow_gil);
        var_pop_scope();
        bc_free(main_bc);
    }

    var_table_free();
    func_table_free();
    free(vm_stack);
    intern_cleanup();

    return 0;
}
