content = r"""%code requires {
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

/* VM State */
static __thread Value *vm_stack = NULL;
static __thread int vm_stack_size = 0;
static __thread int sp = 0;

#define PUSH(v) do { \
    if (sp >= vm_stack_size) { \
        vm_stack_size = (vm_stack_size == 0) ? 1024 : vm_stack_size * 2; \
        vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size); \
    } \
    vm_stack[sp++] = (v); \
} while(0)
#define POP() (vm_stack[--sp])

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
void print_value(Value v);
static Value run_bytecode(Bytecode *bc, int base_offset);
static void var_set(const char *name, Value value);
static Value var_get(const char *name);

static __thread Value pending_exception = 0;
static __thread int has_exception = 0;
typedef struct { int handler_ip; int sp; } TryHandler;
static __thread TryHandler try_stack[64];
static __thread int tsp = 0;

/* Value Helpers */
static int value_truthy(Value v) {
    ValueType t = GET_TYPE(v);
    switch (t) {
        case VAL_INT:    return AS_INT(v) != 0;
        case VAL_FLOAT:  return AS_FLOAT(v) != 0.0;
        case VAL_BOOL:   return AS_BOOL(v);
        case VAL_STRING: return AS_STRING(v) && AS_STRING(v)[0] != '\0';
        case VAL_LIST:   return AS_LIST(v) && AS_LIST(v)->size > 0;
        case VAL_DICT:   return AS_DICT(v) && AS_DICT(v)->size > 0;
        default: return 0;
    }
}

static Value value_string_copy(const char *s) {
    size_t len = strlen(s);
    GCString *gs = gc_alloc(sizeof(GCString) + len + 1, OBJ_STRING);
    strcpy(gs->chars, s);
    return make_value(VAL_STRING, (uint64_t)(uintptr_t)gs);
}

/* Hash Tables */
#define VAR_TABLE_SIZE 16384
typedef struct { const char *name; Value value; int used; } VarEntry;
static VarEntry var_table[VAR_TABLE_SIZE];
static void var_table_init(void) { memset(var_table, 0, sizeof(var_table)); }
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
            var_table[idx].name = name; var_table[idx].value = value; var_table[idx].used = 1;
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

static int func_table_size = 1024;
static FuncEntry *func_table = NULL;
static void func_table_init(void) { func_table = calloc(func_table_size, sizeof(FuncEntry)); }
static FuncEntry* func_table_add(const char *name) {
    unsigned int h = hash_str(name) % func_table_size;
    unsigned int idx = h;
    do {
        if (!func_table[idx].used || strcmp(func_table[idx].name, name) == 0) {
            if (!func_table[idx].used) func_table[idx].used = 1; func_table[idx].name = name; return &func_table[idx];
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

/* Concurrency */
typedef struct {
    Value callee;
    int argc;
    Value *args;
} ThreadData;

pthread_mutex_t glow_gil = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gc_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t threads_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    Value **vm_stack_ptr;
    int *sp_ptr;
} ThreadStack;
static ThreadStack active_stacks[128];
static int active_stack_count = 0;

static void vm_stack_init(void) {
    vm_stack_size = 4096;
    vm_stack = malloc(sizeof(Value) * vm_stack_size);
    sp = 0;
    pthread_mutex_lock(&threads_mutex);
    if (active_stack_count < 128) {
        active_stacks[active_stack_count].vm_stack_ptr = &vm_stack;
        active_stacks[active_stack_count].sp_ptr = &sp;
        active_stack_count++;
    }
    pthread_mutex_unlock(&threads_mutex);
}

static void* thread_wrapper(void *arg) {
    ThreadData *td = (ThreadData*)arg;
    pthread_mutex_lock(&glow_gil);
    vm_stack_init();
    FuncEntry *fe = NULL;
    if (GET_TYPE(td->callee) == VAL_OBJECT && (td->callee & MASK_PAYLOAD)) {
        ObjType ot = GET_OBJ_TYPE(td->callee);
        if (ot == OBJ_NATIVE_FUNC) { AS_NATIVE_FUNC(td->callee)->func(td->argc, td->args); }
        else if (ot == OBJ_BOUND_METHOD) { BoundMethod *bm = AS_BOUND_METHOD(td->callee); fe = bm->method; var_set("self", bm->instance); }
    }
    if (fe && fe->body) {
        for (int i=0; i<fe->param_count && i<td->argc; i++) var_set(fe->params[i], td->args[i]);
        run_bytecode(fe->body, 0);
    }
    free(td->args); free(td); 
    pthread_mutex_unlock(&glow_gil);
    return NULL;
}

/* GC */
static GCObject *gc_objects = NULL;
static size_t gc_allocated = 0;
static size_t gc_threshold = 1024 * 1024;

static void gc_mark_object(GCObject *obj) {
    if (!obj || obj->marked) return;
    obj->marked = 1;
    switch (obj->type) {
        case OBJ_LIST: { List *l = (List*)obj; for (int i=0; i<l->size; i++) gc_mark_value(l->items[i]); break; }
        case OBJ_DICT: { Dict *d = (Dict*)obj; for (int i=0; i<d->size; i++) gc_mark_value(d->values[i]); break; }
        case OBJ_CLOSURE: { Closure *c = (Closure*)obj; for (int i=0; i<c->captured_count; i++) gc_mark_value(c->captured_values[i]); break; }
        case OBJ_CLASS: { Class *c = (Class*)obj; gc_mark_object((GCObject*)c->methods); break; }
        case OBJ_INSTANCE: { Instance *ins = (Instance*)obj; gc_mark_object((GCObject*)ins->klass); if(ins->fields) for(int i=0; i<=ins->shape->offset; i++) gc_mark_value(ins->fields[i]); break; }
        case OBJ_BOUND_METHOD: { BoundMethod *bm = (BoundMethod*)obj; gc_mark_value(bm->instance); break; }
        default: break;
    }
}
static void gc_mark_value(Value v) {
    if (GET_TYPE(v) == VAL_OBJECT || GET_TYPE(v) == VAL_LIST || GET_TYPE(v) == VAL_DICT || GET_TYPE(v) == VAL_CLOSURE || GET_TYPE(v) == VAL_STRING) {
        if (v & MASK_PAYLOAD) gc_mark_object((GCObject*)(uintptr_t)(v & MASK_PAYLOAD));
    }
}
static void gc_collect(void) {
    for (int i=0; i<VAR_TABLE_SIZE; i++) if (var_table[i].used) gc_mark_value(var_table[i].value);
    gc_mark_value(pending_exception);
    pthread_mutex_lock(&threads_mutex);
    for (int i=0; i<active_stack_count; i++) {
        Value *stk = *(active_stacks[i].vm_stack_ptr);
        int stk_sp = *(active_stacks[i].sp_ptr);
        if (stk) for (int j=0; j<stk_sp; j++) gc_mark_value(stk[j]);
    }
    pthread_mutex_unlock(&threads_mutex);
    GCObject **p = &gc_objects;
    while (*p) {
        if (!(*p)->marked) {
            GCObject *unmarked = *p; *p = unmarked->next;
            if (unmarked->type == OBJ_LIST) free(((List*)unmarked)->items);
            else if (unmarked->type == OBJ_DICT) { free(((Dict*)unmarked)->keys); free(((Dict*)unmarked)->values); }
            else if (unmarked->type == OBJ_INSTANCE) free(((Instance*)unmarked)->fields);
            gc_allocated -= unmarked->sz; free(unmarked);
        } else { (*p)->marked = 0; p = &((*p)->next); }
    }
    gc_threshold = gc_allocated > 1024*1024 ? gc_allocated * 2 : 1024*1024;
}
static void* gc_alloc(size_t sz, ObjType type) {
    pthread_mutex_lock(&gc_mutex);
    if (gc_allocated > gc_threshold) gc_collect();
    GCObject *obj = calloc(1, sz); obj->type = type; obj->sz = sz; obj->next = gc_objects; gc_objects = obj; gc_allocated += sz;
    pthread_mutex_unlock(&gc_mutex);
    return obj;
}

/* Helpers */
static Dict* dict_new(int sz) { Dict *d = gc_alloc(sizeof(Dict), OBJ_DICT); d->capacity = sz; d->keys = calloc(sz, sizeof(char*)); d->values = calloc(sz, sizeof(Value)); return d; }
static void dict_set(Dict *d, const char *key, Value val) {
    for (int i=0; i<d->size; i++) if (strcmp(d->keys[i], key) == 0) { d->values[i] = val; return; }
    if (d->size >= d->capacity) { d->capacity = (d->capacity==0)?8:d->capacity*2; d->keys = realloc(d->keys, sizeof(char*)*d->capacity); d->values = realloc(d->values, sizeof(Value)*d->capacity); }
    d->keys[d->size] = key; d->values[d->size] = val; d->size++;
}
static Value dict_get(Dict *d, const char *key) {
    if (!d) return value_none();
    for (int i = 0; i < d->size; i++) if (strcmp(d->keys[i], key) == 0) return d->values[i];
    return value_none();
}
static List* list_new(int sz) { List *l = gc_alloc(sizeof(List), OBJ_LIST); l->capacity = sz; l->items = sz > 0 ? calloc(sz, sizeof(Value)) : NULL; return l; }
static void list_push(List *l, Value v) { if (l->size >= l->capacity) { l->capacity = (l->capacity == 0) ? 4 : l->capacity * 2; l->items = realloc(l->items, sizeof(Value)*l->capacity); } l->items[l->size++] = v; }
static Shape* shape_new(Shape *p, const char *n, int o) { Shape *s = gc_alloc(sizeof(Shape), OBJ_SHAPE); s->parent = p; s->name = n; s->offset = o; s->transitions = dict_new(4); return s; }
static Shape* shape_get_next(Shape* curr, const char *name) {
    for (int i=0; i<curr->transitions->size; i++) if (strcmp(curr->transitions->keys[i], name) == 0) return (Shape*)(uintptr_t)(curr->transitions->values[i] & MASK_PAYLOAD);
    Shape *next = shape_new(curr, name, curr->offset + 1); dict_set(curr->transitions, name, value_obj(next)); return next;
}
static Class* class_new(const char *name, Class *parent) { Class *c = gc_alloc(sizeof(Class), OBJ_CLASS); c->name = strdup(name); c->parent = parent; c->methods = dict_new(8); c->root_shape = shape_new(NULL, "__root__", -1); return c; }
static Instance* instance_new(Class *k) { Instance *i = gc_alloc(sizeof(Instance), OBJ_INSTANCE); i->klass = k; i->shape = k->root_shape; i->field_capacity = 8; i->fields = calloc(8, sizeof(Value)); for(int j=0; j<8; j++) i->fields[j] = value_none(); return i; }
static Value class_get_method(Class *k, const char *n) { while(k) { Value m = dict_get(k->methods, n); if((m & MASK_PAYLOAD) != 0) return m; k=k->parent; } return value_none(); }
static Value instance_get(Instance *inst, const char *name) {
    Shape *s = inst->shape; while (s && s->name && strcmp(s->name, "__root__") != 0) { if (strcmp(s->name, name) == 0) return inst->fields[s->offset]; s = s->parent; }
    return class_get_method(inst->klass, name);
}
static void instance_set(Instance *inst, const char *name, Value val) {
    Shape *s = inst->shape; while (s && s->name && strcmp(s->name, "__root__") != 0) { if (strcmp(s->name, name) == 0) { inst->fields[s->offset] = val; return; } s = s->parent; }
    inst->shape = shape_get_next(inst->shape, name);
    if (inst->shape->offset >= inst->field_capacity) { inst->field_capacity = (inst->field_capacity==0)?8:inst->field_capacity*2; inst->fields = realloc(inst->fields, sizeof(Value)*inst->field_capacity); }
    inst->fields[inst->shape->offset] = val;
}
static Trait* trait_new(const char *name) { Trait *t = gc_alloc(sizeof(Trait), OBJ_TRAIT); t->name = strdup(name); t->methods = dict_new(8); return t; }
static Value bound_method_new(Value inst, FuncEntry *m) { BoundMethod *bm = gc_alloc(sizeof(BoundMethod), OBJ_BOUND_METHOD); bm->instance = inst; bm->method = m; return value_bound_method(bm); }
static Value native_func_new(const char *name, NativeFuncPtr ptr) { NativeFunc *nf = gc_alloc(sizeof(NativeFunc), OBJ_NATIVE_FUNC); nf->name = name; nf->func = ptr; return value_native_func(nf); }

/* AST Constructors */
Node* node_new(NodeType t) { Node *n = calloc(1, sizeof(Node)); n->type = t; n->line = line_num; return n; }
static Node* newIntNode(int64_t v) { Node *n = node_new(NODE_INT_LIT); n->value = value_int(v); return n; }
static Node* newFloatNode(double v) { Node *n = node_new(NODE_FLOAT_LIT); n->value = value_float(v); return n; }
static Node* newBoolNode(int v) { Node *n = node_new(NODE_BOOL_LIT); n->value = value_bool(v); return n; }
static Node* newStringNode(const char *s) { Node *n = node_new(NODE_STRING_LIT); n->name = s; return n; }
static Node* newVarNode(const char *s) { Node *n = node_new(NODE_VAR); n->name = s; return n; }
static Node* newAssignNode(const char *s, Node *e) { Node *n = node_new(NODE_ASSIGN); n->name = s; n->left = e; return n; }
static Node* newBinOp(NodeType t, Node *l, Node *r) { Node *n = node_new(t); n->left = l; n->right = r; return n; }
static Node* newSeqNode(Node *l, Node *r) { if(!l) return r; if(!r) return l; Node *n = node_new(NODE_SEQ); n->left = l; n->right = r; return n; }
static Node* newFuncNode(const char *n, int pc, char **ps, Node *b) { Node *nd = node_new(NODE_FUNC_DEF); nd->name = n; nd->func_param_count = pc; nd->func_params = ps; nd->left = b; return nd; }
static Node* newReturnNode(Node *e) { Node *n = node_new(NODE_RETURN); n->left = e; return n; }
static Node* newIfNode(Node *c, Node *t, Node *e) { Node *n = node_new(NODE_IF); n->left = c; n->right = t; n->third = e; return n; }
static Node* newWhileNode(Node *c, Node *b) { Node *n = node_new(NODE_WHILE); n->left = c; n->right = b; return n; }
static Node* newShowNode(Node **is, int c) { Node *n = node_new(NODE_SHOW); n->print_items = is; n->print_count = c; return n; }
static Node* newExprStmtNode(Node *e) { Node *n = node_new(NODE_EXPR_STMT); n->left = e; return n; }
static Node* newCallExprNode(Node *e, Node **as, int c) { Node *n = node_new(NODE_CALL_EXPR); n->left = e; n->call_args = as; n->call_arg_count = c; return n; }
static Node* newIndexAssignNode(Node *c, Node *i, Node *v) { Node *n = node_new(NODE_INDEX_ASSIGN); n->left = c; n->right = i; n->third = v; return n; }

/* Compiler */
static Bytecode *compiling_bc = NULL;
static Class *last_compiling_class = NULL;
static Trait *last_compiling_trait = NULL;
static int bc_emit(Bytecode *bc, OpCode op, int arg, intptr_t ptr) {
    if (bc->count >= bc->capacity) { bc->capacity = (bc->capacity == 0) ? 8 : bc->capacity * 2; bc->code = realloc(bc->code, sizeof(Instruction)*bc->capacity); }
    int idx = bc->count; bc->code[idx].op = op; bc->code[idx].arg = arg; bc->code[idx].ptr = ptr; bc->code[idx].cache_key = NULL; bc->count++; return idx;
}
static Bytecode* bc_new() { Bytecode *bc = calloc(1, sizeof(Bytecode)); bc->capacity = 512; bc->code = calloc(bc->capacity, sizeof(Instruction)); return bc; }
void compile(Node *n) {
    if (!n) return;
    switch (n->type) {
        case NODE_INT_LIT: bc_emit(compiling_bc, OP_CONST_INT, AS_INT(n->value), 0); break;
        case NODE_FLOAT_LIT: { Value *v = malloc(sizeof(Value)); *v = n->value; bc_emit(compiling_bc, OP_CONST_FLOAT, 0, (intptr_t)v); break; }
        case NODE_BOOL_LIT: bc_emit(compiling_bc, OP_CONST_BOOL, AS_BOOL(n->value), 0); break;
        case NODE_STRING_LIT: bc_emit(compiling_bc, OP_CONST_STRING, 0, (intptr_t)n->name); break;
        case NODE_VAR: bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)n->name); break;
        case NODE_ASSIGN: compile(n->left); bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->name); break;
        case NODE_ADD: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_ADD, 0, 0); break;
        case NODE_SUB: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_SUB, 0, 0); break;
        case NODE_MUL: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_MUL, 0, 0); break;
        case NODE_DIV: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_DIV, 0, 0); break;
        case NODE_LT: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_LT, 0, 0); break;
        case NODE_GT: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_GT, 0, 0); break;
        case NODE_LTE: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_LTE, 0, 0); break;
        case NODE_GTE: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_GTE, 0, 0); break;
        case NODE_EQ: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_EQ, 0, 0); break;
        case NODE_NEQ: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_NEQ, 0, 0); break;
        case NODE_AND: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_AND, 0, 0); break;
        case NODE_OR: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_OR, 0, 0); break;
        case NODE_NOT: compile(n->left); bc_emit(compiling_bc, OP_NOT, 0, 0); break;
        case NODE_SEQ: compile(n->left); compile(n->right); break;
        case NODE_SHOW: for (int i=0; i<n->print_count; i++) compile(n->print_items[i]); bc_emit(compiling_bc, OP_SHOW, n->print_count, 0); break;
        case NODE_FUNC_DEF: {
            FuncEntry *fe = func_table_add(n->name); fe->param_count = n->func_param_count; fe->params = n->func_params; fe->body = bc_new();
            Bytecode *p = compiling_bc; compiling_bc = fe->body; compile(n->left); bc_emit(compiling_bc, OP_RETURN, 0, 0); compiling_bc = p;
            bc_emit(compiling_bc, (last_compiling_class || last_compiling_trait) ? OP_METHOD_DEF : OP_FUNC_DEF, 0, (intptr_t)n->name); break;
        }
        case NODE_CLASS: {
            Class *pc = last_compiling_class; last_compiling_class = (Class*)1;
            if (n->parent_name) bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)n->parent_name);
            bc_emit(compiling_bc, OP_CLASS_DEF, n->parent_name ? 1 : 0, (intptr_t)n->name);
            compile(n->left); last_compiling_class = pc; break;
        }
        case NODE_TRAIT: { Trait *pt = last_compiling_trait; last_compiling_trait = (Trait*)1; bc_emit(compiling_bc, OP_TRAIT_DEF, 0, (intptr_t)n->name); compile(n->left); last_compiling_trait = pt; break; }
        case NODE_USE_TRAIT: bc_emit(compiling_bc, OP_USE_TRAIT, 0, (intptr_t)n->name); break;
        case NODE_IF: {
            compile(n->left); int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            compile(n->right); int j = bc_emit(compiling_bc, OP_JUMP, 0, 0);
            compiling_bc->code[jf].arg = compiling_bc->count;
            if (n->third) compile(n->third);
            compiling_bc->code[j].arg = compiling_bc->count; break;
        }
        case NODE_WHILE: {
            int start = compiling_bc->count; compile(n->left); int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            compile(n->right); bc_emit(compiling_bc, OP_JUMP, start, 0);
            compiling_bc->code[jf].arg = compiling_bc->count; break;
        }
        case NODE_TRY_CATCH: {
            int ts = bc_emit(compiling_bc, OP_TRY_START, 0, 0);
            compile(n->left); bc_emit(compiling_bc, OP_TRY_END, 0, 0); int j = bc_emit(compiling_bc, OP_JUMP, 0, 0);
            compiling_bc->code[ts].arg = compiling_bc->count; bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->name);
            compile(n->right); compiling_bc->code[j].arg = compiling_bc->count; break;
        }
        case NODE_THROW: compile(n->left); bc_emit(compiling_bc, OP_THROW, 0, 0); break;
        case NODE_SPAWN: {
            Node *call = n->left;
            if (call->type == NODE_CALL_EXPR || call->type == NODE_CALL) {
                int ac = call->call_arg_count;
                for (int i=0; i<ac; i++) compile(call->call_args[i]);
                if (call->type == NODE_CALL) bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)call->name);
                else compile(call->left);
                bc_emit(compiling_bc, OP_SPAWN, ac, 0);
            }
            break;
        }
        case NODE_LET: compile(n->left); bc_emit(compiling_bc, OP_STORE_LET, 0, (intptr_t)n->name); break;
        case NODE_CALL_EXPR:
        case NODE_CALL: { int ac = n->call_arg_count; for (int i=0; i<ac; i++) compile(n->call_args[i]); if (n->type == NODE_CALL) bc_emit(compiling_bc, OP_CALL, ac, (intptr_t)n->name); else { compile(n->left); bc_emit(compiling_bc, OP_CALL_VALUE, ac, 0); } break; }
        case NODE_RETURN: compile(n->left); bc_emit(compiling_bc, OP_RETURN, 0, 0); break;
        case NODE_EXPR_STMT: compile(n->left); bc_emit(compiling_bc, OP_POP, 0, 0); break;
        case NODE_INDEX: { if (n->name) { compile(n->left); bc_emit(compiling_bc, OP_GET_PROPERTY, 0, (intptr_t)n->name); } else { compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_INDEX, 0, 0); } break; }
        case NODE_INDEX_ASSIGN: { if (n->name) { compile(n->third); compile(n->left); bc_emit(compiling_bc, OP_SET_PROPERTY, 0, (intptr_t)n->name); } else { compile(n->third); compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_STORE_INDEX, 0, 0); } break; }
        case NODE_LIST_LITERAL: { for (int i=0; i<n->element_count; i++) compile(n->elements[i]); bc_emit(compiling_bc, OP_LIST_LITERAL, n->element_count, 0); break; }
        default: break;
    }
}
%}

%union {
    int64_t int_val;
    double float_val;
    const char *str_val;
    struct Node *node;
    struct { struct Node **nodes; int count; } node_list;
    struct { struct Node **keys; struct Node **values; int count; } dict_list;
    struct { char **names; int count; } str_list;
}

%token <int_val> INT_LIT
%token <float_val> FLOAT_LIT
%token <str_val> ID STRING FSTRING
%token SHOW WRITE WHEN EIF OTHERWISE WHILE UNTIL REPEAT FOR EACH IN FUNC LAMBDA RETURN STOP SKIP END LET TRY CATCH THROW IMPORT CLASS TRAIT EXTENDS USE SPAWN TRUE_KW FALSE_KW TIMES_KW INPUT_KW IS IS_NOT FROM TO NEWLINE COMMA ASSIGN PLUS_ASSIGN MINUS_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN EQ NEQ LT GT LTE GTE PLUS MINUS MUL DIV MOD POW COLON AND_OP OR_OP NOT_OP

%type <node> program lines stmt expr term power postfix primary condition eif_chain otherwise_part
%type <node_list> expr_list arg_list
%type <str_list> param_list

%left OR_OP
%left AND_OP
%left EQ NEQ
%left LT GT LTE GTE
%left PLUS MINUS
%left MUL DIV MOD
%right POW
%nonassoc NOT_OP

%%

program: lines { root = $1; };
lines: lines NEWLINE stmt { $$ = newSeqNode($1, $3); } | lines NEWLINE { $$ = $1; } | stmt { $$ = $1; } | IMPORT STRING { $$ = node_new(NODE_IMPORT); $$->name = $2; } | { $$ = NULL; };
stmt: postfix ASSIGN expr { if ($1->type == NODE_VAR) $$ = newAssignNode($1->name, $3); else { $$ = $1; $$->type = NODE_INDEX_ASSIGN; $$->third = $3; } }
    | FUNC ID '(' param_list ')' COLON NEWLINE lines END { $$ = newFuncNode($2, $4.count, $4.names, $8); }
    | FUNC ID '(' ')' COLON NEWLINE lines END { $$ = newFuncNode($2, 0, NULL, $7); }
    | CLASS ID COLON NEWLINE lines END { $$ = node_new(NODE_CLASS); $$->name = $2; $$->left = $5; }
    | CLASS ID EXTENDS ID COLON NEWLINE lines END { $$ = node_new(NODE_CLASS); $$->name = $2; $$->parent_name = $4; $$->left = $7; }
    | TRAIT ID COLON NEWLINE lines END { $$ = node_new(NODE_TRAIT); $$->name = $2; $$->left = $5; }
    | USE ID { $$ = node_new(NODE_USE_TRAIT); $$->name = $2; }
    | WHEN condition COLON NEWLINE lines eif_chain otherwise_part END { $$ = newIfNode($2, $5, $7 ? newSeqNode($6, $7) : $6); }
    | WHILE condition COLON NEWLINE lines END { $$ = newWhileNode($2, $5); }
    | TRY COLON NEWLINE lines CATCH ID COLON NEWLINE lines END { $$ = node_new(NODE_TRY_CATCH); $$->left = $4; $$->name = $6; $$->right = $9; }
    | THROW expr { $$ = node_new(NODE_THROW); $$->left = $2; }
    | SPAWN expr { $$ = node_new(NODE_SPAWN); $$->left = $2; }
    | LET ID ASSIGN expr { $$ = node_new(NODE_LET); $$->name = $2; $$->left = $4; }
    | SHOW expr_list { $$ = newShowNode($2.nodes, $2.count); }
    | expr { $$ = newExprStmtNode($1); };

expr_list: expr { $$.count = 1; $$.nodes = malloc(sizeof(Node*)); $$.nodes[0] = $1; } | expr_list COMMA expr { $$.nodes = realloc($$.nodes, sizeof(Node*)*($1.count+1)); $$.nodes[$1.count] = $3; $$.count = $1.count + 1; };
arg_list: expr_list { $$ = $1; } | { $$.nodes = NULL; $$.count = 0; };
param_list: ID { $$.count = 1; $$.names = malloc(sizeof(char*)); $$.names[0] = strdup($1); } | param_list COMMA ID { $$.names = realloc($$.names, sizeof(char*)*($1.count+1)); $$.names[$1.count] = strdup($3); $$.count = $1.count + 1; };
condition: expr;
eif_chain: eif_chain EIF condition COLON NEWLINE lines { $$ = newSeqNode($1, newIfNode($3, $6, NULL)); } | { $$ = NULL; };
otherwise_part: OTHERWISE COLON NEWLINE lines { $$ = $4; } | { $$ = NULL; };

expr: term | expr PLUS term { $$ = newBinOp(NODE_ADD, $1, $3); } | expr MINUS term { $$ = newBinOp(NODE_SUB, $1, $3); } | expr EQ term { $$ = newBinOp(NODE_EQ, $1, $3); } | expr NEQ term { $$ = newBinOp(NODE_NEQ, $1, $3); } | expr LT term { $$ = newBinOp(NODE_LT, $1, $3); } | expr GT term { $$ = newBinOp(NODE_GT, $1, $3); } | expr LTE term { $$ = newBinOp(NODE_LTE, $1, $3); } | expr GTE term { $$ = newBinOp(NODE_GTE, $1, $3); } | expr AND_OP term { $$ = newBinOp(NODE_AND, $1, $3); } | expr OR_OP term { $$ = newBinOp(NODE_OR, $1, $3); } | NOT_OP term { $$ = newBinOp(NODE_NOT, $2, NULL); };
term: power | term MUL power { $$ = newBinOp(NODE_MUL, $1, $3); } | term DIV power { $$ = newBinOp(NODE_DIV, $1, $3); } | term MOD power { $$ = newBinOp(NODE_MOD, $1, $3); };
power: postfix | postfix POW power { $$ = newBinOp(NODE_POW, $1, $3); };
postfix: primary | postfix '.' ID { $$ = node_new(NODE_INDEX); $$->left = $1; $$->name = $3; } | postfix '(' arg_list ')' { $$ = newCallExprNode($1, $3.nodes, $3.count); } | postfix '(' ')' { $$ = newCallExprNode($1, NULL, 0); } | postfix '[' expr ']' { $$ = node_new(NODE_INDEX); $$->left = $1; $$->right = $3; };
primary: ID { $$ = newVarNode($1); } | INT_LIT { $$ = newIntNode($1); } | STRING { $$ = newStringNode($1); } | FLOAT_LIT { $$ = newFloatNode($1); } | TRUE_KW { $$ = newBoolNode(1); } | FALSE_KW { $$ = newBoolNode(0); } | '(' expr ')' { $$ = $2; } | '[' expr_list ']' { $$ = node_new(NODE_LIST_LITERAL); $$->elements = $2.nodes; $$->element_count = $2.count; } | '[' ']' { $$ = node_new(NODE_LIST_LITERAL); $$->elements = NULL; $$->element_count = 0; };

%%

static Class *vm_last_class = NULL;
static Trait *vm_last_trait = NULL;

static Value run_bytecode(Bytecode *bc, int base_offset) {
    static void* dt[] = {
        &&l_int, &&l_float, &&l_str, &&l_bool, &&l_load, &&l_store, &&l_add, &&l_sub, &&l_mul, &&l_div, &&l_mod, &&l_pow, &&l_neg,
        &&l_lt, &&l_gt, &&l_lte, &&l_gte, &&l_eq, &&l_neq, &&l_and, &&l_or, &&l_not, &&l_string_concat, &&l_jump, &&l_jf, &&l_jt,
        &&l_len, &&l_range, &&l_type, &&l_append, &&l_input, &&l_show, &&l_write, &&l_list, &&l_dict, &&l_idx, &&l_slice, &&l_sidx,
        &&l_fdef, &&l_call, &&l_ret, &&l_for, &&l_forp, &&l_stop, &&l_skip, &&l_pop, &&l_ps, &&l_pos, &&l_sl, &&l_ll, &&l_sloc,
        &&l_dk, &&l_dv, &&l_dh, &&l_dr, &&l_dm, &&l_abs, &&l_min, &&l_max, &&l_rnd, &&l_flr, &&l_ceil, &&l_sqrt, &&l_upper,
        &&l_lower, &&l_trim, &&l_find, &&l_repl, &&l_sw, &&l_ew, &&l_split, &&l_push, &&l_lpop, &&l_ins, &&l_lrem, &&l_cont,
        &&l_sort, &&l_rev, &&l_fr, &&l_fw, &&l_fa, &&l_fstr, &&l_lcomp, &&l_thr, &&l_ts, &&l_te, &&l_lam, &&l_cv, &&l_cdef,
        &&l_mdef, &&l_tdef, &&l_ut, &&l_gp, &&l_sp, &&l_spawn
    };
    int ip = 0;
#define DISPATCH() do { if(ip >= bc->count) return value_none(); goto *dt[bc->code[ip].op]; } while(0)
    DISPATCH();

    l_int: PUSH(value_int(bc->code[ip].arg)); ip++; DISPATCH();
    l_float: PUSH(*(Value*)bc->code[ip].ptr); ip++; DISPATCH();
    l_str: PUSH(value_string_copy((const char*)bc->code[ip].ptr)); ip++; DISPATCH();
    l_bool: PUSH(value_bool(bc->code[ip].arg)); ip++; DISPATCH();
    l_load: PUSH(var_get((const char*)bc->code[ip].ptr)); ip++; DISPATCH();
    l_store: var_set((const char*)bc->code[ip].ptr, POP()); ip++; DISPATCH();
    l_add: {
        Value b = POP(), a = POP();
        if (GET_TYPE(a) == VAL_STRING || GET_TYPE(b) == VAL_STRING) {
            char buf1[64], buf2[64]; const char *s1, *s2;
            if (GET_TYPE(a) == VAL_STRING) s1 = AS_STRING(a); else if(GET_TYPE(a)==VAL_INT) sprintf(buf1, "%ld", (long)AS_INT(a)), s1 = buf1; else s1="none";
            if (GET_TYPE(b) == VAL_STRING) s2 = AS_STRING(b); else if(GET_TYPE(b)==VAL_INT) sprintf(buf2, "%ld", (long)AS_INT(b)), s2 = buf2; else s2="none";
            char *res = malloc(strlen(s1) + strlen(s2) + 1); strcpy(res, s1); strcat(res, s2);
            PUSH(value_string_copy(res)); free(res);
        } else if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) PUSH(value_int(AS_INT(a) + AS_INT(b)));
        else PUSH(value_float(((GET_TYPE(a)==VAL_INT)?(double)AS_INT(a):AS_FLOAT(a)) + ((GET_TYPE(b)==VAL_INT)?(double)AS_INT(b):AS_FLOAT(b))));
        ip++; DISPATCH();
    }
    l_sub: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) - AS_INT(b))); ip++; DISPATCH(); }
    l_mul: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) * AS_INT(b))); ip++; DISPATCH(); }
    l_div: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) / AS_INT(b))); ip++; DISPATCH(); }
    l_mod: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) % AS_INT(b))); ip++; DISPATCH(); }
    l_pow: { Value b = POP(), a = POP(); PUSH(value_float(pow(AS_FLOAT(a), AS_FLOAT(b)))); ip++; DISPATCH(); }
    l_neg: { Value a = POP(); PUSH(value_int(-AS_INT(a))); ip++; DISPATCH(); }
    l_lt: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) < AS_INT(b))); ip++; DISPATCH(); }
    l_gt: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) > AS_INT(b))); ip++; DISPATCH(); }
    l_lte: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) <= AS_INT(b))); ip++; DISPATCH(); }
    l_gte: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) >= AS_INT(b))); ip++; DISPATCH(); }
    l_eq: { Value b = POP(), a = POP(); PUSH(value_bool(a == b)); ip++; DISPATCH(); }
    l_neq: { Value b = POP(), a = POP(); PUSH(value_bool(a != b)); ip++; DISPATCH(); }
    l_and: { Value b = POP(), a = POP(); PUSH(value_bool(value_truthy(a) && value_truthy(b))); ip++; DISPATCH(); }
    l_or: { Value b = POP(), a = POP(); PUSH(value_bool(value_truthy(a) || value_truthy(b))); ip++; DISPATCH(); }
    l_not: { Value a = POP(); PUSH(value_bool(!value_truthy(a))); ip++; DISPATCH(); }
    l_string_concat: ip++; DISPATCH();
    l_jump: ip = bc->code[ip].arg; DISPATCH();
    l_jf: { Value v = POP(); if (!value_truthy(v)) ip = bc->code[ip].arg; else ip++; DISPATCH(); }
    l_jt: { Value v = POP(); if (value_truthy(v)) ip = bc->code[ip].arg; else ip++; DISPATCH(); }
    l_show: { int n = bc->code[ip].arg; for (int i=0; i<n; i++) { print_value(vm_stack[sp-n+i]); if(i<n-1) printf(" "); } printf("\n"); fflush(stdout); sp-=n; ip++; DISPATCH(); }
    l_cdef: {
        const char *n = (const char*)bc->code[ip].ptr; Class *parent = NULL;
        if (bc->code[ip].arg == 1) { Value pval = POP(); if (GET_TYPE(pval) == VAL_OBJECT && (pval & MASK_PAYLOAD)) parent = AS_CLASS(pval); }
        Value c = value_class(class_new(n, parent)); var_set(n, c); vm_last_class = AS_CLASS(c); ip++; DISPATCH();
    }
    l_mdef: { const char *n = (const char*)bc->code[ip].ptr; FuncEntry *fe = func_get(n); if(vm_last_class) dict_set(vm_last_class->methods, n, bound_method_new(value_none(), fe)); else if(vm_last_trait) dict_set(vm_last_trait->methods, n, bound_method_new(value_none(), fe)); ip++; DISPATCH(); }
    l_gp: {
        const char *n = (const char*)bc->code[ip].ptr; Value o = POP();
        if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(o);
            if(ot==OBJ_INSTANCE) {
                Value v = instance_get(AS_INSTANCE(o), n);
                if(GET_TYPE(v)==VAL_OBJECT && (v & MASK_PAYLOAD) && GET_OBJ_TYPE(v)==OBJ_BOUND_METHOD && GET_TYPE(AS_BOUND_METHOD(v)->instance)==VAL_NONE)
                    v = bound_method_new(o, AS_BOUND_METHOD(v)->method);
                PUSH(v); ip++; DISPATCH();
            }
        }
        PUSH(value_none()); ip++; DISPATCH();
    }
    l_sp: {
        const char *n = (const char*)bc->code[ip].ptr; Value o = POP(); Value v = vm_stack[sp-1];
        if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD) && GET_OBJ_TYPE(o)==OBJ_INSTANCE) instance_set(AS_INSTANCE(o), n, v);
        ip++; DISPATCH();
    }
    l_cv: {
        int ac = bc->code[ip].arg; Value callee = POP();
        if(GET_TYPE(callee)==VAL_OBJECT && (callee & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(callee);
            if(ot==OBJ_CLASS) { PUSH(value_instance(instance_new(AS_CLASS(callee)))); }
            else if(ot==OBJ_BOUND_METHOD) { 
                BoundMethod *bm = AS_BOUND_METHOD(callee); Value s = var_get("self"); var_set("self", bm->instance);
                if(bm->method && bm->method->body) {
                    Value *saved = (bm->method->param_count > 0 && bm->method->params) ? malloc(sizeof(Value)*bm->method->param_count) : NULL;
                    for(int i=0; i<bm->method->param_count && i<ac; i++) { saved[i] = var_get(bm->method->params[i]); if(sp-ac+i>=0) var_set(bm->method->params[i], vm_stack[sp-ac+i]); }
                    sp -= ac; Value res = run_bytecode(bm->method->body, 0);
                    for(int i=0; i<bm->method->param_count && i<ac; i++) var_set(bm->method->params[i], saved[i]);
                    if(saved) free(saved);
                    if(has_exception) { PUSH(pending_exception); var_set("self", s); goto l_thr; }
                    PUSH(res);
                } else { sp -= ac; PUSH(value_none()); }
                var_set("self", s); 
            }
            else if(ot==OBJ_NATIVE_FUNC) { NativeFunc *nf = AS_NATIVE_FUNC(callee); Value *args = &vm_stack[sp-ac]; Value res = nf->func(ac, args); sp-=ac; PUSH(res); }
        }
        ip++; DISPATCH();
    }
    l_call: {
        int ac = bc->code[ip].arg; const char *n = (const char*)bc->code[ip].ptr; FuncEntry *fe = func_get(n);
        if (fe && fe->body) {
            Value *saved = (fe->param_count > 0 && fe->params) ? malloc(sizeof(Value)*fe->param_count) : NULL;
            for(int i=0; i<fe->param_count && i<ac; i++) { saved[i] = var_get(fe->params[i]); if(sp-ac+i>=0) var_set(fe->params[i], vm_stack[sp-ac+i]); }
            sp -= ac; Value res = run_bytecode(fe->body, 0);
            for(int i=0; i<fe->param_count && i<ac; i++) var_set(fe->params[i], saved[i]);
            if(saved) free(saved);
            if(has_exception) { PUSH(pending_exception); goto l_thr; }
            PUSH(res);
        } else { sp -= ac; PUSH(value_none()); }
        ip++; DISPATCH();
    }
    l_list: { int n = bc->code[ip].arg; List *l = list_new(n); for(int i=0; i<n; i++) list_push(l, vm_stack[sp-n+i]); sp-=n; PUSH(value_list(l)); ip++; DISPATCH(); }
    l_idx: { Value i = POP(), o = POP(); if(GET_TYPE(o)==VAL_LIST && GET_TYPE(i)==VAL_INT) { int idx = AS_INT(i); if(idx>=0 && idx<AS_LIST(o)->size) PUSH(AS_LIST(o)->items[idx]); else PUSH(value_none()); } else PUSH(value_none()); ip++; DISPATCH(); }
    l_ret: return sp > 0 ? POP() : value_none();
    l_pop: POP(); ip++; DISPATCH();
    l_ts: try_stack[tsp++] = (TryHandler){ bc->code[ip].arg, sp }; ip++; DISPATCH();
    l_te: tsp--; ip++; DISPATCH();
    l_thr: {
        Value exc = POP();
        if (tsp > 0) { TryHandler h = try_stack[--tsp]; sp = h.sp; PUSH(exc); has_exception = 0; ip = h.handler_ip; DISPATCH(); }
        pending_exception = exc; has_exception = 1; return value_none();
    }
    l_spawn: {
        int ac = bc->code[ip].arg; Value callee = POP(); ThreadData *td = malloc(sizeof(ThreadData)); td->callee = callee; td->argc = ac; td->args = malloc(sizeof(Value)*ac);
        for(int i=0; i<ac; i++) td->args[ac-1-i] = POP();
        pthread_t t; pthread_create(&t, NULL, thread_wrapper, td); pthread_detach(t); ip++; DISPATCH();
    }
    l_sl: var_set((const char*)bc->code[ip].ptr, POP()); ip++; DISPATCH();
    l_tdef: { const char *n = (const char*)bc->code[ip].ptr; Value t = value_trait(trait_new(n)); var_set(n, t); vm_last_trait = AS_TRAIT(t); ip++; DISPATCH(); }
    l_ut: {
        Value t_val = var_get((const char*)bc->code[ip].ptr);
        if (GET_TYPE(t_val) == VAL_OBJECT && (t_val & MASK_PAYLOAD) && GET_OBJ_TYPE(t_val) == OBJ_TRAIT && vm_last_class) {
            Trait *t = AS_TRAIT(t_val);
            for (int i=0; i<t->methods->size; i++) dict_set(vm_last_class->methods, t->methods->keys[i], t->methods->values[i]);
        }
        ip++; DISPATCH();
    }
    l_ps: l_pos: l_sloc: l_ll: l_dk: l_dv: l_dh: l_dr: l_dm: l_abs: l_min: l_max: l_rnd: l_flr: l_ceil: l_sqrt: l_upper: l_lower: l_trim: l_find: l_repl: l_sw: l_ew: l_split: l_push: l_lpop: l_ins: l_lrem: l_cont: l_sort: l_rev: l_fr: l_fw: l_fa: l_fstr: l_lcomp: l_lam: l_sidx: l_fdef: l_for: l_forp: l_stop: l_skip: l_type: l_append: l_input: l_write: l_dict: l_slice: l_range: l_len: ip++; DISPATCH();
}

void print_value(Value v) {
    if (v == 0) { printf("none"); return; }
    ValueType t = GET_TYPE(v);
    if (t == VAL_INT) printf("%ld", (long)AS_INT(v));
    else if (t == VAL_FLOAT) printf("%g", AS_FLOAT(v));
    else if (t == VAL_BOOL) printf(AS_BOOL(v) ? "true" : "false");
    else if (t == VAL_STRING) printf("%s", AS_STRING(v));
    else if (t == VAL_LIST) { List *l = AS_LIST(v); printf("["); for (int i=0; i<l->size; i++) { print_value(l->items[i]); if (i < l->size - 1) printf(", "); } printf("]"); }
    else if (t == VAL_OBJECT) {
        if (!(v & MASK_PAYLOAD)) { printf("none"); return; }
        ObjType ot = GET_OBJ_TYPE(v);
        if (ot == OBJ_CLASS) printf("<class %s>", AS_CLASS(v)->name);
        else if (ot == OBJ_INSTANCE) printf("<instance of %s>", AS_INSTANCE(v)->klass->name);
        else printf("<object>");
    } else printf("none");
}

static Value native_len(int argc, Value *args) { if (argc < 1) return value_int(0); Value v = args[0]; if (GET_TYPE(v) == VAL_STRING) return value_int(strlen(AS_STRING(v))); if (GET_TYPE(v) == VAL_LIST) return value_int(AS_LIST(v)->size); return value_int(0); }
static Value native_range(int argc, Value *args) { int n = (argc > 0) ? (int)AS_INT(args[0]) : 0; List *l = list_new(n); for (int i = 0; i < n; i++) list_push(l, value_int(i)); return value_list(l); }
static Value os_exit(int argc, Value *args) { exit(argc > 0 ? AS_INT(args[0]) : 0); return value_none(); }
static Value os_time(int argc, Value *args) { return value_float((double)time(NULL)); }
static Value os_system(int argc, Value *args) { if (argc < 1) return value_int(-1); return value_int(system(AS_STRING(args[0]))); }

static void std_init(void) {
    var_set("len", native_func_new("len", native_len));
    var_set("range", native_func_new("range", native_range));
    Dict *os = dict_new(4);
    dict_set(os, "exit", native_func_new("exit", os_exit));
    dict_set(os, "time", native_func_new("time", os_time));
    dict_set(os, "system", native_func_new("system", os_system));
    var_set("os", value_dict(os));
}

Node *root = NULL;
GlowError *glow_errors = NULL;
int glow_error_count = 0;
static int glow_error_capacity = 0;
void glow_clear_errors(void) { for (int i = 0; i < glow_error_count; i++) if(glow_errors[i].message) free(glow_errors[i].message); glow_error_count = 0; }
void glow_add_error(int line, const char *msg) {
    if (glow_error_count >= glow_error_capacity) { glow_error_capacity = (glow_error_capacity == 0) ? 8 : glow_error_capacity * 2; glow_errors = realloc(glow_errors, sizeof(GlowError) * glow_error_capacity); }
    glow_errors[glow_error_count].line = line; glow_errors[glow_error_count].message = strdup(msg); glow_error_count++;
}

int main(int argc, char **argv) {
    var_table_init(); func_table_init(); vm_stack_init(); intern_table_init(); std_init();
    if (argc > 1) { yyin = fopen(argv[1], "r"); if(yyin) { yyparse(); if(root) { Bytecode *main_bc = bc_new(); compiling_bc = main_bc; compile(root); bc_emit(main_bc, OP_RETURN, 0, 0); run_bytecode(main_bc, 0); } fclose(yyin); } }
    return 0;
}
void yyerror(const char *s) { fprintf(stderr, "Error: %s at line %d\n", s, line_num); }
"""
with open("src/glowlang.y", "w") as f:
    f.write(content)
