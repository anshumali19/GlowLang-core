#include "glowlang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <process.h>
#define sleep(x) Sleep((x)*1000)
#define usleep(x) Sleep((x)/1000)
typedef int socklen_t;
#else
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <dirent.h>
#ifdef _WIN32
#include "win_pthread.h"
#else
#include <pthread.h>
#endif
#ifdef _WIN32
#include <io.h>
#define access _access
#else
#include <unistd.h>
#endif
#endif
#include <errno.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sqlite3.h>
#if defined(__x86_64__) || defined(_M_X64)
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif
#endif
#include <ctype.h>

Class *vm_last_class = NULL;
Trait *vm_last_trait = NULL;
GlobalTable global_table = {NULL, NULL, 0, 0};

FuncEntry *func_table = NULL;
int func_table_size = 1024;
int func_table_capacity = 0;
FuncEntry main_fe;

const char *compiling_file = NULL;

Value jit_call_helper(FuncEntry *fe, int argc, Value *args, Instruction *pc) {
    (void)argc;
    if (pc) call_stack[call_stack_ptr - 1].pc = pc;
    if (fe && fe->body) {
        TypeSignature sig = compute_signature(args, fe->param_count);
        void *jit = func_get_specialization(fe, sig);
        if (jit && jit != (void*)1) {
            if (pc) {
                pc->cache[0].key = (void*)sig;
                pc->cache[0].val = (intptr_t)jit;
            }
            typedef Value (*JITFunc)(Value *R, int *pc_idx);
            int deopt_pc = 0;
            call_stack_push(fe, NULL);
            Value res = ((JITFunc)jit)(args, &deopt_pc);
            call_stack_pop();
            return res;
        }
        return run_bytecode(fe->body, NULL, NULL, (int)(args - vm_stack));
    }
    return value_none();
}

Value jit_call_helper_name(const char *name, int argc, Value *args, Instruction *pc) {
    if (pc) call_stack[call_stack_ptr - 1].pc = pc;
    FuncEntry *fe = func_get(name);
    if (fe) return jit_call_helper(fe, argc, args, pc);
    Value v = var_get(name);
    return jit_call_helper_val(v, argc, args, pc);
}

Value jit_call_helper_val(Value callee, int argc, Value *args, Instruction *pc) {
    if (pc) call_stack[call_stack_ptr - 1].pc = pc;
    if (GET_TYPE(callee) == VAL_OBJECT && (callee & MASK_PAYLOAD)) {
        ObjType ot = GET_OBJ_TYPE(callee);
        if (ot == OBJ_CLASS) return value_instance(instance_new(AS_CLASS(callee)));
        if (ot == OBJ_BOUND_METHOD) {
            BoundMethod *bm = AS_BOUND_METHOD(callee);
            if (bm->native_method) {
                Value temp_args[256];
                temp_args[0] = bm->instance;
                int count = argc;
                if (count > 254) count = 254;
                int start_offset = (pc && pc->is_method) ? 1 : 0;
                for (int i = 0; i < count; i++) {
                    temp_args[i + 1] = args[i + start_offset];
                }
                return bm->native_method(count + 1, temp_args);
            }
            return jit_call_helper(bm->method, argc, args, pc);
        }
        if (ot == OBJ_CLOSURE) {
            Closure *c = AS_CLOSURE(callee);
            if (c->body) return run_bytecode(c->body, NULL, c, (int)(args - vm_stack));
        }
        if (ot == OBJ_NATIVE_FUNC) {
            if (pc && pc->is_method) {
                args++;
            }
            return AS_NATIVE_FUNC(callee)->func(argc, args);
        }
    }
    return value_none();
}

Value packed_array_get_jit(Value v_pa, Value v_idx) {
    if (GET_TYPE(v_pa) != VAL_OBJECT || GET_OBJ_TYPE(v_pa) != OBJ_PACKED_ARRAY) return value_none();
    PackedArray *pa = AS_PACKED_ARRAY(v_pa);
    int idx = (int)val_to_double(v_idx);
    if (idx < 0 || idx >= pa->size) return value_none();
    if (pa->type == PACKED_INT64) return value_int(((int64_t*)pa->data)[idx]);
    return value_float(((double*)pa->data)[idx]);
}

void packed_array_set_jit(Value v_pa, Value v_idx, Value v_val) {
    if (GET_TYPE(v_pa) != VAL_OBJECT || GET_OBJ_TYPE(v_pa) != OBJ_PACKED_ARRAY) return;
    PackedArray *pa = AS_PACKED_ARRAY(v_pa);
    int idx = (int)val_to_double(v_idx);
    if (idx < 0 || idx >= pa->size) return;
    if (pa->type == PACKED_INT64) ((int64_t*)pa->data)[idx] = (int64_t)val_to_double(v_val);
    else ((double*)pa->data)[idx] = val_to_double(v_val);
}

Value glow_index_jit(Value coll, Value v_idx) {
    if (GET_TYPE(coll) == VAL_LIST) {
        int i = (int)AS_INT(v_idx);
        List *l = AS_LIST(coll);
        if (i < 0) i += l->size;
        if (i >= 0 && i < l->size) return l->items[i];
        return value_none();
    } else if (GET_TYPE(coll) == VAL_OBJECT) {
        if ((coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_PACKED_ARRAY) {
            return packed_array_get_jit(coll, v_idx);
        }
        return value_none();
    } else if (GET_TYPE(coll) == VAL_DICT) {
        return dict_get(AS_DICT(coll), AS_STRING(v_idx));
    }
    return value_none();
}

void glow_store_index_jit(Value coll, Value v_idx, Value v_val) {
    if (GET_TYPE(coll) == VAL_LIST) {
        int i = (int)AS_INT(v_idx);
        List *l = AS_LIST(coll);
        if (i < 0) i += l->size;
        if (i >= 0 && i < l->size) {
            l->items[i] = v_val;
            gc_write_barrier((GCObject*)l, v_val);
        }
    } else if (GET_TYPE(coll) == VAL_OBJECT) {
        if ((coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_PACKED_ARRAY) {
            packed_array_set_jit(coll, v_idx, v_val);
        }
    } else if (GET_TYPE(coll) == VAL_DICT) {
        dict_set(AS_DICT(coll), AS_STRING(v_idx), v_val);
    }
}

void simd_add_helper(Value v_a, Value v_b, Value v_target) {
    if (GET_TYPE(v_a) != VAL_OBJECT || !(v_a & MASK_PAYLOAD) || GET_OBJ_TYPE(v_a) != OBJ_PACKED_ARRAY ||
        GET_TYPE(v_b) != VAL_OBJECT || !(v_b & MASK_PAYLOAD) || GET_OBJ_TYPE(v_b) != OBJ_PACKED_ARRAY ||
        GET_TYPE(v_target) != VAL_OBJECT || !(v_target & MASK_PAYLOAD) || GET_OBJ_TYPE(v_target) != OBJ_PACKED_ARRAY) {
        return;
    }
    PackedArray *pa = AS_PACKED_ARRAY(v_a), *pb = AS_PACKED_ARRAY(v_b), *pt = AS_PACKED_ARRAY(v_target);
    int size = pa->size;
    if (pb->size < size) size = pb->size;
    if (pt->size < size) size = pt->size;
    if (pa->type != pb->type || pa->type != pt->type) return;

    if (pa->type == PACKED_FLOAT64) {
        double *da = (double*)pa->data, *db = (double*)pb->data, *dt = (double*)pt->data;
        #pragma GCC ivdep
        for (int i=0; i<size; i++) dt[i] = da[i] + db[i];
    } else {
        int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *it = (int64_t*)pt->data;
        #pragma GCC ivdep
        for (int i=0; i<size; i++) it[i] = ia[i] + ib[i];
    }
}

double val_to_double(Value v) {
    if (((v) & MASK_NAN) != MASK_NAN) return AS_FLOAT(v);
    uint64_t type = ((v) & MASK_TYPE) >> 48;
    if (type == VAL_INT) return (double)AS_INT(v);
    if (type == VAL_BOOL) return (double)AS_BOOL(v);
    return 0.0;
}

Value value_from_double(double d) {
    if (d == (int64_t)d) return value_int((int64_t)d);
    return value_float(d);
}

#define MAX_CALL_STACK 65536

Value value_string_copy(const char *s) {
    if (!s) s = "";
    size_t len = strlen(s);
    GCString *gs = gc_alloc(sizeof(GCString) + len + 1, OBJ_STRING);
    gs->size = (int)len;
    if (len >= 32) {
        size_t i = 0;
        #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
for (; i + 32 <= len; i += 32) {
            __m256i v = _mm256_loadu_si256((__m256i*)&s[i]);
            _mm256_storeu_si256((__m256i*)&gs->chars[i], v);
        }
#endif
        strcpy(gs->chars + i, s + i);
    } else {
        strcpy(gs->chars, s);
    }
    return make_value(VAL_STRING, (uint64_t)(uintptr_t)gs);
}

Value rope_new(Value left, Value right) {
    RopeNode *rn = gc_alloc(sizeof(RopeNode), OBJ_ROPE);
    rn->length = rope_length(left) + rope_length(right);
    rn->is_leaf = false;
    rn->internal.left = (RopeNode*)(uintptr_t)(left & MASK_PAYLOAD);
    rn->internal.right = (RopeNode*)(uintptr_t)(right & MASK_PAYLOAD);
    return make_value(VAL_STRING, (uint64_t)(uintptr_t)rn);
}

static void rope_flatten_recursive(RopeNode *rn, char *dest, size_t *offset) {
    if (rn->obj.type == OBJ_STRING) {
        GCString *gs = (GCString*)rn;
        strcpy(dest + *offset, gs->chars);
        *offset += strlen(gs->chars);
    } else if (rn->obj.type == OBJ_ROPE) {
        if (rn->is_leaf) {
            GCString *gs = (GCString*)(uintptr_t)(rn->leaf.flat_string & MASK_PAYLOAD);
            strcpy(dest + *offset, gs->chars + rn->leaf.offset);
            *offset += rn->length;
        } else {
            rope_flatten_recursive(rn->internal.left, dest, offset);
            rope_flatten_recursive(rn->internal.right, dest, offset);
        }
    }
}

const char* rope_flatten(Value v) {
    GCObject *obj = (GCObject*)(uintptr_t)(v & MASK_PAYLOAD);
    if (obj->type == OBJ_STRING) return ((GCString*)obj)->chars;
    if (obj->type == OBJ_ROPE) {
        RopeNode *rn = (RopeNode*)obj;
        char *s = malloc(rn->length + 1);
        size_t offset = 0;
        rope_flatten_recursive(rn, s, &offset);
        s[rn->length] = '\0';
        Value flat = value_string_copy(s);
        free(s);
        // Cache it in the rope node? 
        rn->is_leaf = true;
        rn->leaf.flat_string = flat;
        rn->leaf.offset = 0;
        return AS_STRING(flat);
    }
    return "";
}

int global_get_slot(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < global_table.count; i++) {
        if (global_table.names[i] && strcmp(global_table.names[i], name) == 0) return i;
    }
    if (global_table.count >= global_table.capacity) {
        int old_cap = global_table.capacity;
        global_table.capacity = global_table.capacity == 0 ? 1024 : global_table.capacity * 2;
        global_table.names = realloc(global_table.names, sizeof(char*) * global_table.capacity);
        global_table.values = realloc(global_table.values, sizeof(Value) * global_table.capacity);
        for (int i = old_cap; i < global_table.capacity; i++) {
            global_table.names[i] = NULL;
            global_table.values[i] = value_none();
        }
    }
    int slot = global_table.count++;
    global_table.names[slot] = name;
    
    // Check if this is a builtin or already defined function
    FuncEntry *fe = func_get(name);
    if (fe) global_table.values[slot] = bound_method_new(value_none(), fe);
    else global_table.values[slot] = value_none();
    
    return slot;
}

void var_set(const char *name, Value value) {
    int slot = global_get_slot(name);
    if (slot >= 0) global_table.values[slot] = value;
}

Value var_get(const char *name) {
    int slot = global_get_slot(name);
    if (slot >= 0) return global_table.values[slot];
    return value_none();
}

void var_table_init(void) {
    // No longer needed
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <nmmintrin.h>
#endif

static unsigned int hash_str(const char *s) {
    unsigned int h = 5381;
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    const uint64_t *p = (const uint64_t*)s;
    size_t len = strlen(s);
    size_t i = 0;
    for (; i + 8 <= len; i += 8) {
        h = _mm_crc32_u64(h, *p++);
    }
    s = (const char*)p;
    for (; i < len; i++) {
        h = _mm_crc32_u8(h, *s++);
    }
#else
    while (*s) {
        h = ((h << 5) + h) + (*s++);
    }
#endif
    return h;
}

void func_table_init(void) { 
    func_table = calloc(func_table_size, sizeof(FuncEntry)); 
    main_fe.name = "<main>";
    main_fe.param_count = 0;
    main_fe.body = NULL;
}
FuncEntry* func_table_add(const char *name) {
    unsigned int h = hash_str(name) % func_table_size;
    unsigned int idx = h;
    do {
        if (!func_table[idx].used || strcmp(func_table[idx].name, name) == 0) {
            if (!func_table[idx].used) {
                func_table[idx].used = 1;
            }
            func_table[idx].name = name;
            for (int i = 0; i < global_table.count; i++) {
                if (global_table.names[i] && strcmp(global_table.names[i], name) == 0) {
                    global_table.values[i] = bound_method_new(value_none(), &func_table[idx]);
                    break;
                }
            }
            return &func_table[idx];
        }
        idx = (idx + 1) % func_table_size;
    } while (idx != h);
    return NULL;
}
FuncEntry* func_get(const char *name) {
    if (!name || !func_table) return NULL;
    unsigned int h = hash_str(name) % func_table_size;
    unsigned int idx = h;
    do {
        if (!func_table[idx].used) break;
        if (strcmp(func_table[idx].name, name) == 0) return &func_table[idx];
        idx = (idx + 1) % func_table_size;
    } while (idx != h);
    return NULL;
}

TypeSignature compute_signature(Value *regs, int count) {
    TypeSignature sig = 0;
    for (int i = 0; i < count && i < 16; i++) {
        uint64_t t = GET_TYPE(regs[i]);
        uint64_t jt = 0;
        if (t == VAL_INT) jt = 1;
        else if (t == VAL_FLOAT) jt = 2;
        else jt = 0;
        sig |= (jt & 0xFULL) << (i * 4);
    }
    return sig;
}

void* func_get_specialization(FuncEntry *fe, TypeSignature sig) {
    for (int i = 0; i < fe->spec_count; i++) {
        if (fe->specs[i].signature == sig) return fe->specs[i].jit_code;
    }
    return NULL;
}

void func_add_specialization(FuncEntry *fe, TypeSignature sig, void *code, size_t size) {
    if (fe->spec_count >= fe->spec_capacity) {
        fe->spec_capacity = fe->spec_capacity == 0 ? 4 : fe->spec_capacity * 2;
        fe->specs = realloc(fe->specs, sizeof(Specialization) * fe->spec_capacity);
    }
    fe->specs[fe->spec_count].signature = sig;
    fe->specs[fe->spec_count].jit_code = code;
    fe->specs[fe->spec_count].jit_size = size;
    fe->spec_count++;
}

/* Concurrency */

/* Helpers */
static uint32_t hash_string(const char *s) {
    uint32_t hash = 2166136261u;
    for (int i=0; s[i]; i++) { hash ^= (uint8_t)s[i]; hash *= 16777619u; }
    return hash;
}

Dict* dict_new(int sz) {
    int cap = 8; while (cap < sz) cap *= 2;
    Dict *d = gc_alloc(sizeof(Dict), OBJ_DICT);
    d->capacity = cap; d->size = 0;
    d->keys = calloc(cap, sizeof(Value));
    d->values = calloc(cap, sizeof(Value));
    for (int i=0; i<cap; i++) d->keys[i] = value_none();
    return d;
}

void dict_set(Dict *d, const char *key, Value val) {
    if (!key) return;
    const char *interned = intern_string(key);
    gc_write_barrier((GCObject*)d, val);
    uint32_t h = hash_string(interned);
    int idx = h & (d->capacity - 1);
    
    while (GET_TYPE(d->keys[idx]) != VAL_NONE) {
        if ((const char*)(uintptr_t)(d->keys[idx] & MASK_PAYLOAD) == interned) {
            d->values[idx] = val; return;
        }
        idx = (idx + 1) & (d->capacity - 1);
    }
    
    if (d->size * 2 >= d->capacity) {
        int old_cap = d->capacity;
        Value *old_keys = d->keys;
        Value *old_values = d->values;
        d->capacity *= 2;
        d->keys = calloc(d->capacity, sizeof(Value));
        d->values = calloc(d->capacity, sizeof(Value));
        for (int i=0; i<d->capacity; i++) d->keys[i] = value_none();
        d->size = 0;
        for (int i=0; i<old_cap; i++) {
            if (GET_TYPE(old_keys[i]) != VAL_NONE) {
                dict_set(d, (const char*)(uintptr_t)(old_keys[i] & MASK_PAYLOAD), old_values[i]);
            }
        }
        free(old_keys); free(old_values);
        dict_set(d, interned, val);
        return;
    }
    
    d->keys[idx] = make_value(VAL_STRING, (uintptr_t)interned);
    d->values[idx] = val;
    d->size++;
}

Value dict_get(Dict *d, const char *key) {
    if (!d || d->capacity == 0 || !key) return value_none();
    const char *interned = intern_string(key);
    uint32_t h = hash_string(interned);
    int idx = h & (d->capacity - 1);
    while (GET_TYPE(d->keys[idx]) != VAL_NONE) {
        if ((const char*)(uintptr_t)(d->keys[idx] & MASK_PAYLOAD) == interned) return d->values[idx];
        idx = (idx + 1) & (d->capacity - 1);
    }
    return value_none();
}

int dict_get_idx(Dict *d, const char *key) {
    if (!d || d->capacity == 0 || !key) return -1;
    const char *interned = intern_string(key);
    uint32_t h = hash_string(interned);
    int idx = h & (d->capacity - 1);
    while (GET_TYPE(d->keys[idx]) != VAL_NONE) {
        if ((const char*)(uintptr_t)(d->keys[idx] & MASK_PAYLOAD) == interned) return idx;
        idx = (idx + 1) & (d->capacity - 1);
    }
    return -1;
}
List* list_new(int sz) { List *l = gc_alloc(sizeof(List), OBJ_LIST); l->size = 0; l->capacity = sz; l->items = sz > 0 ? calloc(sz, sizeof(Value)) : NULL; return l; }
void list_push(List *l, Value v) {
    gc_write_barrier((GCObject*)l, v);
    if (l->size >= l->capacity) { l->capacity = (l->capacity == 0) ? 4 : l->capacity * 2; l->items = realloc(l->items, sizeof(Value)*l->capacity); } l->items[l->size++] = v;
}

PackedArray* packed_array_new(PackedType type, int capacity) {
    PackedArray *pa = gc_alloc(sizeof(PackedArray), OBJ_PACKED_ARRAY);
    pa->type = type;
    size_t elem_sz = (type == PACKED_INT64) ? sizeof(int64_t) : sizeof(double);
    if (capacity > 0) {
        if (posix_memalign(&pa->data, 32, capacity * elem_sz) != 0) {
            pa->data = NULL;
            pa->size = 0;
            pa->capacity = 0;
        } else {
            pa->size = capacity;
            pa->capacity = capacity;
            memset(pa->data, 0, capacity * elem_sz);
        }
    } else {
        pa->data = NULL;
        pa->size = 0;
        pa->capacity = 0;
    }
    return pa;
}

#if 0
static void packed_array_push(PackedArray *pa, Value v) {
    if (pa->size >= pa->capacity) {
        pa->capacity = (pa->capacity == 0) ? 4 : pa->capacity * 2;
        size_t elem_sz = (pa->type == PACKED_INT64) ? sizeof(int64_t) : sizeof(double);
        pa->data = realloc(pa->data, pa->capacity * elem_sz);
    }
    if (pa->type == PACKED_INT64) {
        ((int64_t*)pa->data)[pa->size++] = AS_INT(v);
    } else {
        ((double*)pa->data)[pa->size++] = AS_FLOAT(v);
    }
}
#endif

Value packed_array_get(PackedArray *pa, int idx) {
    if (idx < 0 || idx >= pa->size) return value_none();
    if (pa->type == PACKED_INT64) return value_int(((int64_t*)pa->data)[idx]);
    return value_from_double(((double*)pa->data)[idx]);
}

void packed_array_set(PackedArray *pa, int idx, Value v) {
    if (idx < 0 || idx >= pa->size) return;
    if (pa->type == PACKED_INT64) ((int64_t*)pa->data)[idx] = (int64_t)val_to_double(v);
    else ((double*)pa->data)[idx] = val_to_double(v);
}

static Shape* shape_new(Shape *p, const char *n, int o) { 
    Shape *s = gc_alloc(sizeof(Shape), OBJ_SHAPE); 
    s->parent = p; 
    s->name = n; 
    s->offset = o; 
    s->transitions = NULL;
    vm_stack[vm_stack_top++] = value_obj(s);
    s->transitions = dict_new(4); 
    vm_stack_top--;
    return s; 
}
#if 0
static Shape* shape_get_next(Shape* curr, const char *name) { 
    for (int i=0; i<curr->transitions->size; i++) 
        if (strcmp(AS_STRING(curr->transitions->keys[i]), name) == 0) 
            return (Shape*)(uintptr_t)(curr->transitions->values[i] & MASK_PAYLOAD); 
    Shape *next = shape_new(curr, name, curr->offset + 1); 
    dict_set(curr->transitions, name, value_obj(next)); 
    return next; 
}
#endif
Class* class_new(const char *name, Class *parent) { 
    Class *c = gc_alloc(sizeof(Class), OBJ_CLASS); 
    c->name = strdup(name); 
    c->parent = parent; 
    c->methods = NULL;
    c->root_shape = NULL;
    c->stable_shape = NULL;
    c->instances_created = 0;
    
    vm_stack[vm_stack_top++] = value_class(c);
    c->methods = dict_new(8); 
    vm_stack[vm_stack_top++] = value_dict(c->methods);
    c->root_shape = shape_new(NULL, "__root__", -1); 
    vm_stack_top -= 2;
    return c; 
}
Instance* instance_new(Class *k) {
    Instance *i = gc_alloc(sizeof(Instance), OBJ_INSTANCE);
    i->klass = k;
    i->properties = NULL;
    k->instances_created++;
    
    vm_stack[vm_stack_top++] = value_instance(i);
    i->properties = dict_new(8);
    vm_stack_top--;
    return i;
}

Value class_get_method(Class *k, const char *n) { 
    while(k) { 
        Value m = dict_get(k->methods, n); 
        if(GET_TYPE(m) != VAL_NONE) return m; 
        k=k->parent; 
    } 
    return value_none(); 
}

Value instance_get(Instance *inst, const char *name) {
    if (trace_mode) printf("DEBUG: instance_get(%s) on Inst %p, Props %p\n", name, (void*)inst, (void*)inst->properties);
    Value v = dict_get(inst->properties, name);
    if (GET_TYPE(v) != VAL_NONE) return v;
    return class_get_method(inst->klass, name);
}

void instance_set(Instance *inst, const char *name, Value val) {
    if (trace_mode) printf("DEBUG: instance_set(%s) on Inst %p, Props %p, val 0x%lx\n", name, (void*)inst, (void*)inst->properties, (unsigned long)val);
    dict_set(inst->properties, name, val);
    gc_write_barrier((GCObject*)inst, val);
}

Trait* trait_new(const char *name) { 
    Trait *t = gc_alloc(sizeof(Trait), OBJ_TRAIT); 
    t->name = strdup(name); 
    t->methods = NULL;
    vm_stack[vm_stack_top++] = value_trait(t);
    t->methods = dict_new(8); 
    vm_stack_top--;
    return t; 
}
Value bound_method_new(Value inst, FuncEntry *m) { BoundMethod *bm = gc_alloc(sizeof(BoundMethod), OBJ_BOUND_METHOD); bm->instance = inst; bm->method = m; bm->native_method = NULL; return value_bound_method(bm); }
Value bound_native_method_new(Value inst, NativeFuncPtr native_m) { BoundMethod *bm = gc_alloc(sizeof(BoundMethod), OBJ_BOUND_METHOD); bm->instance = inst; bm->method = NULL; bm->native_method = native_m; return value_bound_method(bm); }
Value native_func_new(const char *name, NativeFuncPtr ptr) { NativeFunc *nf = gc_alloc(sizeof(NativeFunc), OBJ_NATIVE_FUNC); nf->name = name; nf->func = ptr; return value_native_func(nf); }


void print_value(Value v) {
    if (v == 0) { printf("none"); return; }
    if ((v & MASK_NAN) != MASK_NAN) { printf("%g", AS_FLOAT(v)); return; }
    uint64_t type = (v & MASK_TYPE) >> 48;
    if (type == VAL_INT) printf("%ld", (long)AS_INT(v));
    else if (type == VAL_BOOL) printf(AS_BOOL(v) ? "true" : "false");
    else if (type == VAL_STRING) printf("%s", AS_STRING(v));
    else if (type == VAL_LIST) { List *l = AS_LIST(v); printf("["); for (int i=0; i<l->size; i++) { print_value(l->items[i]); if (i < l->size - 1) printf(", "); } printf("]"); }
    else if (type == VAL_DICT) {
        Dict *d = AS_DICT(v);
        printf("{");
        int printed = 0;
        for (int i = 0; i < d->capacity; i++) {
            if (GET_TYPE(d->keys[i]) != VAL_NONE) {
                if (printed > 0) printf(", ");
                const char *key_str = (const char*)(uintptr_t)(d->keys[i] & MASK_PAYLOAD);
                printf("\"%s\": ", key_str);
                print_value(d->values[i]);
                printed++;
            }
        }
        printf("}");
    }
    else if (type == VAL_OBJECT) {
        if (!(v & MASK_PAYLOAD)) { printf("none"); return; }
        ObjType ot = GET_OBJ_TYPE(v);
        if (ot == OBJ_CLASS) printf("<class %s>", AS_CLASS(v)->name);
        else if (ot == OBJ_INSTANCE) printf("<instance of %s>", AS_INSTANCE(v)->klass->name);
        else if (ot == OBJ_PACKED_ARRAY) {
            PackedArray *pa = AS_PACKED_ARRAY(v);
            printf("[");
            for (int i=0; i<pa->size; i++) {
                if (pa->type == PACKED_INT64) printf("%ld", (long)((int64_t*)pa->data)[i]);
                else printf("%g", ((double*)pa->data)[i]);
                if (i < pa->size - 1) printf(", ");
            }
            printf("]");
        }
        else printf("<object>");
    } else printf("none");
}

GlowError *glow_errors = NULL; int glow_error_count = 0; static int glow_error_capacity = 0;
void glow_clear_errors(void) { for (int i = 0; i < glow_error_count; i++) if(glow_errors[i].message) free(glow_errors[i].message); glow_error_count = 0; }
void glow_add_error(int line, const char *msg) {
    if (glow_error_count >= glow_error_capacity) { glow_error_capacity = (glow_error_capacity == 0) ? 8 : glow_error_capacity * 2; glow_errors = realloc(glow_errors, sizeof(GlowError) * glow_error_capacity); }
    glow_errors[glow_error_count].line = line; glow_errors[glow_error_count].message = strdup(msg); glow_error_count++;
}

static Value native_len(int argc, Value *args) { 
    if (argc < 1) return value_int(0); 
    Value v = args[0]; 
    if (GET_TYPE(v) == VAL_STRING) return value_int(strlen(AS_STRING(v))); 
    if (GET_TYPE(v) == VAL_LIST) return value_int(AS_LIST(v)->size); 
    if (GET_TYPE(v) == VAL_DICT) return value_int(AS_DICT(v)->size);
    if (GET_TYPE(v) == VAL_OBJECT) {
        if ((v & MASK_PAYLOAD) && GET_OBJ_TYPE(v) == OBJ_PACKED_ARRAY) return value_int(AS_PACKED_ARRAY(v)->size);
    }
    return value_int(0); 
}
static Value native_range(int argc, Value *args) { int n = (argc > 0) ? (int)AS_INT(args[0]) : 0; List *l = list_new(n); for (int i = 0; i < n; i++) list_push(l, value_int(i)); return value_list(l); }
static Value native_os_gc_info(int argc, Value *args) {
    (void)argc; (void)args;
    Dict *d = dict_new(2);
    dict_set(d, "allocated", value_int(gc_allocated));
    dict_set(d, "threshold", value_int(gc_threshold));
    return value_dict(d);
}

static Value os_exit(int argc, Value *args) { exit(argc > 0 ? AS_INT(args[0]) : 0); return value_none(); }
static Value os_system(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_int(-1);
    const char *cmd = AS_STRING(args[0]);
    int status = system(cmd);
    return value_int(status);
}
#include <math.h>
#include <openssl/sha.h>

static Value math_sin(int argc, Value *args) { return argc > 0 ? value_from_double(sin(val_to_double(args[0]))) : value_none(); }
static Value math_cos(int argc, Value *args) { return argc > 0 ? value_from_double(cos(val_to_double(args[0]))) : value_none(); }
static Value math_sqrt(int argc, Value *args) { return argc > 0 ? value_from_double(sqrt(val_to_double(args[0]))) : value_none(); }
static Value math_pow(int argc, Value *args) { return argc > 1 ? value_from_double(pow(val_to_double(args[0]), val_to_double(args[1]))) : value_none(); }

static Value crypto_sha256(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_none();
    const char *str = AS_STRING(args[0]);
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)str, strlen(str), hash);
    char out[SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) sprintf(out + (i * 2), "%02x", hash[i]);
    out[SHA256_DIGEST_LENGTH * 2] = 0;
    return value_string_copy(out);
}

static void json_append_str(const char *s, StringBuilder *sb) {
    int len = strlen(s);
    if (sb->size + len >= sb->capacity) {
        sb->capacity = (sb->size + len + 1) * 2;
        sb->buffer = realloc(sb->buffer, sb->capacity);
    }
    memcpy(sb->buffer + sb->size, s, len);
    sb->size += len;
    sb->buffer[sb->size] = '\0';
}

static void json_stringify_helper(Value v, StringBuilder *sb) {
    // For now, keep it simple but use the efficient append
    if (v == 0) { json_append_str("null", sb); return; }
    if ((v & MASK_NAN) != MASK_NAN) { 
        char tmp[64]; snprintf(tmp, sizeof(tmp), "%g", AS_FLOAT(v)); json_append_str(tmp, sb); return; 
    }
    uint64_t type = (v & MASK_TYPE) >> 48;
    if (type == VAL_INT) {
        char tmp[64]; snprintf(tmp, sizeof(tmp), "%ld", (long)AS_INT(v)); json_append_str(tmp, sb);
    } else if (type == VAL_BOOL) {
        json_append_str(AS_BOOL(v) ? "true" : "false", sb);
    } else if (type == VAL_STRING) {
        json_append_str("\"", sb); json_append_str(AS_STRING(v), sb); json_append_str("\"", sb);
    } else if (type == VAL_LIST) {
        json_append_str("[", sb);
        List *l = AS_LIST(v);
        for (int i = 0; i < l->size; i++) {
            json_stringify_helper(l->items[i], sb);
            if (i < l->size - 1) json_append_str(", ", sb);
        }
        json_append_str("]", sb);
    } else if (type == VAL_DICT) {
        json_append_str("{", sb);
        Dict *d = AS_DICT(v);
        int printed = 0;
        for (int i = 0; i < d->capacity; i++) {
            if (GET_TYPE(d->keys[i]) != VAL_NONE) {
                if (printed > 0) json_append_str(", ", sb);
                json_append_str("\"", sb); 
                const char *key = (const char*)(uintptr_t)(d->keys[i] & MASK_PAYLOAD);
                json_append_str(key, sb); 
                json_append_str("\": ", sb);
                json_stringify_helper(d->values[i], sb);
                printed++;
            }
        }
        json_append_str("}", sb);
    } else {
        json_append_str("null", sb);
    }
}

static Value json_parse_value(const char **src) {
    while (**src && isspace(**src)) (*src)++;
    if (**src == '{') {
        (*src)++;
        Dict *d = dict_new(8);
        while (**src && **src != '}') {
            while (**src && isspace(**src)) (*src)++;
            if (**src == '"') {
                (*src)++;
                const char *start = *src;
                while (**src && **src != '"') (*src)++;
                int len = *src - start;
                char *key = malloc(len + 1); memcpy(key, start, len); key[len] = 0;
                const char *interned = intern_string(key); free(key);
                (*src)++;
                while (**src && isspace(**src)) (*src)++;
                if (**src == ':') (*src)++;
                dict_set(d, interned, json_parse_value(src));
            }
            while (**src && isspace(**src)) (*src)++;
            if (**src == ',') (*src)++;
        }
        if (**src == '}') (*src)++;
        return value_dict(d);
    } else if (**src == '[') {
        (*src)++;
        List *l = list_new(8);
        while (**src && **src != ']') {
            list_push(l, json_parse_value(src));
            while (**src && isspace(**src)) (*src)++;
            if (**src == ',') (*src)++;
        }
        if (**src == ']') (*src)++;
        return value_list(l);
    } else if (**src == '"') {
        (*src)++;
        const char *start = *src;
        while (**src && **src != '"') (*src)++;
        int len = *src - start;
        char *str = malloc(len + 1); memcpy(str, start, len); str[len] = 0;
        Value res = value_string_copy(str);
        free(str);
        (*src)++;
        return res;
    } else if (isdigit(**src) || **src == '-') {
        char *end;
        double d = strtod(*src, &end);
        *src = end;
        return value_from_double(d);
    } else if (strncmp(*src, "true", 4) == 0) { *src += 4; return value_bool(1); }
    else if (strncmp(*src, "false", 5) == 0) { *src += 5; return value_bool(0); }
    else if (strncmp(*src, "null", 4) == 0) { *src += 4; return value_none(); }
    return value_none();
}

static Value native_json_parse(int argc, Value *args) {
    if (argc >= 2 && GET_TYPE(args[0]) == VAL_DICT) { argc--; args++; }
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_none();
    const char *src = AS_STRING(args[0]);
    return json_parse_value(&src);
}

static Value native_json_stringify(int argc, Value *args) {
    if (argc >= 2 && GET_TYPE(args[0]) == VAL_DICT) { argc--; args++; }
    if (argc < 1) return value_none();
    StringBuilder sb;
    sb.capacity = 1024;
    sb.size = 0;
    sb.buffer = malloc(sb.capacity);
    sb.buffer[0] = '\0';
    json_stringify_helper(args[0], &sb);
    Value res = value_string_copy(sb.buffer);
    free(sb.buffer);
    return res;
}

Value native_dict_has(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_DICT || GET_TYPE(args[1]) != VAL_STRING) {
        return value_bool(0);
    }
    Dict *d = AS_DICT(args[0]);
    const char *key = AS_STRING(args[1]);
    Value val = dict_get(d, key);
    return value_bool(GET_TYPE(val) != VAL_NONE);
}

Value native_dict_keys(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_DICT) {
        return value_none();
    }
    Dict *d = AS_DICT(args[0]);
    List *l = list_new(d->size);
    for (int i = 0; i < d->capacity; i++) {
        if (GET_TYPE(d->keys[i]) != VAL_NONE) {
            list_push(l, value_string_copy((const char*)(uintptr_t)(d->keys[i] & MASK_PAYLOAD)));
        }
    }
    return value_list(l);
}

Value native_dict_values(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_DICT) {
        return value_none();
    }
    Dict *d = AS_DICT(args[0]);
    List *l = list_new(d->size);
    for (int i = 0; i < d->capacity; i++) {
        if (GET_TYPE(d->keys[i]) != VAL_NONE) {
            list_push(l, d->values[i]);
        }
    }
    return value_list(l);
}

Value native_dict_remove(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_DICT || GET_TYPE(args[1]) != VAL_STRING) {
        return value_none();
    }
    Dict *d = AS_DICT(args[0]);
    const char *key = AS_STRING(args[1]);
    const char *interned = intern_string(key);
    uint32_t h = hash_string(interned);
    int idx = h & (d->capacity - 1);
    while (GET_TYPE(d->keys[idx]) != VAL_NONE) {
        if ((const char*)(uintptr_t)(d->keys[idx] & MASK_PAYLOAD) == interned) {
            Value val = d->values[idx];
            d->keys[idx] = value_none();
            d->values[idx] = value_none();
            d->size--;
            
            // Rehash cluster following idx
            int next_idx = (idx + 1) & (d->capacity - 1);
            while (GET_TYPE(d->keys[next_idx]) != VAL_NONE) {
                Value k_rehash = d->keys[next_idx];
                Value v_rehash = d->values[next_idx];
                d->keys[next_idx] = value_none();
                d->values[next_idx] = value_none();
                d->size--;
                dict_set(d, (const char*)(uintptr_t)(k_rehash & MASK_PAYLOAD), v_rehash);
                next_idx = (next_idx + 1) & (d->capacity - 1);
            }
            return val;
        }
        idx = (idx + 1) & (d->capacity - 1);
    }
    return value_none();
}

Value native_dict_merge(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_DICT || GET_TYPE(args[1]) != VAL_DICT) {
        return value_none();
    }
    Dict *d1 = AS_DICT(args[0]);
    Dict *d2 = AS_DICT(args[1]);
    for (int i = 0; i < d2->capacity; i++) {
        if (GET_TYPE(d2->keys[i]) != VAL_NONE) {
            dict_set(d1, (const char*)(uintptr_t)(d2->keys[i] & MASK_PAYLOAD), d2->values[i]);
        }
    }
    return value_dict(d1);
}

Value native_str_contains(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_STRING || GET_TYPE(args[1]) != VAL_STRING) {
        return value_bool(0);
    }
    const char *s = AS_STRING(args[0]);
    const char *sub = AS_STRING(args[1]);
    return value_bool(strstr(s, sub) != NULL);
}

Value native_str_find(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_STRING || GET_TYPE(args[1]) != VAL_STRING) {
        return value_int(-1);
    }
    const char *s = AS_STRING(args[0]);
    const char *sub = AS_STRING(args[1]);
    const char *found = strstr(s, sub);
    if (found) {
        return value_int(found - s);
    }
    return value_int(-1);
}

Value native_str_slice(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_STRING) {
        return value_none();
    }
    const char *s = AS_STRING(args[0]);
    int len = strlen(s);
    
    int start = 0;
    if (GET_TYPE(args[1]) == VAL_INT) {
        start = (int)AS_INT(args[1]);
    } else if (GET_TYPE(args[1]) == VAL_FLOAT) {
        start = (int)AS_FLOAT(args[1]);
    }
    
    if (start < 0) {
        start = len + start;
        if (start < 0) start = 0;
    }
    if (start > len) start = len;
    
    int end = len;
    if (argc >= 3 && GET_TYPE(args[2]) != VAL_NONE) {
        if (GET_TYPE(args[2]) == VAL_INT) {
            end = (int)AS_INT(args[2]);
        } else if (GET_TYPE(args[2]) == VAL_FLOAT) {
            end = (int)AS_FLOAT(args[2]);
        }
        if (end < 0) {
            end = len + end;
            if (end < 0) end = 0;
        }
        if (end > len) end = len;
    }
    
    if (start > end) {
        return value_string_copy("");
    }
    
    int slice_len = end - start;
    char *buf = malloc(slice_len + 1);
    memcpy(buf, s + start, slice_len);
    buf[slice_len] = '\0';
    Value res = value_string_copy(buf);
    free(buf);
    return res;
}

Value native_str_split(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) {
        return value_none();
    }
    const char *s = AS_STRING(args[0]);
    const char *delim = " ";
    if (argc >= 2 && GET_TYPE(args[1]) == VAL_STRING) {
        delim = AS_STRING(args[1]);
    }
    
    List *l = list_new(8);
    
    size_t delim_len = strlen(delim);
    if (delim_len == 0) {
        int len = strlen(s);
        for (int i = 0; i < len; i++) {
            char buf[2] = { s[i], '\0' };
            list_push(l, value_string_copy(buf));
        }
        return value_list(l);
    }
    
    const char *current = s;
    const char *next = strstr(current, delim);
    while (next != NULL) {
        size_t part_len = next - current;
        char *buf = malloc(part_len + 1);
        memcpy(buf, current, part_len);
        buf[part_len] = '\0';
        list_push(l, value_string_copy(buf));
        free(buf);
        current = next + delim_len;
        next = strstr(current, delim);
    }
    list_push(l, value_string_copy(current));
    
    return value_list(l);
}

static Value http_request_native(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_STRING || GET_TYPE(args[1]) != VAL_STRING) {
        return value_none();
    }
    const char *method = AS_STRING(args[0]);
    const char *url = AS_STRING(args[1]);
    Value options = argc > 2 ? args[2] : value_none();

    int is_ssl = 0;
    const char *host_start = url;
    if (strncmp(url, "https://", 8) == 0) {
        is_ssl = 1;
        host_start = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        host_start = url + 7;
    }

    char hostname[256];
    int port = is_ssl ? 443 : 80;
    const char *path = "/";

    const char *slash = strchr(host_start, '/');
    const char *colon = strchr(host_start, ':');

    if (colon && (!slash || colon < slash)) {
        int host_len = colon - host_start;
        if (host_len > 255) host_len = 255;
        strncpy(hostname, host_start, host_len);
        hostname[host_len] = '\0';
        port = atoi(colon + 1);
        if (slash) path = slash;
    } else {
        if (slash) {
            int host_len = slash - host_start;
            if (host_len > 255) host_len = 255;
            strncpy(hostname, host_start, host_len);
            hostname[host_len] = '\0';
            path = slash;
        } else {
            strncpy(hostname, host_start, 255);
            hostname[255] = '\0';
        }
    }

    // Construct the request content BEFORE releasing GIL
    char req[16384];
    int req_len = snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: GlowLang/1.0\r\n", method, path, hostname);

    const char *body_str = NULL;
    int body_len = 0;
    if (GET_TYPE(options) == VAL_DICT) {
        Dict *opts = AS_DICT(options);
        Value v_headers = dict_get(opts, "headers");
        if (GET_TYPE(v_headers) == VAL_DICT) {
            Dict *hdrs = AS_DICT(v_headers);
            for (int i = 0; i < hdrs->size; i++) {
                if (GET_TYPE(hdrs->keys[i]) == VAL_STRING && GET_TYPE(hdrs->values[i]) == VAL_STRING) {
                    req_len += snprintf(req + req_len, sizeof(req) - req_len, "%s: %s\r\n", AS_STRING(hdrs->keys[i]), AS_STRING(hdrs->values[i]));
                }
            }
        }
        Value v_body = dict_get(opts, "body");
        if (GET_TYPE(v_body) == VAL_STRING) {
            body_str = AS_STRING(v_body);
            body_len = strlen(body_str);
            req_len += snprintf(req + req_len, sizeof(req) - req_len, "Content-Length: %d\r\n", body_len);
        }
    }
    req_len += snprintf(req + req_len, sizeof(req) - req_len, "\r\n");

    pthread_mutex_unlock(&glow_gil);

    struct hostent *he = gethostbyname(hostname);
    if (!he) {
        pthread_mutex_lock(&glow_gil);
        return value_none();
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        pthread_mutex_lock(&glow_gil);
        return value_none();
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr.s_addr, he->h_addr_list[0], he->h_length);

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        pthread_mutex_lock(&glow_gil);
        return value_none();
    }

    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    if (is_ssl) {
        SSL_library_init();
        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            close(sockfd);
            pthread_mutex_lock(&glow_gil);
            return value_none();
        }
        ssl = SSL_new(ctx);
        if (!ssl) {
            SSL_CTX_free(ctx);
            close(sockfd);
            pthread_mutex_lock(&glow_gil);
            return value_none();
        }
        SSL_set_fd(ssl, sockfd);
        if (SSL_connect(ssl) <= 0) {
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            close(sockfd);
            pthread_mutex_lock(&glow_gil);
            return value_none();
        }
    }

    if (is_ssl) {
        SSL_write(ssl, req, req_len);
        if (body_len > 0) SSL_write(ssl, body_str, body_len);
    } else {
        ssize_t _wr = write(sockfd, req, req_len);
        if (body_len > 0) _wr = write(sockfd, body_str, body_len);
        (void)_wr;
    }

    char *resp_buf = malloc(65536);
    int resp_cap = 65536;
    int resp_len = 0;

    while (1) {
        if (resp_len + 4096 >= resp_cap) {
            resp_cap *= 2;
            resp_buf = realloc(resp_buf, resp_cap);
        }
        int n;
        if (is_ssl) {
            n = SSL_read(ssl, resp_buf + resp_len, 4096);
        } else {
            n = read(sockfd, resp_buf + resp_len, 4096);
        }
        if (n <= 0) break;
        resp_len += n;
    }
    resp_buf[resp_len] = '\0';

    if (is_ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
    }
    close(sockfd);

    pthread_mutex_lock(&glow_gil);

    int status = 0;
    const char *body = "";
    Dict *resp_headers = dict_new(4);

    if (resp_len > 0) {
        char *status_line = resp_buf;
        char *headers_start = strstr(resp_buf, "\r\n");
        if (headers_start) {
            *headers_start = '\0';
            headers_start += 2;
            char *space1 = strchr(status_line, ' ');
            if (space1) {
                status = atoi(space1 + 1);
            }

            char *body_start = strstr(headers_start, "\r\n\r\n");
            if (body_start) {
                *body_start = '\0';
                body = body_start + 4;
            }

            char *line = headers_start;
            while (line && *line) {
                char *next_line = strstr(line, "\r\n");
                if (next_line) {
                    *next_line = '\0';
                    next_line += 2;
                }
                char *colon = strchr(line, ':');
                if (colon) {
                    *colon = '\0';
                    char *val = colon + 1;
                    while (*val == ' ') val++;
                    dict_set(resp_headers, line, value_string_copy(val));
                }
                line = next_line;
            }
        }
    }

    Dict *result = dict_new(4);
    dict_set(result, "status", value_int(status));
    dict_set(result, "body", value_string_copy(body));
    dict_set(result, "headers", value_dict(resp_headers));

    free(resp_buf);
    return value_dict(result);
}

static Value os_time(int argc, Value *args) { (void)argc; (void)args; struct timeval tv; gettimeofday(&tv, NULL); return value_float(tv.tv_sec + tv.tv_usec / 1000000.0); }

static SSL_CTX *ssl_ctx = NULL;
static void ssl_init_native() { SSL_library_init(); OpenSSL_add_all_algorithms(); SSL_load_error_strings(); ssl_ctx = SSL_CTX_new(TLS_server_method()); }
static Value ssl_listen(int argc, Value *args) {
    if (argc < 3) return value_int(-1);
    int port = AS_INT(args[0]); const char *cert = AS_STRING(args[1]), *key = AS_STRING(args[2]);
    if (!ssl_ctx) ssl_init_native();
    if (SSL_CTX_use_certificate_file(ssl_ctx, cert, SSL_FILETYPE_PEM) <= 0) return value_int(-2);
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx, key, SSL_FILETYPE_PEM) <= 0) return value_int(-3);
    int sockfd = socket(AF_INET, SOCK_STREAM, 0); struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(port);
    int opt = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return value_int(-4);
    listen(sockfd, 10); return value_int(sockfd);
}
static Value ssl_accept(int argc, Value *args) {
    if (argc < 1) return value_none();
    int server_fd = AS_INT(args[0]);
    pthread_mutex_unlock(&glow_gil);
    int client_fd = accept(server_fd, NULL, NULL);
    pthread_mutex_lock(&glow_gil);
    if (client_fd < 0) return value_none();
    SSL *ssl = SSL_new(ssl_ctx); SSL_set_fd(ssl, client_fd);
    pthread_mutex_unlock(&glow_gil);
    int accept_res = SSL_accept(ssl);
    pthread_mutex_lock(&glow_gil);
    if (accept_res <= 0) { SSL_free(ssl); close(client_fd); return value_none(); }
    return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)ssl);
}
static Value net_listen(int argc, Value *args) {
    if (argc < 1) return value_int(-1);
    int port = AS_INT(args[0]), sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(port);
    int opt = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return value_int(-1);
    listen(sockfd, 10); return value_int(sockfd);
}
static Value net_close(int argc, Value *args) { if (argc < 1) return value_int(-1); close(AS_INT(args[0])); return value_none(); }
static Value http_listen(int argc, Value *args) {
    if (argc < 1) return value_int(-1);
    int port = AS_INT(args[0]), sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(port);
    int opt = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return value_int(-1);
    listen(sockfd, 1024); return value_int(sockfd);
}
static Value http_accept(int argc, Value *args) {
    if (argc < 1) return value_int(-1);
    int sockfd = AS_INT(args[0]);
    struct sockaddr_in client_addr; socklen_t client_len = sizeof(client_addr);
    pthread_mutex_unlock(&glow_gil);
    int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
    pthread_mutex_lock(&glow_gil);
    return value_int(client_fd);
}
static Value http_read(int argc, Value *args) {
    if (argc < 1) return value_none();
    int fd = AS_INT(args[0]);
    char buf[4096];
    pthread_mutex_unlock(&glow_gil);
    int n = read(fd, buf, 4095);
    pthread_mutex_lock(&glow_gil);
    if (n <= 0) return value_none();
    buf[n] = '\0';
    return value_string_copy(buf);
}
static Value http_write(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[1]) != VAL_STRING) return value_int(-1);
    int fd = AS_INT(args[0]);
    const char *resp = AS_STRING(args[1]);
    pthread_mutex_unlock(&glow_gil);
    int res = write(fd, resp, strlen(resp));
    pthread_mutex_lock(&glow_gil);
    if (res < 0) return value_int(-1);
    return value_none();
}

static Value http_close(int argc, Value *args) { if (argc < 1) return value_int(-1); close(AS_INT(args[0])); return value_none(); }

static Value native_packed_int(int argc, Value *args) { int sz = argc > 0 ? (int)AS_INT(args[0]) : 0; return value_packed(packed_array_new(PACKED_INT64, sz)); }
static Value native_packed_float(int argc, Value *args) { int sz = argc > 0 ? (int)AS_INT(args[0]) : 0; return value_packed(packed_array_new(PACKED_FLOAT64, sz)); }

static Value native_packed_add(int argc, Value *args) { 
    if (argc < 2) return value_none();
    Value a = args[0], b = args[1];
    if (GET_TYPE(a) == VAL_OBJECT && GET_OBJ_TYPE(a) == OBJ_PACKED_ARRAY &&
        GET_TYPE(b) == VAL_OBJECT && GET_OBJ_TYPE(b) == OBJ_PACKED_ARRAY) {
        PackedArray *pa = AS_PACKED_ARRAY(a), *pb = AS_PACKED_ARRAY(b);
        if (pa->type == pb->type && pa->size == pb->size) {
            PackedArray *res = packed_array_new(pa->type, pa->size);
            int sz = pa->size;
            if (pa->type == PACKED_FLOAT64) {
                double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
                int i = 0;
                #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
for (; i <= sz - 4; i += 4) {
                    _mm256_storeu_pd(&dr[i], _mm256_add_pd(_mm256_loadu_pd(&da[i]), _mm256_loadu_pd(&db[i])));
                }
#endif
                for (; i < sz; i++) dr[i] = da[i] + db[i];
            } else {
                int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
                int i = 0;
                #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
for (; i <= sz - 4; i += 4) {
                    _mm256_storeu_si256((__m256i*)&ir[i], _mm256_add_epi64(_mm256_loadu_si256((__m256i*)&ia[i]), _mm256_loadu_si256((__m256i*)&ib[i])));
                }
#endif
                for (; i < sz; i++) ir[i] = ia[i] + ib[i];
            }
            return value_packed(res);
        }
    }
    return value_none();
}

static Value native_packed_sub(int argc, Value *args) { 
    if (argc < 2) return value_none();
    Value a = args[0], b = args[1];
    if (GET_TYPE(a) == VAL_OBJECT && GET_OBJ_TYPE(a) == OBJ_PACKED_ARRAY &&
        GET_TYPE(b) == VAL_OBJECT && GET_OBJ_TYPE(b) == OBJ_PACKED_ARRAY) {
        PackedArray *pa = AS_PACKED_ARRAY(a), *pb = AS_PACKED_ARRAY(b);
        if (pa->type == pb->type && pa->size == pb->size) {
            PackedArray *res = packed_array_new(pa->type, pa->size);
            int sz = pa->size;
            if (pa->type == PACKED_FLOAT64) {
                double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
                int i = 0;
                #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
for (; i <= sz - 4; i += 4) {
                    _mm256_storeu_pd(&dr[i], _mm256_sub_pd(_mm256_loadu_pd(&da[i]), _mm256_loadu_pd(&db[i])));
                }
#endif
                for (; i < sz; i++) dr[i] = da[i] - db[i];
            } else {
                int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
                int i = 0;
                #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
for (; i <= sz - 4; i += 4) {
                    _mm256_storeu_si256((__m256i*)&ir[i], _mm256_sub_epi64(_mm256_loadu_si256((__m256i*)&ia[i]), _mm256_loadu_si256((__m256i*)&ib[i])));
                }
#endif
                for (; i < sz; i++) ir[i] = ia[i] - ib[i];
            }
            return value_packed(res);
        }
    }
    return value_none();
}

static Value native_packed_mul(int argc, Value *args) { 
    if (argc < 2) return value_none();
    Value a = args[0], b = args[1];
    if (GET_TYPE(a) == VAL_OBJECT && GET_OBJ_TYPE(a) == OBJ_PACKED_ARRAY &&
        GET_TYPE(b) == VAL_OBJECT && GET_OBJ_TYPE(b) == OBJ_PACKED_ARRAY) {
        PackedArray *pa = AS_PACKED_ARRAY(a), *pb = AS_PACKED_ARRAY(b);
        if (pa->type == pb->type && pa->size == pb->size) {
            PackedArray *res = packed_array_new(pa->type, pa->size);
            int sz = pa->size;
            if (pa->type == PACKED_FLOAT64) {
                double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
                int i = 0;
                #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
for (; i <= sz - 4; i += 4) {
                    _mm256_storeu_pd(&dr[i], _mm256_mul_pd(_mm256_loadu_pd(&da[i]), _mm256_loadu_pd(&db[i])));
                }
#endif
                for (; i < sz; i++) dr[i] = da[i] * db[i];
            } else {
                int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
                for (int i=0; i<sz; i++) ir[i] = ia[i] * ib[i]; // No native AVX2 64-bit multiply
            }
            return value_packed(res);
        }
    }
    return value_none();
}

static Value native_packed_dot(int argc, Value *args) { 
    if (argc < 2) return value_none();
    Value a = args[0], b = args[1];
    if (GET_TYPE(a) == VAL_OBJECT && GET_OBJ_TYPE(a) == OBJ_PACKED_ARRAY &&
        GET_TYPE(b) == VAL_OBJECT && GET_OBJ_TYPE(b) == OBJ_PACKED_ARRAY) {
        PackedArray *pa = AS_PACKED_ARRAY(a), *pb = AS_PACKED_ARRAY(b);
        if (pa->type == pb->type && pa->size == pb->size) {
            int sz = pa->size;
            if (pa->type == PACKED_FLOAT64) {
                double *da = (double*)pa->data, *db = (double*)pb->data;
                double sum = 0;
int i = 0;
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
__m256d sum_vec = _mm256_setzero_pd();
for (; i <= sz - 4; i += 4) {
    sum_vec = _mm256_add_pd(sum_vec, _mm256_mul_pd(_mm256_loadu_pd(&da[i]), _mm256_loadu_pd(&db[i])));
}
double temp[4];
_mm256_storeu_pd(temp, sum_vec);
sum = temp[0] + temp[1] + temp[2] + temp[3];
#endif
                for (; i < sz; i++) sum += da[i] * db[i];
                return value_float(sum);
            } else {
                int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, sum = 0;
                for (int i=0; i<sz; i++) sum += ia[i] * ib[i];
                return value_int(sum);
            }
        }
    }
    return value_none();
}

static Value native_sqlite_open(int argc, Value *args) {
    if (argc >= 2 && GET_TYPE(args[0]) == VAL_DICT) { argc--; args++; }
    if (argc < 1) return value_none();
    sqlite3 *db;
    const char *path = AS_STRING(args[0]);
    if (sqlite3_open(path, &db) != SQLITE_OK) return value_none();
    SQLiteHandle *h = gc_alloc(sizeof(SQLiteHandle), OBJ_SQLITE_HANDLE);
    h->db = db;
    if (trace_mode) printf("DEBUG: sqlite.open(%s) -> Handle %p, Object %p\n", path, (void*)db, (void*)h);
    return make_value(VAL_OBJECT, (uintptr_t)h);
}

static Value native_sqlite_exec(int argc, Value *args) {
    if (argc >= 3 && GET_TYPE(args[0]) == VAL_DICT) { argc--; args++; }
    if (argc < 2) return value_int(-1);
    if (GET_TYPE(args[0]) != VAL_OBJECT || GET_OBJ_TYPE(args[0]) != OBJ_SQLITE_HANDLE) {
        printf("Error: sqlite.exec requires a SQLite handle. Raw: 0x%lx (Type: %d, ObjType: %d)\n", 
               (unsigned long)args[0], (int)GET_TYPE(args[0]), (args[0] & MASK_PAYLOAD) ? (int)GET_OBJ_TYPE(args[0]) : -1);
        return value_int(-2);
    }
    SQLiteHandle *h = (SQLiteHandle*)(uintptr_t)(args[0] & MASK_PAYLOAD);
    sqlite3 *db = (sqlite3*)h->db;
    const char *sql = AS_STRING(args[1]);
    char *err_msg = 0;
    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) { 
        if (err_msg) { printf("SQLite Error: %s\n", err_msg); sqlite3_free(err_msg); }
        return value_int(rc); 
    }
    return value_int(0);
}

static Value native_sqlite_step_query(int argc, Value *args) {
    if (argc >= 3 && GET_TYPE(args[0]) == VAL_DICT) { argc--; args++; }
    if (argc < 2) return value_none();
    if (GET_TYPE(args[0]) != VAL_OBJECT || GET_OBJ_TYPE(args[0]) != OBJ_SQLITE_HANDLE) return value_none();
    SQLiteHandle *h = (SQLiteHandle*)(uintptr_t)(args[0] & MASK_PAYLOAD);
    sqlite3 *db = (sqlite3*)h->db;
    const char *sql = AS_STRING(args[1]);
    sqlite3_stmt *res;
    if (sqlite3_prepare_v2(db, sql, -1, &res, 0) != SQLITE_OK) return value_none();
    
    SQLiteIter *it = gc_alloc(sizeof(SQLiteIter), OBJ_SQLITE_ITER);
    it->stmt = res;
    it->done = 0;
    return make_value(VAL_OBJECT, (uintptr_t)it);
}

static Value native_sqlite_query(int argc, Value *args) {
    if (argc >= 3 && GET_TYPE(args[0]) == VAL_DICT) { argc--; args++; }
    if (argc < 2) return value_none();
    if (GET_TYPE(args[0]) != VAL_OBJECT || GET_OBJ_TYPE(args[0]) != OBJ_SQLITE_HANDLE) return value_none();
    SQLiteHandle *h = (SQLiteHandle*)(uintptr_t)(args[0] & MASK_PAYLOAD);
    sqlite3 *db = (sqlite3*)h->db;
    const char *sql = AS_STRING(args[1]);
    sqlite3_stmt *res;
    if (sqlite3_prepare_v2(db, sql, -1, &res, 0) != SQLITE_OK) return value_none();

    List *rows = list_new(8);
    int cols = sqlite3_column_count(res);
    
    vm_stack[vm_stack_top++] = value_list(rows);

    while (sqlite3_step(res) == SQLITE_ROW) {
        Dict *row = dict_new(cols);
        vm_stack[vm_stack_top++] = value_dict(row);
        for (int i = 0; i < cols; i++) {
            const char *col_name = sqlite3_column_name(res, i);
            const char *interned = intern_string(col_name);
            Value val = value_none();
            int type = sqlite3_column_type(res, i);
            if (type == SQLITE_INTEGER) val = value_int(sqlite3_column_int64(res, i));
            else if (type == SQLITE_FLOAT) val = value_float(sqlite3_column_double(res, i));
            else if (type == SQLITE_TEXT) {
                Value s_val = value_string_copy((const char*)sqlite3_column_text(res, i));
                vm_stack[vm_stack_top++] = s_val;
                val = s_val;
                vm_stack_top--;
            }
            dict_set(row, interned, val);
        }
        list_push(rows, value_dict(row));
        vm_stack_top--;
    }
    sqlite3_finalize(res);
    vm_stack_top--;
    return value_list(rows);
}

static Value native_redis_connect(int argc, Value *args) {
    if (argc < 2) return value_none();
    const char *host = AS_STRING(args[0]);
    int port = (int)AS_INT(args[1]);
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    struct hostent *server = gethostbyname(host);
    if (!server) return value_none();
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) return value_none();
    return value_int(sockfd);
}

static Value native_redis_set(int argc, Value *args) {
    if (argc < 3) return value_none();
    int fd = (int)AS_INT(args[0]);
    const char *key = AS_STRING(args[1]);
    const char *val = AS_STRING(args[2]);
    char cmd[1024];
    int len = sprintf(cmd, "*3\r\n$3\r\nSET\r\n$%zu\r\n%s\r\n$%zu\r\n%s\r\n", strlen(key), key, strlen(val), val);
    if (write(fd, cmd, len) < 0) return value_bool(0);
    char buf[128]; 
    if (read(fd, buf, 127) <= 0) return value_bool(0);
    return value_bool(buf[0] == '+');
}

static Value native_redis_get(int argc, Value *args) {
    if (argc < 2) return value_none();
    int fd = (int)AS_INT(args[0]);
    const char *key = AS_STRING(args[1]);
    char cmd[1024];
    int len = sprintf(cmd, "*2\r\n$3\r\nGET\r\n$%zu\r\n%s\r\n", strlen(key), key);
    if (write(fd, cmd, len) < 0) return value_none();
    
    char head[1];
    if (read(fd, head, 1) <= 0) return value_none();
    if (head[0] == '$') {
        char len_buf[32]; int i=0;
        while (i < 31 && read(fd, &len_buf[i], 1) > 0 && len_buf[i] != '\r') i++;
        len_buf[i] = 0;
        int bulk_len = atoi(len_buf);
        if (bulk_len == -1) { char nl[1]; if (read(fd, nl, 1) > 0) {} return value_none(); }
        char nl[1]; if (read(fd, nl, 1) > 0) {} // skip \n
        
        char *data = malloc(bulk_len + 1);
        int total = 0;
        while (total < bulk_len) {
            int n = read(fd, data + total, bulk_len - total);
            if (n <= 0) break;
            total += n;
        }
        data[total] = 0;
        char crlf[2]; if (read(fd, crlf, 2) > 0) {} // skip \r\n
        Value res = value_string_copy(data);
        free(data);
        return res;
    }
    return value_none();
}

static Value native_sqlite_close(int argc, Value *args) {
    if (argc >= 2 && GET_TYPE(args[0]) == VAL_DICT) { argc--; args++; }
    if (argc < 1) return value_int(-1);
    if (GET_TYPE(args[0]) != VAL_OBJECT || GET_OBJ_TYPE(args[0]) != OBJ_SQLITE_HANDLE) return value_int(-2);
    SQLiteHandle *h = (SQLiteHandle*)(uintptr_t)(args[0] & MASK_PAYLOAD);
    return value_int(sqlite3_close(h->db));
}

static Value native_stacktrace(int argc, Value *args) {
    (void)argc; (void)args;
    print_stack_trace();
    return value_none();
}

static Value native_profile_start(int argc, Value *args) {
    (void)argc; (void)args;
    profiling_active = 1;
    return value_none();
}

static int profile_compare(const void *a, const void *b) {
    typedef struct { const char *file; int line; long count; } ProfileEntry;
    return (int)(((ProfileEntry*)b)->count - ((ProfileEntry*)a)->count);
}

static Value native_profile_stop(int argc, Value *args) {
    (void)argc; (void)args;
    profiling_active = 0;
    typedef struct { const char *file; int line; long count; } ProfileEntry;
    int max_entries = 10000;
    ProfileEntry *entries = malloc(sizeof(ProfileEntry) * max_entries);
    int count = 0;

    // Collect from func_table
    for (int i=0; i<func_table_size; i++) {
        if (func_table[i].used && func_table[i].body) {
            Bytecode *bc = func_table[i].body;
            for (int j=0; j<bc->count; j++) {
                Instruction *pc = &bc->code[j];
                if (pc->exec_count > 0 && pc->line > 0 && pc->file) {
                     int found = -1;
                     for (int k=0; k<count; k++) {
                         if (entries[k].line == pc->line && strcmp(entries[k].file, pc->file) == 0) {
                             found = k; break;
                         }
                     }
                     if (found != -1) entries[found].count += pc->exec_count;
                     else if (count < max_entries) {
                         entries[count].file = pc->file;
                         entries[count].line = pc->line;
                         entries[count].count = pc->exec_count;
                         count++;
                     }
                }
            }
        }
    }
    // Collect from main_fe
    if (main_fe.body) {
        Bytecode *bc = main_fe.body;
        for (int j=0; j<bc->count; j++) {
            Instruction *pc = &bc->code[j];
            if (pc->exec_count > 0 && pc->line > 0 && pc->file) {
                 int found = -1;
                 for (int k=0; k<count; k++) {
                     if (entries[k].line == pc->line && strcmp(entries[k].file, pc->file) == 0) {
                         found = k; break;
                     }
                 }
                 if (found != -1) entries[found].count += pc->exec_count;
                 else if (count < max_entries) {
                     entries[count].file = pc->file;
                     entries[count].line = pc->line;
                     entries[count].count = pc->exec_count;
                     count++;
                 }
            }
        }
    }

    qsort(entries, count, sizeof(ProfileEntry), profile_compare);

    printf("\n--- Performance Hot-Spots ---\n");
    printf("  %10s | %s\n", "Executions", "Source Location");
    printf("  -----------+------------------\n");
    for (int i=0; i<count && i < 10; i++) {
        printf("  %10ld | %s:%d\n", entries[i].count, entries[i].file, entries[i].line);
        print_error_line(entries[i].file, entries[i].line);
    }
    printf("------------------------------\n");

    free(entries);
    return value_none();
}

void print_error_trace(Value exception);

static Value os_sleep(int argc, Value *args) {
    if (argc > 0) {
        int ms = (int)AS_INT(args[0]);
        // Drop GIL while sleeping so other threads can run
        pthread_mutex_unlock(&glow_gil);
        usleep(ms * 1000);
        pthread_mutex_lock(&glow_gil);
    }
    return value_none();
}

static Value native_tensor_create(int argc, Value *args) {
    if (argc < 1 || (GET_TYPE(args[0]) != VAL_INT && GET_TYPE(args[0]) != VAL_FLOAT)) return value_none();
    int sz = (int)val_to_double(args[0]);
    if (sz < 0 || sz > 10000000) return value_none();
    return value_packed(packed_array_new(PACKED_FLOAT64, sz));
}

static Value native_tensor_zeros(int argc, Value *args) {
    if (argc < 1 || (GET_TYPE(args[0]) != VAL_INT && GET_TYPE(args[0]) != VAL_FLOAT)) return value_none();
    int sz = (int)val_to_double(args[0]);
    if (sz < 0 || sz > 10000000) return value_none();
    PackedArray *pa = packed_array_new(PACKED_FLOAT64, sz);
    if (pa->data && sz > 0) {
        memset(pa->data, 0, sz * sizeof(double));
    }
    return value_packed(pa);
}

static Value native_tensor_get(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_OBJECT || GET_OBJ_TYPE(args[0]) != OBJ_PACKED_ARRAY || (GET_TYPE(args[1]) != VAL_INT && GET_TYPE(args[1]) != VAL_FLOAT)) {
        return value_none();
    }
    int idx = (int)val_to_double(args[1]);
    PackedArray *pa = AS_PACKED_ARRAY(args[0]);
    if (idx < 0 || idx >= pa->size) return value_none();
    return packed_array_get(pa, idx);
}

static Value native_tensor_set(int argc, Value *args) {
    if (argc < 3 || GET_TYPE(args[0]) != VAL_OBJECT || GET_OBJ_TYPE(args[0]) != OBJ_PACKED_ARRAY || (GET_TYPE(args[1]) != VAL_INT && GET_TYPE(args[1]) != VAL_FLOAT)) {
        return value_none();
    }
    int idx = (int)val_to_double(args[1]);
    PackedArray *pa = AS_PACKED_ARRAY(args[0]);
    if (idx >= 0 && idx < pa->size) {
        packed_array_set(pa, idx, args[2]);
    }
    return value_none();
}

static Value native_tensor_transpose(int argc, Value *args) {
    if (argc < 3) return value_none(); // tensor, rows, cols
    if (GET_TYPE(args[0]) != VAL_OBJECT || !(args[0] & MASK_PAYLOAD) || GET_OBJ_TYPE(args[0]) != OBJ_PACKED_ARRAY) return value_none();
    PackedArray *src = AS_PACKED_ARRAY(args[0]);
    int rows = (int)val_to_double(args[1]), cols = (int)val_to_double(args[2]);
    if (rows < 0 || cols < 0 || rows * cols > src->size) return value_none();
    PackedArray *res = packed_array_new(src->type, src->size);
    if (!res->data) return value_none();
    if (src->type == PACKED_FLOAT64) {
        double *s = (double*)src->data, *r = (double*)res->data;
        for (int i = 0; i < rows; i++) for (int j = 0; j < cols; j++) r[j * rows + i] = s[i * cols + j];
    }
    return value_packed(res);
}

#if defined(__x86_64__) || defined(_M_X64)
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

static Value native_tensor_matmul(int argc, Value *args) {
    if (argc < 6) return value_none(); // A, rowsA, colsA, B, rowsB, colsB
    if (GET_TYPE(args[0]) != VAL_OBJECT || !(args[0] & MASK_PAYLOAD) || GET_OBJ_TYPE(args[0]) != OBJ_PACKED_ARRAY) return value_none();
    if (GET_TYPE(args[3]) != VAL_OBJECT || !(args[3] & MASK_PAYLOAD) || GET_OBJ_TYPE(args[3]) != OBJ_PACKED_ARRAY) return value_none();
    
    PackedArray *pa = AS_PACKED_ARRAY(args[0]);
    int rA = (int)val_to_double(args[1]), cA = (int)val_to_double(args[2]);
    PackedArray *pb = AS_PACKED_ARRAY(args[3]);
    int rB = (int)val_to_double(args[4]), cB = (int)val_to_double(args[5]);
    
    if (rA < 0 || cA < 0 || rB < 0 || cB < 0) return value_none();
    if (rA * cA > pa->size || rB * cB > pb->size) return value_none();
    if (cA != rB) return value_none();
    
    PackedArray *res = packed_array_new(pa->type, rA * cB);
    if (!res || !res->data) return value_none();
    double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
    
    for (int i = 0; i < rA; i++) {
        for (int j = 0; j < cB; j++) {
            double sum = 0;
int k = 0;
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
__m256d sum_vec = _mm256_setzero_pd();
for (; k <= cA - 4; k += 4) {
    __m256d va = _mm256_loadu_pd(&da[i * cA + k]);
    __m256d vb = _mm256_set_pd(db[(k+3) * cB + j], db[(k+2) * cB + j], db[(k+1) * cB + j], db[k * cB + j]);
    sum_vec = _mm256_add_pd(sum_vec, _mm256_mul_pd(va, vb));
}
double temp[4];
_mm256_storeu_pd(temp, sum_vec);
sum = temp[0] + temp[1] + temp[2] + temp[3];
#endif
            for (; k < cA; k++) sum += da[i * cA + k] * db[k * cB + j];
            dr[i * cB + j] = sum;
        }
    }
    return value_packed(res);
}
#else
static Value native_tensor_matmul(int argc, Value *args) {
    if (argc < 6) return value_none();
    if (GET_TYPE(args[0]) != VAL_OBJECT || !(args[0] & MASK_PAYLOAD) || GET_OBJ_TYPE(args[0]) != OBJ_PACKED_ARRAY) return value_none();
    if (GET_TYPE(args[3]) != VAL_OBJECT || !(args[3] & MASK_PAYLOAD) || GET_OBJ_TYPE(args[3]) != OBJ_PACKED_ARRAY) return value_none();
    
    PackedArray *pa = AS_PACKED_ARRAY(args[0]);
    int rA = (int)val_to_double(args[1]), cA = (int)val_to_double(args[2]);
    PackedArray *pb = AS_PACKED_ARRAY(args[3]);
    int rB = (int)val_to_double(args[4]), cB = (int)val_to_double(args[5]);
    
    if (rA < 0 || cA < 0 || rB < 0 || cB < 0) return value_none();
    if (rA * cA > pa->size || rB * cB > pb->size) return value_none();
    if (cA != rB) return value_none();
    
    PackedArray *res = packed_array_new(pa->type, rA * cB);
    if (!res || !res->data) return value_none();
    double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
    
    for (int i = 0; i < rA; i++) {
        for (int j = 0; j < cB; j++) {
            double sum = 0.0;
            for (int k = 0; k < cA; k++) {
                sum += da[i * cA + k] * db[k * cB + j];
            }
            dr[i * cB + j] = sum;
        }
    }
    return value_packed(res);
}
#endif

#ifndef _WIN32
#include <X11/Xlib.h>
#include <X11/Xutil.h>

static Display *gui_display = NULL;
static Window gui_window;
static GC gui_gc;
static int gui_screen;

static Value native_gui_open(int argc, Value *args) {
    (void)argc; (void)args;
    gui_display = XOpenDisplay(NULL);
    if (!gui_display) return value_bool(0);
    gui_screen = DefaultScreen(gui_display);
    gui_window = XCreateSimpleWindow(gui_display, RootWindow(gui_display, gui_screen), 10, 10, 800, 600, 1,
                                   BlackPixel(gui_display, gui_screen), WhitePixel(gui_display, gui_screen));
    XSelectInput(gui_display, gui_window, ExposureMask | KeyPressMask | ButtonPressMask);
    XMapWindow(gui_display, gui_window);
    gui_gc = XCreateGC(gui_display, gui_window, 0, NULL);
    return value_bool(1);
}

static Value native_gui_clear(int argc, Value *args) {
    (void)argc; (void)args;
    if (!gui_display) return value_none();
    XClearWindow(gui_display, gui_window);
    return value_none();
}

static Value native_gui_rect(int argc, Value *args) {
    if (argc < 4 || !gui_display) return value_none();
    int x = (int)val_to_double(args[0]), y = (int)val_to_double(args[1]);
    int w = (int)val_to_double(args[2]), h = (int)val_to_double(args[3]);
    XFillRectangle(gui_display, gui_window, gui_gc, x, y, w, h);
    XFlush(gui_display);
    return value_none();
}
#else
static Value native_gui_open(int argc, Value *args) { (void)argc; (void)args; return value_bool(0); }
static Value native_gui_clear(int argc, Value *args) { (void)argc; (void)args; return value_none(); }
static Value native_gui_rect(int argc, Value *args) { (void)argc; (void)args; return value_none(); }
#endif

static Value native_gui_poll(int argc, Value *args) {
    (void)argc; (void)args;
    if (!gui_display) return value_none();
    if (XPending(gui_display)) {
        XEvent ev; XNextEvent(gui_display, &ev);
        if (ev.type == KeyPress) return value_string_copy("key");
        if (ev.type == ButtonPress) return value_string_copy("click");
    }
    return value_none();
}

static Value native_collision_aabb(int argc, Value *args) {
    if (argc < 8) return value_bool(0);
    double x1 = val_to_double(args[0]), y1 = val_to_double(args[1]), w1 = val_to_double(args[2]), h1 = val_to_double(args[3]);
    double x2 = val_to_double(args[4]), y2 = val_to_double(args[5]), w2 = val_to_double(args[6]), h2 = val_to_double(args[7]);
    return value_bool(x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}

static Value math_vec2_add(int argc, Value *args) {
    if (argc < 4) return value_none();
    double x1 = val_to_double(args[0]), y1 = val_to_double(args[1]), x2 = val_to_double(args[2]), y2 = val_to_double(args[3]);
    List *l = list_new(2); list_push(l, value_from_double(x1 + x2)); list_push(l, value_from_double(y1 + y2));
    return value_list(l);
}

static Value math_vec2_dist(int argc, Value *args) {
    if (argc < 4) return value_none();
    double x1 = val_to_double(args[0]), y1 = val_to_double(args[1]), x2 = val_to_double(args[2]), y2 = val_to_double(args[3]);
    return value_from_double(sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2)));
}

static Value native_fs_readdir(int argc, Value *args) {
    if (argc < 1) return value_none();
    DIR *d = opendir(AS_STRING(args[0]));
    if (!d) return value_none();
    List *l = list_new(8);
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) { if (dir->d_name[0] != '.') list_push(l, value_string_copy(dir->d_name)); }
    closedir(d);
    return value_list(l);
}

static Value native_fs_stat(int argc, Value *args) {
    if (argc < 1) return value_none();
    struct stat st;
    if (stat(AS_STRING(args[0]), &st) != 0) return value_none();
    Dict *d = dict_new(4);
    dict_set(d, "size", value_int(st.st_size));
    dict_set(d, "is_dir", value_bool(S_ISDIR(st.st_mode)));
    return value_dict(d);
}

static Value native_fs_exists(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_bool(0);
    struct stat st;
    return value_bool(stat(AS_STRING(args[0]), &st) == 0);
}

static Value native_fs_size(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_int(0);
    struct stat st;
    if (stat(AS_STRING(args[0]), &st) != 0) return value_int(0);
    return value_int(st.st_size);
}

static Value native_fs_mkdir(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_bool(0);
    return value_bool(mkdir(AS_STRING(args[0]), 0777) == 0);
}

static Value native_fs_remove(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_bool(0);
    const char *path = AS_STRING(args[0]);
    struct stat st;
    if (stat(path, &st) != 0) return value_bool(0);
    if (S_ISDIR(st.st_mode)) {
        return value_bool(rmdir(path) == 0);
    } else {
        return value_bool(unlink(path) == 0);
    }
}

static Value native_fs_write(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_STRING || GET_TYPE(args[1]) != VAL_STRING) return value_bool(0);
    FILE *f = fopen(AS_STRING(args[0]), "w");
    if (!f) return value_bool(0);
    fputs(AS_STRING(args[1]), f);
    fclose(f);
    return value_bool(1);
}

static Value native_fs_read(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_none();
    FILE *f = fopen(AS_STRING(args[0]), "r");
    if (!f) return value_none();
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return value_none(); }
    char *buf = malloc(sz + 1);
    size_t read_bytes = fread(buf, 1, sz, f);
    buf[read_bytes] = '\0';
    fclose(f);
    Value res = value_string_copy(buf);
    free(buf);
    return res;
}

static Value native_os_getenv(int argc, Value *args) {
    if (argc < 1) return value_none();
    char *v = getenv(AS_STRING(args[0]));
    return v ? value_string_copy(v) : value_none();
}

static Value native_string_builder_new(int argc, Value *args) {
    (void)argc; (void)args;
    StringBuilder *sb = gc_alloc(sizeof(StringBuilder), OBJ_STRING_BUILDER);
    sb->capacity = 32;
    sb->size = 0;
    sb->buffer = malloc(sb->capacity);
    sb->buffer[0] = '\0';
    return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)sb);
}

static Value native_string_builder_append(int argc, Value *args) {
    if (argc < 2) return args[0];
    if (GET_TYPE(args[0]) == VAL_LIST) {
        list_push(AS_LIST(args[0]), args[1]);
        return args[0];
    }
    StringBuilder *sb = (StringBuilder*)(uintptr_t)(args[0] & MASK_PAYLOAD);
    const char *s = AS_STRING(args[1]);
    int len = strlen(s);
    if (sb->size + len >= sb->capacity) {
        sb->capacity = (sb->size + len + 1) * 2;
        sb->buffer = realloc(sb->buffer, sb->capacity);
    }
    memcpy(sb->buffer + sb->size, s, len);
    sb->size += len;
    sb->buffer[sb->size] = '\0';
    return args[0];
}

static Value native_string_builder_build(int argc, Value *args) {
    (void)argc; (void)args;
    StringBuilder *sb = (StringBuilder*)(uintptr_t)(args[0] & MASK_PAYLOAD);
    return value_string_copy(sb->buffer);
}

static Value native_type(int argc, Value *args) {
    if (argc < 1) return value_none();
    uint64_t t = GET_TYPE(args[0]);
    if (t == VAL_INT) return value_string_copy("int");
    if (t == VAL_FLOAT) return value_string_copy("float");
    if (t == VAL_BOOL) return value_string_copy("bool");
    if (t == VAL_STRING) return value_string_copy("string");
    if (t == VAL_LIST) return value_string_copy("list");
    if (t == VAL_DICT) return value_string_copy("dict");
    if (t == VAL_OBJECT) {
        if (!(args[0] & MASK_PAYLOAD)) return value_string_copy("object");
        ObjType ot = GET_OBJ_TYPE(args[0]);
        if (ot == OBJ_CLASS) return value_string_copy("class");
        if (ot == OBJ_INSTANCE) return value_string_copy("instance");
        if (ot == OBJ_BOUND_METHOD) return value_string_copy("method");
        if (ot == OBJ_NATIVE_FUNC) return value_string_copy("native_func");
        return value_string_copy("object");
    }
    return value_string_copy("unknown");
}

static Value native_dump_stack(int argc, Value *args) {
    int start = 0;
    if (argc > 0) start = (int)AS_INT(args[0]);
    int count = 20;
    if (argc > 1) count = (int)AS_INT(args[1]);
    printf("DEBUG STACK [abs %d to %d]:\n", start, start + count);
    for (int i=0; i<count; i++) {
        int idx = start + i;
        if (idx >= vm_stack_size) break;
        Value v = vm_stack[idx];
        printf("  [%d]: ", idx);
        if (v == 0) printf("nil\n");
        else if (((v) & MASK_NAN) != MASK_NAN) printf("float(%g)\n", AS_FLOAT(v));
        else {
            uint64_t t = GET_TYPE(v);
            if (t == VAL_INT) printf("int(%ld)\n", (long)AS_INT(v));
            else if (t == VAL_STRING) printf("string(\"%s\")\n", AS_STRING(v));
            else if (t == VAL_BOOL) printf("bool(%s)\n", AS_BOOL(v) ? "true" : "false");
            else if (t == VAL_LIST) printf("list(size=%d)\n", AS_LIST(v)->size);
            else if (t == VAL_DICT) printf("dict(size=%d)\n", AS_DICT(v)->size);
            else if (t == VAL_OBJECT) printf("obj(type=%d)\n", (int)GET_OBJ_TYPE(v));
            else printf("unknown(0x%lx)\n", (unsigned long)v);
        }
    }
    return value_none();
}

void std_init(void) {
    var_set("len", native_func_new("len", native_len));
    var_set("type", native_func_new("type", native_type));
    var_set("range", native_func_new("range", native_range));
    
    Dict *debug = dict_new(4);
    dict_set(debug, "dump_stack", native_func_new("dump_stack", native_dump_stack));
    dict_set(debug, "stacktrace", native_func_new("stacktrace", native_stacktrace));
    dict_set(debug, "profile_start", native_func_new("profile_start", native_profile_start));
    dict_set(debug, "profile_stop", native_func_new("profile_stop", native_profile_stop));
    var_set("debug", value_dict(debug));
    
    var_set("packed_int", native_func_new("packed_int", native_packed_int));
    var_set("packed_float", native_func_new("packed_float", native_packed_float));
    var_set("packed_add", native_func_new("packed_add", native_packed_add));
    var_set("packed_sub", native_func_new("packed_sub", native_packed_sub));
    var_set("net_close", native_func_new("net_close", net_close));
    var_set("text_builder", native_func_new("text_builder", native_string_builder_new));
    var_set("append", native_func_new("append", native_string_builder_append));
    var_set("build", native_func_new("build", native_string_builder_build));

    Dict *math = dict_new(4);
    dict_set(math, "sin", native_func_new("sin", math_sin));
    dict_set(math, "cos", native_func_new("cos", math_cos));
    dict_set(math, "sqrt", native_func_new("sqrt", math_sqrt));
    dict_set(math, "pow", native_func_new("pow", math_pow));
    dict_set(math, "vec2_add", native_func_new("vec2_add", math_vec2_add));
    dict_set(math, "vec2_dist", native_func_new("vec2_dist", math_vec2_dist));
    var_set("math", value_dict(math));

    Dict *collision = dict_new(4);
    dict_set(collision, "aabb", native_func_new("aabb", native_collision_aabb));
    var_set("collision", value_dict(collision));

    Dict *crypto = dict_new(4);
    dict_set(crypto, "sha256", native_func_new("sha256", crypto_sha256));
    var_set("crypto", value_dict(crypto));

    Dict *tensor = dict_new(4);
    dict_set(tensor, "create", native_func_new("create", native_tensor_create));
    dict_set(tensor, "zeros", native_func_new("zeros", native_tensor_zeros));
    dict_set(tensor, "get", native_func_new("get", native_tensor_get));
    dict_set(tensor, "set", native_func_new("set", native_tensor_set));
    dict_set(tensor, "add", native_func_new("add", native_packed_add));
    dict_set(tensor, "sub", native_func_new("sub", native_packed_sub));
    dict_set(tensor, "mul", native_func_new("mul", native_packed_mul));
    dict_set(tensor, "dot", native_func_new("dot", native_packed_dot));
    dict_set(tensor, "transpose", native_func_new("transpose", native_tensor_transpose));
    dict_set(tensor, "matmul", native_func_new("matmul", native_tensor_matmul));
    var_set("tensor", value_dict(tensor));

    Dict *sqlite = dict_new(4);
    dict_set(sqlite, "open", native_func_new("open", native_sqlite_open));
    dict_set(sqlite, "exec", native_func_new("exec", native_sqlite_exec));
    dict_set(sqlite, "query", native_func_new("query", native_sqlite_query));
    dict_set(sqlite, "step_query", native_func_new("step_query", native_sqlite_step_query));
    dict_set(sqlite, "close", native_func_new("close", native_sqlite_close));
    var_set("sqlite", value_dict(sqlite));

    Dict *redis = dict_new(4);
    dict_set(redis, "connect", native_func_new("connect", native_redis_connect));
    dict_set(redis, "set", native_func_new("set", native_redis_set));
    dict_set(redis, "get", native_func_new("get", native_redis_get));
    var_set("redis", value_dict(redis));

    Dict *gui = dict_new(4);
    dict_set(gui, "open", native_func_new("open", native_gui_open));
    dict_set(gui, "clear", native_func_new("clear", native_gui_clear));
    dict_set(gui, "draw_rect", native_func_new("draw_rect", native_gui_rect));
    dict_set(gui, "poll", native_func_new("poll", native_gui_poll));
    var_set("gui", value_dict(gui));

    Dict *os = dict_new(4); 
    dict_set(os, "exit", native_func_new("exit", os_exit)); 
    dict_set(os, "time", native_func_new("time", os_time)); 
    dict_set(os, "sleep", native_func_new("sleep", os_sleep)); 
    dict_set(os, "getenv", native_func_new("getenv", native_os_getenv));
    dict_set(os, "gc_info", native_func_new("gc_info", native_os_gc_info));
    dict_set(os, "verify_gc", native_func_new("verify_gc", native_verify_gc));
    dict_set(os, "system", native_func_new("system", os_system));
    var_set("os", value_dict(os));

    Dict *fs = dict_new(4);
    dict_set(fs, "readdir", native_func_new("readdir", native_fs_readdir));
    dict_set(fs, "stat", native_func_new("stat", native_fs_stat));
    dict_set(fs, "exists", native_func_new("exists", native_fs_exists));
    dict_set(fs, "size", native_func_new("size", native_fs_size));
    dict_set(fs, "mkdir", native_func_new("mkdir", native_fs_mkdir));
    dict_set(fs, "remove", native_func_new("remove", native_fs_remove));
    dict_set(fs, "write", native_func_new("write", native_fs_write));
    dict_set(fs, "read", native_func_new("read", native_fs_read));
    dict_set(fs, "list", native_func_new("list", native_fs_readdir));
    var_set("fs", value_dict(fs));

    Dict *net = dict_new(4); dict_set(net, "listen", native_func_new("listen", net_listen)); dict_set(net, "close", native_func_new("close", net_close)); var_set("net", value_dict(net));
    Dict *http = dict_new(4); 
    dict_set(http, "listen", native_func_new("listen", http_listen)); 
    dict_set(http, "accept", native_func_new("accept", http_accept)); 
    dict_set(http, "read", native_func_new("read", http_read)); 
    dict_set(http, "send", native_func_new("send", http_write)); 
    dict_set(http, "close", native_func_new("close", http_close)); 
    dict_set(http, "request", native_func_new("request", http_request_native));
    var_set("http", value_dict(http));
    Dict *ssl = dict_new(4); dict_set(ssl, "listen", native_func_new("listen", ssl_listen)); dict_set(ssl, "accept", native_func_new("accept", ssl_accept)); var_set("ssl", value_dict(ssl));
    Dict *json = dict_new(4); 
    dict_set(json, "stringify", native_func_new("stringify", native_json_stringify)); 
    dict_set(json, "parse", native_func_new("parse", native_json_parse));
    var_set("json", value_dict(json));
}

static void func_table_cleanup(void) {
    if (func_table) {
        for (int i = 0; i < func_table_size; i++) {
            if (func_table[i].used) {
                func_entry_cleanup(&func_table[i]);
            }
        }
        free(func_table);
        func_table = NULL;
    }
    func_entry_cleanup(&main_fe);
}

void std_cleanup(void) {
    // 1. Clear all global table values so they can be GC'd
    for (int i = 0; i < global_table.count; i++) {
        global_table.values[i] = value_none();
    }
    
    // 2. Clear VM stack values and reset top to allow them to be GC'd
    if (vm_stack) {
        for (int i = 0; i < vm_stack_size; i++) {
            vm_stack[i] = value_none();
        }
    }
    vm_stack_top = 0;

    // 3. Clear pending exceptions
    pending_exception = value_none();

    // 4. Perform a full garbage collection sweep to free now-unreferenced objects
    gc_full_collect();
    
    // 3. Free the global_table allocations
    if (global_table.names) {
        free(global_table.names);
        global_table.names = NULL;
    }
    if (global_table.values) {
        free(global_table.values);
        global_table.values = NULL;
    }
    global_table.count = 0;
    global_table.capacity = 0;

    // 4. Clean up main thread VM stack
    vm_stack_cleanup();

    // 5. Clean up func table
    func_table_cleanup();

    // 6. Clean up string interning and thread-local arena
    intern_cleanup();
    if (stack_arena) {
        free(stack_arena);
        stack_arena = NULL;
    }
    stack_arena_used = 0;
    arena_free_all();
    lexer_cleanup();
}
