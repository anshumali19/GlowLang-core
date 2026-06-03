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
#include <netdb.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "glowlang.h"
#include "lsp.h"
#include "jit.h"
#include "opt.h"

/* VM State */
static __thread Value *vm_stack = NULL;
static __thread int vm_stack_size = 0;
static __thread int sp = 0;
static __thread int vm_stack_top = 0; // The highest active register for GC scanning

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
static Value run_bytecode(Bytecode *bc, Instruction *start_pc, Closure *current_closure, int base_reg);
static void var_set(const char *name, Value value);
static Value var_get(const char *name);
FuncEntry* func_get(const char *name);
static Instance* instance_new(Class *k);
static Value bound_method_new(Value instance, FuncEntry *method);

Value jit_call_helper(FuncEntry *fe, int argc, Value *args, Instruction *pc) {
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
            return jit_call_helper(bm->method, argc, args, pc);
        }
        if (ot == OBJ_CLOSURE) {
            Closure *c = AS_CLOSURE(callee);
            if (c->body) return run_bytecode(c->body, NULL, c, (int)(args - vm_stack));
        }
        if (ot == OBJ_NATIVE_FUNC) return AS_NATIVE_FUNC(callee)->func(argc, args);
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

void simd_add_helper(Value v_a, Value v_b, Value v_target) {
    PackedArray *pa = AS_PACKED_ARRAY(v_a), *pb = AS_PACKED_ARRAY(v_b), *pt = AS_PACKED_ARRAY(v_target);
    int size = pa->size;
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
__thread CallFrame call_stack[MAX_CALL_STACK];
__thread int call_stack_ptr = 0;

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

static int val_eq(Value a, Value b) {
    if (a == b) return 1;
    if (((a) & MASK_NAN) != MASK_NAN && ((b) & MASK_NAN) != MASK_NAN) return AS_FLOAT(a) == AS_FLOAT(b);
    uint64_t ta = ((a) & MASK_NAN) != MASK_NAN ? VAL_FLOAT : ((a) & MASK_TYPE) >> 48;
    uint64_t tb = ((b) & MASK_NAN) != MASK_NAN ? VAL_FLOAT : ((b) & MASK_TYPE) >> 48;
    if (ta == VAL_INT && tb == VAL_FLOAT) return (double)AS_INT(a) == AS_FLOAT(b);
    if (ta == VAL_FLOAT && tb == VAL_INT) return AS_FLOAT(a) == (double)AS_INT(b);
    if (ta == VAL_STRING && tb == VAL_STRING) return string_compare_simd(AS_STRING(a), AS_STRING(b));
    return 0;
}

static __thread Value pending_exception = 0;
static __thread int has_exception = 0;

static int value_truthy(Value v) {
    if ((v & MASK_NAN) != MASK_NAN) return AS_FLOAT(v) != 0.0;
    uint64_t type = (v & MASK_TYPE) >> 48;
    if (type == VAL_INT) return AS_INT(v) != 0;
    if (type == VAL_BOOL) return AS_BOOL(v);
    if (type == VAL_STRING) return AS_STRING(v) && AS_STRING(v)[0] != '\0';
    if (type == VAL_LIST) return AS_LIST(v) && AS_LIST(v)->size > 0;
    return 0;
}

static Value value_string_copy(const char *s) {
    size_t len = strlen(s);
    GCString *gs = gc_alloc(sizeof(GCString) + len + 1, OBJ_STRING);
    gs->size = (int)len;
    if (len >= 32) {
        size_t i = 0;
        for (; i + 32 <= len; i += 32) {
            __m256i v = _mm256_loadu_si256((__m256i*)&s[i]);
            _mm256_storeu_si256((__m256i*)&gs->chars[i], v);
        }
        strcpy(gs->chars + i, s + i);
    } else {
        strcpy(gs->chars, s);
    }
    return make_value(VAL_STRING, (uint64_t)(uintptr_t)gs);
}

static Value rope_new(Value left, Value right) {
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

GlobalTable global_table = {NULL, NULL, 0, 0};

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

static void var_set(const char *name, Value value) {
    int slot = global_get_slot(name);
    global_table.values[slot] = value;
}

static Value var_get(const char *name) {
    int slot = global_get_slot(name);
    return global_table.values[slot];
}

static void var_table_init(void) {
    // No longer needed
}

#include <nmmintrin.h>

static unsigned int hash_str(const char *s) {
    unsigned int h = 5381;
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
    return h;
}

static int func_table_size = 1024;
static FuncEntry *func_table = NULL;
static FuncEntry main_fe;
static void func_table_init(void) { 
    func_table = calloc(func_table_size, sizeof(FuncEntry)); 
    main_fe.name = "<main>";
    main_fe.param_count = 0;
    main_fe.body = NULL;
}
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
typedef struct { Value callee; int argc; Value *args; } ThreadData;
pthread_mutex_t glow_gil = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gc_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t threads_mutex = PTHREAD_MUTEX_INITIALIZER;
typedef struct { Value **vm_stack_ptr; int *sp_ptr; int *top_ptr; Value *pending_exc_ptr; } ThreadStack;
static ThreadStack active_stacks[128];
static int active_stack_count = 0;

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

static void vm_stack_cleanup(void) {
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
    for (int i = 0; i < td->argc; i++) {
        vm_stack[i] = td->args[i];
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
        has_exception = 0;
    }
    
    free(td->args); free(td); 
    vm_stack_cleanup();
    pthread_mutex_unlock(&glow_gil);
    return NULL;
}

/* GC */
static GCObject *gc_objects = NULL;
static size_t gc_allocated = 0;
static size_t gc_threshold = 128 * 1024 * 1024;

typedef struct GCBlock {
    struct GCBlock *next;
    size_t size;
    size_t used;
    int live_count;
    char data[];
} GCBlock;

static GCBlock *gc_block_list = NULL;
static GCBlock *gc_current_block = NULL;
static size_t gc_block_size = 1024 * 1024; // 1MB blocks

typedef struct RememberedSet {
    GCObject *obj;
    struct RememberedSet *next;
} RememberedSet;
static RememberedSet *gc_remembered_set = NULL;

static void gc_write_barrier(GCObject *src, Value target) {
    if (!src || src->generation == 0) return;
    if (GET_TYPE(target) != VAL_OBJECT && GET_TYPE(target) != VAL_LIST && GET_TYPE(target) != VAL_DICT && GET_TYPE(target) != VAL_STRING) return;
    if (!(target & MASK_PAYLOAD)) return;
    GCObject *to = (GCObject*)(uintptr_t)(target & MASK_PAYLOAD);
    if (to->generation == 1) return;
    if (src->in_remembered_set) return;
    RememberedSet *rs = malloc(sizeof(RememberedSet));
    rs->obj = src; rs->next = gc_remembered_set; gc_remembered_set = rs;
    src->in_remembered_set = 1;
}

static GCBlock* gc_block_new(void) {
    GCBlock *b = malloc(sizeof(GCBlock) + gc_block_size);
    b->size = gc_block_size;
    b->used = 0;
    b->live_count = 0;
    b->next = gc_block_list;
    gc_block_list = b;
    return b;
}

static void gc_mark_object(GCObject *obj) {
    if (!obj || obj->marked) return;
    obj->marked = 1;
    switch (obj->type) {
        case OBJ_LIST: { List *l = (List*)obj; for (int i=0; i<l->size; i++) gc_mark_value(l->items[i]); break; }
        case OBJ_DICT: { Dict *d = (Dict*)obj; for (int i=0; i<d->size; i++) gc_mark_value(d->values[i]); break; }
        case OBJ_CLASS: { 
            Class *c = (Class*)obj; 
            gc_mark_object((GCObject*)c->methods); 
            gc_mark_object((GCObject*)c->root_shape); 
            if (c->stable_shape) gc_mark_object((GCObject*)c->stable_shape);
            break; 
        }
        case OBJ_SHAPE: {
            Shape *s = (Shape*)obj;
            if (s->parent) gc_mark_object((GCObject*)s->parent);
            gc_mark_object((GCObject*)s->transitions);
            break;
        }
        case OBJ_INSTANCE: { Instance *ins = (Instance*)obj; gc_mark_object((GCObject*)ins->klass); gc_mark_object((GCObject*)ins->shape); if(ins->fields) { int lim = ins->shape ? ins->shape->offset : -1; for(int i=0; i<=lim; i++) gc_mark_value(ins->fields[i]); } break; }
        case OBJ_BOUND_METHOD: { BoundMethod *bm = (BoundMethod*)obj; gc_mark_value(bm->instance); break; }
        case OBJ_ROPE: {
            RopeNode *rn = (RopeNode*)obj;
            if (rn->is_leaf) gc_mark_value(rn->leaf.flat_string);
            else {
                gc_mark_object((GCObject*)rn->internal.left);
                gc_mark_object((GCObject*)rn->internal.right);
            }
            break;
        }
        case OBJ_STRING_BUILDER: break; // Buffer is raw malloc'd, no nested objects to mark
        default: break;
    }
}
static void gc_mark_value(Value v) {
    if (GET_TYPE(v) == VAL_OBJECT || GET_TYPE(v) == VAL_LIST || GET_TYPE(v) == VAL_DICT || GET_TYPE(v) == VAL_STRING) {
        if (v & MASK_PAYLOAD) gc_mark_object((GCObject*)(uintptr_t)(v & MASK_PAYLOAD));
    }
}

static void gc_full_collect(void) {
    for (int i=0; i<global_table.count; i++) gc_mark_value(global_table.values[i]);
    pthread_mutex_lock(&threads_mutex);
    for (int i=0; i<active_stack_count; i++) {
        Value *stk = *(active_stacks[i].vm_stack_ptr);
        int stk_sp = *(active_stacks[i].top_ptr);
        if (stk) {
            // Safety check for main stack
            int lim = (stk_sp > 1000000) ? 1000000 : stk_sp;
            for (int j=0; j<lim; j++) gc_mark_value(stk[j]);
        }
        gc_mark_value(*(active_stacks[i].pending_exc_ptr));
    }
    pthread_mutex_unlock(&threads_mutex);
    GCObject **p = &gc_objects;
    while (*p) {
        if (!(*p)->marked) {
            GCObject *unmarked = *p; *p = unmarked->next;
            if (unmarked->type == OBJ_LIST) free(((List*)unmarked)->items);
            else if (unmarked->type == OBJ_DICT) { free(((Dict*)unmarked)->keys); free(((Dict*)unmarked)->values); }
            else if (unmarked->type == OBJ_INSTANCE) free(((Instance*)unmarked)->fields);
            else if (unmarked->type == OBJ_CLASS) free((char*)((Class*)unmarked)->name);
            else if (unmarked->type == OBJ_TRAIT) free((char*)((Trait*)unmarked)->name);
            else if (unmarked->type == OBJ_PACKED_ARRAY) free(((PackedArray*)unmarked)->data);
            else if (unmarked->type == OBJ_STRING_BUILDER) free(((StringBuilder*)unmarked)->buffer);
            gc_allocated -= unmarked->sz;
            if (unmarked->is_block_allocated) { ((GCBlock*)unmarked->block_ptr)->live_count--; } else { free(unmarked); }
        } else {
            (*p)->marked = 0;
            if ((*p)->generation == 0) (*p)->generation = 1; // Promote
            p = &((*p)->next);
        }
    }
    while (gc_remembered_set) { RememberedSet *next = gc_remembered_set->next; gc_remembered_set->obj->in_remembered_set = 0; free(gc_remembered_set); gc_remembered_set = next; }
}

static void gc_minor_collect(void) {
    for (int i=0; i<global_table.count; i++) gc_mark_value(global_table.values[i]);
    pthread_mutex_lock(&threads_mutex);
    for (int i=0; i<active_stack_count; i++) {
        Value *stk = *(active_stacks[i].vm_stack_ptr);
        int stk_sp = *(active_stacks[i].top_ptr);
        if (stk) {
            int lim = (stk_sp > 1000000) ? 1000000 : stk_sp;
            for (int j=0; j<lim; j++) gc_mark_value(stk[j]);
        }
        gc_mark_value(*(active_stacks[i].pending_exc_ptr));
    }
    pthread_mutex_unlock(&threads_mutex);
    RememberSetScan:;
    RememberedSet *rs = gc_remembered_set;
    while (rs) { gc_mark_object(rs->obj); rs = rs->next; }

    GCObject **p = &gc_objects;
    while (*p) {
        if ((*p)->generation == 1) { p = &((*p)->next); continue; } // Skip old gen sweep
        if (!(*p)->marked) {
            GCObject *unmarked = *p; *p = unmarked->next;
            if (unmarked->type == OBJ_LIST) free(((List*)unmarked)->items);
            else if (unmarked->type == OBJ_DICT) { free(((Dict*)unmarked)->keys); free(((Dict*)unmarked)->values); }
            else if (unmarked->type == OBJ_INSTANCE) free(((Instance*)unmarked)->fields);
            else if (unmarked->type == OBJ_CLASS) free((char*)((Class*)unmarked)->name);
            else if (unmarked->type == OBJ_TRAIT) free((char*)((Trait*)unmarked)->name);
            else if (unmarked->type == OBJ_PACKED_ARRAY) free(((PackedArray*)unmarked)->data);
            else if (unmarked->type == OBJ_STRING_BUILDER) free(((StringBuilder*)unmarked)->buffer);
            gc_allocated -= unmarked->sz;
            if (unmarked->is_block_allocated) { ((GCBlock*)unmarked->block_ptr)->live_count--; } else { free(unmarked); }
        } else {
            (*p)->marked = 0;
            (*p)->generation = 1; // Promote survivors
            p = &((*p)->next);
        }
    }
}

static void gc_collect(void) { gc_full_collect(); }

static void* gc_alloc(size_t sz, ObjType type) {
    pthread_mutex_lock(&gc_mutex);
    if (gc_allocated > gc_threshold) gc_full_collect();
    else if (gc_allocated > gc_threshold / 2) gc_minor_collect();
    
    sz = (sz + 7) & ~7;
    GCObject *obj = NULL;
    if (sz < gc_block_size / 4) {
        if (!gc_current_block || gc_current_block->used + sz > gc_current_block->size) {
            gc_current_block = gc_block_new();
        }
        obj = (GCObject*)(gc_current_block->data + gc_current_block->used);
        gc_current_block->used += sz;
        gc_current_block->live_count++;
        memset(obj, 0, sz);
        obj->is_block_allocated = 1;
        obj->block_ptr = gc_current_block;
    } else {
        obj = calloc(1, sz);
        obj->is_block_allocated = 0;
        obj->block_ptr = NULL;
    }
    obj->generation = 0;
    obj->is_stack_allocated = 0;
    obj->type = type; obj->sz = sz; obj->next = gc_objects; gc_objects = obj; gc_allocated += sz;
    pthread_mutex_unlock(&gc_mutex);
    return obj;
}

#define STACK_ARENA_SIZE (1024 * 1024)
static __thread char *stack_arena = NULL;
static __thread size_t stack_arena_used = 0;

static void* gc_alloc_stack(size_t sz, ObjType type) {
    if (!stack_arena) stack_arena = malloc(STACK_ARENA_SIZE);
    sz = (sz + 7) & ~7;
    if (stack_arena_used + sz > STACK_ARENA_SIZE) return gc_alloc(sz, type); // Fallback to heap
    
    GCObject *obj = (GCObject*)(stack_arena + stack_arena_used);
    stack_arena_used += sz;
    memset(obj, 0, sz);
    obj->type = type;
    obj->sz = sz;
    obj->is_stack_allocated = 1;
    return obj;
}

/* Helpers */
static Dict* dict_new(int sz) { Dict *d = gc_alloc(sizeof(Dict), OBJ_DICT); d->capacity = sz; d->keys = calloc(sz, sizeof(char*)); d->values = calloc(sz, sizeof(Value)); return d; }
static void dict_set(Dict *d, const char *key, Value val) {
    gc_write_barrier((GCObject*)d, val);
    for (int i=0; i<d->size; i++) if (strcmp(d->keys[i], key) == 0) { d->values[i] = val; return; }
    if (d->size >= d->capacity) { d->capacity = (d->capacity==0)?8:d->capacity*2; d->keys = realloc(d->keys, sizeof(char*)*d->capacity); d->values = realloc(d->values, sizeof(Value)*d->capacity); }
    d->keys[d->size] = key; d->values[d->size] = val; d->size++;
}
static Value dict_get(Dict *d, const char *key) {
    if (!d) return value_none();
    for (int i = 0; i < d->size; i++) if (strcmp(d->keys[i], key) == 0) return d->values[i];
    return value_none();
}
static List* list_new(int sz) { List *l = gc_alloc(sizeof(List), OBJ_LIST); l->size = 0; l->capacity = sz; l->items = sz > 0 ? calloc(sz, sizeof(Value)) : NULL; return l; }
static void list_push(List *l, Value v) {
    gc_write_barrier((GCObject*)l, v);
    if (l->size >= l->capacity) { l->capacity = (l->capacity == 0) ? 4 : l->capacity * 2; l->items = realloc(l->items, sizeof(Value)*l->capacity); } l->items[l->size++] = v;
}

static PackedArray* packed_array_new(PackedType type, int capacity) {
    PackedArray *pa = gc_alloc(sizeof(PackedArray), OBJ_PACKED_ARRAY);
    pa->type = type;
    pa->size = capacity;
    pa->capacity = capacity;
    size_t elem_sz = (type == PACKED_INT64) ? sizeof(int64_t) : sizeof(double);
    if (capacity > 0) {
        if (posix_memalign(&pa->data, 32, capacity * elem_sz) != 0) {
            pa->data = NULL;
        } else {
            memset(pa->data, 0, capacity * elem_sz);
        }
    } else {
        pa->data = NULL;
    }
    return pa;
}

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

static Value packed_array_get(PackedArray *pa, int idx) {
    if (idx < 0 || idx >= pa->size) return value_none();
    if (pa->type == PACKED_INT64) return value_int(((int64_t*)pa->data)[idx]);
    return value_from_double(((double*)pa->data)[idx]);
}

static void packed_array_set(PackedArray *pa, int idx, Value v) {
    if (idx < 0 || idx >= pa->size) return;
    if (pa->type == PACKED_INT64) ((int64_t*)pa->data)[idx] = (int64_t)val_to_double(v);
    else ((double*)pa->data)[idx] = val_to_double(v);
}

static Shape* shape_new(Shape *p, const char *n, int o) { Shape *s = gc_alloc(sizeof(Shape), OBJ_SHAPE); s->parent = p; s->name = n; s->offset = o; s->transitions = dict_new(4); return s; }
static Shape* shape_get_next(Shape* curr, const char *name) { for (int i=0; i<curr->transitions->size; i++) if (strcmp(curr->transitions->keys[i], name) == 0) return (Shape*)(uintptr_t)(curr->transitions->values[i] & MASK_PAYLOAD); Shape *next = shape_new(curr, name, curr->offset + 1); dict_set(curr->transitions, name, value_obj(next)); return next; }
static Class* class_new(const char *name, Class *parent) { 
    Class *c = gc_alloc(sizeof(Class), OBJ_CLASS); 
    c->name = strdup(name); 
    c->parent = parent; 
    c->methods = dict_new(8); 
    c->root_shape = shape_new(NULL, "__root__", -1); 
    c->stable_shape = NULL;
    c->instances_created = 0;
    return c; 
}
static Instance* instance_new(Class *k) {
    Instance *i = gc_alloc(sizeof(Instance), OBJ_INSTANCE);
    i->klass = k;
    k->instances_created++;
    if (k->stable_shape) {
        i->shape = k->stable_shape;
        i->field_capacity = i->shape->offset + 1;
        if (i->field_capacity < 8) i->field_capacity = 8;
        i->fields = calloc(i->field_capacity, sizeof(Value));
    } else {
        i->shape = k->root_shape;
        i->field_capacity = 8;
        i->fields = calloc(8, sizeof(Value));
    }
    for(int j=0; j<i->field_capacity; j++) i->fields[j] = value_none();
    return i;
}
static Value class_get_method(Class *k, const char *n) { while(k) { Value m = dict_get(k->methods, n); if((m & MASK_PAYLOAD) != 0) return m; k=k->parent; } return value_none(); }
static Value instance_get(Instance *inst, const char *name) {
    Shape *s = inst->shape; while (s && s->name && strcmp(s->name, "__root__") != 0) { if (strcmp(s->name, name) == 0) return inst->fields[s->offset]; s = s->parent; }
    return class_get_method(inst->klass, name);
}
static void instance_set(Instance *inst, const char *name, Value val) {
    gc_write_barrier((GCObject*)inst, val);
    Shape *s = inst->shape; while (s && s->name && strcmp(s->name, "__root__") != 0) { if (strcmp(s->name, name) == 0) { inst->fields[s->offset] = val; return; } s = s->parent; }
    
    inst->shape = shape_get_next(inst->shape, name);

    /* Transition Pruning: if we reach a certain depth after many instances, promote it */
    if (inst->shape->offset >= 2 && inst->klass->instances_created > 50 && !inst->klass->stable_shape) {
        inst->klass->stable_shape = inst->shape;
    }

    if (inst->shape->offset >= inst->field_capacity) { int old = inst->field_capacity; inst->field_capacity *= 2; inst->fields = realloc(inst->fields, sizeof(Value)*inst->field_capacity); for(int j=old; j<inst->field_capacity; j++) inst->fields[j] = value_none(); }
    inst->fields[inst->shape->offset] = val;
}
static Trait* trait_new(const char *name) { Trait *t = gc_alloc(sizeof(Trait), OBJ_TRAIT); t->name = strdup(name); t->methods = dict_new(8); return t; }
static Value bound_method_new(Value inst, FuncEntry *m) { BoundMethod *bm = gc_alloc(sizeof(BoundMethod), OBJ_BOUND_METHOD); bm->instance = inst; bm->method = m; return value_bound_method(bm); }
static Value native_func_new(const char *name, NativeFuncPtr ptr) { NativeFunc *nf = gc_alloc(sizeof(NativeFunc), OBJ_NATIVE_FUNC); nf->name = name; nf->func = ptr; return value_native_func(nf); }

Node* node_new(NodeType t) { Node *n = arena_alloc(sizeof(Node)); memset(n, 0, sizeof(Node)); n->type = t; n->line = line_num; return n; }
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
static int next_free_reg = 0;
static int global_lambda_count = 0;
static int compiling_line = 0;
static const char *compiling_file = NULL;
static __thread int profiling_active = 0;

static int bc_emit(Bytecode *bc, OpCode op, int arg, intptr_t ptr) {
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
static Bytecode* bc_new(FuncEntry *owner) { 
    Bytecode *bc = calloc(1, sizeof(Bytecode)); 
    bc->capacity = 1024; 
    bc->code = calloc(bc->capacity, sizeof(Instruction)); 
    bc->owner = owner;
    bc->exception_table = NULL;
    bc->exception_count = 0;
    return bc;
}

static void bc_add_handler(Bytecode *bc, int start, int end, int handler) {
    bc->exception_table = realloc(bc->exception_table, sizeof(ExceptionEntry) * (bc->exception_count + 1));
    bc->exception_table[bc->exception_count].start_pc = start;
    bc->exception_table[bc->exception_count].end_pc = end;
    bc->exception_table[bc->exception_count].handler_pc = handler;
    bc->exception_count++;
}

static void func_add_local(FuncEntry *fe, const char *name) {
    if (!fe || !name) return;
    for (int i=0; i<fe->local_count; i++) if (strcmp(fe->local_names[i], name) == 0) return;
    for (int i=0; i<fe->param_count; i++) if (strcmp(fe->params[i], name) == 0) return;
    if (fe->local_count >= fe->local_capacity) {
        fe->local_capacity = (fe->local_capacity == 0) ? 8 : fe->local_capacity * 2;
        fe->local_names = realloc(fe->local_names, sizeof(char*) * fe->local_capacity);
    }
    fe->local_names[fe->local_count++] = (char*)name;
    next_free_reg++; // Reserve register for the new local
}
static FuncEntry *compiling_func = NULL;

static int get_local_reg(const char *name) {
    if (!compiling_func) return -1;
    for (int i=0; i<compiling_func->param_count; i++) {
        if (strcmp(compiling_func->params[i], name) == 0) return i;
    }
    for (int i=0; i<compiling_func->local_count; i++) {
        if (strcmp(compiling_func->local_names[i], name) == 0) return compiling_func->param_count + i;
    }
    return -1;
}

static void analyze_escapes(Bytecode *bc) {
    if (!bc) return;
    unsigned char *escapes = calloc(bc->local_count + 256, 1);
    
    // Pass 1: Identify all registers that escape
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

static void compile_stmt(Node *n) {
    if (!n) return;
    compiling_line = n->line;
    if (compiling_func && n->name) {
        if (n->type == NODE_VAR || n->type == NODE_ASSIGN || n->type == NODE_LET) {
            func_add_local(compiling_func, n->name);
        }
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
        case NODE_LET: {
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
            int cond = compile_expr(n->left, -1);
            int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            compiling_bc->code[jf].r0 = cond;
            compile_stmt(n->right); 
            bc_emit(compiling_bc, OP_JUMP, start, 0);
            compiling_bc->code[jf].arg = compiling_bc->count;
            break;
        }
        case NODE_FOR_RANGE: {
            int start_val = compile_expr(n->left, -1);
            int end_val = compile_expr(n->right, -1);
            int slot = global_get_slot(n->name);
            int idx = bc_emit(compiling_bc, OP_STORE_VAR, slot, 0);
            compiling_bc->code[idx].r0 = start_val;
            int loop_start = compiling_bc->count;
            int i_reg = next_free_reg++;
            idx = bc_emit(compiling_bc, OP_LOAD_VAR, slot, 0);
            compiling_bc->code[idx].r0 = i_reg;
            int cond_reg = next_free_reg++;
            bc_emit_reg(compiling_bc, OP_LTE, cond_reg, i_reg, end_val);
            int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            compiling_bc->code[jf].r0 = cond_reg;
            compile_stmt(n->third);
            idx = bc_emit(compiling_bc, OP_LOAD_VAR, slot, 0);
            compiling_bc->code[idx].r0 = i_reg;
            int one_reg = next_free_reg++;
            idx = bc_emit(compiling_bc, OP_CONST_INT, 1, 0);
            compiling_bc->code[idx].r0 = one_reg;
            bc_emit_reg(compiling_bc, OP_ADD, i_reg, i_reg, one_reg);
            idx = bc_emit(compiling_bc, OP_STORE_VAR, slot, 0);
            compiling_bc->code[idx].r0 = i_reg;
            bc_emit(compiling_bc, OP_JUMP, loop_start, 0);
            compiling_bc->code[jf].arg = compiling_bc->count;
            break;
        }
        case NODE_RETURN: {
            int r = compile_expr(n->left, -1);
            int idx = bc_emit(compiling_bc, OP_RETURN, 0, 0);
            compiling_bc->code[idx].r0 = r;
            break;
        }
        case NODE_FUNC_DEF: {
            FuncEntry *fe = func_table_add(n->name); fe->param_count = n->func_param_count; fe->params = n->func_params; fe->body = bc_new(fe);
            Bytecode *p = compiling_bc; compiling_bc = fe->body; 
            FuncEntry *pf = compiling_func; compiling_func = fe;
            int old_free = next_free_reg; next_free_reg = fe->param_count + fe->local_count;
            compile_stmt(n->left); 
            fe->body->local_count = next_free_reg;
            bc_emit(compiling_bc, OP_RETURN, 0, 0); 
            optimize_bytecode(compiling_bc);
            analyze_escapes(compiling_bc);
            compiling_bc = p; compiling_func = pf; next_free_reg = old_free;
            bc_emit(compiling_bc, (last_compiling_class || last_compiling_trait) ? OP_METHOD_DEF : OP_FUNC_DEF, 0, (intptr_t)n->name); 
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
        case NODE_EXPR_STMT: compile_expr(n->left, -1); break;
        case NODE_STOP: bc_emit(compiling_bc, OP_STOP, 0, 0); break;
        case NODE_SKIP: bc_emit(compiling_bc, OP_SKIP, 0, 0); break;
        case NODE_CLASS: {
            Class *pc = last_compiling_class; last_compiling_class = (Class*)1;
            if (n->parent_name) {
                int r = next_free_reg++;
                int slot = global_get_slot(n->parent_name);
                int idx = bc_emit(compiling_bc, OP_LOAD_VAR, slot, 0);
                compiling_bc->code[idx].r0 = r;
            }
            bc_emit(compiling_bc, OP_CLASS_DEF, n->parent_name ? 1 : 0, (intptr_t)n->name);
            compile_stmt(n->left); last_compiling_class = pc; break;
        }
        case NODE_TRAIT: { Trait *pt = last_compiling_trait; last_compiling_trait = (Trait*)1; bc_emit(compiling_bc, OP_TRAIT_DEF, 0, (intptr_t)n->name); compile_stmt(n->left); last_compiling_trait = pt; break; }
        case NODE_USE_TRAIT: bc_emit(compiling_bc, OP_USE_TRAIT, 0, (intptr_t)n->name); break;
        case NODE_TRY_CATCH: {
            int start_pc = compiling_bc->count;
            compile_stmt(n->left);
            int end_pc = compiling_bc->count;
            int j = bc_emit(compiling_bc, OP_JUMP, 0, 0);
            int h = compiling_bc->count;
            bc_add_handler(compiling_bc, start_pc, end_pc, h);
            int err_reg = get_local_reg(n->name);
            if (err_reg == -1) err_reg = next_free_reg++;
            int slot = global_get_slot(n->name);
            int idx = bc_emit(compiling_bc, OP_STORE_VAR, slot, 0);
            compiling_bc->code[idx].r0 = 0; // Exception value is in R(0)
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
             int idx = bc_emit(compiling_bc, OP_SPAWN, ac, 0);
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
            sleep_args[0] = newFloatNode(0.01);
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
            int idx = bc_emit(compiling_bc, OP_CONST_INT, AS_INT(n->value), 0);
            compiling_bc->code[idx].r0 = reg;
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
                else reg = src;
            } else {
                int slot = global_get_slot(n->name);
                int idx = bc_emit(compiling_bc, OP_LOAD_VAR, slot, 0);
                compiling_bc->code[idx].r0 = (unsigned short)reg;
            }
            break;
        }
        case NODE_AND: {
            int left = compile_expr(n->left, -1);
            if (target_reg == -1) reg = next_free_reg++;
            bc_emit_reg(compiling_bc, OP_MOVE, reg, left, 0);
            int jf = bc_emit(compiling_bc, OP_JUMP_IF_FALSE, 0, 0);
            compiling_bc->code[jf].r0 = left;
            int right = compile_expr(n->right, -1);
            bc_emit_reg(compiling_bc, OP_MOVE, reg, right, 0);
            compiling_bc->code[jf].arg = compiling_bc->count;
            break;
        }
        case NODE_OR: {
            int left = compile_expr(n->left, -1);
            if (target_reg == -1) reg = next_free_reg++;
            bc_emit_reg(compiling_bc, OP_MOVE, reg, left, 0);
            int jt = bc_emit(compiling_bc, OP_JUMP_IF_TRUE, 0, 0);
            compiling_bc->code[jt].r0 = left;
            int right = compile_expr(n->right, -1);
            bc_emit_reg(compiling_bc, OP_MOVE, reg, right, 0);
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
        case NODE_CALL_EXPR:
        case NODE_CALL: {
            int ac = n->call_arg_count;
            if (n->type == NODE_CALL && n->name) {
                if (strcmp(n->name, "type") == 0 && ac == 1) {
                    int arg = compile_expr(n->call_args[0], -1);
                    if (target_reg == -1) reg = next_free_reg++;
                    bc_emit_reg(compiling_bc, OP_TYPE, reg, arg, 0);
                    break;
                }
                if (strcmp(n->name, "len") == 0 && ac == 1) {
                    int arg = compile_expr(n->call_args[0], -1);
                    if (target_reg == -1) reg = next_free_reg++;
                    bc_emit_reg(compiling_bc, OP_LEN, reg, arg, 0);
                    break;
                }
            }
            int base = next_free_reg;
            next_free_reg += ac + 1;
            for (int i=0; i<ac; i++) compile_expr(n->call_args[i], base + i);
            int callee_reg = base + ac;
            if (n->type == NODE_CALL) {
                FuncEntry *fe = func_get(n->name);
                if (try_inline(fe, reg, ac, base)) break;
                int slot = global_get_slot(n->name);
                int idx = bc_emit(compiling_bc, OP_LOAD_VAR, slot, 0);
                compiling_bc->code[idx].r0 = callee_reg;
            } else {
                compile_expr(n->left, callee_reg);
            }
            int idx = bc_emit(compiling_bc, (n->type == NODE_CALL) ? OP_CALL : OP_CALL_VALUE, ac, (intptr_t)n->name);
            compiling_bc->code[idx].r0 = reg;
            compiling_bc->code[idx].r1 = callee_reg;
            compiling_bc->code[idx].r2 = base;
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
        case NODE_LIST_LITERAL: {
            int base = next_free_reg;
            next_free_reg += n->element_count;
            for (int i=0; i<n->element_count; i++) compile_expr(n->elements[i], base + i);
            int idx = bc_emit(compiling_bc, OP_LIST_LITERAL, n->element_count, 0);
            compiling_bc->code[idx].r0 = reg;
            compiling_bc->code[idx].r1 = base;
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
            break;
        }
        case NODE_LAMBDA: {
            FuncEntry *fe = func_table_add(n->name);
            fe->param_count = n->func_param_count;
            fe->params = n->func_params;
            fe->body = bc_new(fe);
            Bytecode *p = compiling_bc; compiling_bc = fe->body; FuncEntry *pf = compiling_func; compiling_func = fe;
            int old_free = next_free_reg; next_free_reg = fe->param_count;
            compile_stmt(n->left);
            bc_emit(compiling_bc, OP_RETURN, 0, 0);
            compiling_bc->local_count = next_free_reg;
            compiling_bc = p; compiling_func = pf; next_free_reg = old_free;

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

void yyerror(const char *s) { fprintf(stderr, "Error: %s at line %d\n", s, line_num); }
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
%token SHOW WRITE WHEN WHENEVER EIF OTHERWISE WHILE UNTIL REPEAT FOR EACH IN FUNC LAMBDA RETURN STOP SKIP END LET TRY CATCH THROW IMPORT CLASS TRAIT EXTENDS USE SPAWN TRUE_KW FALSE_KW TIMES_KW INPUT_KW IS IS_NOT FROM TO NEWLINE COMMA ASSIGN PLUS_ASSIGN MINUS_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN EQ NEQ LT GT LTE GTE PLUS MINUS MUL DIV MOD POW COLON AND_OP OR_OP NOT_OP

%type <node> program lines stmt expr term power postfix primary condition eif_chain otherwise_part
%type <node_list> expr_list arg_list
%type <dict_list> dict_elements
%type <str_list> param_list

%left OR_OP
%left AND_OP
%left EQ NEQ IS IS_NOT
%left LT GT LTE GTE
%left PLUS MINUS
%left MUL DIV MOD
%right POW
%nonassoc NOT_OP

%%

program: lines { root = $1; };
lines: lines NEWLINE stmt { $$ = newSeqNode($1, $3); } | lines NEWLINE { $$ = $1; } | stmt { $$ = $1; } | IMPORT STRING { $$ = node_new(NODE_IMPORT); $$->name = $2; } | { $$ = NULL; };
stmt: postfix ASSIGN expr { if ($1->type == NODE_VAR) $$ = newAssignNode($1->name, $3); else { $$ = $1; $$->type = NODE_INDEX_ASSIGN; $$->third = $3; } }
    | RETURN expr { $$ = newReturnNode($2); }
    | STOP { $$ = node_new(NODE_STOP); }
    | SKIP { $$ = node_new(NODE_SKIP); }
    | FOR EACH ID IN expr COLON NEWLINE lines END { $$ = node_new(NODE_FOR_EACH); $$->name = $3; $$->left = $5; $$->right = $8; }
    | FOR ID FROM expr TO expr COLON NEWLINE lines END { $$ = node_new(NODE_FOR_RANGE); $$->name = $2; $$->left = $4; $$->right = $6; $$->third = $9; }
    | FUNC ID '(' param_list ')' COLON NEWLINE lines END { $$ = newFuncNode($2, $4.count, $4.names, $8); }
    | FUNC ID '(' ')' COLON NEWLINE lines END { $$ = newFuncNode($2, 0, NULL, $7); }
    | CLASS ID COLON NEWLINE lines END { $$ = node_new(NODE_CLASS); $$->name = $2; $$->left = $5; }
    | CLASS ID EXTENDS ID COLON NEWLINE lines END { $$ = node_new(NODE_CLASS); $$->name = $2; $$->parent_name = $4; $$->left = $7; }
    | TRAIT ID COLON NEWLINE lines END { $$ = node_new(NODE_TRAIT); $$->name = $2; $$->left = $5; }
    | USE ID { $$ = node_new(NODE_USE_TRAIT); $$->name = $2; }
    | WHENEVER condition COLON NEWLINE lines END { $$ = node_new(NODE_WHENEVER); $$->left = $2; $$->right = $5; }
    | WHEN condition COLON NEWLINE lines eif_chain otherwise_part END { $$ = newIfNode($2, $5, $7 ? newSeqNode($6, $7) : $6); }
    | WHILE condition COLON NEWLINE lines END { $$ = newWhileNode($2, $5); }
    | TRY COLON NEWLINE lines CATCH ID COLON NEWLINE lines END { $$ = node_new(NODE_TRY_CATCH); $$->left = $4; $$->name = $6; $$->right = $9; }
    | THROW expr { $$ = node_new(NODE_THROW); $$->left = $2; }
    | SPAWN expr { $$ = node_new(NODE_SPAWN); $$->left = $2; }
    | LET ID ASSIGN expr { $$ = node_new(NODE_LET); $$->name = $2; $$->left = $4; }
    | SHOW expr_list { $$ = newShowNode($2.nodes, $2.count); }
    | expr { $$ = newExprStmtNode($1); };

expr_list: expr { $$.count = 1; $$.nodes = arena_alloc(sizeof(Node*)); $$.nodes[0] = $1; } | expr_list COMMA expr { $$.nodes = arena_realloc($$.nodes, sizeof(Node*)*($1.count), sizeof(Node*)*($1.count+1)); $$.nodes[$1.count] = $3; $$.count = $1.count + 1; };
arg_list: expr_list { $$ = $1; };
param_list: ID { $$.count = 1; $$.names = arena_alloc(sizeof(char*)); $$.names[0] = (char*)intern_string($1); } | param_list COMMA ID { $$.names = arena_realloc($$.names, sizeof(char*)*($1.count), sizeof(char*)*($1.count+1)); $$.names[$1.count] = (char*)intern_string($3); $$.count = $1.count + 1; };
condition: expr;
eif_chain: eif_chain EIF condition COLON NEWLINE lines { $$ = newSeqNode($1, newIfNode($3, $6, NULL)); } | { $$ = NULL; };
otherwise_part: OTHERWISE COLON NEWLINE lines { $$ = $4; } | { $$ = NULL; };

expr: term | expr PLUS term { $$ = newBinOp(NODE_ADD, $1, $3); } | expr MINUS term { $$ = newBinOp(NODE_SUB, $1, $3); } | expr EQ term { $$ = newBinOp(NODE_EQ, $1, $3); } | expr NEQ term { $$ = newBinOp(NODE_NEQ, $1, $3); } | expr IS term { $$ = newBinOp(NODE_EQ, $1, $3); } | expr IS_NOT term { $$ = newBinOp(NODE_NEQ, $1, $3); } | expr LT term { $$ = newBinOp(NODE_LT, $1, $3); } | expr GT term { $$ = newBinOp(NODE_GT, $1, $3); } | expr LTE term { $$ = newBinOp(NODE_LTE, $1, $3); } | expr GTE term { $$ = newBinOp(NODE_GTE, $1, $3); } | expr AND_OP term { $$ = newBinOp(NODE_AND, $1, $3); } | expr OR_OP term { $$ = newBinOp(NODE_OR, $1, $3); } | NOT_OP term { $$ = newBinOp(NODE_NOT, $2, NULL); };
term: power | term MUL power { $$ = newBinOp(NODE_MUL, $1, $3); } | term DIV power { $$ = newBinOp(NODE_DIV, $1, $3); } | term MOD power { $$ = newBinOp(NODE_MOD, $1, $3); };
power: postfix | postfix POW power { $$ = newBinOp(NODE_POW, $1, $3); };
postfix: primary | postfix '.' ID { $$ = node_new(NODE_INDEX); $$->left = $1; $$->name = $3; } | postfix '(' arg_list ')' { $$ = newCallExprNode($1, $3.nodes, $3.count); } | postfix '(' ')' { $$ = newCallExprNode($1, NULL, 0); } | postfix '[' expr ']' { $$ = node_new(NODE_INDEX); $$->left = $1; $$->right = $3; };
primary: ID { $$ = newVarNode($1); } | INT_LIT { $$ = newIntNode($1); } | STRING { $$ = newStringNode($1); } | FLOAT_LIT { $$ = newFloatNode($1); } | TRUE_KW { $$ = newBoolNode(1); } | FALSE_KW { $$ = newBoolNode(0); } | '(' expr ')' { $$ = $2; } | '[' expr_list ']' { $$ = node_new(NODE_LIST_LITERAL); $$->elements = $2.nodes; $$->element_count = $2.count; } | '[' ']' { $$ = node_new(NODE_LIST_LITERAL); $$->elements = NULL; $$->element_count = 0; } | '{' dict_elements '}' { $$ = node_new(NODE_DICT_LITERAL); $$->dict_keys = $2.keys; $$->dict_values = $2.values; $$->dict_count = $2.count; } | '{' '}' { $$ = node_new(NODE_DICT_LITERAL); $$->dict_keys = NULL; $$->dict_values = NULL; $$->dict_count = 0; } | LAMBDA '(' param_list ')' COLON expr { char buf[32]; snprintf(buf, 32, "lambda_%d", global_lambda_count++); Node *n = newFuncNode(intern_string(buf), $3.count, $3.names, newReturnNode($6)); n->type = NODE_LAMBDA; $$ = n; } | LAMBDA '(' ')' COLON expr { char buf[32]; snprintf(buf, 32, "lambda_%d", global_lambda_count++); Node *n = newFuncNode(intern_string(buf), 0, NULL, newReturnNode($5)); n->type = NODE_LAMBDA; $$ = n; } | LAMBDA '(' param_list ')' COLON NEWLINE lines END { char buf[32]; snprintf(buf, 32, "lambda_%d", global_lambda_count++); Node *n = newFuncNode(intern_string(buf), $3.count, $3.names, $7); n->type = NODE_LAMBDA; $$ = n; } | LAMBDA '(' ')' COLON NEWLINE lines END { char buf[32]; snprintf(buf, 32, "lambda_%d", global_lambda_count++); Node *n = newFuncNode(intern_string(buf), 0, NULL, $6); n->type = NODE_LAMBDA; $$ = n; };

dict_elements: expr COLON expr { $$.count = 1; $$.keys = arena_alloc(sizeof(Node*)); $$.keys[0] = $1; $$.values = arena_alloc(sizeof(Node*)); $$.values[0] = $3; } | dict_elements COMMA expr COLON expr { $$.keys = arena_realloc($$.keys, sizeof(Node*)*($1.count), sizeof(Node*)*($1.count+1)); $$.keys[$1.count] = $3; $$.values = arena_realloc($$.values, sizeof(Node*)*($1.count), sizeof(Node*)*($1.count+1)); $$.values[$1.count] = $5; $$.count = $1.count + 1; };

%%

static Class *vm_last_class = NULL;
static Trait *vm_last_trait = NULL;

void jit_compile_trace(Trace *t);

static __thread char *exception_stack_trace = NULL;
static __thread const char *error_file = NULL;
static __thread int error_line = 0;

char* format_stack_trace(void) {
    char *buf = malloc(4096);
    buf[0] = '\0';
    strcat(buf, "Stack Trace:\n");
    for (int i = call_stack_ptr - 1; i >= 0; i--) {
        FuncEntry *fe = call_stack[i].fe;
        Instruction *pc = call_stack[i].pc;
        int line = pc ? pc->line : 0;
        const char *file = pc ? pc->file : "<unknown>";
        char frame[256];
        snprintf(frame, 256, "  at %s (%s:%d)\n", fe ? fe->name : "<unknown>", file, line);
        strcat(buf, frame);
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
    else if ((exception & MASK_NAN) != MASK_NAN) { fprintf(stderr, "%g", AS_FLOAT(exception)); }
    else {
        uint64_t type = (exception & MASK_TYPE) >> 48;
        if (type == VAL_INT) fprintf(stderr, "%ld", (long)AS_INT(exception));
        else if (type == VAL_BOOL) fprintf(stderr, AS_BOOL(exception) ? "true" : "false");
        else if (type == VAL_STRING) fprintf(stderr, "%s", AS_STRING(exception));
        else {
            ObjType ot = GET_OBJ_TYPE(exception);
            if (ot == OBJ_CLASS) fprintf(stderr, "<class %s>", AS_CLASS(exception)->name);
            else if (ot == OBJ_INSTANCE) fprintf(stderr, "<instance of %s>", AS_INSTANCE(exception)->klass->name);
            else fprintf(stderr, "<object>");
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

static Value run_bytecode(Bytecode *bc, Instruction *start_pc, Closure *current_closure, int base_reg) {
    static __thread int call_depth = 0;
    if (++call_depth > 10000) {
        fprintf(stderr, "ERROR: Maximum recursion depth exceeded\n");
        call_depth--;
        return value_none();
    }

    call_stack_push(bc->owner, start_pc ? start_pc : bc->code);
    int my_frame_idx = call_stack_ptr - 1;

    size_t old_arena_used = stack_arena_used;
    int old_top = vm_stack_top;
    if (base_reg + bc->local_count > vm_stack_top) vm_stack_top = base_reg + bc->local_count;

    static __thread long total_instructions = 0;
    static __thread Trace *recording_trace = NULL;

    Instruction *pc = start_pc ? start_pc : bc->code;

    if (!start_pc && bc->count > 0 && !bc->jit_code && !bc->jit_failed) {
        bc->code[0].exec_count++;
        if (0) jit_compile(bc);
    }
    if (!start_pc && bc->jit_code) {
        typedef Value (*JITFunc)(Value *R, int *pc_idx);
        int deopt_pc = 0;
        Value res = ((JITFunc)bc->jit_code)(&vm_stack[base_reg], &deopt_pc);
        if (GET_TYPE(res) == VAL_DEOPT) {
            pc = &bc->code[deopt_pc];
            goto l_RESUME_INTERP;
        }
        call_stack_pop();
        vm_stack_top = old_top;
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
        &&l_OP_ITER_NEXT
    };

#define R(idx) vm_stack[base_reg + (idx)]
#define DISPATCH() { \
    if (profiling_active) pc->exec_count++; \
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
        // printf("LOAD_VAR: slot=%d, count=%d, values=%p\n", slot, global_table.count, global_table.values);
        if (slot >= global_table.count) { fprintf(stderr, "ERROR: LOAD_VAR slot %d out of bounds (%d)\n", slot, global_table.count); exit(1); }
        R(pc->r0) = global_table.values[slot];
        pc++; DISPATCH();
    }
    l_OP_STORE_VAR: {
        int slot = pc->arg;
        // printf("STORE_VAR: slot=%d, count=%d, values=%p\n", slot, global_table.count, global_table.values);
        if (slot >= global_table.count) { fprintf(stderr, "ERROR: STORE_VAR slot %d out of bounds (%d)\n", slot, global_table.count); exit(1); }
        global_table.values[slot] = R(pc->r0);
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
            else { char buf[64]; if(GET_TYPE(a)==VAL_INT) snprintf(buf,64,"%ld",(long)AS_INT(a)); else if(GET_TYPE(a)==VAL_FLOAT) snprintf(buf,64,"%g",AS_FLOAT(a)); else strcpy(buf, ""); sa = value_string_copy(buf); }
            if (GET_TYPE(b) == VAL_STRING) sb = b;
            else { char buf[64]; if(GET_TYPE(b)==VAL_INT) snprintf(buf,64,"%ld",(long)AS_INT(b)); else if(GET_TYPE(b)==VAL_FLOAT) snprintf(buf,64,"%g",AS_FLOAT(b)); else strcpy(buf, ""); sb = value_string_copy(buf); }
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
        if (pc->arg < (pc - bc->code)) {
            pc->exec_count++;
            /* if (pc->exec_count > 1000 && !recording_trace && !pc->trace) {
                recording_trace = malloc(sizeof(Trace));
                recording_trace->instructions = malloc(sizeof(Instruction) * 128);
                recording_trace->original_offsets = malloc(sizeof(int) * 128);
                recording_trace->count = 0; recording_trace->capacity = 128;
                recording_trace->start_pc = pc;
            } */
        }
        pc = bc->code + pc->arg; DISPATCH();
    }
    l_OP_JUMP_IF_FALSE: {
        if (pc->arg < (pc - bc->code) && !value_truthy(R(pc->r0))) {
            pc->exec_count++;
            /* if (pc->exec_count > 1000 && !recording_trace && !pc->trace) {
                recording_trace = malloc(sizeof(Trace));
                recording_trace->instructions = malloc(sizeof(Instruction) * 128);
                recording_trace->original_offsets = malloc(sizeof(int) * 128);
                recording_trace->count = 0; recording_trace->capacity = 128;
                recording_trace->start_pc = pc;
            } */
        }
        if(!value_truthy(R(pc->r0))) pc = bc->code + pc->arg; else pc++; DISPATCH();
    }
    l_OP_JUMP_IF_TRUE: {
        if (pc->arg < (pc - bc->code) && value_truthy(R(pc->r0))) {
            pc->exec_count++;
            /* if (pc->exec_count > 1000 && !recording_trace && !pc->trace) {
                recording_trace = malloc(sizeof(Trace));
                recording_trace->instructions = malloc(sizeof(Instruction) * 128);
                recording_trace->original_offsets = malloc(sizeof(int) * 128);
                recording_trace->count = 0; recording_trace->capacity = 128;
                recording_trace->start_pc = pc;
            } */
        }
        if(value_truthy(R(pc->r0))) pc = bc->code + pc->arg; else pc++; DISPATCH();
    }
    l_OP_FUNC_DEF: pc++; DISPATCH();
    l_OP_CALL: {
        int ac = pc->arg; const char *n = (const char*)pc->ptr;
        FuncEntry *fe = func_get(n);
        call_stack[my_frame_idx].pc = pc; // Update current frame PC
        if (fe && fe->body) {
            TypeSignature sig = compute_signature(&vm_stack[base_reg + pc->r2], fe->param_count);
            
            // 1. PIC Lookup
            if (pc->cache[0].key == (void*)sig && pc->cache[0].val) {
                typedef Value (*JITFunc)(Value *R, int *pc_idx);
                JITFunc f = (JITFunc)pc->cache[0].val;
                int deopt_pc = 0;
                Value res = f(&vm_stack[base_reg + pc->r2], &deopt_pc);
                if (GET_TYPE(res) == VAL_DEOPT) {
                    res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, base_reg + pc->r2);
                }
                if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
                pc++; DISPATCH();
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
                if (GET_TYPE(res) == VAL_DEOPT) {
                    res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, base_reg + pc->r2);
                }
                if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
            } else {
                fe->used++;
                if (fe->used > 100 && fe->spec_count < 8) {
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
                if (ot == OBJ_CLASS) { R(pc->r0) = value_instance(instance_new(AS_CLASS(v))); }
                else if (ot == OBJ_NATIVE_FUNC) { R(pc->r0) = AS_NATIVE_FUNC(v)->func(ac, &R(pc->r2)); }
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
        int ac = pc->arg; FuncEntry *fe = bc->owner;
        if (fe && fe->body) {
            TypeSignature sig = compute_signature(&vm_stack[base_reg + pc->r2], fe->param_count);
            void *jit = func_get_specialization(fe, sig);
            if (jit && jit != (void*)1) {
                typedef Value (*JITFunc)(Value *R, int *pc_idx);
                JITFunc f = (JITFunc)jit; int deopt_pc = 0;
                Value res = f(&vm_stack[base_reg + pc->r2], &deopt_pc);
                if (GET_TYPE(res) == VAL_DEOPT) res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, base_reg + pc->r2);
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
        FuncEntry *fe = func_get(n);
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
            for (int i=0; i<ac && i<fe->param_count; i++) vm_stack[base_reg + i] = vm_stack[arg_base + i];
            for (int i=ac; i<fe->param_count; i++) vm_stack[base_reg + i] = value_none();
            bc = fe->body; pc = bc->code;
            stack_arena_used = old_arena_used;
            if (bc->jit_code) {
                typedef Value (*JITFunc)(Value *R, int *pc_idx);
                int deopt_pc = 0;
                Value res = ((JITFunc)bc->jit_code)(&vm_stack[base_reg], &deopt_pc);
                if (GET_TYPE(res) == VAL_DEOPT) { pc = &bc->code[deopt_pc]; goto l_RESUME_INTERP; }
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
        if(vm_last_class) dict_set(vm_last_class->methods, fe->name, bound_method_new(value_none(), fe));
        else if(vm_last_trait) dict_set(vm_last_trait->methods, fe->name, bound_method_new(value_none(), fe));
        pc++; DISPATCH();
    }
    l_OP_GET_PROPERTY: {
        const char *n = (const char*)pc->ptr; Value o = R(pc->r1);
        if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(o);
            if(ot==OBJ_INSTANCE) {
                Instance *inst = AS_INSTANCE(o);
                int found_cache = 0;
                for (int i=0; i<4; i++) {
                    if (pc->cache[i].key == inst->shape) {
                        R(pc->r0) = inst->fields[pc->cache[i].val];
                        found_cache = 1; break;
                    }
                }
                if (!found_cache) {
                    Shape *s = inst->shape;
                    int found = 0;
                    while (s && s->name && strcmp(s->name, "__root__") != 0) {
                        if (strcmp(s->name, n) == 0) {
                            R(pc->r0) = inst->fields[s->offset];
                            pc->cache[pc->cache_next].key = inst->shape;
                            pc->cache[pc->cache_next].val = s->offset;
                            pc->cache_next = (pc->cache_next + 1) % 4;
                            found = 1; break;
                        }
                        s = s->parent;
                    }
                    if (!found) {
                        Value v = class_get_method(inst->klass, n);
                        if(GET_TYPE(v)==VAL_OBJECT && (v & MASK_PAYLOAD) && GET_OBJ_TYPE(v)==OBJ_BOUND_METHOD && GET_TYPE(AS_BOUND_METHOD(v)->instance)==VAL_NONE)
                            v = bound_method_new(o, AS_BOUND_METHOD(v)->method);
                        R(pc->r0) = v;
                    }
                }
            } else R(pc->r0) = value_none();
        } else if(GET_TYPE(o)==VAL_DICT) R(pc->r0) = dict_get(AS_DICT(o), n);
        else R(pc->r0) = value_none(); pc++; DISPATCH();
    }
    l_OP_SET_PROPERTY: {
        const char *n = (const char*)pc->ptr; Value o = R(pc->r0); Value v = R(pc->r1);
        if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD) && GET_OBJ_TYPE(o)==OBJ_INSTANCE) {
            Instance *inst = AS_INSTANCE(o);
            int found_cache = 0;
            for (int i=0; i<4; i++) {
                if (pc->cache[i].key == inst->shape) {
                    gc_write_barrier((GCObject*)inst, v);
                    inst->fields[pc->cache[i].val] = v;
                    found_cache = 1; break;
                }
            }
            if (!found_cache) {
                Shape *s = inst->shape;
                int found = 0;
                while (s && s->name && strcmp(s->name, "__root__") != 0) {
                    if (strcmp(s->name, n) == 0) {
                        gc_write_barrier((GCObject*)inst, v);
                        inst->fields[s->offset] = v;
                        pc->cache[pc->cache_next].key = inst->shape;
                        pc->cache[pc->cache_next].val = s->offset;
                        pc->cache_next = (pc->cache_next + 1) % 4;
                        found = 1; break;
                    }
                    s = s->parent;
                }
                if (!found) instance_set(inst, n, v);
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
            if(ot==OBJ_CLASS) { R(pc->r0) = value_instance(instance_new(AS_CLASS(callee))); }
            else if(ot==OBJ_BOUND_METHOD) {
                BoundMethod *bm = AS_BOUND_METHOD(callee);
                if(bm->method && bm->method->body) {
                    Value old_self = var_get("self");
                    var_set("self", bm->instance);
                    TypeSignature sig = compute_signature(&vm_stack[base_reg + pc->r2], bm->method->param_count);
                    void *jit = func_get_specialization(bm->method, sig);
                    if (jit && jit != (void*)1) {
                        typedef Value (*JITFunc)(Value *R, int *pc_idx);
                        JITFunc f = (JITFunc)jit;
                        int deopt_pc = 0;
                        call_stack_push(bm->method, NULL);
                        Value res = f(&vm_stack[base_reg + pc->r2], &deopt_pc);
                        call_stack_pop();
                        if (GET_TYPE(res) == VAL_DEOPT) {
                            res = run_bytecode(bm->method->body, &bm->method->body->code[deopt_pc], NULL, base_reg + pc->r2);
                        }
                        var_set("self", old_self);
                        if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
                    } else {
                        bm->method->used++;
                        if (bm->method->used > 100 && bm->method->spec_count < 8) {
                            jit_compile_specialized(bm->method, sig);
                        }
                        Value res = run_bytecode(bm->method->body, NULL, NULL, base_reg + pc->r2);
                        var_set("self", old_self);
                        if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
                    }
                } else R(pc->r0) = value_none();
            } else if(ot==OBJ_CLOSURE) {
                Closure *c = AS_CLOSURE(callee);
                if(c->body) {
                    Value res = run_bytecode(c->body, NULL, c, base_reg + pc->r2);
                    if(has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } R(pc->r0) = res;
                } else R(pc->r0) = value_none();
            } else if (ot == OBJ_NATIVE_FUNC) {
                R(pc->r0) = AS_NATIVE_FUNC(callee)->func(ac, &R(pc->r2));
            } else {
                pending_exception = value_string_copy("Object is not callable");
                has_exception = 1;
                goto l_OP_THROW;
            }
        } else {
            pending_exception = value_string_copy("Attempted to call a non-object value");
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
            R(pc->r0) = dict_get(AS_DICT(coll), AS_STRING(idx_arg));
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
    l_OP_STOP: goto l_FINISH;
    l_OP_SKIP: pc++; DISPATCH();
    l_OP_TRAIT_DEF: { const char *n = (const char*)pc->ptr; Value t = value_trait(trait_new(n)); var_set(n, t); vm_last_trait = AS_TRAIT(t); pc++; DISPATCH(); }
    l_OP_USE_TRAIT: { Value tv = var_get((const char*)pc->ptr); if (GET_TYPE(tv) == VAL_OBJECT && (tv & MASK_PAYLOAD) && GET_OBJ_TYPE(tv) == OBJ_TRAIT && vm_last_class) { Trait *t = AS_TRAIT(tv); for (int i=0; i<t->methods->size; i++) dict_set(vm_last_class->methods, t->methods->keys[i], t->methods->values[i]); } pc++; DISPATCH(); }
    l_OP_INPUT: pc++; DISPATCH();
    l_OP_WRITE: pc++; DISPATCH();
    l_OP_DICT_LITERAL: {
        int n = pc->arg; Dict *d = pc->is_stack_alloc ? gc_alloc_stack(sizeof(Dict), OBJ_DICT) : gc_alloc(sizeof(Dict), OBJ_DICT);
        d->capacity = n; d->keys = calloc(n, sizeof(char*)); d->values = calloc(n, sizeof(Value)); d->size = 0;
        for (int i=0; i<n; i++) {
            Value k = R(pc->r1 + i * 2);
            Value v = R(pc->r1 + i * 2 + 1);
            if (GET_TYPE(k) == VAL_STRING) {
                dict_set(d, AS_STRING(k), v);
            }
        }
        R(pc->r0) = value_dict(d); pc++; DISPATCH();
    }
    l_OP_SLICE: pc++; DISPATCH();
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
        if (parent_fe) capture_size += parent_fe->param_count + parent_fe->local_count;
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
                for (int i=0; i<parent_fe->param_count; i++) {
                    c->captured_names[idx] = parent_fe->params[i];
                    c->captured_values[idx] = vm_stack[base_reg + i];
                    idx++;
                }
                for (int i=0; i<parent_fe->local_count; i++) {
                    c->captured_names[idx] = parent_fe->local_names[i];
                    c->captured_values[idx] = vm_stack[base_reg + parent_fe->param_count + i];
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
        if (GET_TYPE(res) == VAL_TRACE_EXIT) { pc = bc->code + off; goto *dt[pc->op]; }
        pc->op = pc->original_op; goto *dt[pc->op];
    }
    l_OP_USE_FILE: {
        const char *path = (const char*)pc->ptr;
        FILE *f = fopen(path, "r");
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

        compiling_file = path;
        int old_line = line_num; line_num = 1;
        YY_BUFFER_STATE bp = yy_scan_string(buf);
        root = NULL; int parse_res = yyparse(); yy_delete_buffer(bp);
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
    l_OP_ITER_START:
    l_OP_ITER_NEXT:
    l_OP_NOP: pc++; DISPATCH();
    l_RECORD_STEP: {
        pc++; DISPATCH();
    }
    l_OP_DEFAULT: pc++; DISPATCH();

l_FINISH:
    if (has_exception && call_depth == 1) {
        print_error_trace(pending_exception);
        has_exception = 0; // Prevent double report
    }
    call_stack_pop();
    vm_stack_top = old_top;
    stack_arena_used = old_arena_used;
    call_depth--;
    return (has_exception) ? value_none() : R(0);
}

void print_value(Value v) {
    if (v == 0) { printf("none"); return; }
    if ((v & MASK_NAN) != MASK_NAN) { printf("%g", AS_FLOAT(v)); return; }
    uint64_t type = (v & MASK_TYPE) >> 48;
    if (type == VAL_INT) printf("%ld", (long)AS_INT(v));
    else if (type == VAL_BOOL) printf(AS_BOOL(v) ? "true" : "false");
    else if (type == VAL_STRING) printf("%s", AS_STRING(v));
    else if (type == VAL_LIST) { List *l = AS_LIST(v); printf("["); for (int i=0; i<l->size; i++) { print_value(l->items[i]); if (i < l->size - 1) printf(", "); } printf("]"); }
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
static Value os_exit(int argc, Value *args) { exit(argc > 0 ? AS_INT(args[0]) : 0); return value_none(); }
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
        int first = 1;
        for (int i = 0; i < d->capacity; i++) {
            if (d->keys[i]) {
                if (!first) json_append_str(", ", sb);
                json_append_str("\"", sb); json_append_str(d->keys[i], sb); json_append_str("\": ", sb);
                json_stringify_helper(d->values[i], sb);
                first = 0;
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
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_none();
    const char *src = AS_STRING(args[0]);
    return json_parse_value(&src);
}

static Value native_json_stringify(int argc, Value *args) {
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

static Value os_time(int argc, Value *args) { struct timeval tv; gettimeofday(&tv, NULL); return value_float(tv.tv_sec + tv.tv_usec / 1000000.0); }

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
    int server_fd = AS_INT(args[0]), client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) return value_none();
    SSL *ssl = SSL_new(ssl_ctx); SSL_set_fd(ssl, client_fd);
    if (SSL_accept(ssl) <= 0) { SSL_free(ssl); close(client_fd); return value_none(); }
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
    int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
    return value_int(client_fd);
}
static Value http_read(int argc, Value *args) {
    if (argc < 1) return value_none();
    int fd = AS_INT(args[0]);
    char buf[4096];
    int n = read(fd, buf, 4095);
    if (n <= 0) return value_none();
    buf[n] = '\0';
    return value_string_copy(buf);
}
static Value http_write(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[1]) != VAL_STRING) return value_int(-1);
    int fd = AS_INT(args[0]);
    const char *resp = AS_STRING(args[1]);
    if (write(fd, resp, strlen(resp)) < 0) return value_int(-1);
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
                for (; i <= sz - 4; i += 4) {
                    _mm256_storeu_pd(&dr[i], _mm256_add_pd(_mm256_loadu_pd(&da[i]), _mm256_loadu_pd(&db[i])));
                }
                for (; i < sz; i++) dr[i] = da[i] + db[i];
            } else {
                int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
                int i = 0;
                for (; i <= sz - 4; i += 4) {
                    _mm256_storeu_si256((__m256i*)&ir[i], _mm256_add_epi64(_mm256_loadu_si256((__m256i*)&ia[i]), _mm256_loadu_si256((__m256i*)&ib[i])));
                }
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
                for (; i <= sz - 4; i += 4) {
                    _mm256_storeu_pd(&dr[i], _mm256_sub_pd(_mm256_loadu_pd(&da[i]), _mm256_loadu_pd(&db[i])));
                }
                for (; i < sz; i++) dr[i] = da[i] - db[i];
            } else {
                int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
                int i = 0;
                for (; i <= sz - 4; i += 4) {
                    _mm256_storeu_si256((__m256i*)&ir[i], _mm256_sub_epi64(_mm256_loadu_si256((__m256i*)&ia[i]), _mm256_loadu_si256((__m256i*)&ib[i])));
                }
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
                for (; i <= sz - 4; i += 4) {
                    _mm256_storeu_pd(&dr[i], _mm256_mul_pd(_mm256_loadu_pd(&da[i]), _mm256_loadu_pd(&db[i])));
                }
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
                __m256d sum_vec = _mm256_setzero_pd();
                int i = 0;
                for (; i <= sz - 4; i += 4) {
                    sum_vec = _mm256_add_pd(sum_vec, _mm256_mul_pd(_mm256_loadu_pd(&da[i]), _mm256_loadu_pd(&db[i])));
                }
                double temp[4];
                _mm256_storeu_pd(temp, sum_vec);
                double sum = temp[0] + temp[1] + temp[2] + temp[3];
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

#include <sqlite3.h>

static Value native_sqlite_open(int argc, Value *args) {
    if (argc < 1) return value_none();
    sqlite3 *db;
    if (sqlite3_open(AS_STRING(args[0]), &db) != SQLITE_OK) return value_none();
    return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)db);
}

static Value native_sqlite_exec(int argc, Value *args) {
    if (argc < 2) return value_int(-1);
    sqlite3 *db = (sqlite3*)(uintptr_t)(args[0] & MASK_PAYLOAD);
    char *err_msg = 0;
    int rc = sqlite3_exec(db, AS_STRING(args[1]), 0, 0, &err_msg);
    if (rc != SQLITE_OK) { sqlite3_free(err_msg); return value_int(rc); }
    return value_int(0);
}

static Value native_sqlite_query(int argc, Value *args) {
    if (argc < 2) return value_none();
    sqlite3 *db = (sqlite3*)(uintptr_t)(args[0] & MASK_PAYLOAD);
    const char *sql = AS_STRING(args[1]);
    sqlite3_stmt *res;
    if (sqlite3_prepare_v2(db, sql, -1, &res, 0) != SQLITE_OK) return value_none();

    List *rows = list_new(8);
    int cols = sqlite3_column_count(res);

    while (sqlite3_step(res) == SQLITE_ROW) {
        Dict *row = dict_new(cols);
        for (int i = 0; i < cols; i++) {
            const char *col_name = sqlite3_column_name(res, i);
            Value val;
            int type = sqlite3_column_type(res, i);
            if (type == SQLITE_INTEGER) val = value_int(sqlite3_column_int64(res, i));
            else if (type == SQLITE_FLOAT) val = value_float(sqlite3_column_double(res, i));
            else if (type == SQLITE_TEXT) val = value_string_copy((const char*)sqlite3_column_text(res, i));
            else val = value_none();
            dict_set(row, col_name, val);
        }
        list_push(rows, value_dict(row));
    }
    sqlite3_finalize(res);
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
    char buf[4096]; int n = read(fd, buf, 4095);
    if (n <= 0 || buf[0] == '-') return value_none();
    if (buf[0] == '$') {
        char *p = strchr(buf, '\r') + 2;
        char *end = strstr(p, "\r\n");
        if (end) *end = 0;
        return value_string_copy(p);
    }
    return value_none();
}

static Value native_sqlite_close(int argc, Value *args) {
    if (argc < 1) return value_int(-1);
    sqlite3 *db = (sqlite3*)(uintptr_t)(args[0] & MASK_PAYLOAD);
    return value_int(sqlite3_close(db));
}

static Value native_stacktrace(int argc, Value *args) {
    print_stack_trace();
    return value_none();
}

static Value native_profile_start(int argc, Value *args) {
    profiling_active = 1;
    return value_none();
}

static int profile_compare(const void *a, const void *b) {
    typedef struct { const char *file; int line; long count; } ProfileEntry;
    return (int)(((ProfileEntry*)b)->count - ((ProfileEntry*)a)->count);
}

static Value native_profile_stop(int argc, Value *args) {
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
    int sz = (argc > 0) ? (int)AS_INT(args[0]) : 0;
    return value_packed(packed_array_new(PACKED_FLOAT64, sz));
}

static Value native_tensor_zeros(int argc, Value *args) {
    int sz = (argc > 0) ? (int)AS_INT(args[0]) : 0;
    PackedArray *pa = packed_array_new(PACKED_FLOAT64, sz);
    memset(pa->data, 0, sz * sizeof(double));
    return value_packed(pa);
}

static Value native_tensor_transpose(int argc, Value *args) {
    if (argc < 3) return value_none(); // tensor, rows, cols
    PackedArray *src = AS_PACKED_ARRAY(args[0]);
    int rows = (int)val_to_double(args[1]), cols = (int)val_to_double(args[2]);
    PackedArray *res = packed_array_new(src->type, src->size);
    if (src->type == PACKED_FLOAT64) {
        double *s = (double*)src->data, *r = (double*)res->data;
        for (int i = 0; i < rows; i++) for (int j = 0; j < cols; j++) r[j * rows + i] = s[i * cols + j];
    }
    return value_packed(res);
}

#include <immintrin.h>

static Value native_tensor_matmul(int argc, Value *args) {
    if (argc < 6) return value_none(); // A, rowsA, colsA, B, rowsB, colsB
    PackedArray *pa = AS_PACKED_ARRAY(args[0]);
    int rA = (int)val_to_double(args[1]), cA = (int)val_to_double(args[2]);
    PackedArray *pb = AS_PACKED_ARRAY(args[3]);
    int rB = (int)val_to_double(args[4]), cB = (int)val_to_double(args[5]);
    if (cA != rB) return value_none();
    PackedArray *res = packed_array_new(pa->type, rA * cB);
    double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
    
    for (int i = 0; i < rA; i++) {
        for (int j = 0; j < cB; j++) {
            __m256d sum_vec = _mm256_setzero_pd();
            int k = 0;
            for (; k <= cA - 4; k += 4) {
                __m256d va = _mm256_loadu_pd(&da[i * cA + k]);
                __m256d vb = _mm256_set_pd(db[(k+3) * cB + j], db[(k+2) * cB + j], db[(k+1) * cB + j], db[k * cB + j]);
                sum_vec = _mm256_add_pd(sum_vec, _mm256_mul_pd(va, vb));
            }
            double temp[4];
            _mm256_storeu_pd(temp, sum_vec);
            double sum = temp[0] + temp[1] + temp[2] + temp[3];
            for (; k < cA; k++) sum += da[i * cA + k] * db[k * cB + j];
            dr[i * cB + j] = sum;
        }
    }
    return value_packed(res);
}

#include <X11/Xlib.h>
#include <X11/Xutil.h>

static Display *gui_display = NULL;
static Window gui_window;
static GC gui_gc;
static int gui_screen;

static Value native_gui_open(int argc, Value *args) {
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

static Value native_gui_poll(int argc, Value *args) {
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

static Value native_os_getenv(int argc, Value *args) {
    if (argc < 1) return value_none();
    char *v = getenv(AS_STRING(args[0]));
    return v ? value_string_copy(v) : value_none();
}

static Value native_string_builder_new(int argc, Value *args) {
    StringBuilder *sb = gc_alloc(sizeof(StringBuilder), OBJ_STRING_BUILDER);
    sb->capacity = 32;
    sb->size = 0;
    sb->buffer = malloc(sb->capacity);
    sb->buffer[0] = '\0';
    return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)sb);
}

static Value native_string_builder_append(int argc, Value *args) {
    if (argc < 2) return args[0];
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
    StringBuilder *sb = (StringBuilder*)(uintptr_t)(args[0] & MASK_PAYLOAD);
    return value_string_copy(sb->buffer);
}

static void std_init(void) {
    var_set("len", native_func_new("len", native_len));
    var_set("range", native_func_new("range", native_range));
    var_set("packed_int", native_func_new("packed_int", native_packed_int));
    var_set("packed_float", native_func_new("packed_float", native_packed_float));
    var_set("packed_add", native_func_new("packed_add", native_packed_add));
    var_set("packed_sub", native_func_new("packed_sub", native_packed_sub));
    var_set("net_close", native_func_new("net_close", net_close));
    var_set("text_builder", native_func_new("text_builder", native_string_builder_new));
    var_set("append", native_func_new("append", native_string_builder_append));
    var_set("build", native_func_new("build", native_string_builder_build));

    Dict *debug = dict_new(4);
    dict_set(debug, "stacktrace", native_func_new("stacktrace", native_stacktrace));
    dict_set(debug, "profile_start", native_func_new("profile_start", native_profile_start));
    dict_set(debug, "profile_stop", native_func_new("profile_stop", native_profile_stop));
    var_set("debug", value_dict(debug));

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
    var_set("os", value_dict(os));

    Dict *fs = dict_new(4);
    dict_set(fs, "readdir", native_func_new("readdir", native_fs_readdir));
    dict_set(fs, "stat", native_func_new("stat", native_fs_stat));
    var_set("fs", value_dict(fs));

    Dict *net = dict_new(4); dict_set(net, "listen", native_func_new("listen", net_listen)); dict_set(net, "close", native_func_new("close", net_close)); var_set("net", value_dict(net));
    Dict *http = dict_new(4); 
    dict_set(http, "listen", native_func_new("listen", http_listen)); 
    dict_set(http, "accept", native_func_new("accept", http_accept)); 
    dict_set(http, "read", native_func_new("read", http_read)); 
    dict_set(http, "send", native_func_new("send", http_write)); 
    dict_set(http, "close", native_func_new("close", http_close)); 
    var_set("http", value_dict(http));
    Dict *ssl = dict_new(4); dict_set(ssl, "listen", native_func_new("listen", ssl_listen)); dict_set(ssl, "accept", native_func_new("accept", ssl_accept)); var_set("ssl", value_dict(ssl));
    Dict *json = dict_new(4); 
    dict_set(json, "stringify", native_func_new("stringify", native_json_stringify)); 
    dict_set(json, "parse", native_func_new("parse", native_json_parse));
    var_set("json", value_dict(json));
}

Node *root = NULL;
int main(int argc, char **argv) {
    var_table_init(); func_table_init(); vm_stack_init(); intern_table_init(); std_init();
    next_free_reg = 0;
    if (argc > 1) {
        if (strcmp(argv[1], "--lsp") == 0) { handle_client(0); return 0; }
        yyin = fopen(argv[1], "r");
        if(yyin) { 
            compiling_file = argv[1];
            yyparse(); 
            if(root) { 
                Bytecode *main_bc = bc_new(&main_fe); compiling_bc = main_bc; next_free_reg = 0; 
                main_fe.body = main_bc;
                compile(root); 
                main_bc->local_count = next_free_reg;
                bc_emit(main_bc, OP_RETURN, 0, 0); 
                optimize_bytecode(main_bc); 
                run_bytecode(main_bc, NULL, NULL, 0);
                if (has_exception) {
                    print_error_trace(pending_exception);
                }
            } 
            fclose(yyin); 
        }
    } else {
        printf("GlowLang v1.0 Interactive REPL\nType 'exit' to quit.\n");
        char line_buf[4096];
        compiling_file = "<stdin>";
        
        Bytecode *main_bc = bc_new(&main_fe); compiling_bc = main_bc; next_free_reg = 0;
        main_fe.body = main_bc;

        while (1) {
            printf("glow> ");
            if (!fgets(line_buf, sizeof(line_buf), stdin)) break;
            if (strncmp(line_buf, "exit", 4) == 0) break;
            
            root = NULL;
            glow_clear_errors();
            
            YY_BUFFER_STATE bp = yy_scan_string(line_buf);
            yyparse();
            yy_delete_buffer(bp);
            
            if (glow_error_count == 0 && root) {
                int old_count = main_bc->count;
                compile(root);
                
                if (main_bc->count > old_count) {
                    main_bc->local_count = next_free_reg;
                    int ret_idx = bc_emit(main_bc, OP_RETURN, 0, 0);
                    
                    Instruction *start_pc = &main_bc->code[old_count];
                    Value res = run_bytecode(main_bc, start_pc, NULL, 0);
                    
                    if (has_exception) {
                        print_error_trace(pending_exception);
                        has_exception = 0;
                    } else if (res != 0 && GET_TYPE(res) != VAL_NONE) {
                        print_value(res);
                        printf("\n");
                    }
                    
                    main_bc->count = ret_idx; // Remove temporary return
                }
            }
        }
    }
    return 0;
}
