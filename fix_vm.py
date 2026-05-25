import re

with open("src/glowlang.y", "r") as f:
    text = f.read()

# 1. Clean up the top declarations
top_decls = """
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
#include <sched.h>
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
Value value_string(const char *s);

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

static long total_calls = 0;
static long total_returns = 0;

static __thread int vm_loop_stop = 0, vm_loop_skip = 0;
static __thread Value vm_return_value;
static __thread int vm_has_return = 0;
"""

# Replace everything from the start to the first "static void print_value"
idx = text.find("static void print_value")
text = top_decls + "\\n" + text[idx:]

# 2. Fix vm_stack_init to register thread
vm_init_fix = """
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
"""
text = re.sub(r"static void vm_stack_init\(void\) \{.*?\}", vm_init_fix, text, flags=re.DOTALL)

# 3. Fix gc_alloc to use mutex
gc_alloc_fix = """
static void* gc_alloc(size_t sz, ObjType type) {
    pthread_mutex_lock(&gc_mutex);
    if (gc_allocated > gc_threshold) gc_collect();
    GCObject *obj = malloc(sz);
    obj->type = type;
    obj->marked = 0;
    obj->sz = sz;
    obj->next = gc_objects;
    gc_objects = obj;
    gc_allocated += sz;
    pthread_mutex_unlock(&gc_mutex);
    return obj;
}
"""
text = re.sub(r"static void\* gc_alloc\(size_t sz, ObjType type\) \{.*?\}", gc_alloc_fix, text, flags=re.DOTALL)

# 4. Fix gc_collect to mark all threads
gc_collect_fix = """
static void gc_collect(void) {
    /* Mark global variables */
    for (int i = 0; i < VAR_TABLE_SIZE; i++) {
        if (var_table[i].used) gc_mark_value(var_table[i].value);
    }
    
    /* Mark all active thread stacks */
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
            if (cf->local_values && cf->local_count > 16) {
                for (int k = 0; k < cf->local_count; k++) gc_mark_value(cf->local_values[k]);
            }
        }
    }
    pthread_mutex_unlock(&threads_mutex);

    /* Sweep */
    GCObject **p = &gc_objects;
    while (*p) {
        if (!(*p)->marked) {
            GCObject *unmarked = *p;
            *p = unmarked->next;
            if (unmarked->type == OBJ_LIST) {
                List *l = (List*)unmarked; free(l->items);
            } else if (unmarked->type == OBJ_DICT) {
                Dict *d = (Dict*)unmarked; free(d->keys); free(d->values);
            } else if (unmarked->type == OBJ_CLOSURE) {
                Closure *c = (Closure*)unmarked; free(c->params); free(c->captured_names); free(c->captured_values);
            } else if (unmarked->type == OBJ_INSTANCE) {
                Instance *ins = (Instance*)unmarked; free(ins->fields);
            }
            gc_allocated -= unmarked->sz;
            free(unmarked);
        } else {
            (*p)->marked = 0;
            p = &((*p)->next);
        }
    }
    gc_threshold = gc_allocated > 1024*1024 ? gc_allocated * 2 : 1024*1024;
}
"""
text = re.sub(r"static void gc_collect\(void\) \{.*?\}", gc_collect_fix, text, flags=re.DOTALL)

with open("src/glowlang.y", "w") as f:
    f.write(text)
