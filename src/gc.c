#include "glowlang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <setjmp.h>
#ifdef _WIN32
#include "win_pthread.h"
#else
#include <pthread.h>
#endif
#include <sqlite3.h>

/* ================================================================
 *  Garbage collector
 *
 *  Non-moving mark-sweep. Roots are:
 *    - precise: globals, constant strings, every thread's VM register stack,
 *      pending exceptions, values handed to threads that have not started;
 *    - conservative: every word on every thread's native C stack (and its
 *      saved callee-saved registers). This keeps alive objects that native
 *      code holds only in C locals while it allocates more (e.g. a list being
 *      filled, the halves of a string concatenation), which a precise-only
 *      scan would free from under it.
 *
 *  Collection only runs on the thread that holds the GIL; every other thread
 *  is parked with its stack bounds and registers recorded (see vm.c).
 *
 *  A generational mode (minor collections of young objects through a
 *  remembered set) is available with GLOW_GC_GENERATIONAL=1. It is off by
 *  default because it is only correct when every store of a young object into
 *  an old one goes through gc_write_barrier, and not every store site does.
 * ================================================================ */

#if defined(__clang__) || defined(__GNUC__)
#define GC_NO_SANITIZE __attribute__((no_sanitize_address))
#define GC_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define GC_NO_SANITIZE
#define GC_NOINLINE __declspec(noinline)
#else
#define GC_NO_SANITIZE
#define GC_NOINLINE
#endif

static void gc_mark_value(Value v);

GCObject *gc_objects = NULL;
size_t gc_allocated = 0;
#define GC_MIN_THRESHOLD ((size_t)32 * 1024 * 1024)
size_t gc_threshold = GC_MIN_THRESHOLD;
size_t gc_allocated_at_last_gc = 0;

static int gc_stress = 0;          /* GLOWLANG_GC_STRESS=1: collect very often */
static int gc_generational = 0;    /* GLOW_GC_GENERATIONAL=1: enable minor GCs */
static int gc_in_collection = 0;

extern void gc_mark_pending_spawn_roots(void (*mark)(Value));

/* ---- Heap blocks -------------------------------------------------------
   Small objects are bump-allocated from 1MB blocks. Each block keeps a bitmap
   with one bit per 8-byte granule marking where an object starts, so the
   conservative scanner can map any address to the object containing it. */
#define GC_BLOCK_BYTES ((size_t)1 << 20)
#define GC_GRANULE 8
#define GC_BITMAP_WORDS (GC_BLOCK_BYTES / GC_GRANULE / 64)

typedef struct GCBlock {
    struct GCBlock *next;
    size_t size;
    size_t used;
    size_t live_count;
    uint64_t starts[GC_BITMAP_WORDS];
    char data[];  /* 16-byte aligned: the header above is a multiple of 16 */
} GCBlock;

#define GC_BLOCK_DATA_SIZE (GC_BLOCK_BYTES - sizeof(GCBlock))

static GCBlock *gc_block_list = NULL;
static GCBlock *gc_current_block = NULL;

/* Sorted address indexes of blocks and of large (individually allocated)
   objects, for address -> object lookups during conservative scanning. */
typedef struct { uintptr_t *addr; int count; int cap; } AddrIndex;
static AddrIndex gc_block_index = {0};
static AddrIndex gc_large_index = {0};

static int addr_index_floor(const AddrIndex *ix, uintptr_t a) {
    int lo = 0, hi = ix->count - 1, ans = -1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (ix->addr[mid] <= a) { ans = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return ans;
}

static void addr_index_insert(AddrIndex *ix, uintptr_t a) {
    if (ix->count == ix->cap) {
        ix->cap = ix->cap ? ix->cap * 2 : 64;
        ix->addr = realloc(ix->addr, sizeof(uintptr_t) * (size_t)ix->cap);
    }
    int pos = addr_index_floor(ix, a) + 1;
    memmove(&ix->addr[pos + 1], &ix->addr[pos], sizeof(uintptr_t) * (size_t)(ix->count - pos));
    ix->addr[pos] = a;
    ix->count++;
}

static void addr_index_remove(AddrIndex *ix, uintptr_t a) {
    int pos = addr_index_floor(ix, a);
    if (pos < 0 || ix->addr[pos] != a) return;
    memmove(&ix->addr[pos], &ix->addr[pos + 1], sizeof(uintptr_t) * (size_t)(ix->count - pos - 1));
    ix->count--;
}

static inline int gc_highest_bit(uint64_t x) {
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long idx;
    _BitScanReverse64(&idx, x);
    return (int)idx;
#else
    return 63 - __builtin_clzll(x);
#endif
}

static inline void gc_block_set_start(GCBlock *b, void *obj) {
    size_t g = (size_t)((char*)obj - b->data) / GC_GRANULE;
    b->starts[g / 64] |= (uint64_t)1 << (g % 64);
}

static inline void gc_block_clear_start(GCBlock *b, void *obj) {
    size_t g = (size_t)((char*)obj - b->data) / GC_GRANULE;
    b->starts[g / 64] &= ~((uint64_t)1 << (g % 64));
}

static uintptr_t gc_heap_lo = 0, gc_heap_hi = 0;

static void gc_update_heap_bounds(void) {
    uintptr_t lo = UINTPTR_MAX, hi = 0;
    if (gc_block_index.count) {
        lo = gc_block_index.addr[0];
        hi = gc_block_index.addr[gc_block_index.count - 1] + GC_BLOCK_BYTES;
    }
    if (gc_large_index.count) {
        uintptr_t llo = gc_large_index.addr[0];
        GCObject *last = (GCObject*)gc_large_index.addr[gc_large_index.count - 1];
        uintptr_t lhi = (uintptr_t)last + last->sz;
        if (llo < lo) lo = llo;
        if (lhi > hi) hi = lhi;
    }
    gc_heap_lo = lo; gc_heap_hi = hi;
}

/* Map an arbitrary address to the live heap object containing it, or NULL. */
GC_NO_SANITIZE static GCObject *gc_find_object(uintptr_t p) {
    if (p < gc_heap_lo || p >= gc_heap_hi) return NULL;
    int bi = addr_index_floor(&gc_block_index, p);
    if (bi >= 0) {
        GCBlock *b = (GCBlock*)gc_block_index.addr[bi];
        if (p < (uintptr_t)b + GC_BLOCK_BYTES) {
            uintptr_t d = (uintptr_t)b->data;
            if (p < d || p >= d + b->used) return NULL;
            size_t g = (size_t)(p - d) / GC_GRANULE;
            size_t w = g / 64;
            uint64_t bits = b->starts[w] & (((uint64_t)2 << (g % 64)) - 1);
            for (;;) {
                if (bits) {
                    size_t sg = w * 64 + (size_t)gc_highest_bit(bits);
                    GCObject *o = (GCObject*)(d + sg * GC_GRANULE);
                    return (p < (uintptr_t)o + o->sz) ? o : NULL;
                }
                if (w == 0) return NULL;
                bits = b->starts[--w];
            }
        }
    }
    int li = addr_index_floor(&gc_large_index, p);
    if (li >= 0) {
        GCObject *o = (GCObject*)gc_large_index.addr[li];
        if (p < (uintptr_t)o + o->sz) return o;
    }
    return NULL;
}

typedef struct RememberedSet {
    GCObject *obj;
    struct RememberedSet *next;
} RememberedSet;
static RememberedSet *gc_remembered_set = NULL;

/* Frozen string literals produced by OP_CONST_STRING are allocated once and
   shared (see vm_get_const_string in vm.c). They are kept alive for the life
   of the process by being registered here as permanent GC roots, so the
   collector never frees a literal that later loads still reference. */
static Value *gc_const_roots = NULL;
static int gc_const_roots_cap = 0;
static int gc_const_roots_len = 0;

void gc_register_constant_root(Value v) {
    pthread_mutex_lock(&gc_mutex);
    if (gc_const_roots_len == gc_const_roots_cap) {
        int ncap = gc_const_roots_cap ? gc_const_roots_cap * 2 : 64;
        gc_const_roots = realloc(gc_const_roots, ncap * sizeof(Value));
        gc_const_roots_cap = ncap;
    }
    gc_const_roots[gc_const_roots_len++] = v;
    pthread_mutex_unlock(&gc_mutex);
}

void gc_write_barrier(GCObject *src, Value target) {
    if (!gc_generational) return;
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
    GCBlock *b = malloc(GC_BLOCK_BYTES);
    if (!b) { fprintf(stderr, "Fatal: out of memory allocating GC block\n"); abort(); }
    b->size = GC_BLOCK_DATA_SIZE;
    b->used = 0;
    b->live_count = 0;
    memset(b->starts, 0, sizeof(b->starts));
    b->next = gc_block_list;
    gc_block_list = b;
    addr_index_insert(&gc_block_index, (uintptr_t)b);
    gc_update_heap_bounds();
    return b;
}

/* Find an existing block with at least `sz` bytes of free tail space. */
static GCBlock* gc_block_find_fit(size_t sz) {
    for (GCBlock *b = gc_block_list; b; b = b->next) {
        if (b->size - b->used >= sz) return b;
    }
    return NULL;
}

/* Free every block whose live_count has dropped to zero. Called at the end of
   a collection, after the object sweep has unlinked every dead object from
   gc_objects, so no live object can still reference a freed block. */
static void gc_reclaim_empty_blocks(void) {
    GCBlock **pp = &gc_block_list;
    int changed = 0;
    while (*pp) {
        GCBlock *b = *pp;
        if (b->live_count == 0) {
            if (b == gc_current_block) gc_current_block = NULL;
            *pp = b->next;
            addr_index_remove(&gc_block_index, (uintptr_t)b);
            free(b);
            changed = 1;
        } else {
            pp = &b->next;
        }
    }
    if (!gc_current_block && gc_block_list) gc_current_block = gc_block_list;
    if (changed) gc_update_heap_bounds();
}

static GCObject **grey_stack = NULL;
static int grey_capacity = 0;
static int grey_count = 0;

static void grey_push_raw(GCObject *obj) {
    if (grey_count >= grey_capacity) {
        grey_capacity = grey_capacity < 256 ? 256 : grey_capacity * 2;
        grey_stack = realloc(grey_stack, grey_capacity * sizeof(GCObject*));
    }
    grey_stack[grey_count++] = obj;
}

static void push_grey(GCObject *obj) {
    if (!obj || obj->marked) return;
    obj->marked = 1;
    grey_push_raw(obj);
}

static inline int value_is_heap_ref(Value v) {
    ValueType t = GET_TYPE(v);
    return (t == VAL_OBJECT || t == VAL_LIST || t == VAL_DICT || t == VAL_STRING) && (v & MASK_PAYLOAD);
}

static void gc_mark_value(Value v) {
    if (value_is_heap_ref(v)) push_grey((GCObject*)(uintptr_t)(v & MASK_PAYLOAD));
}

/* Root slots (VM registers, globals) can hold stale or non-heap payloads, so
   only mark them when they really point at the start of a live object. */
static void gc_mark_root_checked(Value v) {
    if (!value_is_heap_ref(v)) return;
    uintptr_t p = (uintptr_t)(v & MASK_PAYLOAD);
    GCObject *o = gc_find_object(p);
    if (o && (uintptr_t)o == p) push_grey(o);
}

static void gc_trace_object(GCObject *obj, void (*mark_value)(Value), void (*mark_obj)(GCObject*)) {
    switch (obj->type) {
        case OBJ_LIST: { List *l = (List*)obj; for (int i=0; i<l->size; i++) mark_value(l->items[i]); break; }
        case OBJ_ITERATOR: { Iterator *it = (Iterator*)obj; mark_value(it->iterable); break; }
        case OBJ_DICT: { Dict *d = (Dict*)obj; for (int i=0; i<d->capacity; i++) { if (GET_TYPE(d->keys[i]) != VAL_NONE) { mark_value(d->values[i]); } } break; }
        case OBJ_SQLITE_ITER: break;
        case OBJ_SQLITE_HANDLE: break;
        case OBJ_BIGINT: break;
        case OBJ_CLASS: {
            Class *c = (Class*)obj;
            mark_obj((GCObject*)c->parent);
            mark_obj((GCObject*)c->methods);
            mark_obj((GCObject*)c->root_shape);
            mark_obj((GCObject*)c->stable_shape);
            break;
        }
        case OBJ_SHAPE: {
            Shape *s = (Shape*)obj;
            mark_obj((GCObject*)s->parent);
            mark_obj((GCObject*)s->transitions);
            break;
        }
        case OBJ_TRAIT: {
            Trait *t = (Trait*)obj;
            mark_obj((GCObject*)t->methods);
            break;
        }
        case OBJ_INSTANCE: {
            Instance *ins = (Instance*)obj;
            mark_obj((GCObject*)ins->klass);
            mark_obj((GCObject*)ins->properties);
            break;
        }
        case OBJ_BOUND_METHOD: { BoundMethod *bm = (BoundMethod*)obj; mark_value(bm->instance); break; }
        case OBJ_CLOSURE: {
            Closure *c = (Closure*)obj;
            if (c->captured_values) for (int i = 0; i < c->captured_count; i++) mark_value(c->captured_values[i]);
            break;
        }
        case OBJ_ROPE: {
            RopeNode *rn = (RopeNode*)obj;
            if (rn->is_leaf) mark_value(rn->leaf.flat_string);
            else {
                mark_obj((GCObject*)rn->internal.left);
                mark_obj((GCObject*)rn->internal.right);
            }
            break;
        }
        case OBJ_PACKED_ARRAY: {
            PackedArray *pa = (PackedArray*)obj;
            if (pa->is_view && pa->base) mark_obj((GCObject*)pa->base);
            break;
        }
        case OBJ_STRING_BUILDER: break;
        default: break;
    }
}

static void process_grey_stack(void) {
    while (grey_count > 0) {
        GCObject *obj = grey_stack[--grey_count];
        gc_trace_object(obj, gc_mark_value, push_grey);
    }
}

static void gc_free_object_payload(GCObject *unmarked) {
    if (unmarked->type == OBJ_LIST) list_free_storage((List*)unmarked);
    else if (unmarked->type == OBJ_DICT) dict_free_storage((Dict*)unmarked);
    else if (unmarked->type == OBJ_SQLITE_ITER) { if (((SQLiteIter*)unmarked)->stmt) sqlite3_finalize(((SQLiteIter*)unmarked)->stmt); }
    /* close_v2: the handle and its query iterators may be swept in any order. */
    else if (unmarked->type == OBJ_SQLITE_HANDLE) { if (((SQLiteHandle*)unmarked)->db) sqlite3_close_v2(((SQLiteHandle*)unmarked)->db); }
    else if (unmarked->type == OBJ_INSTANCE) { /* No fields array to free, Dict is handled as OBJ_DICT */ }
    else if (unmarked->type == OBJ_CLASS) free((char*)((Class*)unmarked)->name);
    else if (unmarked->type == OBJ_TRAIT) free((char*)((Trait*)unmarked)->name);
    else if (unmarked->type == OBJ_PACKED_ARRAY) { if (!((PackedArray*)unmarked)->is_view) tensor_free(((PackedArray*)unmarked)->data, ((PackedArray*)unmarked)->capacity * (((PackedArray*)unmarked)->type == PACKED_FLOAT64 ? sizeof(double) : sizeof(int64_t)), ((PackedArray*)unmarked)->device); }
    else if (unmarked->type == OBJ_STRING_BUILDER) free(((StringBuilder*)unmarked)->buffer);
    else if (unmarked->type == OBJ_NATIVE_FUNC) { NativeFunc *nf = (NativeFunc*)unmarked; if (nf->finalize) nf->finalize(nf->data); }
    else if (unmarked->type == OBJ_CLOSURE) {
        Closure *c = (Closure*)unmarked;
        if (c->captured_names) free(c->captured_names);
        if (c->captured_values) free(c->captured_values);
    }
}

/* Release one dead object's memory and its address-index entry. */
static void gc_release_object(GCObject *obj) {
    gc_free_object_payload(obj);
    if (gc_allocated >= obj->sz) gc_allocated -= obj->sz; else gc_allocated = 0;
    if (obj->is_block_allocated && obj->block_ptr) {
        GCBlock *b = (GCBlock*)obj->block_ptr;
        gc_block_clear_start(b, obj);
        if (b->live_count > 0) b->live_count--;
    } else {
        addr_index_remove(&gc_large_index, (uintptr_t)obj);
        free(obj);
    }
}

/* ---- Conservative native-stack scanning ---- */
GC_NO_SANITIZE static void gc_scan_words(const void *lo, const void *hi, void (*mark_obj)(GCObject*)) {
    uintptr_t a = ((uintptr_t)lo + (sizeof(uintptr_t) - 1)) & ~(uintptr_t)(sizeof(uintptr_t) - 1);
    uintptr_t end = (uintptr_t)hi;
    for (; a + sizeof(uintptr_t) <= end; a += sizeof(uintptr_t)) {
        uintptr_t w = *(const uintptr_t*)a;
        GCObject *o = gc_find_object(w);
        if (!o && !IS_FLOAT_BITS(w)) o = gc_find_object((uintptr_t)(w & MASK_PAYLOAD));
        if (o) mark_obj(o);
    }
}

static void gc_scan_native_stacks(void *current_lo, void (*mark_obj)(GCObject*)) {
    for (int i = 0; i < active_stack_count; i++) {
        GlowThreadCtx *ctx = active_stacks[i].ctx;
        if (!ctx || !ctx->stack_hi) continue;
        if (ctx == &glow_thread_ctx) {
            if ((char*)current_lo < (char*)ctx->stack_hi) gc_scan_words(current_lo, ctx->stack_hi, mark_obj);
        } else {
            void *lo = ctx->stack_lo;
            if (lo && (char*)lo < (char*)ctx->stack_hi) gc_scan_words(lo, ctx->stack_hi, mark_obj);
            gc_scan_words(&ctx->regs, (char*)&ctx->regs + sizeof(ctx->regs), mark_obj);
        }
    }
}

static void gc_mark_precise_roots(void (*mark_root)(Value)) {
    for (int i=0; i<global_table.count; i++) mark_root(global_table.values[i]);
    if (vm_last_class) mark_root(value_class(vm_last_class));
    if (vm_last_trait) mark_root(value_trait(vm_last_trait));
    for (int i = 0; i < gc_const_roots_len; i++) mark_root(gc_const_roots[i]);
    gc_mark_pending_spawn_roots(mark_root);
    for (int i=0; i<active_stack_count; i++) {
        Value *stk = *(active_stacks[i].vm_stack_ptr);
        int stk_sp = *(active_stacks[i].top_ptr);
        if (stk) {
            int lim = stk_sp + 65536;
            if (lim > vm_stack_size) lim = vm_stack_size;
            for (int j=0; j<lim; j++) mark_root(stk[j]);
        }
        mark_root(*(active_stacks[i].pending_exc_ptr));
    }
}

static void gc_clear_remembered_set(void) {
    while (gc_remembered_set) {
        RememberedSet *next = gc_remembered_set->next;
        gc_remembered_set->obj->in_remembered_set = 0;
        free(gc_remembered_set);
        gc_remembered_set = next;
    }
}

static void gc_set_next_threshold(void) {
    gc_allocated_at_last_gc = gc_allocated;
    if (gc_stress) { gc_threshold = gc_allocated + 64 * 1024; return; }
    size_t next_threshold = gc_allocated * 2;
    if (next_threshold < GC_MIN_THRESHOLD) next_threshold = GC_MIN_THRESHOLD;
    gc_threshold = next_threshold;
}

GC_NOINLINE static void gc_full_collect_impl(void) {
    char *stack_marker = GLOW_STACK_POS();
    gc_mark_precise_roots(gc_mark_root_checked);
    gc_scan_native_stacks(stack_marker, push_grey);
    process_grey_stack();

    GCObject **p = &gc_objects;
    while (*p) {
        GCObject *obj = *p;
        if (!obj->marked) {
            *p = obj->next;
            gc_release_object(obj);
        } else {
            obj->marked = 0;
            obj->generation = 1;
            p = &obj->next;
        }
    }
    gc_clear_remembered_set();
    gc_reclaim_empty_blocks();
    gc_set_next_threshold();
}

void gc_full_collect(void) {
    if (gc_in_collection) return;
    gc_in_collection = 1;
    /* Spill callee-saved registers into this frame so the conservative scan
       of the current thread's stack sees values that only live in registers. */
    jmp_buf regs;
    setjmp(regs);
    gc_full_collect_impl();
    /* Keep `regs` observably live across the call. */
    if (((volatile char*)&regs)[0] == 0x7f && gc_in_collection == 2) abort();
    gc_in_collection = 0;
}

/* ---- Generational (minor) collection, opt-in ---- */
static void push_grey_minor(GCObject *obj) {
    if (!obj || obj->marked || obj->generation != 0) return;
    obj->marked = 1;
    grey_push_raw(obj);
}

static void gc_mark_value_minor(Value v) {
    if (value_is_heap_ref(v)) push_grey_minor((GCObject*)(uintptr_t)(v & MASK_PAYLOAD));
}

static void gc_mark_root_checked_minor(Value v) {
    if (!value_is_heap_ref(v)) return;
    uintptr_t p = (uintptr_t)(v & MASK_PAYLOAD);
    GCObject *o = gc_find_object(p);
    if (o && (uintptr_t)o == p) push_grey_minor(o);
}

GC_NOINLINE static void gc_minor_collect_impl(void) {
    char *stack_marker = GLOW_STACK_POS();
    gc_mark_precise_roots(gc_mark_root_checked_minor);
    gc_scan_native_stacks(stack_marker, push_grey_minor);
    for (RememberedSet *rs = gc_remembered_set; rs; rs = rs->next) {
        if (rs->obj) gc_trace_object(rs->obj, gc_mark_value_minor, push_grey_minor);
    }
    while (grey_count > 0) {
        GCObject *obj = grey_stack[--grey_count];
        gc_trace_object(obj, gc_mark_value_minor, push_grey_minor);
    }

    GCObject **p = &gc_objects;
    while (*p) {
        GCObject *obj = *p;
        if (obj->generation == 0) {
            if (!obj->marked) {
                *p = obj->next;
                gc_release_object(obj);
                continue;
            }
            obj->marked = 0;
            obj->generation = 1;
        }
        p = &obj->next;
    }
    gc_clear_remembered_set();
    gc_reclaim_empty_blocks();
    gc_allocated_at_last_gc = gc_allocated;
}

void gc_minor_collect(void) {
    if (!gc_generational) { gc_full_collect(); return; }
    if (gc_in_collection) return;
    gc_in_collection = 1;
    jmp_buf regs;
    setjmp(regs);
    gc_minor_collect_impl();
    if (((volatile char*)&regs)[0] == 0x7f && gc_in_collection == 2) abort();
    gc_in_collection = 0;
    if (gc_allocated > gc_threshold) gc_full_collect();
}

void* gc_alloc(size_t sz, ObjType type) {
    static int gc_env_checked = 0, gc_nogc = 0, gc_forcefull = 0;
    if (!gc_env_checked) {
        gc_env_checked = 1;
        const char *stress = getenv("GLOWLANG_GC_STRESS");
        if (stress && strcmp(stress, "1") == 0) { gc_stress = 1; gc_threshold = 64 * 1024; }
        gc_generational = getenv("GLOW_GC_GENERATIONAL") != NULL;
        gc_nogc = getenv("GLOW_NOGC") != NULL;
        gc_forcefull = getenv("GLOW_FORCEFULL") != NULL;
    }
    if (!gc_nogc && !gc_in_collection) {
        if (gc_forcefull || gc_allocated > gc_threshold) {
            gc_full_collect();
        } else if (gc_generational) {
            size_t since = (gc_allocated >= gc_allocated_at_last_gc) ? (gc_allocated - gc_allocated_at_last_gc) : 0;
            if (since >= 2 * 1024 * 1024) gc_minor_collect();
        }
    }

    sz = (sz + (GC_GRANULE - 1)) & ~(size_t)(GC_GRANULE - 1);
    GCObject *obj = NULL;
    if (sz < GC_BLOCK_DATA_SIZE / 4) {
        if (!gc_current_block || gc_current_block->used + sz > gc_current_block->size) {
            /* Reuse a block that still has free tail space before allocating a
               new one, so the arena does not grow unbounded. */
            GCBlock *fit = gc_block_find_fit(sz);
            gc_current_block = fit ? fit : gc_block_new();
        }
        obj = (GCObject*)(gc_current_block->data + gc_current_block->used);
        gc_current_block->used += sz;
        gc_current_block->live_count++;
        memset(obj, 0, sz);
        obj->is_block_allocated = 1;
        obj->block_ptr = gc_current_block;
        gc_block_set_start(gc_current_block, obj);
    } else {
        obj = calloc(1, sz);
        if (!obj) { fprintf(stderr, "Fatal: out of memory (%zu bytes)\n", sz); abort(); }
        obj->is_block_allocated = 0;
        obj->block_ptr = NULL;
        obj->sz = sz;
        addr_index_insert(&gc_large_index, (uintptr_t)obj);
        gc_update_heap_bounds();
    }
    obj->generation = 0;
    obj->is_stack_allocated = 0;
    obj->type = type; obj->sz = sz; obj->next = gc_objects; gc_objects = obj; gc_allocated += sz;
    return obj;
}

/* Escape-analysed "stack" allocation is disabled: the analysis did not prove
   that every use keeps the object frame-local (method-call arguments, for
   one, were not treated as escaping), so such objects could outlive their
   frame. Everything is heap allocated; the arena API is kept for callers. */
THREAD_LOCAL char *stack_arena = NULL;
THREAD_LOCAL size_t stack_arena_used = 0;

void* gc_alloc_stack(size_t sz, ObjType type) {
    return gc_alloc(sz, type);
}

void gc_free_stack_arena_range(size_t old_used, size_t new_used) {
    (void)old_used; (void)new_used;
}


Value native_verify_gc(int argc, Value *args) {
    (void)argc; (void)args;
    pthread_mutex_lock(&gc_mutex);
    GCObject *curr = gc_objects;
    while (curr) {
        if (curr->type > OBJ_SQLITE_HANDLE) {
            fprintf(stderr, "Heap Corruption Detected: Invalid object type %d at %p\n", curr->type, (void*)curr);
            abort();
        }
        if (curr->sz <= 0 || curr->sz > 1024 * 1024 * 1024) {
            fprintf(stderr, "Heap Corruption Detected: Invalid size %zu for object type %d at %p\n", curr->sz, curr->type, (void*)curr);
            abort();
        }
        switch (curr->type) {
            case OBJ_LIST: {
                List *l = (List*)curr;
                if (l->items == NULL && l->capacity > 0) {
                    fprintf(stderr, "Heap Corruption Detected: NULL items for List with capacity %d at %p\n", l->capacity, (void*)curr);
                    abort();
                }
                if (l->size < 0 || l->capacity < 0 || l->size > l->capacity) {
                    fprintf(stderr, "Heap Corruption Detected: List invalid size/capacity %d/%d at %p\n", l->size, l->capacity, (void*)curr);
                    abort();
                }
                break;
            }
            case OBJ_DICT: {
                Dict *d = (Dict*)curr;
                if (d->keys == NULL && d->capacity > 0) {
                    fprintf(stderr, "Heap Corruption Detected: NULL keys for Dict at %p\n", (void*)curr);
                    abort();
                }
                if (d->values == NULL && d->capacity > 0) {
                    fprintf(stderr, "Heap Corruption Detected: NULL values for Dict at %p\n", (void*)curr);
                    abort();
                }
                if (d->size < 0 || d->capacity < 0 || d->size > d->capacity) {
                    fprintf(stderr, "Heap Corruption Detected: Dict invalid size/capacity %d/%d at %p\n", d->size, d->capacity, (void*)curr);
                    abort();
                }
                break;
            }
            case OBJ_PACKED_ARRAY: {
                PackedArray *pa = (PackedArray*)curr;
                if (pa->data == NULL && pa->capacity > 0) {
                    fprintf(stderr, "Heap Corruption Detected: NULL data for PackedArray at %p\n", (void*)curr);
                    abort();
                }
                if (pa->size < 0 || pa->capacity < 0 || pa->size > pa->capacity) {
                    fprintf(stderr, "Heap Corruption Detected: PackedArray invalid size/capacity %d/%d at %p\n", pa->size, pa->capacity, (void*)curr);
                    abort();
                }
                break;
            }
            case OBJ_STRING_BUILDER: {
                StringBuilder *sb = (StringBuilder*)curr;
                if (sb->buffer == NULL && sb->capacity > 0) {
                    fprintf(stderr, "Heap Corruption Detected: NULL buffer for StringBuilder at %p\n", (void*)curr);
                    abort();
                }
                if (sb->size < 0 || sb->capacity < 0 || sb->size >= sb->capacity) {
                    fprintf(stderr, "Heap Corruption Detected: StringBuilder invalid size/capacity %d/%d at %p\n", sb->size, sb->capacity, (void*)curr);
                    abort();
                }
                break;
            }
            case OBJ_ROPE: {
                RopeNode *rn = (RopeNode*)curr;
                if (!rn->is_leaf) {
                    if (rn->internal.left) {
                        GCObject *left_obj = (GCObject*)rn->internal.left;
                        if (left_obj->type != OBJ_STRING && left_obj->type != OBJ_ROPE) {
                            fprintf(stderr, "Heap Corruption Detected: Invalid RopeNode left child type %d at %p\n", left_obj->type, (void*)curr);
                            abort();
                        }
                    }
                    if (rn->internal.right) {
                        GCObject *right_obj = (GCObject*)rn->internal.right;
                        if (right_obj->type != OBJ_STRING && right_obj->type != OBJ_ROPE) {
                            fprintf(stderr, "Heap Corruption Detected: Invalid RopeNode right child type %d at %p\n", right_obj->type, (void*)curr);
                            abort();
                        }
                    }
                }
                break;
            }
            case OBJ_CLOSURE: {
                Closure *cl = (Closure*)curr;
                if (cl->body == NULL) {
                    fprintf(stderr, "Heap Corruption Detected: NULL body for Closure at %p\n", (void*)curr);
                    abort();
                }
                break;
            }
            default:
                break;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&gc_mutex);
    return value_bool(1);
}


