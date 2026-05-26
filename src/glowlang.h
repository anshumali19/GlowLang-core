#ifndef GLOWLANG_H
#define GLOWLANG_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

/* ================================================================
 *  VALUE TYPES
 * ================================================================ */
typedef enum {
    VAL_INT=0, VAL_FLOAT=1, VAL_BOOL=2, VAL_STRING=3, VAL_LIST=4, VAL_DICT=5, VAL_CLOSURE=6, VAL_OBJECT=7
} ValueType;

#define VAL_NONE VAL_OBJECT

typedef uint64_t Value;

/* Forward declarations */
typedef struct List List;
typedef struct Dict Dict;
typedef struct Closure Closure;
typedef struct Class Class;
typedef struct Instance Instance;
typedef struct BoundMethod BoundMethod;
typedef struct Trait Trait;
typedef struct NativeFunc NativeFunc;
typedef struct Shape Shape;
typedef struct Node Node;
typedef struct Bytecode Bytecode;
typedef struct Instruction Instruction;

/* ================================================================
 *  GC & OBJECTS
 * ================================================================ */
typedef enum {
    OBJ_STRING, OBJ_LIST, OBJ_DICT, OBJ_CLOSURE, OBJ_CLASS, OBJ_INSTANCE, OBJ_BOUND_METHOD, OBJ_SHAPE, OBJ_TRAIT, OBJ_NATIVE_FUNC
} ObjType;

typedef struct GCObject {
    struct GCObject *next;
    unsigned char type;
    unsigned char marked;
    size_t sz;
} GCObject;

typedef struct {
    GCObject obj;
    char chars[];
} GCString;

#define MASK_NAN      ((uint64_t)0x7FF8000000000000)
#define MASK_TYPE     ((uint64_t)0x0007000000000000)
#define MASK_PAYLOAD  ((uint64_t)0x0000FFFFFFFFFFFF)

/* Extract bits to type */
#define GET_TYPE(v)   ( (((v) & MASK_NAN) != MASK_NAN) ? VAL_FLOAT : (((v) & MASK_TYPE) >> 48) )
#define GET_OBJ_TYPE(v) (((GCObject*)(uintptr_t)((v) & MASK_PAYLOAD))->type)

/* Accessors */
static inline double AS_FLOAT(Value v) { double d; memcpy(&d, &v, sizeof(v)); return d; }
static inline int64_t AS_INT(Value v) {
    uint64_t payload = v & MASK_PAYLOAD;
    if (payload & 0x0000800000000000ULL) return (int64_t)(payload | 0xFFFF000000000000ULL);
    return (int64_t)payload;
}
static inline int AS_BOOL(Value v) { return (int)(v & MASK_PAYLOAD); }
static inline const char* AS_STRING(Value v) { 
    GCString *gs = (GCString*)(uintptr_t)(v & MASK_PAYLOAD);
    return gs ? gs->chars : "";
}
static inline List* AS_LIST(Value v) { return (List*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline Dict* AS_DICT(Value v) { return (Dict*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline Closure* AS_CLOSURE(Value v) { return (Closure*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline Class* AS_CLASS(Value v) { return (Class*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline Instance* AS_INSTANCE(Value v) { return (Instance*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline BoundMethod* AS_BOUND_METHOD(Value v) { return (BoundMethod*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline Trait* AS_TRAIT(Value v) { return (Trait*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline NativeFunc* AS_NATIVE_FUNC(Value v) { return (NativeFunc*)(uintptr_t)(v & MASK_PAYLOAD); }

/* Value Constructors */
static inline Value make_value(uint64_t type, uint64_t payload) {
    Value v = MASK_NAN | (type << 48) | (payload & MASK_PAYLOAD);
    return v;
}

static inline Value value_int(int64_t v) { return make_value(VAL_INT, (uint64_t)v); }
static inline Value value_float(double v) { Value val; memcpy(&val, &v, sizeof(v)); return val; }
static inline Value value_bool(int v) { return make_value(VAL_BOOL, v ? 1 : 0); }
static inline Value value_list(List *l) { return make_value(VAL_LIST, (uint64_t)(uintptr_t)l); }
static inline Value value_dict(Dict *d) { return make_value(VAL_DICT, (uint64_t)(uintptr_t)d); }
static inline Value value_closure(Closure *c) { return make_value(VAL_CLOSURE, (uint64_t)(uintptr_t)c); }
static inline Value value_class(Class *c) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)c); }
static inline Value value_trait(Trait *t) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)t); }
static inline Value value_instance(Instance *i) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)i); }
static inline Value value_obj(void *ptr) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)ptr); }
static inline Value value_bound_method(BoundMethod *bm) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)bm); }
static inline Value value_native_func(NativeFunc *nf) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)nf); }
static inline Value value_none(void) { return make_value(VAL_NONE, 0); }

/* ================================================================
 *  BYTECODE
 * ================================================================ */
typedef enum {
    OP_CONST_INT, OP_CONST_FLOAT, OP_CONST_STRING, OP_CONST_BOOL,
    OP_LOAD_VAR, OP_STORE_VAR,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW, OP_NEG,
    OP_LT, OP_GT, OP_LTE, OP_GTE, OP_EQ, OP_NEQ,
    OP_AND, OP_OR, OP_NOT,
    OP_STRING_CONCAT,
    OP_JUMP, OP_JUMP_IF_FALSE, OP_JUMP_IF_TRUE,
    OP_LEN, OP_RANGE, OP_TYPE, OP_APPEND, OP_INPUT, OP_SHOW, OP_WRITE,
    OP_LIST_LITERAL, OP_DICT_LITERAL,
    OP_INDEX, OP_SLICE, OP_STORE_INDEX,
    OP_FUNC_DEF, OP_CALL, OP_RETURN,
    OP_FOR_EACH, OP_FOR_EACH_PAIR,
    OP_STOP, OP_SKIP,
    OP_POP,
    OP_PUSH_SCOPE, OP_POP_SCOPE, OP_STORE_LET,
    OP_LOAD_LOCAL, OP_STORE_LOCAL,
    OP_DICT_KEYS, OP_DICT_VALUES, OP_DICT_HAS, OP_DICT_REMOVE, OP_DICT_MERGE,
    OP_MATH_ABS, OP_MATH_MIN, OP_MATH_MAX, OP_MATH_ROUND, OP_MATH_FLOOR, OP_MATH_CEIL, OP_MATH_SQRT,
    OP_STR_UPPER, OP_STR_LOWER, OP_STR_TRIM, OP_STR_FIND, OP_STR_REPLACE, OP_STR_STARTSWITH, OP_STR_ENDSWITH, OP_STR_SPLIT,
    OP_LIST_PUSH, OP_LIST_POP, OP_LIST_INSERT, OP_LIST_REMOVE, OP_LIST_CONTAINS, OP_LIST_SORT, OP_LIST_REVERSE,
    OP_FILE_READ, OP_FILE_WRITE, OP_FILE_APPEND,
    OP_FSTRING,
    OP_LIST_COMP,
    OP_THROW,
    OP_TRY_START, OP_TRY_END,
    OP_LAMBDA,
    OP_CALL_VALUE,
    OP_CLASS_DEF,
    OP_METHOD_DEF,
    OP_TRAIT_DEF,
    OP_USE_TRAIT,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_SPAWN
} OpCode;

struct Instruction {
    OpCode op;
    int arg;
    intptr_t ptr;
    void *cache_key;
    intptr_t cache_val;
    long exec_count;
};

struct Bytecode {
    Instruction *code;
    int capacity;
    int count;
    void *jit_code;
    size_t jit_size;
    int jit_failed;
    int local_count;
};

/* ================================================================
 *  NODE TYPES
 * ================================================================ */
typedef enum {
    NODE_INT_LIT, NODE_FLOAT_LIT, NODE_BOOL_LIT, NODE_STRING_LIT,
    NODE_VAR,
    NODE_ASSIGN,
    NODE_COMPOUND_ADD, NODE_COMPOUND_SUB, NODE_COMPOUND_MUL,
    NODE_COMPOUND_DIV, NODE_COMPOUND_MOD,
    NODE_ADD, NODE_SUB, NODE_MUL, NODE_DIV, NODE_MOD, NODE_POW, NODE_NEG,
    NODE_LT, NODE_GT, NODE_LTE, NODE_GTE, NODE_EQ, NODE_NEQ,
    NODE_AND, NODE_OR, NODE_NOT,
    NODE_IF, NODE_WHILE, NODE_UNTIL, NODE_REPEAT,
    NODE_FOR_EACH,
    NODE_STOP, NODE_SKIP,
    NODE_LET,
    NODE_FUNC_DEF, NODE_CALL, NODE_RETURN,
    NODE_LIST_LITERAL, NODE_DICT_LITERAL,
    NODE_INDEX, NODE_SLICE,
    NODE_SHOW, NODE_WRITE,
    NODE_LEN, NODE_RANGE, NODE_TYPE, NODE_APPEND, NODE_INPUT,
    NODE_SEQ,
    NODE_EXPR_STMT,
    NODE_FILE_READ, NODE_FILE_WRITE, NODE_FILE_APPEND,
    NODE_FSTRING,
    NODE_TRY_CATCH, NODE_THROW,
    NODE_LIST_COMP,
    NODE_LAMBDA,
    NODE_CALL_EXPR,
    NODE_INDEX_ASSIGN,
    NODE_IMPORT,
    NODE_IN,
    NODE_CLASS,
    NODE_TRAIT,
    NODE_USE_TRAIT,
    NODE_SPAWN
} NodeType;

struct Node {
    NodeType type;
    Value value;
    const char *name;
    const char *second_name;
    Node *left, *right, *third;
    Node **elements; int element_count;
    Node **print_items; int print_count;
    Node **dict_keys, **dict_values; int dict_count;
    Node **call_args; int call_arg_count;
    char **func_params; int func_param_count;
    int uses_let;
    Node **fstring_parts; int fstring_part_count;
    Node *comp_expr;
    const char *comp_var;
    Node *comp_filter;
    struct Closure *closure;
    int line;
    const char *parent_name;
    Node **methods; int method_count;
    Node **fields; int field_count;
};

typedef Value (*NativeFuncPtr)(int argc, Value *args);

struct NativeFunc {
    GCObject obj;
    const char *name;
    NativeFuncPtr func;
};

typedef struct FuncEntry FuncEntry;

struct FuncEntry {
    const char *name;
    int param_count;
    char **params;
    Bytecode *body;
    int used;
    int uses_let;
    char **local_names;
    int local_count;
    int local_capacity;
};

struct BoundMethod {
    GCObject obj;
    Value instance;
    FuncEntry *method;
};

struct Trait {
    GCObject obj;
    const char *name;
    Dict *methods;
};

struct Shape {
    GCObject obj;
    struct Shape *parent;
    const char *name;
    int offset;
    Dict *transitions;
};

struct Class {
    GCObject obj;
    const char *name;
    struct Class *parent;
    Dict *methods;
    Shape *root_shape;
};

struct Instance {
    GCObject obj;
    struct Class *klass;
    Shape *shape;
    Value *fields;
    int field_capacity;
};

struct List {
    GCObject obj;
    int refcount;
    int size;
    int capacity;
    Value *items;
};

struct Dict {
    GCObject obj;
    int refcount;
    int size;
    int capacity;
    const char **keys;
    Value *values;
};

struct Closure {
    GCObject obj;
    int refcount;
    int param_count;
    char **params;
    Bytecode *body;
    int captured_count;
    const char **captured_names;
    Value *captured_values;
};

extern int line_num;
extern Node *root;
extern FILE *yyin;
int yyparse(void);
void yyerror(const char *s);
Node *node_new(NodeType type);
void compile(Node *n);

typedef struct {
    int line;
    char *message;
} GlowError;

extern GlowError *glow_errors;
extern int glow_error_count;
void glow_clear_errors(void);
void glow_add_error(int line, const char *msg);

#endif
