#include "glowlang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#ifdef _WIN32
#include "win_pthread.h"
#else
#include <pthread.h>
#endif
#include <sqlite3.h>

/* GC */
static void gc_mark_value(Value v);

GCObject *gc_objects = NULL;
size_t gc_allocated = 0;
size_t gc_threshold = 128 * 1024 * 1024;
size_t gc_allocated_at_last_gc = 0;

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

/* Find an existing block with at least `sz` bytes of free tail space. */
static GCBlock* gc_block_find_fit(size_t sz) {
    for (GCBlock *b = gc_block_list; b; b = b->next) {
        if (b->size - b->used >= sz) return b;
    }
    return NULL;
}

/* Free every block whose live_count has dropped to zero. Called at the end of
   a collection, after the object sweep has unlinked every dead object from
   gc_objects, so no live object can still reference a freed block. This bounds
   the block arena to the set of blocks that actually hold live objects instead
   of growing monotonically. */
static void gc_reclaim_empty_blocks(void) {
    GCBlock **pp = &gc_block_list;
    while (*pp) {
        GCBlock *b = *pp;
        if (b->live_count == 0) {
            if (b == gc_current_block) gc_current_block = NULL;
            *pp = b->next;
            free(b);
        } else {
            pp = &b->next;
        }
    }
    if (!gc_current_block && gc_block_list) gc_current_block = gc_block_list;
}

static GCObject **grey_stack = NULL;
static int grey_capacity = 0;
static int grey_count = 0;

static void push_grey(GCObject *obj) {
    if (!obj || obj->marked) return;
    obj->marked = 1;
    if (grey_count >= grey_capacity) {
        grey_capacity = grey_capacity < 256 ? 256 : grey_capacity * 2;
        grey_stack = realloc(grey_stack, grey_capacity * sizeof(GCObject*));
    }
    grey_stack[grey_count++] = obj;
}

static void gc_mark_value(Value v) {
    if (GET_TYPE(v) == VAL_OBJECT || GET_TYPE(v) == VAL_LIST || GET_TYPE(v) == VAL_DICT || GET_TYPE(v) == VAL_STRING) {
        if (v & MASK_PAYLOAD) push_grey((GCObject*)(uintptr_t)(v & MASK_PAYLOAD));
    }
}

static void gc_mark_object(GCObject *obj) {
    push_grey(obj);
}

static void process_grey_stack(void) {
    while (grey_count > 0) {
        GCObject *obj = grey_stack[--grey_count];
        switch (obj->type) {
            case OBJ_LIST: { List *l = (List*)obj; for (int i=0; i<l->size; i++) gc_mark_value(l->items[i]); break; }
            case OBJ_ITERATOR: { Iterator *it = (Iterator*)obj; gc_mark_value(it->iterable); break; }
            case OBJ_DICT: { Dict *d = (Dict*)obj; for (int i=0; i<d->capacity; i++) { if (GET_TYPE(d->keys[i]) != VAL_NONE) { gc_mark_value(d->values[i]); } } break; }
            case OBJ_SQLITE_ITER: break; 
            case OBJ_SQLITE_HANDLE: break;
            case OBJ_CLASS: { 
                Class *c = (Class*)obj; 
                push_grey((GCObject*)c->methods); 
                push_grey((GCObject*)c->root_shape); 
                if (c->stable_shape) push_grey((GCObject*)c->stable_shape);
                break; 
            }
            case OBJ_SHAPE: {
                Shape *s = (Shape*)obj;
                if (s->parent) push_grey((GCObject*)s->parent);
                push_grey((GCObject*)s->transitions);
                break;
            }
            case OBJ_TRAIT: {
                Trait *t = (Trait*)obj;
                push_grey((GCObject*)t->methods);
                break;
            }
            case OBJ_INSTANCE: { 
                Instance *ins = (Instance*)obj; 
                push_grey((GCObject*)ins->klass); 
                push_grey((GCObject*)ins->properties); 
                break; 
            }
            case OBJ_BOUND_METHOD: { BoundMethod *bm = (BoundMethod*)obj; gc_mark_value(bm->instance); break; }
            case OBJ_ROPE: {
                RopeNode *rn = (RopeNode*)obj;
                if (rn->is_leaf) gc_mark_value(rn->leaf.flat_string);
                else {
                    push_grey((GCObject*)rn->internal.left);
                    push_grey((GCObject*)rn->internal.right);
                }
                break;
            }
            case OBJ_PACKED_ARRAY: {
                PackedArray *pa = (PackedArray*)obj;
                if (pa->is_view && pa->base) {
                    push_grey((GCObject*)pa->base);
                }
                break;
            }
            case OBJ_STRING_BUILDER: break;
            default: break;
        }
    }
}

void gc_full_collect(void) {
    pthread_mutex_lock(&gc_mutex);
    for (int i=0; i<global_table.count; i++) gc_mark_value(global_table.values[i]);
    if (vm_last_class) gc_mark_value(value_class(vm_last_class));
    if (vm_last_trait) gc_mark_value(value_trait(vm_last_trait));
    for (int i = 0; i < gc_const_roots_len; i++) gc_mark_value(gc_const_roots[i]);
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
    process_grey_stack();
    GCObject **p = &gc_objects;
    while (*p) {
        if (!(*p)->marked) {
            GCObject *unmarked = *p; *p = unmarked->next;
            if (unmarked->type == OBJ_LIST) free(((List*)unmarked)->items);
            else if (unmarked->type == OBJ_DICT) { free(((Dict*)unmarked)->keys); free(((Dict*)unmarked)->values); }
            else if (unmarked->type == OBJ_SQLITE_ITER) { if (((SQLiteIter*)unmarked)->stmt) sqlite3_finalize(((SQLiteIter*)unmarked)->stmt); }
            else if (unmarked->type == OBJ_SQLITE_HANDLE) { if (((SQLiteHandle*)unmarked)->db) sqlite3_close(((SQLiteHandle*)unmarked)->db); }
            else if (unmarked->type == OBJ_INSTANCE) { /* No fields array to free, Dict is handled as OBJ_DICT */ }
            else if (unmarked->type == OBJ_CLASS) free((char*)((Class*)unmarked)->name);
            else if (unmarked->type == OBJ_TRAIT) free((char*)((Trait*)unmarked)->name);
            else if (unmarked->type == OBJ_PACKED_ARRAY) { if (!((PackedArray*)unmarked)->is_view) tensor_free(((PackedArray*)unmarked)->data, ((PackedArray*)unmarked)->capacity * (((PackedArray*)unmarked)->type == PACKED_FLOAT64 ? sizeof(double) : sizeof(int64_t)), ((PackedArray*)unmarked)->device); }
            else if (unmarked->type == OBJ_STRING_BUILDER) free(((StringBuilder*)unmarked)->buffer);
            else if (unmarked->type == OBJ_CLOSURE) {
                Closure *c = (Closure*)unmarked;
                if (c->captured_names) free(c->captured_names);
                if (c->captured_values) free(c->captured_values);
            }
            gc_allocated -= unmarked->sz;
            if (unmarked->is_block_allocated) { ((GCBlock*)unmarked->block_ptr)->live_count--; } else { free(unmarked); }
        } else {
            (*p)->marked = 0;
            if ((*p)->generation == 0) (*p)->generation = 1; // Promote
            p = &((*p)->next);
        }
    }
    while (gc_remembered_set) { RememberedSet *next = gc_remembered_set->next; gc_remembered_set->obj->in_remembered_set = 0; free(gc_remembered_set); gc_remembered_set = next; }
    gc_reclaim_empty_blocks();
    gc_allocated_at_last_gc = gc_allocated;
    pthread_mutex_unlock(&gc_mutex);
}

void gc_minor_collect(void) {
    pthread_mutex_lock(&gc_mutex);
    
    /* Scan young roots: only stack slots pointing to generation 0 objects */
    pthread_mutex_lock(&threads_mutex);
    for (int i = 0; i < active_stack_count; i++) {
        Value *stk = *(active_stacks[i].vm_stack_ptr);
        int stk_sp = *(active_stacks[i].top_ptr);
        if (stk) {
            int lim = (stk_sp > 1000000) ? 1000000 : stk_sp;
            for (int j = 0; j < lim; j++) {
                Value v = stk[j];
                if (GET_TYPE(v) == VAL_OBJECT || GET_TYPE(v) == VAL_LIST || GET_TYPE(v) == VAL_DICT || GET_TYPE(v) == VAL_STRING) {
                    if (v & MASK_PAYLOAD) {
                        GCObject *obj = (GCObject*)(uintptr_t)(v & MASK_PAYLOAD);
                        if (obj->generation == 0) push_grey(obj);
                    }
                }
            }
        }
        /* pending_exc may point to young objects */
        Value exc = *(active_stacks[i].pending_exc_ptr);
        if (GET_TYPE(exc) == VAL_OBJECT || GET_TYPE(exc) == VAL_LIST || GET_TYPE(exc) == VAL_DICT || GET_TYPE(exc) == VAL_STRING) {
            if (exc & MASK_PAYLOAD) {
                GCObject *obj = (GCObject*)(uintptr_t)(exc & MASK_PAYLOAD);
                if (obj->generation == 0) push_grey(obj);
            }
        }
    }
    pthread_mutex_unlock(&threads_mutex);

    /* Globals (and the last-compiled class/trait) are roots that may hold
       young (gen 0) objects. The stack scan above only catches gen-0 objects
       still live in a register; once an assignment overwrites that register
       the global is the only reference. Skipping these here regressed
       correctness vs. the full collector (which scans them) and could free a
       live young object. Only mark gen-0 objects to stay generational. */
    for (int i = 0; i < global_table.count; i++) {
        Value v = global_table.values[i];
        if (GET_TYPE(v) == VAL_OBJECT || GET_TYPE(v) == VAL_LIST || GET_TYPE(v) == VAL_DICT || GET_TYPE(v) == VAL_STRING) {
            if (v & MASK_PAYLOAD) {
                GCObject *obj = (GCObject*)(uintptr_t)(v & MASK_PAYLOAD);
                if (obj->generation == 0) push_grey(obj);
            }
        }
    }
    if (vm_last_class) {
        GCObject *o = (GCObject*)(uintptr_t)(value_class(vm_last_class) & MASK_PAYLOAD);
        if (o && o->generation == 0) push_grey(o);
    }
    if (vm_last_trait) {
        GCObject *o = (GCObject*)(uintptr_t)(value_trait(vm_last_trait) & MASK_PAYLOAD);
        if (o && o->generation == 0) push_grey(o);
    }
    /* Frozen string literals are roots too (only young ones matter here). */
    for (int i = 0; i < gc_const_roots_len; i++) {
        Value v = gc_const_roots[i];
        if (GET_TYPE(v) == VAL_STRING && (v & MASK_PAYLOAD)) {
            GCObject *o = (GCObject*)(uintptr_t)(v & MASK_PAYLOAD);
            if (o->generation == 0) push_grey(o);
        }
    }

    /* Scan remembered set: old objects that reference young objects */
    RememberedSet *rs = gc_remembered_set;
    while (rs) { gc_mark_object(rs->obj); rs = rs->next; }

    process_grey_stack();

    /* Sweep only generation 0 (nursery) objects */
    GCObject **p = &gc_objects;
    while (*p) {
        if ((*p)->generation == 1) {
            (*p)->marked = 0;
            p = &((*p)->next);
            continue;
        }
        if (!(*p)->marked) {
            GCObject *unmarked = *p; *p = unmarked->next;
            if (unmarked->type == OBJ_LIST) free(((List*)unmarked)->items);
            else if (unmarked->type == OBJ_DICT) { free(((Dict*)unmarked)->keys); free(((Dict*)unmarked)->values); }
            else if (unmarked->type == OBJ_SQLITE_ITER) { if (((SQLiteIter*)unmarked)->stmt) sqlite3_finalize(((SQLiteIter*)unmarked)->stmt); }
            else if (unmarked->type == OBJ_SQLITE_HANDLE) { if (((SQLiteHandle*)unmarked)->db) sqlite3_close(((SQLiteHandle*)unmarked)->db); }
            else if (unmarked->type == OBJ_INSTANCE) { }
            else if (unmarked->type == OBJ_CLASS) free((char*)((Class*)unmarked)->name);
            else if (unmarked->type == OBJ_TRAIT) free((char*)((Trait*)unmarked)->name);
            else if (unmarked->type == OBJ_PACKED_ARRAY) { if (!((PackedArray*)unmarked)->is_view) tensor_free(((PackedArray*)unmarked)->data, ((PackedArray*)unmarked)->capacity * (((PackedArray*)unmarked)->type == PACKED_FLOAT64 ? sizeof(double) : sizeof(int64_t)), ((PackedArray*)unmarked)->device); }
            else if (unmarked->type == OBJ_STRING_BUILDER) free(((StringBuilder*)unmarked)->buffer);
            else if (unmarked->type == OBJ_CLOSURE) {
                Closure *c = (Closure*)unmarked;
                if (c->captured_names) free(c->captured_names);
                if (c->captured_values) free(c->captured_values);
            }
            gc_allocated -= unmarked->sz;
            if (unmarked->is_block_allocated) { ((GCBlock*)unmarked->block_ptr)->live_count--; } else { free(unmarked); }
        } else {
            /* Survived minor collection: promote to generation 1 */
            (*p)->generation = 1;
            (*p)->marked = 0;
            p = &((*p)->next);
        }
    }

    /* Clear remembered set after minor collection (old->young refs are now old->old) */
    while (gc_remembered_set) {
        RememberedSet *next = gc_remembered_set->next;
        gc_remembered_set->obj->in_remembered_set = 0;
        free(gc_remembered_set);
        gc_remembered_set = next;
    }

    gc_reclaim_empty_blocks();
    gc_allocated_at_last_gc = gc_allocated;
    pthread_mutex_unlock(&gc_mutex);
}

#if 0
static void gc_collect(void) { gc_full_collect(); }
#endif

void* gc_alloc(size_t sz, ObjType type) {
    static bool checked_env = false;
    if (!checked_env) {
        checked_env = true;
        char *stress = getenv("GLOWLANG_GC_STRESS");
        if (stress && strcmp(stress, "1") == 0) {
            gc_threshold = 128 * 1024;
        }
    }
    size_t allocated_since_gc = gc_allocated - gc_allocated_at_last_gc;
    if (gc_allocated > gc_threshold) gc_full_collect();
    else if (allocated_since_gc > gc_threshold / 4) gc_minor_collect();
    
    pthread_mutex_lock(&gc_mutex);
    
    sz = (sz + 7) & ~7;
    GCObject *obj = NULL;
    if (sz < gc_block_size / 4) {
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
THREAD_LOCAL char *stack_arena = NULL;
THREAD_LOCAL size_t stack_arena_used = 0;

void* gc_alloc_stack(size_t sz, ObjType type) {
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

void gc_free_stack_arena_range(size_t old_used, size_t new_used) {
    if (!stack_arena) return;
    char *ptr = stack_arena + old_used;
    char *end = stack_arena + new_used;
    while (ptr < end) {
        GCObject *obj = (GCObject*)ptr;
        size_t sz = obj->sz;
        if (sz == 0) {
            break;
        }
        if (obj->type == OBJ_LIST) {
            List *l = (List*)obj;
            if (l->items) {
                free(l->items);
                l->items = NULL;
            }
        } else if (obj->type == OBJ_DICT) {
            Dict *d = (Dict*)obj;
            if (d->keys) {
                free(d->keys);
                d->keys = NULL;
            }
            if (d->values) {
                free(d->values);
                d->values = NULL;
            }
        }
        ptr += sz;
    }
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


