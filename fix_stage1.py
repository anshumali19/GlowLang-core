import re

with open("src/glowlang.y", "r") as f:
    full_text = f.read()

# Split the file into three parts: Definitions, Grammar, C Code
# Part 1: Definitions (ends at first %%)
# Part 2: Grammar (ends at second %%)
# Part 3: C Code (everything after second %%)

first_pp = full_text.find("%%")
second_pp = full_text.find("%%", first_pp + 2)

c_code = full_text[second_pp:] if second_pp != -1 else ""
grammar = full_text[first_pp:second_pp] if second_pp != -1 else full_text[first_pp:]

top_decls = """%code requires {
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
#include <sys/mman.h>
#include <unistd.h>
#include "glowlang.h"

/* Forward declarations */
extern const char *intern_string(const char *s);
extern void intern_cleanup(void);
extern void intern_table_init(void);
extern int line_num;
extern int yylex(void);
extern FILE *yyin;
void yyerror(const char *s);

static void gc_mark_value(Value v);
static void gc_mark_object(GCObject *obj);
static void gc_collect(void);
static void* gc_alloc(size_t sz, ObjType type);
static void print_value(Value v);
static Value run_bytecode(Bytecode *bc, int base_offset);
static void jit_compile(Bytecode *bc);
static void bc_optimize(Bytecode *bc);
static void bc_free(Bytecode *bc);
static void var_set(const char *name, Value value);

/* Value Helpers */
static int val_to_bool(Value v) { return value_truthy(v); }
static int64_t val_to_int(Value v) { return AS_INT(v); }
static double val_to_int_or_float(Value v) {
    if (GET_TYPE(v) == VAL_INT) return (double)AS_INT(v);
    if (GET_TYPE(v) == VAL_FLOAT) return AS_FLOAT(v);
    return 0.0;
}

/* Let shadow: saves old values when let shadows a variable */
typedef struct LetShadow {
    const char *name;
    Value old_value;
    int had_old;
    struct LetShadow *next;
} LetShadow;

static __thread LetShadow *let_shadow_list = NULL;

pthread_mutex_t glow_gil = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gc_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t threads_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Call frame for iterative VM */
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

typedef struct {
    Value **vm_stack_ptr;
    int *sp_ptr;
    CallFrame *call_stack;
    int *csp_ptr;
} ThreadStack;

static ThreadStack active_stacks[128];
static int active_stack_count = 0;

typedef struct {
    Value callee;
    int argc;
    Value *args;
} ThreadData;

#define CALL_STACK_SIZE 65536
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

static __thread int vm_loop_stop = 0, vm_loop_skip = 0;
static __thread Value vm_return_value;
static __thread int vm_has_return = 0;

static GCObject *gc_objects = NULL;
static size_t gc_allocated = 0;
static size_t gc_threshold = 1024 * 1024;

typedef struct {
    uint8_t *code;
    size_t size;
    size_t capacity;
} JitBuffer;

#define JIT_THRESHOLD 500

static void vm_stack_init(void) {
    vm_stack_size = 16384;
    vm_stack = malloc(sizeof(Value) * vm_stack_size);
    sp = 0;
    pthread_mutex_lock(&threads_mutex);
    if (active_stack_count < 128) {
        active_stacks[active_stack_count].vm_stack_ptr = &vm_stack;
        active_stacks[active_stack_count].sp_ptr = &sp;
        active_stacks[active_stack_count].call_stack = call_stack;
        active_stacks[active_stack_count].csp_ptr = &csp;
        active_stack_count++;
    }
    pthread_mutex_unlock(&threads_mutex);
}

/* ================================================================
 *  DYNAMIC HASH TABLE: VARIABLES
 * ================================================================ */
#define VAR_TABLE_SIZE 16384
typedef struct { const char *name; Value value; int used; } VarEntry;
static VarEntry var_table[VAR_TABLE_SIZE];
static int var_count = 0;

static void var_table_init(void) { memset(var_table, 0, sizeof(var_table)); var_count = 0; }
static unsigned int hash_str(const char *s) {
    unsigned int h = 5381;
    while (*s) h = ((h << 5) + h) + *s++;
    return h;
}

static void var_set(const char *name, Value value) {
    unsigned int h = hash_str(name) % VAR_TABLE_SIZE;
    unsigned int idx = h;
    do {
        if (!var_table[idx].used || strcmp(var_table[idx].name, name) == 0) {
            if (!var_table[idx].used) var_count++;
            var_table[idx].name = name;
            var_table[idx].value = value;
            var_table[idx].used = 1;
            return;
        }
        idx = (idx + 1) % VAR_TABLE_SIZE;
    } while (idx != h);
}

static Value var_get(const char *name) {
    unsigned int h = hash_str(name) % VAR_TABLE_SIZE;
    unsigned int idx = h;
    do {
        if (!var_table[idx].used) break;
        if (strcmp(var_table[idx].name, name) == 0) return var_table[idx].value;
        idx = (idx + 1) % VAR_TABLE_SIZE;
    } while (idx != h);
    return value_none();
}

/* ================================================================
 *  DYNAMIC HASH TABLE: FUNCTIONS
 * ================================================================ */
static int func_table_size = 1024;
static int func_table_count = 0;
static FuncEntry *func_table = NULL;

static void func_table_init(void) {
    func_table = calloc(func_table_size, sizeof(FuncEntry));
}

static FuncEntry* func_table_add(const char *name) {
    unsigned int h = hash_str(name) % func_table_size;
    unsigned int idx = h;
    do {
        if (!func_table[idx].used || strcmp(func_table[idx].name, name) == 0) {
            if (!func_table[idx].used) func_table_count++;
            func_table[idx].used = 1;
            func_table[idx].name = name;
            return &func_table[idx];
        }
        idx = (idx + 1) % func_table_size;
    } while (idx != h);
    return NULL;
}

static FuncEntry* func_get(const char *name) {
    if (!func_table) return NULL;
    unsigned int h = hash_str(name) % func_table_size;
    unsigned int idx = h;
    do {
        if (!func_table[idx].used) break;
        if (strcmp(func_table[idx].name, name) == 0) return &func_table[idx];
        idx = (idx + 1) % func_table_size;
    } while (idx != h);
    return NULL;
}

/* ================================================================
 *  GC IMPLEMENTATION
 * ================================================================ */
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
        case OBJ_TRAIT: {
            Trait *t = (Trait*)obj;
            gc_mark_object((GCObject*)t->methods);
            break;
        }
        case OBJ_BOUND_METHOD: {
            BoundMethod *bm = (BoundMethod*)obj;
            gc_mark_value(bm->instance);
            break;
        }
        default: break;
    }
}

static void gc_mark_value(Value v) {
    ValueType t = GET_TYPE(v);
    if (t == VAL_LIST) gc_mark_object((GCObject*)AS_LIST(v));
    else if (t == VAL_DICT) gc_mark_object((GCObject*)AS_DICT(v));
    else if (t == VAL_CLOSURE) gc_mark_object((GCObject*)AS_CLOSURE(v));
    else if (t == VAL_OBJECT && (v & MASK_PAYLOAD)) {
        gc_mark_object((GCObject*)(uintptr_t)(v & MASK_PAYLOAD));
    }
    else if (t == VAL_STRING) gc_mark_object((GCObject*)(uintptr_t)(v & MASK_PAYLOAD));
}

static void gc_collect(void) {
    for (int i = 0; i < VAR_TABLE_SIZE; i++) {
        if (var_table[i].used) gc_mark_value(var_table[i].value);
    }
    pthread_mutex_lock(&threads_mutex);
    for (int i = 0; i < active_stack_count; i++) {
        Value *stk = *(active_stacks[i].vm_stack_ptr);
        int stk_sp = *(active_stacks[i].sp_ptr);
        for (int j = 0; j < stk_sp; j++) gc_mark_value(stk[j]);
        CallFrame *cs = active_stacks[i].call_stack;
        int cs_csp = *(active_stacks[i].csp_ptr);
        for (int j = 0; j < cs_csp; j++) {
            CallFrame *cf = &cs[j];
            for (int k = 0; k < 16; k++) gc_mark_value(cf->inline_locals[k]);
            if (cf->local_values && cf->local_count > 16)
                for (int k = 0; k < cf->local_count; k++) gc_mark_value(cf->local_values[k]);
        }
    }
    pthread_mutex_unlock(&threads_mutex);

    GCObject **p = &gc_objects;
    while (*p) {
        if (!(*p)->marked) {
            GCObject *unmarked = *p;
            *p = unmarked->next;
            if (unmarked->type == OBJ_LIST) { free(((List*)unmarked)->items); }
            else if (unmarked->type == OBJ_DICT) { free(((Dict*)unmarked)->keys); free(((Dict*)unmarked)->values); }
            else if (unmarked->type == OBJ_CLOSURE) { Closure *c = (Closure*)unmarked; free(c->params); free(c->captured_names); free(c->captured_values); }
            else if (unmarked->type == OBJ_INSTANCE) { free(((Instance*)unmarked)->fields); }
            gc_allocated -= unmarked->sz;
            free(unmarked);
        } else {
            (*p)->marked = 0;
            p = &((*p)->next);
        }
    }
    gc_threshold = gc_allocated > 1024*1024 ? gc_allocated * 2 : 1024*1024;
}

static void* gc_alloc(size_t sz, ObjType type) {
    pthread_mutex_lock(&gc_mutex);
    if (gc_allocated > gc_threshold) gc_collect();
    GCObject *obj = malloc(sz);
    obj->type = type; obj->marked = 0; obj->sz = sz;
    obj->next = gc_objects; gc_objects = obj;
    gc_allocated += sz;
    pthread_mutex_unlock(&gc_mutex);
    return obj;
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
        else if (ot == OBJ_INSTANCE) printf("<instance of %s>", AS_INSTANCE(v)->klass->name);
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
            if (ot == OBJ_CLASS) { char b[128]; snprintf(b, sizeof(b), "<class %s>", AS_CLASS(v)->name); return strdup(b); }
            if (ot == OBJ_INSTANCE) { char b[128]; snprintf(b, sizeof(b), "<instance of %s>", AS_INSTANCE(v)->klass->name); return strdup(b); }
            if (ot == OBJ_BOUND_METHOD) { char b[128]; snprintf(b, sizeof(b), "<bound method %s>", AS_BOUND_METHOD(v)->method->name); return strdup(b); }
            return strdup("<object>");
        }
        default: return strdup("none");
    }
    return strdup(buf);
}

static Dict *dict_new(int sz) {
    Dict *d = gc_alloc(sizeof(Dict), OBJ_DICT);
    d->size = 0; d->capacity = sz;
    d->keys = calloc(sz, sizeof(const char *));
    d->values = calloc(sz, sizeof(Value));
    return d;
}

static void dict_set(Dict *d, const char *key, Value val) {
    for (int i = 0; i < d->size; i++) {
        if (strcmp(d->keys[i], key) == 0) { d->values[i] = val; return; }
    }
    if (d->size >= d->capacity) {
        d->capacity *= 2;
        d->keys = realloc(d->keys, sizeof(const char *) * d->capacity);
        d->values = realloc(d->values, sizeof(Value) * d->capacity);
    }
    d->keys[d->size] = key; d->values[d->size] = val; d->size++;
}

static List *list_new(int sz) {
    List *l = gc_alloc(sizeof(List), OBJ_LIST);
    l->size = sz; l->capacity = sz;
    l->items = sz > 0 ? calloc(sz, sizeof(Value)) : NULL;
    return l;
}

static void list_push(List *l, Value v) {
    if (l->size >= l->capacity) {
        l->capacity = (l->capacity == 0) ? 4 : l->capacity * 2;
        l->items = realloc(l->items, sizeof(Value) * l->capacity);
    }
    l->items[l->size++] = v;
}

static Shape *shape_new(Shape *parent, const char *name, int offset) {
    Shape *s = gc_alloc(sizeof(Shape), OBJ_SHAPE);
    s->parent = parent; s->name = name; s->offset = offset;
    s->transitions = dict_new(4);
    return s;
}

static Shape *shape_get_next(Shape *current, const char *name) {
    for (int i = 0; i < current->transitions->size; i++) {
        if (strcmp(current->transitions->keys[i], name) == 0) return (Shape*)(uintptr_t)(current->transitions->values[i] & MASK_PAYLOAD);
    }
    Shape *next = shape_new(current, name, current->offset + 1);
    dict_set(current->transitions, name, value_obj(next));
    return next;
}

static Class *class_new(const char *name, Class *parent) {
    Class *c = gc_alloc(sizeof(Class), OBJ_CLASS);
    c->name = strdup(name); c->parent = parent;
    c->methods = dict_new(8);
    c->root_shape = shape_new(NULL, "__root__", -1);
    return c;
}

static Instance *instance_new(Class *klass) {
    Instance *i = gc_alloc(sizeof(Instance), OBJ_INSTANCE);
    i->klass = klass; i->shape = klass->root_shape;
    i->fields = NULL; i->field_capacity = 0;
    return i;
}

static BoundMethod *bound_method_new(Value instance, FuncEntry *method) {
    BoundMethod *bm = gc_alloc(sizeof(BoundMethod), OBJ_BOUND_METHOD);
    bm->instance = instance; bm->method = method;
    return bm;
}

static Closure *closure_new(int pc, char **params, Bytecode *body) {
    Closure *c = gc_alloc(sizeof(Closure), OBJ_CLOSURE);
    c->param_count = pc; c->params = malloc(sizeof(char*)*pc);
    for (int i=0; i<pc; i++) c->params[i] = strdup(params[i]);
    c->body = body; c->captured_count = 0;
    c->captured_names = NULL; c->captured_values = NULL;
    return c;
}

static NativeFunc *native_func_new(const char *name, NativeFuncPtr ptr) {
    NativeFunc *nf = gc_alloc(sizeof(NativeFunc), OBJ_NATIVE_FUNC);
    nf->name = name; nf->func = ptr;
    return nf;
}

/* Tokens and Grammar logic */
%union {
    int64_t int_val;
    double float_val;
    const char *str_val;
    struct Node *node;
    struct { struct Node **nodes; int count; } node_list;
    struct { struct Node **keys; struct Node **values; int count; } dict_list;
    struct { char **names; int count; } str_list;
}

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

%type <node> program lines function_body stmt expr term power postfix primary dict_literal
%type <node> condition eif_chain otherwise_part
%type <node_list> expr_list arg_list
%type <dict_list> dict_pairs
%type <str_list> param_list

%left OR_OP
%left AND_OP
%left EQ NEQ LT GT LTE GTE IS IS_NOT IN
%left PLUS MINUS
%left MUL DIV MOD
%right POW
%right UMINUS
%nonassoc NOT_OP

%}

"""

# Re-read the file to ensure we have it clean
with open("src/glowlang.y", "r") as f:
    orig_lines = f.readlines()

# Extract grammar and C code
grammar_start = -1
for i, line in enumerate(orig_lines):
    if "program:" in line:
        grammar_start = i
        break

# The grammar starts with program: and ends before the second %%
# Let's just find the first %% after grammar_start
first_pp_idx = -1
for i, line in enumerate(orig_lines):
    if line.strip() == "%%":
        first_pp_idx = i
        break

# Wait! The grammar actually starts AFTER the first %% which follows the %union and tokens.
# Let's find the FIRST %% and the SECOND %%.
pps = [i for i, line in enumerate(orig_lines) if line.strip() == "%%"]

if len(pps) >= 2:
    grammar_content = "".join(orig_lines[pps[0]:pps[1]])
    final_c_code = "".join(orig_lines[pps[1]:])
else:
    # Fallback
    grammar_content = "%%\\nprogram: lines { root = $1; };\\n"
    final_c_code = "%%\\n"

with open("src/glowlang.y", "w") as f:
    f.write(top_decls + grammar_content + final_c_code)
