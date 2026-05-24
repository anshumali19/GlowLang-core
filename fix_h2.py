with open("src/glowlang.h", "r") as f:
    text = f.read()

import re
# We just want the bottom half after the duplicated stuff.
# The bottom half starts with /* ================================================================
# *  BYTECODE
# * ================================================================ */
idx = text.rfind("/* ================================================================\n *  BYTECODE")

bottom = text[idx:]

top = """#ifndef GLOWLANG_H
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

"""

with open("src/glowlang.h", "w") as f:
    f.write(top + bottom)
