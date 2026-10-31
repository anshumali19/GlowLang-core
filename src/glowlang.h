#ifdef _MSC_VER
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL __thread
#endif
#ifndef GLOWLANG_H
#define GLOWLANG_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <setjmp.h>
#ifdef _WIN32
#include "win_pthread.h"
#else
#include <pthread.h>
#endif
#include "arena.h"

#if defined(_MSC_VER) && !defined(__clang__)
#ifndef __builtin_expect
#define __builtin_expect(x, expected_value) (x)
#endif
#endif

/* True when the compiler supports GNU labels-as-values (computed goto). MSVC
   does not; clang-cl and GCC do, so the VM uses threaded dispatch with them. */
#if defined(__GNUC__) || defined(__clang__)
#define GLOW_COMPUTED_GOTO 1
#endif

/* ================================================================
 *  VALUE TYPES
 * ================================================================ */
typedef enum {
    VAL_INT=0, VAL_FLOAT=1, VAL_BOOL=2, VAL_STRING=3, VAL_LIST=4, VAL_DICT=5, VAL_OBJECT=6, VAL_NONE=7, VAL_DEOPT=15, VAL_TRACE_EXIT=16
} ValueType;


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
typedef struct FuncEntry FuncEntry;
/* daemon: a background thread (a `whenever` monitor) that program exit and
   threads.join_all() do not wait for. */
typedef struct { Value callee; int argc; Value *args; const char *name; int daemon; } ThreadData;

/* ================================================================
 *  GC & OBJECTS
 * ================================================================ */
typedef enum {
    OBJ_STRING, OBJ_ROPE, OBJ_LIST, OBJ_DICT, OBJ_CLOSURE, OBJ_CLASS, OBJ_INSTANCE, OBJ_BOUND_METHOD, OBJ_SHAPE, OBJ_TRAIT, OBJ_NATIVE_FUNC, OBJ_PACKED_ARRAY, OBJ_ITERATOR, OBJ_STRING_BUILDER, OBJ_BIGINT, OBJ_SQLITE_ITER, OBJ_SQLITE_HANDLE
} ObjType;

typedef struct GCObject {
    struct GCObject *next;
    unsigned char type;
    unsigned char marked;
    unsigned char is_block_allocated;
    unsigned char generation;
    unsigned char in_remembered_set;
    unsigned char is_stack_allocated;
    void *block_ptr;
    size_t sz;
} GCObject;

typedef struct {
    GCObject obj;
    void *db;
} SQLiteHandle;

typedef enum {
    PACKED_INT64,
    PACKED_FLOAT64
} PackedType;

typedef struct PackedArray {
    GCObject obj;
    void *data;
    PackedType type;
    int size;          // total number of elements
    int capacity;      // allocated capacity
    int ndims;         // number of dimensions
    int shape[8];      // shape of each dimension (up to 8D)
    int strides[8];    // strides for each dimension
    int offset;        // offset from the start of data pointer
    bool is_view;      // if true, do not free data directly without base check
    struct PackedArray *base; // parent tensor if this is a view
    char device[16];   // "cpu", "cuda", etc.
} PackedArray;

typedef struct {
    GCObject obj;
    int size;
    char chars[];
} GCString;

/* Arbitrary-precision signed integer (numeric tower stage 2). Stored as a heap
   GC object carried in a VAL_OBJECT Value, so it reuses the existing object
   machinery (the 8-slot ValueType tag space is already full). Little-endian
   base-2^32 limbs. */
typedef struct BigInt {
    GCObject obj;
    int neg;      /* 1 if negative, 0 if >= 0 */
    int nlimbs;   /* number of used limbs (>= 1) */
    uint32_t limbs[]; /* flexible array member */
} BigInt;

typedef struct RopeNode {
    GCObject obj;
    size_t length;
    bool is_leaf;
    union {
        struct {
            struct RopeNode *left;
            struct RopeNode *right;
        } internal;
        struct {
            Value flat_string;
            int offset;
        } leaf;
    };
} RopeNode;

typedef struct {
    GCObject obj;
    Value iterable;
    int index;
} Iterator;

typedef struct {
    GCObject obj;
    void *stmt;
    int done;
} SQLiteIter;

typedef struct {
    GCObject obj;
    char *buffer;
    int size;
    int capacity;
} StringBuilder;

#define MASK_NAN      ((uint64_t)0x7FF8000000000000)
/* Tags never set the sign bit, so a pattern with it set is always a double.
   NaN doubles are stored as CANON_NAN (x86's default NaN): a NaN with the
   sign clear would look like a tagged value (it used to read as the int 0). */
#define MASK_SIGN     ((uint64_t)0x8000000000000000)
#define CANON_NAN     ((uint64_t)0xFFF8000000000000)
#define IS_FLOAT_BITS(v) ((((uint64_t)(v)) & (MASK_NAN | MASK_SIGN)) != MASK_NAN)
#define MASK_TYPE     ((uint64_t)0x0007000000000000)
#define MASK_PAYLOAD  ((uint64_t)0x0000FFFFFFFFFFFF)

/* Inline integer range: GlowLang packs integers into the 48-bit VALUE payload.
   Results that exceed this signed range are promoted to a double (stage 1 of the
   numeric tower). Doubles are exact integers up to 2^53. */
#define INT48_MIN  ((int64_t)0xFFFF800000000000LL)  /* -2^47 */
#define INT48_MAX  ((int64_t)0x00007FFFFFFFFFFFLL)  /*  2^47 - 1 */
static inline int fits_int48(int64_t v) { return v >= INT48_MIN && v <= INT48_MAX; }
/* 2^53: largest integer a double represents exactly. Beyond this, integer
   literals are kept exact as heap big-ints (stage 2 of the numeric tower). */
#define INT53_MAX ((int64_t)9007199254740992LL)

#define VAL_DEOPT_SENTINEL (MASK_NAN | ((uint64_t)VAL_NONE << 48) | 0xDE0FULL)
#define IS_DEOPT(v)      ((v) == VAL_DEOPT_SENTINEL)
#define IS_TRACE_EXIT(v) ((v) == (MASK_NAN | ((uint64_t)VAL_TRACE_EXIT << 48)))

/* Extract bits to type */
#define IS_INT(v) (((v) & 0xFFFF000000000000ULL) == 0x7FF8000000000000ULL)
static inline ValueType GET_TYPE(Value v) {
    return IS_FLOAT_BITS(v) ? VAL_FLOAT : (ValueType)(((v) & MASK_TYPE) >> 48);
}
#define GET_OBJ_TYPE(v) (((GCObject*)(uintptr_t)((v) & MASK_PAYLOAD))->type)

static inline size_t rope_length(Value v) {
    if (GET_TYPE(v) == VAL_STRING) {
        GCObject *obj = (GCObject*)(uintptr_t)(v & MASK_PAYLOAD);
        if (obj->type == OBJ_STRING) return (size_t)((GCString*)obj)->size;
        if (obj->type == OBJ_ROPE) return ((RopeNode*)obj)->length;
    }
    return 0;
}

/* Accessors */
static inline double AS_FLOAT(Value v) { double d; memcpy(&d, &v, sizeof(v)); return d; }
static inline int64_t AS_INT(Value v) {
    uint64_t payload = v & MASK_PAYLOAD;
    if (payload & 0x0000800000000000ULL) return (int64_t)(payload | 0xFFFF000000000000ULL);
    return (int64_t)payload;
}
static inline int AS_BOOL(Value v) { return (int)(v & MASK_PAYLOAD); }
extern const char* rope_flatten(Value v);
static inline const char* AS_STRING(Value v) {
    if (GET_TYPE(v) != VAL_STRING) return "";
    return rope_flatten(v);
}
static inline List* AS_LIST(Value v) { return (List*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline Dict* AS_DICT(Value v) { return (Dict*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline Closure* AS_CLOSURE(Value v) { return (Closure*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline Class* AS_CLASS(Value v) { return (Class*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline Instance* AS_INSTANCE(Value v) { return (Instance*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline BoundMethod* AS_BOUND_METHOD(Value v) { return (BoundMethod*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline Trait* AS_TRAIT(Value v) { return (Trait*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline NativeFunc* AS_NATIVE_FUNC(Value v) { return (NativeFunc*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline PackedArray* AS_PACKED_ARRAY(Value v) { return (PackedArray*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline BigInt* AS_BIGINT(Value v) { return (BigInt*)(uintptr_t)(v & MASK_PAYLOAD); }
static inline int is_bigint(Value v) { return GET_TYPE(v) == VAL_OBJECT && ((GCObject*)(uintptr_t)(v & MASK_PAYLOAD))->type == OBJ_BIGINT; }

/* Value Constructors */
static inline Value make_value(uint64_t type, uint64_t payload) {
    Value v = MASK_NAN | (type << 48) | (payload & MASK_PAYLOAD);
    return v;
}

static inline Value value_int(int64_t v) { return make_value(VAL_INT, (uint64_t)v); }
static inline Value value_float(double v) { Value val; if (v != v) return CANON_NAN; memcpy(&val, &v, sizeof(v)); return val; }
static inline Value value_bool(int v) { return make_value(VAL_BOOL, v ? 1 : 0); }
static inline Value value_list(List *l) { return make_value(VAL_LIST, (uint64_t)(uintptr_t)l); }
static inline Value value_packed(PackedArray *pa) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)pa); }
static inline Value value_dict(Dict *d) { return make_value(VAL_DICT, (uint64_t)(uintptr_t)d); }
static inline Value value_closure(Closure *c) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)c); }
static inline Value value_class(Class *c) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)c); }
static inline Value value_trait(Trait *t) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)t); }
static inline Value value_instance(Instance *i) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)i); }
static inline Value value_obj(void *ptr) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)ptr); }
static inline Value value_bound_method(BoundMethod *bm) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)bm); }
static inline Value value_native_func(NativeFunc *nf) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)nf); }
static inline Value value_none(void) { return make_value(VAL_NONE, 0); }
static inline Value value_bigint(BigInt *b) { return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)b); }

/* Big integer operations (numeric tower stage 2). */
BigInt* bigint_from_int64(int64_t v);
BigInt* bigint_from_string(const char *s);
char* bigint_to_new_string(BigInt *b);
int bigint_cmp(BigInt *a, BigInt *b);          /* -1, 0, 1 */
BigInt* bigint_add(BigInt *a, BigInt *b);
BigInt* bigint_sub(BigInt *a, BigInt *b);
BigInt* bigint_negate(BigInt *a);
BigInt* bigint_mul(BigInt *a, BigInt *b);
double bigint_to_double(BigInt *b);
int bigint_fits_int64(BigInt *b, int64_t *out);

typedef uint64_t TypeSignature;

typedef struct GlobalTable {
    const char **names;
    Value *values;
    int count;
    int capacity;
    int *index;      // open-addressed hash: name-hash bucket -> slot, -1 = empty
    int index_cap;   // power-of-two size of index (0 = not yet built)
} GlobalTable;

extern GlobalTable global_table;
int global_get_slot(const char *name);

typedef struct CallFrame {
    struct FuncEntry *fe;
    struct Instruction *pc;
} CallFrame;
extern THREAD_LOCAL CallFrame call_stack[65536];
extern THREAD_LOCAL int call_stack_ptr;
void call_stack_push(struct FuncEntry *fe, struct Instruction *pc);
void call_stack_pop(void);

extern TypeSignature compute_signature(Value *regs, int count);
extern Value jit_call_helper(struct FuncEntry *fe, int argc, Value *args, struct Instruction *pc);
extern Value jit_call_helper_val(Value callee, int argc, Value *args, struct Instruction *pc);
extern Value jit_self_call_direct(struct FuncEntry *fe, Value *args);
extern Value vm_call_jit_direct(Value (*f)(Value *, int *), struct FuncEntry *fe, int callee_base, int *deopt_pc, int *frame_base);
extern Value jit_call_method_helper(Value obj, const char *n, int argc, Value *args, struct Instruction *pc);
extern Value jit_get_property_helper(Value o, const char *n, struct Instruction *pc);
extern Value jit_set_property_helper(Value o, const char *n, Value v, struct Instruction *pc);
extern Value packed_array_get_jit(Value v_pa, Value v_idx);
extern void packed_array_set_jit(Value v_pa, Value v_idx, Value v_val);
extern Value glow_index_jit(Value coll, Value v_idx);
extern Value glow_store_index_jit(Value coll, Value v_idx, Value v_val);
/* Wide-integer helpers used by the JIT so integer arithmetic matches the
   interpreter (promote to double on overflow / cross-type compare). */
extern Value jit_int_add(Value a, Value b);
extern Value jit_int_sub(Value a, Value b);
extern Value jit_int_mul(Value a, Value b);
extern Value jit_int_lt(Value a, Value b);
extern Value jit_int_gt(Value a, Value b);
extern Value jit_int_lte(Value a, Value b);
extern Value jit_int_gte(Value a, Value b);
extern Value jit_int_eq(Value a, Value b);
extern void simd_add_helper(Value v_a, Value v_b, Value v_target);
extern Value value_from_double(double d);
extern void* tensor_malloc(size_t size);
extern void tensor_free(void *ptr, size_t size, const char *device);

/* ================================================================
 *  BYTECODE
 * ================================================================ */
typedef enum {
    OP_CONST_INT,
    OP_CONST_FLOAT,
    OP_CONST_STRING,
    OP_CONST_BOOL,
    OP_LOAD_VAR,
    OP_STORE_VAR,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_POW,
    OP_NEG,
    OP_LT,
    OP_GT,
    OP_LTE,
    OP_GTE,
    OP_EQ,
    OP_NEQ,
    OP_AND,
    OP_OR,
    OP_NOT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_LEN,
    OP_RANGE,
    OP_TYPE,
    OP_APPEND,
    OP_INPUT,
    OP_SHOW,
    OP_WRITE,
    OP_LIST_LITERAL,
    OP_DICT_LITERAL,
    OP_INDEX,
    OP_SLICE,
    OP_STORE_INDEX,
    OP_FUNC_DEF,
    OP_CALL,
    OP_CALL_SELF_FAST,
    OP_TAIL_CALL,
    OP_RETURN,
    OP_FOR_EACH,
    OP_FOR_EACH_PAIR,
    OP_STOP,
    OP_SKIP,
    OP_POP,
    OP_PUSH_SCOPE,
    OP_POP_SCOPE,
    OP_STORE_LET,
    OP_LOAD_LOCAL,
    OP_STORE_LOCAL,
    OP_DICT_KEYS,
    OP_DICT_VALUES,
    OP_DICT_HAS,
    OP_DICT_REMOVE,
    OP_DICT_MERGE,
    OP_MATH_ABS,
    OP_MATH_MIN,
    OP_MATH_MAX,
    OP_MATH_ROUND,
    OP_MATH_FLOOR,
    OP_MATH_CEIL,
    OP_MATH_SQRT,
    OP_STR_UPPER,
    OP_STR_LOWER,
    OP_STR_TRIM,
    OP_STR_FIND,
    OP_STR_REPLACE,
    OP_STR_STARTSWITH,
    OP_STR_ENDSWITH,
    OP_STR_SPLIT,
    OP_LIST_PUSH,
    OP_LIST_POP,
    OP_LIST_INSERT,
    OP_LIST_REMOVE,
    OP_LIST_CONTAINS,
    OP_LIST_SORT,
    OP_LIST_REVERSE,
    OP_FILE_READ,
    OP_FILE_WRITE,
    OP_FILE_APPEND,
    OP_FSTRING,
    OP_LIST_COMP,
    OP_THROW,
    OP_TRY_START,
    OP_TRY_END,
    OP_LAMBDA,
    OP_CALL_VALUE,
    OP_CLASS_DEF,
    OP_METHOD_DEF,
    OP_TRAIT_DEF,
    OP_USE_TRAIT,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_SPAWN,
    OP_MOVE,
    OP_NOP,
    OP_TRACE_ENTER,
    OP_PACKED_ADD,
    OP_PACKED_SUB,
    OP_PACKED_MUL,
    OP_PACKED_LOAD,
    OP_PACKED_STORE,
    OP_PACKED_FUSED_MUL_ADD,
    OP_MATRIX_MUL,
    OP_SIMD_ADD,
    OP_USE_FILE,
    OP_ITER_START,
    OP_ITER_NEXT,
    OP_IN,
    OP_ADD_INT,
    OP_SUB_INT,
    OP_MUL_INT,
    OP_LT_INT,
    OP_GT_INT,
    OP_LTE_INT,
    OP_GTE_INT,
    OP_EQ_INT,
    OP_CALL_METHOD
} OpCode;

typedef struct {
    void *key;
    intptr_t val;
} CacheEntry;
struct Instruction {
    OpCode op;
    OpCode original_op;
    int arg;
    intptr_t ptr;
    CacheEntry cache[4];
    unsigned char cache_next;
    long exec_count;
    unsigned short r0, r1, r2;
    unsigned char type; // 0=Unknown, 1=Int, 2=Float, 3=String, 4=Bool
    unsigned char is_stack_alloc;
    unsigned char is_method;
    int line;
    const char *file;
    void *trace;
};

typedef struct Trace {
    Instruction *instructions;
    int *original_offsets;
    int count;
    int capacity;
    void *jit_code;
    size_t jit_size;
    Instruction *start_pc;
} Trace;

typedef struct {
    int start_pc;
    int end_pc;
    int handler_pc;
    int exc_reg;      /* register that receives the caught exception */
} ExceptionEntry;

typedef enum {
    JIT_TIER_INTERP = 0,
    JIT_TIER_BASELINE = 1,
    JIT_TIER_OPTIMIZING = 2
} JITTier;

struct Bytecode {
    Instruction *code;
    int capacity;
    int count;
    void *jit_code;
    size_t jit_size;
    uint32_t *jit_offsets;
    int jit_failed;
    int local_count;
    struct FuncEntry *owner;
    ExceptionEntry *exception_table;
    int exception_count;
    int preserved_locals;
    JITTier jit_tier;
    uint32_t exec_count;
    uint32_t tier_up_threshold;
    void *baseline_jit_code;
    size_t baseline_jit_size;
    uint32_t *baseline_jit_offsets;
    /* On-stack replacement: native entry points at loop headers (indexed by
       instruction), so a hot loop can switch into compiled code mid-call.
       osr_code may contain side exits back to the interpreter for opcodes the
       JIT does not handle; it is only ever entered through osr_entries. */
    void **osr_entries;
    void *osr_code;
    size_t osr_code_size;
    int osr_failed;
    uint32_t loop_iterations;
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
    NODE_FOR_RANGE,
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
    NODE_SPAWN,
    NODE_PACKED_ADD,
    NODE_PACKED_SUB,
    NODE_WHENEVER,
    NODE_COND          /* value when condition otherwise other */
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
    /* Per-function state (an FFI function's C signature), read through
       glow_current_native; finalize frees it when the GC frees the object. */
    void *data;
    void (*finalize)(void *data);
};

/* The native function being called on this thread: every call goes through
   glow_call_native, so a native can find its own data. */
extern THREAD_LOCAL NativeFunc *glow_current_native;
static inline Value glow_call_native(NativeFunc *nf, int argc, Value *args) {
    NativeFunc *saved = glow_current_native;
    glow_current_native = nf;
    Value r = nf->func(argc, args);
    glow_current_native = saved;
    return r;
}

typedef struct FuncEntry FuncEntry;

typedef uint64_t TypeSignature;

typedef struct {
    TypeSignature signature;
    void *jit_code;
    size_t jit_size;
} Specialization;

struct FuncEntry {
    const char *name;
    int param_count;
    char **params;
    Bytecode *body;
    int used;
    int uses_let;
    char **local_names;
    int *local_regs;  /* register assigned to each local (not its index) */
    int reg_floor;    /* first register above every local; temps start here */
    int local_count;
    int local_capacity;
    Specialization *specs;
    int spec_count;
    int spec_capacity;
    JITTier jit_tier;
    uint32_t exec_count;
    uint32_t tier_up_threshold;
    void *baseline_jit_code;
    size_t baseline_jit_size;
    uint32_t *baseline_jit_offsets;
};

struct BoundMethod {
    GCObject obj;
    Value instance;
    FuncEntry *method;
    NativeFuncPtr native_method;
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
    Shape *stable_shape;
    int instances_created;
};

struct Instance {
    GCObject obj;
    struct Class *klass;
    Dict *properties;
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
    Value *keys;
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

/* ================================================================
 *  AST HELPER FUNCTION CONSTRUCTORS
 * ================================================================ */
Node *node_new(NodeType type);
Node* newIntNode(int64_t v);
Node* newIntNodeStr(const char *s);
Node* newFloatNode(double v);
Node* newBoolNode(int v);
Node* newStringNode(const char *s);
Node* newVarNode(const char *s);
Node* newAssignNode(const char *s, Node *e);
Node* newBinOp(NodeType t, Node *l, Node *r);
Node* newSeqNode(Node *l, Node *r);
Node* newPairLoopNode(const char *a, const char *b, Node *coll, Node *body);
Node* newFuncNode(const char *n, int pc, char **ps, Node *b);
Node* newReturnNode(Node *e);
Node* newIfNode(Node *c, Node *t, Node *e);
Node* newWhileNode(Node *c, Node *b);
Node* newUntilNode(Node *c, Node *b);
Node* newRepeatNode(Node *c, Node *b);
Node* newCompoundAssignNode(NodeType t, const char *s, Node *e);
Node* newShowNode(Node **is, int c);
Node* newWriteNode(Node **is, int c);
Node* newExprStmtNode(Node *e);
Node* newCallExprNode(Node *e, Node **as, int c);

/* ================================================================
 *  GC & ALLOCATOR FUNCTIONS (gc.c)
 * ================================================================ */
void* gc_alloc(size_t sz, ObjType type);
void* gc_alloc_stack(size_t sz, ObjType type);
void gc_free_stack_arena_range(size_t old_used, size_t new_used);
void gc_write_barrier(GCObject *src, Value target);
void gc_full_collect(void);
void gc_minor_collect(void);
void gc_register_constant_root(Value v);
extern GCObject *gc_objects;
extern size_t gc_allocated;
extern size_t gc_threshold;
extern pthread_mutex_t gc_mutex;
extern pthread_mutex_t threads_mutex;
extern THREAD_LOCAL char *stack_arena;
extern THREAD_LOCAL size_t stack_arena_used;
Value native_verify_gc(int argc, Value *args);

/* ================================================================
 *  VM INTERPRETER & THREADING (vm.c)
 * ================================================================ */
Value run_bytecode(Bytecode *bc, Instruction *start_pc, Closure *current_closure, int base_reg);
void print_bytecode(Bytecode *bc);
void print_error_trace(Value exception);
void print_error_line(const char *file, int line);
void print_stack_trace(void);
char* format_stack_trace(void);
void vm_stack_init(void);
void vm_stack_cleanup(void);
void call_stack_push(FuncEntry *fe, Instruction *pc);
void call_stack_pop(void);
int val_eq(Value a, Value b);
int value_truthy(Value v);
extern THREAD_LOCAL Value *vm_stack;
extern THREAD_LOCAL int vm_stack_size;
extern THREAD_LOCAL int sp;
extern THREAD_LOCAL int vm_stack_top;
extern THREAD_LOCAL Value pending_exception;
extern THREAD_LOCAL int has_exception;
extern THREAD_LOCAL int runtime_error_thrown;
extern THREAD_LOCAL char *exception_stack_trace;
extern THREAD_LOCAL const char *error_file;
extern THREAD_LOCAL int error_line;
extern THREAD_LOCAL int profiling_active;
extern THREAD_LOCAL int trace_mode;
extern THREAD_LOCAL int debug_mode;
extern THREAD_LOCAL Bytecode *g_dbg_bc;
extern THREAD_LOCAL Instruction *g_dbg_pc;
extern int next_free_reg;
/* Frame size needed by the top-level code compiled so far. */
int compiler_reg_peak(void);
void compiler_set_reg_peak(int peak);
extern int global_lambda_count;
/* Per-thread machine-stack bounds and a register snapshot, used by the GC to
   scan native C stacks conservatively. stack_lo/regs are refreshed every time
   the thread releases the GIL, so a parked thread's roots are always visible. */
typedef struct GlowThreadCtx {
    void *stack_hi;
    void *volatile stack_lo;
    jmp_buf regs;
    /* Lowest address recursion may reach before it must raise a recursion
       error instead of overflowing the machine stack (limit plus a margin). */
    void *stack_limit;
} GlowThreadCtx;
typedef struct { Value **vm_stack_ptr; int *sp_ptr; int *top_ptr; Value *pending_exc_ptr; GlowThreadCtx *ctx; } ThreadStack;
extern ThreadStack active_stacks[128];
extern int active_stack_count;
extern volatile int threads_ever_spawned;
extern THREAD_LOCAL GlowThreadCtx glow_thread_ctx;

/* True when the current thread is close to the end of its machine stack.
   Every path that nests native frames per GlowLang call checks this. */
/* The current machine-stack position. The address of a local is not good
   enough: AddressSanitizer may place locals on a heap "fake stack". */
#if defined(__GNUC__) || defined(__clang__)
#define GLOW_STACK_POS() ((char*)__builtin_frame_address(0))
#else
#include <intrin.h>
#define GLOW_STACK_POS() ((char*)_AddressOfReturnAddress())
#endif

static inline int glow_stack_low(void) {
    return GLOW_STACK_POS() < (char*)glow_thread_ctx.stack_limit;
}

/* Global interpreter lock. Exactly one thread runs GlowLang code at a time;
   blocking natives release it (glow_gil_release / glow_gil_acquire) and the
   interpreter hands it over at loop back-edges when another thread waits. */
void glow_gil_acquire(void);
void glow_gil_release(void);
void glow_gil_yield(void);
extern volatile int glow_gil_waiters;
#define GLOW_GIL_POLL() do { if (__builtin_expect(glow_gil_waiters != 0, 0)) glow_gil_yield(); } while (0)
void glow_threads_join_all(void);
int glow_threads_alive(void);
void glow_thread_ctx_init(void);
extern THREAD_LOCAL int call_stack_ptr;
extern THREAD_LOCAL int call_depth;
/* Cached debug environment flags (defined in vm.c, checked once at startup) */
extern int glow_jit_debug;
extern int glow_nojitexec;
extern int glow_nobaseexec;
extern int glow_jitdbg;
extern int glow_calldbg;
extern int glow_noarenafree;
/* ================================================================
 *  COMPILER ROUTINES (compiler.c)
 * ================================================================ */
Bytecode* bc_new(FuncEntry *owner);
void bc_free(Bytecode *bc);
int bc_emit(Bytecode *bc, OpCode op, int arg, intptr_t ptr);
void optimize_bytecode(Bytecode *bc);
void compile(Node *n);
void func_entry_cleanup(FuncEntry *fe);
extern Bytecode *compiling_bc;
extern FuncEntry *compiling_func;
extern Class *last_compiling_class;
extern Trait *last_compiling_trait;

/* ================================================================
 *  OBJECT MODEL, NATIVE MODULES & RUNTIME HELPERS (stdlib.c)
 * ================================================================ */
Dict* dict_new(int sz);
void dict_set(Dict *d, const char *key, Value val);
Value dict_get(Dict *d, const char *key);
int dict_get_idx(Dict *d, const char *key);
List* list_new(int sz);
void list_push(List *l, Value val);
PackedArray* packed_array_new(PackedType type, int capacity);
Value packed_array_get(PackedArray *pa, int idx);
void packed_array_set(PackedArray *pa, int idx, Value v);
void visualize_stack(int base_reg, int local_count);

Value native_dict_has(int argc, Value *args);
Value native_dict_keys(int argc, Value *args);
Value native_dict_values(int argc, Value *args);
Value native_dict_remove(int argc, Value *args);
Value native_dict_merge(int argc, Value *args);
Value native_str_contains(int argc, Value *args);
Value native_str_find(int argc, Value *args);
Value native_str_slice(int argc, Value *args);
Value native_str_split(int argc, Value *args);
Value native_list_append(int argc, Value *args);
Value native_list_pop(int argc, Value *args);
Value native_list_len(int argc, Value *args);
Value native_val_type(int argc, Value *args);
Value native_type(int argc, Value *args);
Value native_len(int argc, Value *args);
Value list_pop(List *l);
void list_insert(List *l, int idx, Value val);
void list_remove(List *l, int idx);
int list_contains(List *l, Value val);
void list_sort(List *l);
void list_reverse(List *l);
Value value_string_copy(const char *s);
Value rope_new(Value left, Value right);
Value glow_string_concat(Value a, Value b);
const char* rope_flatten(Value v);
Class* class_new(const char *name, Class *parent);
Value class_get_method(Class *c, const char *name);
Instance* instance_new(Class *c);
Value instance_get(Instance *inst, const char *name);
void instance_set(Instance *inst, const char *name, Value val);
Trait* trait_new(const char *name);
Value bound_method_new(Value inst, FuncEntry *fe);
Value bound_native_method_new(Value inst, NativeFuncPtr native_m);
/* Core library (corelib.c): built-in list/string/dict methods and globals. */
NativeFuncPtr glow_builtin_method(Value v, const char *name);
void glow_corelib_init(void);
void glow_datetime_init(void);
/* process module (process.c): run and talk to other programs. */
void glow_process_init(void);
/* ffi module (ffi.c): call functions in C libraries. */
void glow_ffi_init(void);
Value native_func_new(const char *name, NativeFuncPtr ptr);
Value native_func_new_data(const char *name, NativeFuncPtr ptr, void *data, void (*finalize)(void *data));
/* The current row of a SQLite statement as a dict (stdlib.c). */
Value glow_sqlite_row(void *stmt);
const char *intern_string(const char *s);
/* Full path of the running glowlang executable, written to out
   (NUL-terminated). Returns its length, or -1 when it is not known. */
int glow_self_exe_path(char *out, size_t outsz);
const char *intern_string_hash(const char *s, uint32_t *out_hash);
const char *intern_string_find(const char *s, uint32_t *out_hash);
void intern_table_init(void);
void intern_cleanup(void);
void lexer_cleanup(void);
void var_table_init(void);
int global_get_slot(const char *name);
int global_find_slot(const char *name);
void global_mark_declared(int slot);
int global_is_declared(const char *name);
char *value_to_new_cstring(Value v);
Value value_to_string_value(Value v);
void var_set(const char *name, Value val);
Value var_get(const char *name);
void func_table_init(void);
FuncEntry* func_table_add(const char *name);
FuncEntry* func_get(const char *name);
void std_init(void);
void std_cleanup(void);
void print_value(Value v);
void format_double(char *buf, size_t n, double d);
double val_to_double(Value v);
extern Class *vm_last_class;
extern Trait *vm_last_trait;
extern FuncEntry *func_table;
extern int func_table_size;
extern int func_table_capacity;
extern FuncEntry main_fe;
extern const char *compiling_file;

/* Method Specialization Helpers */
TypeSignature compute_signature(Value *regs, int count);
void* func_get_specialization(FuncEntry *fe, TypeSignature sig);
void func_add_specialization(FuncEntry *fe, TypeSignature sig, void *code, size_t size);

typedef struct {
    int line;
    char *message;
} GlowError;

extern GlowError *glow_errors;
extern int glow_error_count;
void glow_clear_errors(void);
void glow_add_error(int line, const char *msg);

typedef struct yy_buffer_state *YY_BUFFER_STATE;
extern YY_BUFFER_STATE yy_scan_string(const char * str);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);
extern YY_BUFFER_STATE get_current_buffer(void);
extern void yy_switch_to_buffer(YY_BUFFER_STATE buffer);

#endif
