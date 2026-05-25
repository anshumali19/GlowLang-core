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

extern const char *intern_string(const char *s);
extern void intern_table_init(void);
extern int line_num;
extern int yylex(void);
extern FILE *yyin;
void yyerror(const char *s);

static Value run_bytecode(Bytecode *bc, int base_offset);
void print_value(Value v);
void vm_stack_init(void);
static void std_init(void);

/* Hash Tables for Globals and Functions */
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
            func_table[idx].used = 1; func_table[idx].name = name; return &func_table[idx];
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

/* GC Dummy */
GCObject *gc_objects = NULL;
static void* gc_alloc(size_t sz, ObjType type) {
    GCObject *obj = calloc(1, sz); obj->type = type; obj->sz = sz; obj->next = gc_objects; gc_objects = obj;
    return obj;
}

/* Helpers */
static Value value_string_copy(const char *s) {
    size_t len = strlen(s);
    GCString *gs = gc_alloc(sizeof(GCString) + len + 1, OBJ_STRING);
    strcpy(gs->chars, s);
    return make_value(VAL_STRING, (uint64_t)(uintptr_t)gs);
}
Dict* dict_new(int sz) { Dict *d = gc_alloc(sizeof(Dict), OBJ_DICT); d->capacity = sz; d->keys = calloc(sz, sizeof(char*)); d->values = calloc(sz, sizeof(Value)); return d; }
void dict_set(Dict *d, const char *key, Value val) {
    for (int i=0; i<d->size; i++) if (strcmp(d->keys[i], key) == 0) { d->values[i] = val; return; }
    if (d->size >= d->capacity) { d->capacity = (d->capacity==0)?8:d->capacity*2; d->keys = realloc(d->keys, sizeof(char*)*d->capacity); d->values = realloc(d->values, sizeof(Value)*d->capacity); }
    d->keys[d->size] = key; d->values[d->size] = val; d->size++;
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
static Value class_get_method(Class *k, const char *n) { while(k) { for(int i=0; i<k->methods->size; i++) if(strcmp(k->methods->keys[i], n)==0) return k->methods->values[i]; k=k->parent; } return value_none(); }
static Value instance_get(Instance *inst, const char *name) {
    Shape *s = inst->shape; while (s && s->name && strcmp(s->name, "__root__") != 0) { if (strcmp(s->name, name) == 0) return inst->fields[s->offset]; s = s->parent; }
    return class_get_method(inst->klass, name);
}
static void instance_set(Instance *inst, const char *name, Value val) {
    Shape *s = inst->shape; while (s && s->name && strcmp(s->name, "__root__") != 0) { if (strcmp(s->name, name) == 0) { inst->fields[s->offset] = val; return; } s = s->parent; }
    inst->shape = shape_get_next(inst->shape, name);
    if (inst->shape->offset >= inst->field_capacity) { int old = inst->field_capacity; inst->field_capacity = (inst->field_capacity==0)?8:inst->field_capacity*2; inst->fields = realloc(inst->fields, sizeof(Value)*inst->field_capacity); for(int j=old; j<inst->field_capacity; j++) inst->fields[j] = value_none(); }
    inst->fields[inst->shape->offset] = val;
}
static Value bound_method_new(Value inst, FuncEntry *m) { BoundMethod *bm = gc_alloc(sizeof(BoundMethod), OBJ_BOUND_METHOD); bm->instance = inst; bm->method = m; return value_bound_method(bm); }
Value native_func_new(const char *name, NativeFuncPtr ptr) { NativeFunc *nf = gc_alloc(sizeof(NativeFunc), OBJ_NATIVE_FUNC); nf->name = name; nf->func = ptr; return value_native_func(nf); }

/* AST Nodes */
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
static Node* newShowNode(Node **is, int c) { Node *n = node_new(NODE_SHOW); n->print_items = is; n->print_count = c; return n; }
static Node* newExprStmtNode(Node *e) { Node *n = node_new(NODE_EXPR_STMT); n->left = e; return n; }
static Node* newCallExprNode(Node *e, Node **as, int c) { Node *n = node_new(NODE_CALL_EXPR); n->left = e; n->call_args = as; n->call_arg_count = c; return n; }
static Node* newIndexAssignNode(Node *c, Node *i, Node *v) { Node *n = node_new(NODE_INDEX_ASSIGN); n->left = c; n->right = i; n->third = v; return n; }
static Node* newCompoundNode(NodeType t, const char *s, Node *e) { Node *n = node_new(t); n->name = s; n->left = e; return n; }
static Node* newIfNode(Node *c, Node *t, Node *e) { Node *n = node_new(NODE_IF); n->left = c; n->right = t; n->third = e; return n; }
static Node* newWhileNode(Node *c, Node *b) { Node *n = node_new(NODE_WHILE); n->left = c; n->right = b; return n; }

/* Compiler */
static Bytecode *compiling_bc = NULL;
static Class *last_compiling_class = NULL;

static int bc_emit(Bytecode *bc, OpCode op, int arg, intptr_t ptr) {
    if (bc->count >= bc->capacity) { bc->capacity = (bc->capacity == 0) ? 8 : bc->capacity * 2; bc->code = realloc(bc->code, sizeof(Instruction)*bc->capacity); }
    int idx = bc->count; bc->code[idx].op = op; bc->code[idx].arg = arg; bc->code[idx].ptr = ptr; bc->count++; return idx;
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
        case NODE_COMPOUND_ADD: bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)n->name); compile(n->left); bc_emit(compiling_bc, OP_ADD, 0, 0); bc_emit(compiling_bc, OP_STORE_VAR, 0, (intptr_t)n->name); break;
        case NODE_ADD: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_ADD, 0, 0); break;
        case NODE_SUB: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_SUB, 0, 0); break;
        case NODE_MUL: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_MUL, 0, 0); break;
        case NODE_DIV: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_DIV, 0, 0); break;
        case NODE_LT: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_LT, 0, 0); break;
        case NODE_GT: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_GT, 0, 0); break;
        case NODE_EQ: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_EQ, 0, 0); break;
        case NODE_NEQ: compile(n->left); compile(n->right); bc_emit(compiling_bc, OP_EQ, 0, 0); bc_emit(compiling_bc, OP_NOT, 0, 0); break;
        case NODE_SEQ: compile(n->left); compile(n->right); break;
        case NODE_SHOW: for (int i=0; i<n->print_count; i++) compile(n->print_items[i]); bc_emit(compiling_bc, OP_BUILTIN_SHOW, n->print_count, 0); break;
        case NODE_FUNC_DEF: {
            FuncEntry *fe = func_table_add(n->name); fe->param_count = n->func_param_count; fe->params = n->func_params; fe->body = bc_new();
            Bytecode *p = compiling_bc; compiling_bc = fe->body; compile(n->left); bc_emit(compiling_bc, OP_RETURN, 0, 0); compiling_bc = p;
            bc_emit(compiling_bc, last_compiling_class ? OP_METHOD_DEF : OP_FUNC_DEF, 0, (intptr_t)n->name); break;
        }
        case NODE_CLASS: { Class *pc = last_compiling_class; last_compiling_class = (Class*)1; bc_emit(compiling_bc, OP_CLASS_DEF, 0, (intptr_t)n->name); compile(n->left); last_compiling_class = pc; break; }
        case NODE_CALL_EXPR: { int ac = n->call_arg_count; for (int i=0; i<ac; i++) compile(n->call_args[i]); compile(n->left); bc_emit(compiling_bc, OP_CALL_VALUE, ac, 0); break; }
        case NODE_CALL: { int ac = n->call_arg_count; for (int i=0; i<ac; i++) compile(n->call_args[i]); bc_emit(compiling_bc, OP_CALL, ac, (intptr_t)n->name); break; }
        case NODE_RETURN: compile(n->left); bc_emit(compiling_bc, OP_RETURN, 0, 0); break;
        case NODE_IF: {
            compile(n->left); int patch1 = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            compile(n->right); int patch2 = bc_emit(compiling_bc, OP_JUMP, 0, 0);
            compiling_bc->code[patch1].arg = compiling_bc->count;
            if(n->third) compile(n->third);
            compiling_bc->code[patch2].arg = compiling_bc->count;
            break;
        }
        case NODE_WHILE: {
            int start = compiling_bc->count; compile(n->left); int patch = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            compile(n->right); bc_emit(compiling_bc, OP_JUMP, start, 0);
            compiling_bc->code[patch].arg = compiling_bc->count;
            break;
        }
        case NODE_LIST_LITERAL: for (int i=0; i<n->element_count; i++) compile(n->elements[i]); bc_emit(compiling_bc, OP_LIST_LITERAL, n->element_count, 0); break;
        case NODE_INDEX: compile(n->left); if (n->name) bc_emit(compiling_bc, OP_GET_PROPERTY, 0, (intptr_t)n->name); else { compile(n->right); bc_emit(compiling_bc, OP_INDEX, 0, 0); } break;
        case NODE_INDEX_ASSIGN: compile(n->third); compile(n->left); if (n->name) bc_emit(compiling_bc, OP_SET_PROPERTY, 0, (intptr_t)n->name); else { compile(n->right); bc_emit(compiling_bc, OP_STORE_INDEX, 0, 0); } break;
        case NODE_EXPR_STMT: compile(n->left); bc_emit(compiling_bc, OP_POP, 0, 0); break;
        case NODE_DICT_LITERAL: for(int i=0; i<n->dict_count; i++) { compile(n->dict_keys[i]); compile(n->dict_values[i]); } bc_emit(compiling_bc, OP_DICT_LITERAL, n->dict_count, 0); break;
        case NODE_IMPORT: bc_emit(compiling_bc, OP_CONST_STRING, 0, (intptr_t)n->name); bc_emit(compiling_bc, OP_LOAD_VAR, 0, (intptr_t)"import"); bc_emit(compiling_bc, OP_CALL_VALUE, 1, 0); bc_emit(compiling_bc, OP_POP, 0, 0); break;
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
    struct { char **names; int count; } str_list;
    struct { struct Node **keys; struct Node **values; int count; } dict_list;
}

%token <int_val> INT_LIT
%token <float_val> FLOAT_LIT
%token <str_val> ID STRING FSTRING
%token SHOW WRITE WHEN EIF OTHERWISE WHILE UNTIL REPEAT FOR EACH IN FUNC LAMBDA RETURN STOP SKIP END LET TRY CATCH THROW IMPORT CLASS TRAIT EXTENDS USE SPAWN TRUE_KW FALSE_KW TIMES_KW INPUT_KW IS IS_NOT FROM TO NEWLINE COMMA ASSIGN PLUS_ASSIGN MINUS_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN EQ NEQ LT GT LTE GTE PLUS MINUS MUL DIV MOD POW COLON AND_OP OR_OP NOT_OP

%type <node> program lines stmt expr equality relational additive multiplicative unary postfix primary condition eif_chain otherwise_part
%type <node_list> expr_list arg_list
%type <str_list> param_list
%type <dict_list> dict_pairs

%start program

%%

program: lines { root = $1; };

lines: 
      lines NEWLINE stmt { $$ = newSeqNode($1, $3); }
    | lines NEWLINE      { $$ = $1; }
    | stmt               { $$ = $1; }
    | IMPORT STRING      { Node *n = node_new(NODE_IMPORT); n->name = $2; $$ = n; }
    | /* empty */        { $$ = NULL; }
    ;

stmt:
      postfix ASSIGN expr { if ($1->type == NODE_VAR) $$ = newAssignNode($1->name, $3); else if ($1->type == NODE_INDEX) { if ($1->name) $$ = newIndexAssignNode($1->left, NULL, $3); else $$ = newIndexAssignNode($1->left, $1->right, $3); $$->name = $1->name; } else $$ = $1; }
    | ID PLUS_ASSIGN expr  { $$ = newCompoundNode(NODE_COMPOUND_ADD,  $1, $3); }
    | FUNC ID '(' param_list ')' COLON NEWLINE lines END { $$ = newFuncNode($2, $4.count, $4.names, $8); }
    | CLASS ID COLON NEWLINE lines END { $$ = node_new(NODE_CLASS); $$->name = $2; $$->left = $5; }
    | WHEN condition COLON NEWLINE lines eif_chain otherwise_part END { $$ = newIfNode($2, $5, $7 ? newSeqNode($6, $7) : $6); }
    | WHILE condition COLON NEWLINE lines END { $$ = newWhileNode($2, $5); }
    | SHOW expr_list { $$ = newShowNode($2.nodes, $2.count); }
    | expr { $$ = newExprStmtNode($1); }
;

condition: expr ;

eif_chain: 
      /* empty */ { $$ = NULL; }
    | eif_chain EIF condition COLON NEWLINE lines { $$ = newSeqNode($1, newIfNode($3, $6, NULL)); }
    ;

otherwise_part: 
      /* empty */ { $$ = NULL; }
    | OTHERWISE COLON NEWLINE lines { $$ = $4; }
    ;

expr_list: 
      expr { $$.count = 1; $$.nodes = malloc(sizeof(Node*)); $$.nodes[0] = $1; } 
    | expr_list COMMA expr { $$.nodes = realloc($$.nodes, sizeof(Node*)*($1.count+1)); $$.nodes[$1.count] = $3; $$.count = $1.count + 1; }
    ;

arg_list: 
      expr_list { $$ = $1; }
    | /* empty */ { $$.nodes = NULL; $$.count = 0; }
    ;

param_list: 
      ID { $$.count = 1; $$.names = malloc(sizeof(char*)); $$.names[0] = strdup($1); } 
    | param_list COMMA ID { $$.names = realloc($$.names, sizeof(char*)*($1.count+1)); $$.names[$1.count] = strdup($3); $$.count = $1.count + 1; }
    | /* empty */ { $$.count = 0; $$.names = NULL; }
    ;

dict_pairs:
      expr COLON expr { $$.count = 1; $$.keys = malloc(sizeof(Node*)); $$.values = malloc(sizeof(Node*)); $$.keys[0] = $1; $$.values[0] = $3; }
    | dict_pairs COMMA expr COLON expr { $$.keys = realloc($$.keys, sizeof(Node*)*($1.count+1)); $$.values = realloc($$.values, sizeof(Node*)*($1.count+1)); $$.keys[$1.count] = $3; $$.values[$1.count] = $5; $$.count = $1.count + 1; }
    ;

expr: equality ;

equality: 
      relational 
    | equality EQ relational { $$ = newBinOp(NODE_EQ, $1, $3); }
    | equality NEQ relational { $$ = newBinOp(NODE_NEQ, $1, $3); }
    ;

relational: 
      additive 
    | relational LT additive { $$ = newBinOp(NODE_LT, $1, $3); }
    | relational GT additive { $$ = newBinOp(NODE_GT, $1, $3); }
    ;

additive: 
      multiplicative 
    | additive PLUS multiplicative { $$ = newBinOp(NODE_ADD, $1, $3); }
    | additive MINUS multiplicative { $$ = newBinOp(NODE_SUB, $1, $3); }
    ;

multiplicative: 
      unary 
    | multiplicative MUL unary { $$ = newBinOp(NODE_MUL, $1, $3); }
    | multiplicative DIV unary { $$ = newBinOp(NODE_DIV, $1, $3); }
    ;

unary: 
      postfix 
    | MINUS unary { $$ = newBinOp(NODE_NEG, $2, NULL); }
    | NOT_OP unary { $$ = newBinOp(NODE_NOT, $2, NULL); }
    ;

postfix: 
      primary 
    | postfix '.' ID { $$ = node_new(NODE_INDEX); $$->left = $1; $$->name = $3; }
    | postfix '(' arg_list ')' { $$ = newCallExprNode($1, $3.nodes, $3.count); }
    | postfix '[' expr ']' { $$ = node_new(NODE_INDEX); $$->left = $1; $$->right = $3; }
    ;

primary: 
      ID { $$ = newVarNode($1); }
    | INT_LIT { $$ = newIntNode($1); }
    | FLOAT_LIT { $$ = newFloatNode($1); }
    | STRING { $$ = newStringNode($1); }
    | TRUE_KW { $$ = newBoolNode(1); }
    | FALSE_KW { $$ = newBoolNode(0); }
    | '(' expr ')' { $$ = $2; }
    | '[' expr_list ']' { $$ = node_new(NODE_LIST_LITERAL); $$->elements = $2.nodes; $$->element_count = $2.count; }
    | '[' ']' { $$ = node_new(NODE_LIST_LITERAL); $$->elements = NULL; $$->element_count = 0; }
    | '{' dict_pairs '}' { $$ = node_new(NODE_DICT_LITERAL); $$->dict_keys = $2.keys; $$->dict_values = $2.values; $$->dict_count = $2.count; }
    | LAMBDA param_list COLON expr { $$ = newFuncNode("lambda", $2.count, $2.names, $4); $$->type = NODE_LAMBDA; }
    ;

%%

static Class *vm_last_class = NULL;

static Value run_bytecode(Bytecode *bc, int base_offset) {
    int ip = 0;
    while (ip < bc->count) {
        Instruction inst = bc->code[ip];
        switch (inst.op) {
            case OP_CONST_INT: PUSH(value_int(inst.arg)); ip++; break;
            case OP_CONST_FLOAT: PUSH(*(Value*)inst.ptr); ip++; break;
            case OP_CONST_BOOL: PUSH(value_bool(inst.arg)); ip++; break;
            case OP_CONST_STRING: PUSH(value_string_copy((const char*)inst.ptr)); ip++; break;
            case OP_LOAD_VAR: PUSH(var_get((const char*)inst.ptr)); ip++; break;
            case OP_STORE_VAR: var_set((const char*)inst.ptr, vm_stack[sp-1]); ip++; break;
            case OP_ADD: {
                Value b = POP(), a = POP();
                if (GET_TYPE(a) == VAL_INT && GET_TYPE(b) == VAL_INT) PUSH(value_int(AS_INT(a) + AS_INT(b)));
                else if (GET_TYPE(a) == VAL_STRING && GET_TYPE(b) == VAL_STRING) {
                    const char *sa = AS_STRING(a); const char *sb = AS_STRING(b);
                    char *res = malloc(strlen(sa) + strlen(sb) + 1); strcpy(res, sa); strcat(res, sb);
                    PUSH(value_string_copy(res)); free(res);
                } else PUSH(value_int(0));
                ip++; break;
            }
            case OP_SUB: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) - AS_INT(b))); ip++; break; }
            case OP_MUL: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(a) * AS_INT(b))); ip++; break; }
            case OP_DIV: { Value b = POP(), a = POP(); PUSH(value_int(AS_INT(b)!=0 ? AS_INT(a) / AS_INT(b) : 0)); ip++; break; }
            case OP_LT: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) < AS_INT(b))); ip++; break; }
            case OP_GT: { Value b = POP(), a = POP(); PUSH(value_bool(AS_INT(a) > AS_INT(b))); ip++; break; }
            case OP_EQ: { Value b = POP(), a = POP(); PUSH(value_bool(a == b || (GET_TYPE(a)==VAL_STRING && GET_TYPE(b)==VAL_STRING && strcmp(AS_STRING(a),AS_STRING(b))==0))); ip++; break; }
            case OP_NOT: { Value a = POP(); PUSH(value_bool(!AS_BOOL(a))); ip++; break; }
            case OP_JUMP: ip = inst.arg; break;
            case OP_JUMP_IF_FALSE: { if (!AS_BOOL(POP())) ip = inst.arg; else ip++; break; }
            case OP_BUILTIN_SHOW: { int n = inst.arg; for (int i=0; i<n; i++) { print_value(vm_stack[sp-n+i]); if(i<n-1) printf(" "); } printf("\n"); sp-=n; ip++; break; }
            case OP_CLASS_DEF: { const char *n = (const char*)inst.ptr; Value c = value_class(class_new(n, NULL)); var_set(n, c); vm_last_class = AS_CLASS(c); ip++; break; }
            case OP_METHOD_DEF: { const char *n = (const char*)inst.ptr; FuncEntry *fe = func_get(n); if(vm_last_class) dict_set(vm_last_class->methods, n, bound_method_new(value_none(), fe)); ip++; break; }
            case OP_GET_PROPERTY: {
                const char *n = (const char*)inst.ptr; Value o = POP();
                if(GET_TYPE(o)==VAL_OBJECT && GET_OBJ_TYPE(o)==OBJ_INSTANCE) {
                    Value v = instance_get(AS_INSTANCE(o), n);
                    if(GET_TYPE(v)==VAL_OBJECT && GET_OBJ_TYPE(v)==OBJ_BOUND_METHOD && AS_BOUND_METHOD(v)->instance == value_none())
                        v = bound_method_new(o, AS_BOUND_METHOD(v)->method);
                    PUSH(v);
                } else if(GET_TYPE(o)==VAL_DICT) {
                    Dict *d = AS_DICT(o); Value res = value_none();
                    for(int i=0; i<d->size; i++) if(strcmp(d->keys[i], n)==0) { res = d->values[i]; break; }
                    PUSH(res);
                } else PUSH(value_none());
                ip++; break;
            }
            case OP_SET_PROPERTY: { const char *n = (const char*)inst.ptr; Value o = POP(); Value v = vm_stack[sp-1]; if(GET_TYPE(o)==VAL_OBJECT && GET_OBJ_TYPE(o)==OBJ_INSTANCE) instance_set(AS_INSTANCE(o), n, v); ip++; break; }
            case OP_LIST_LITERAL: { int n = inst.arg; List *l = list_new(n); for (int i=0; i<n; i++) list_push(l, vm_stack[sp-n+i]); sp-=n; PUSH(value_list(l)); ip++; break; }
            case OP_INDEX: { Value i = POP(); Value o = POP(); if(GET_TYPE(o)==VAL_LIST && GET_TYPE(i)==VAL_INT) { List *l = AS_LIST(o); int idx = AS_INT(i); if(idx>=0 && idx<l->size) PUSH(l->items[idx]); else PUSH(value_none()); } else PUSH(value_none()); ip++; break; }
            case OP_DICT_LITERAL: { int n = inst.arg; Dict *d = dict_new(n); for(int i=0; i<n; i++) { Value v = POP(); Value k = POP(); dict_set(d, AS_STRING(k), v); } PUSH(value_dict(d)); ip++; break; }
            case OP_CALL_VALUE:
            case OP_CALL: {
                int ac = inst.arg; Value callee = (inst.op == OP_CALL) ? var_get((const char*)inst.ptr) : POP();
                if(GET_TYPE(callee)==VAL_OBJECT) {
                    ObjType ot = GET_OBJ_TYPE(callee);
                    if(ot==OBJ_CLASS) { PUSH(value_instance(instance_new(AS_CLASS(callee)))); }
                    else if(ot==OBJ_BOUND_METHOD) {
                        BoundMethod *bm = AS_BOUND_METHOD(callee); Value s = var_get("self"); var_set("self", bm->instance);
                        if (bm->method) {
                            Value *saved = bm->method->param_count > 0 ? malloc(sizeof(Value)*bm->method->param_count) : NULL;
                            for(int i=0; i<bm->method->param_count; i++) { saved[i] = var_get(bm->method->params[i]); var_set(bm->method->params[i], vm_stack[sp-ac+i]); }
                            sp -= ac; PUSH(run_bytecode(bm->method->body, 0));
                            for(int i=0; i<bm->method->param_count; i++) var_set(bm->method->params[i], saved[i]);
                            if(saved) free(saved);
                        } else { sp -= ac; PUSH(value_none()); }
                        var_set("self", s);
                    } else if(ot==OBJ_NATIVE_FUNC) { NativeFunc *nf = AS_NATIVE_FUNC(callee); Value res = nf->func(ac, &vm_stack[sp-ac]); sp -= ac; PUSH(res); }
                } else {
                    FuncEntry *fe = func_get((const char*)inst.ptr);
                    if(fe) {
                        Value *saved = fe->param_count > 0 ? malloc(sizeof(Value)*fe->param_count) : NULL;
                        for(int i=0; i<fe->param_count; i++) { saved[i] = var_get(fe->params[i]); var_set(fe->params[i], vm_stack[sp-ac+i]); }
                        sp -= ac; PUSH(run_bytecode(fe->body, 0));
                        for(int i=0; i<fe->param_count; i++) var_set(fe->params[i], saved[i]);
                        if(saved) free(saved);
                    } else { sp -= ac; PUSH(value_none()); }
                }
                ip++; break;
            }
            case OP_RETURN: return sp > 0 ? POP() : value_none();
            case OP_POP: POP(); ip++; break;
            case OP_FUNC_DEF: ip++; break;
            default: ip++; break;
        }
    }
    return value_none();
}

void print_value(Value v) {
    ValueType t = GET_TYPE(v);
    if (t == VAL_INT) printf("%ld", (long)AS_INT(v));
    else if (t == VAL_FLOAT) printf("%g", AS_FLOAT(v));
    else if (t == VAL_BOOL) printf(AS_BOOL(v) ? "true" : "false");
    else if (t == VAL_STRING) printf("%s", AS_STRING(v));
    else if (t == VAL_LIST) { List *l = AS_LIST(v); printf("["); for (int i=0; i<l->size; i++) { print_value(l->items[i]); if (i < l->size - 1) printf(", "); } printf("]"); }
    else if (t == VAL_DICT) { Dict *d = AS_DICT(v); printf("{"); for (int i=0; i<d->size; i++) { printf("%s: ", d->keys[i]); print_value(d->values[i]); if (i < d->size - 1) printf(", "); } printf("}"); }
    else if (t == VAL_OBJECT) {
        if (!(v & MASK_PAYLOAD)) { printf("none"); return; }
        ObjType ot = GET_OBJ_TYPE(v);
        if (ot == OBJ_CLASS) printf("<class %s>", AS_CLASS(v)->name);
        else if (ot == OBJ_INSTANCE) printf("<instance of %s>", AS_INSTANCE(v)->klass->name);
        else if (ot == OBJ_NATIVE_FUNC) printf("<native func %s>", AS_NATIVE_FUNC(v)->name);
        else printf("<object>");
    } else printf("none");
}

/* StdLib */
static Value native_len(int argc, Value *args) { if (argc < 1) return value_int(0); Value v = args[0]; if (GET_TYPE(v) == VAL_STRING) return value_int(strlen(AS_STRING(v))); if (GET_TYPE(v) == VAL_LIST) return value_int(AS_LIST(v)->size); return value_int(0); }
static Value os_exit(int argc, Value *args) { exit(argc > 0 ? AS_INT(args[0]) : 0); return value_none(); }
static Value os_time(int argc, Value *args) { return value_float((double)time(NULL)); }
static Value os_system(int argc, Value *args) { if (argc < 1) return value_int(-1); return value_int(system(AS_STRING(args[0]))); }

static void std_init(void) {
    var_set("len", native_func_new("len", native_len));
    Dict *os = dict_new(4);
    dict_set(os, "exit", native_func_new("exit", os_exit));
    dict_set(os, "time", native_func_new("time", os_time));
    dict_set(os, "system", native_func_new("system", os_system));
    var_set("os", value_dict(os));
    var_set("json", value_dict(dict_new(4)));
    var_set("net", value_dict(dict_new(4)));
    var_set("fs", value_dict(dict_new(4)));
}

Node *root = NULL;
GlowError *glow_errors = NULL;
int glow_error_count = 0;
static int glow_error_capacity = 0;

void glow_clear_errors(void) {
    for (int i = 0; i < glow_error_count; i++) free(glow_errors[i].message);
    glow_error_count = 0;
}

void glow_add_error(int line, const char *msg) {
    if (glow_error_count >= glow_error_capacity) {
        glow_error_capacity = (glow_error_capacity == 0) ? 8 : glow_error_capacity * 2;
        glow_errors = realloc(glow_errors, sizeof(GlowError) * glow_error_capacity);
    }
    glow_errors[glow_error_count].line = line;
    glow_errors[glow_error_count].message = strdup(msg);
    glow_error_count++;
}

void vm_stack_init(void) { vm_stack_size = 16384; vm_stack = malloc(sizeof(Value) * vm_stack_size); sp = 0; }

int main(int argc, char **argv) {
    var_table_init(); func_table_init(); vm_stack_init(); intern_table_init();
    std_init();
    if (argc > 1) {
        yyin = fopen(argv[1], "r"); if(!yyin) { perror("fopen"); return 1; }
        if (yyparse() == 0 && root) {
            Bytecode *main_bc = bc_new(); compiling_bc = main_bc;
            compile(root);
            run_bytecode(main_bc, 0);
        }
    }
    return 0;
}
void yyerror(const char *s) { fprintf(stderr, "Error: %s at line %d\n", s, line_num); }
