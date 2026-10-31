#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* pthread_getattr_np, for the thread's stack bounds */
#endif
#include "glowlang.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#ifdef _WIN32
#include "win_pthread.h"
#else
#include <pthread.h>
#include <unistd.h>
#endif
#if defined(__x86_64__) || defined(_M_X64)
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif
#endif
#include <math.h>
#include <sqlite3.h>
#include "jit.h"
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif
#include "opt.h"

THREAD_LOCAL Bytecode *g_dbg_bc = NULL;
THREAD_LOCAL Instruction *g_dbg_pc = NULL;

/* Cached debug environment flags – checked once at startup */
static int glow_dbg_flags_init = 0;
int glow_jit_debug = 0;
int glow_nojitexec = 0;
int glow_nobaseexec = 0;
static int glow_entrydbg = 0;
static int glow_topprobe = 0;
int glow_jitdbg = 0;
static int glow_no_baseline = 0;
static int glow_optgate = 0;
int glow_calldbg = 0;
int glow_noarenafree = 0;
static const char *glow_base_only = NULL;

static void glow_init_debug_flags(void) {
    if (glow_dbg_flags_init) return;
    glow_dbg_flags_init = 1;
    glow_jit_debug = (getenv("GLOW_JIT_DEBUG") != NULL);
    glow_nojitexec = (getenv("GLOW_NOJITEXEC") != NULL);
    glow_nobaseexec = (getenv("GLOW_NOBASEEXEC") != NULL);
    glow_entrydbg = (getenv("GLOW_ENTRYDBG") != NULL);
    glow_topprobe = (getenv("GLOW_TOPPROBE") != NULL);
    glow_jitdbg = (getenv("GLOW_JITDBG") != NULL);
    glow_no_baseline = (getenv("GLOW_NO_BASELINE") != NULL);
    glow_optgate = (getenv("GLOW_OPTGATE") != NULL);
    glow_calldbg = (getenv("GLOW_CALLDBG") != NULL);
    glow_noarenafree = (getenv("GLOW_NOARENAFREE") != NULL);
    glow_base_only = getenv("GLOW_BASE_ONLY");
}

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
extern const char *op_names[];
static int g_veh_registered = 0;

/* Native backtrace of a crash, symbolized when glowlang.pdb is next to the
   executable. Frames inside JIT-generated code have no unwind data, so the
   walk usually ends at the first one. */
static void glow_print_native_backtrace(CONTEXT *ctx) {
    HANDLE proc = GetCurrentProcess(), thr = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(proc, NULL, TRUE);
    CONTEXT c = *ctx;
    STACKFRAME64 f;
    memset(&f, 0, sizeof(f));
    f.AddrPC.Offset = c.Rip; f.AddrPC.Mode = AddrModeFlat;
    f.AddrFrame.Offset = c.Rbp; f.AddrFrame.Mode = AddrModeFlat;
    f.AddrStack.Offset = c.Rsp; f.AddrStack.Mode = AddrModeFlat;
    fprintf(stderr, "Native stack:\n");
    for (int i = 0; i < 32; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thr, &f, &c, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL)) break;
        DWORD64 pc = f.AddrPC.Offset;
        if (!pc) break;
        char buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *si = (SYMBOL_INFO*)buf;
        si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 255;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line; line.SizeOfStruct = sizeof(line);
        DWORD ldisp = 0;
        if (SymFromAddr(proc, pc, &disp, si)) {
            if (SymGetLineFromAddr64(proc, pc, &ldisp, &line))
                fprintf(stderr, "  #%d %s+0x%llx (%s:%lu)\n", i, si->Name, (unsigned long long)disp, line.FileName, (unsigned long)line.LineNumber);
            else
                fprintf(stderr, "  #%d %s+0x%llx\n", i, si->Name, (unsigned long long)disp);
        } else {
            fprintf(stderr, "  #%d 0x%llx\n", i, (unsigned long long)pc);
        }
    }
}

static LONG CALLBACK glow_veh(EXCEPTION_POINTERS *ep) {
    if (ep && ep->ExceptionRecord && ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        void *addr = (void*)ep->ExceptionRecord->ExceptionInformation[1];
        fprintf(stderr, "\n[AV] faulting addr=%p at pc=%p thread=%lu\n", addr,
                ep->ExceptionRecord->ExceptionAddress, (unsigned long)GetCurrentThreadId());
        glow_print_native_backtrace(ep->ContextRecord);
        if (g_dbg_bc) {
            const char *fname = (g_dbg_bc->owner && g_dbg_bc->owner->name) ? g_dbg_bc->owner->name : "(null)";
            long idx = g_dbg_pc ? (long)(g_dbg_pc - g_dbg_bc->code) : -1;
            const char *op = (g_dbg_pc && g_dbg_pc->op >= 0 && g_dbg_pc->op < 256) ? op_names[g_dbg_pc->op] : "?";
            fprintf(stderr, "[AV] func=%s op_index=%ld op=%s\n", fname, idx, op);
            fprintf(stderr, "--- bytecode of %s (count=%d) ---\n", fname, g_dbg_bc->count);
            int n = g_dbg_bc->count < 0 ? 0 : (g_dbg_bc->count > 45 ? 45 : g_dbg_bc->count);
            for (int _i = 0; _i < n; _i++) {
                Instruction *_pc = &g_dbg_bc->code[_i];
                const char *_nm = (_pc->op >= 0 && _pc->op < 256) ? op_names[_pc->op] : "UNKNOWN";
                fprintf(stderr, "%04d: %-15s r0:%d r1:%d r2:%d arg:%d\n", _i, _nm, _pc->r0, _pc->r1, _pc->r2, _pc->arg);
            }
        }
        fprintf(stderr, "Call stack (depth=%d):\n", call_stack_ptr);
        for (int _c = 0; _c < call_stack_ptr && _c < 32; _c++) {
            const char *fn = (call_stack[_c].fe && call_stack[_c].fe->name) ? call_stack[_c].fe->name : "<unknown>";
            fprintf(stderr, "  frame [%d]: %s\n", _c, fn);
        }
        FuncEntry *top_fe = (call_stack_ptr > 0) ? call_stack[call_stack_ptr - 1].fe : NULL;
        if (top_fe && top_fe->body) {
            fprintf(stderr, "--- Bytecode of top frame %s (count=%d) ---\n", top_fe->name, top_fe->body->count);
            for (int _i = 0; _i < top_fe->body->count; _i++) {
                Instruction *_pc = &top_fe->body->code[_i];
                const char *_nm = (_pc->op >= 0 && _pc->op < 256) ? op_names[_pc->op] : "UNKNOWN";
                fprintf(stderr, "%04d: %-15s r0:%d r1:%d r2:%d arg:%d\n", _i, _nm, _pc->r0, _pc->r1, _pc->r2, _pc->arg);
            }
        }
        fflush(stderr);
        abort();
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static void glow_ensure_veh(void) {
    if (!g_veh_registered) { AddVectoredExceptionHandler(1, glow_veh); g_veh_registered = 1; }
}
#endif

/* Portable signed-64-bit overflow builtins for compilers that lack the
   __builtin_*_overflow intrinsics (e.g. MSVC). GCC/Clang use the natives. */
#if !defined(__GNUC__) && !defined(__clang__)
static inline int glow_add_overflow_i64(int64_t a, int64_t b, int64_t *res) {
    uint64_t ur = (uint64_t)a + (uint64_t)b;
    int64_t r = (int64_t)ur;
    *res = r;
    return (((a ^ r) & (b ^ r)) < 0);
}
static inline int glow_sub_overflow_i64(int64_t a, int64_t b, int64_t *res) {
    uint64_t ur = (uint64_t)a - (uint64_t)b;
    int64_t r = (int64_t)ur;
    *res = r;
    return (((a ^ b) & (a ^ r)) < 0);
}
static inline int glow_mul_overflow_i64(int64_t a, int64_t b, int64_t *res) {
    if (a == 0 || b == 0) { *res = 0; return 0; }
    if ((a == INT64_MIN && b == -1) || (b == INT64_MIN && a == -1)) { *res = INT64_MIN; return 1; }
    int64_t r = (int64_t)((uint64_t)a * (uint64_t)b);
    *res = r;
    return (r / a) != b;
}
#define __builtin_add_overflow glow_add_overflow_i64
#define __builtin_sub_overflow glow_sub_overflow_i64
#define __builtin_mul_overflow glow_mul_overflow_i64
#endif

#define MAX_CALL_STACK 65536

// VM Thread local Stack and frames
THREAD_LOCAL Value *vm_stack = NULL;
THREAD_LOCAL int vm_stack_size = 0;
THREAD_LOCAL int sp = 0;
THREAD_LOCAL int vm_stack_top = 0;

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
volatile int in_fuzz_timeout = 0;
#endif

THREAD_LOCAL CallFrame call_stack[65536];
THREAD_LOCAL int call_stack_ptr = 0;

/* ---- Global interpreter lock -------------------------------------------
   A FIFO ticket lock: each acquirer takes a ticket and waits until it is
   served, so a thread that yields at a loop back-edge cannot immediately
   re-grab the lock ahead of a waiter. Before releasing, a thread records its
   stack pointer and callee-saved registers so the GC (which only ever runs on
   the thread holding the lock) can scan the parked thread's C stack. */
static pthread_mutex_t gil_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gil_cond = PTHREAD_COND_INITIALIZER;
static unsigned long gil_next_ticket = 0;
static unsigned long gil_now_serving = 0;
volatile int glow_gil_waiters = 0;
THREAD_LOCAL GlowThreadCtx glow_thread_ctx;

void glow_thread_ctx_init(void) {
    char *here = GLOW_STACK_POS();
    void *hi = NULL;
#if defined(_WIN32)
    hi = ((NT_TIB*)NtCurrentTeb())->StackBase;
#elif defined(__APPLE__)
    hi = pthread_get_stackaddr_np(pthread_self());
#elif defined(__linux__)
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void *addr; size_t size;
        if (pthread_attr_getstack(&attr, &addr, &size) == 0) hi = (char*)addr + size;
        pthread_attr_destroy(&attr);
    }
#endif
    /* Fallback: everything above this frame is the thread's live stack. Give
       it some headroom so caller frames of the entry point are included. */
    if (!hi) hi = here + 4096;
    glow_thread_ctx.stack_hi = hi;
    glow_thread_ctx.stack_lo = here;

    /* Lowest usable stack address. Recursion stops with a recursion error
       once less than a quarter of the stack (at most 1 MB) is left, which
       leaves room for natives called from the deepest frame. */
    char *lo = NULL;
#if defined(_WIN32)
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(here, &mbi, sizeof(mbi))) lo = (char*)mbi.AllocationBase;
#elif defined(__APPLE__)
    lo = (char*)hi - pthread_get_stacksize_np(pthread_self());
#elif defined(__linux__)
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void *addr; size_t size;
        if (pthread_attr_getstack(&attr, &addr, &size) == 0) lo = (char*)addr;
        pthread_attr_destroy(&attr);
    }
#endif
    if (!lo || lo >= here) lo = here - 512 * 1024;
    size_t margin = (size_t)((char*)hi - lo) / 4;
    if (margin > 1024 * 1024) margin = 1024 * 1024;
    glow_thread_ctx.stack_limit = lo + margin;
}

void glow_gil_acquire(void) {
    pthread_mutex_lock(&gil_mutex);
    unsigned long ticket = gil_next_ticket++;
    glow_gil_waiters++;
    while (ticket != gil_now_serving) pthread_cond_wait(&gil_cond, &gil_mutex);
    glow_gil_waiters--;
    pthread_mutex_unlock(&gil_mutex);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
void glow_gil_release(void) {
    char *here = GLOW_STACK_POS();
    setjmp(glow_thread_ctx.regs);
    glow_thread_ctx.stack_lo = here;
    pthread_mutex_lock(&gil_mutex);
    gil_now_serving++;
    pthread_cond_broadcast(&gil_cond);
    pthread_mutex_unlock(&gil_mutex);
}

void glow_gil_yield(void) {
    glow_gil_release();
    glow_gil_acquire();
}

/* Spawned-thread bookkeeping so the program (and `threads.join_all()`) can
   wait for every thread to finish. Guarded by the GIL. */
static int threads_alive = 0;
static pthread_mutex_t threads_done_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t threads_done_cond = PTHREAD_COND_INITIALIZER;

int glow_threads_alive(void) { return threads_alive; }

void glow_threads_join_all(void) {
    /* Called with the GIL held; drop it while waiting so workers can run. */
    while (1) {
        pthread_mutex_lock(&threads_done_mutex);
        int alive = threads_alive;
        pthread_mutex_unlock(&threads_done_mutex);
        if (alive == 0) return;
        glow_gil_release();
        pthread_mutex_lock(&threads_done_mutex);
        while (threads_alive == alive) pthread_cond_wait(&threads_done_cond, &threads_done_mutex);
        pthread_mutex_unlock(&threads_done_mutex);
        glow_gil_acquire();
    }
}

/* Values handed to a thread that has not started yet live only in malloc'd
   ThreadData, so they are registered here as GC roots until the thread has
   copied them onto its own VM stack. */
#define MAX_PENDING_SPAWNS 1024
static ThreadData *pending_spawns[MAX_PENDING_SPAWNS];
static int pending_spawn_count = 0;

void gc_mark_pending_spawn_roots(void (*mark)(Value)) {
    for (int i = 0; i < pending_spawn_count; i++) {
        ThreadData *td = pending_spawns[i];
        mark(td->callee);
        for (int j = 0; j < td->argc; j++) mark(td->args[j]);
    }
}

static void pending_spawn_remove(ThreadData *td) {
    for (int i = 0; i < pending_spawn_count; i++) {
        if (pending_spawns[i] == td) { pending_spawns[i] = pending_spawns[--pending_spawn_count]; return; }
    }
}

THREAD_LOCAL Value pending_exception = 0;
THREAD_LOCAL int has_exception = 0;
THREAD_LOCAL int runtime_error_thrown = 0;

THREAD_LOCAL int profiling_active = 0;
THREAD_LOCAL int trace_mode = 0;
THREAD_LOCAL int call_depth = 0;
int next_free_reg = 0;
THREAD_LOCAL char *exception_stack_trace = NULL;
THREAD_LOCAL const char *error_file = NULL;
THREAD_LOCAL int error_line = 0;



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

static int string_equal_simd(const char *s1, size_t l1, const char *s2, size_t l2) {
    if (s1 == s2) return 1;
    if (!s1 || !s2) return 0;
    if (l1 != l2) return 0;
    /* memcmp is vectorised by the C runtime for the running CPU. */
    return memcmp(s1, s2, l1) == 0;
}

/* Frozen, GC-rooted canonical strings for OP_CONST_STRING. The same literal
   string is shared instead of re-allocating a fresh GCString on every load.
   Keyed by the interned char* in pc->ptr (stable for the process lifetime),
   so pc->ptr keeps its original meaning and the JIT is unaffected. An open-
   addressed pointer hash keeps each load O(1). */
static Value *g_const_cache_vals = NULL;
static const char **g_const_cache_keys = NULL;
static uint32_t g_const_cache_cap = 0;
static uint32_t g_const_cache_len = 0;

static inline uint32_t const_cache_hash(const char *s) {
    uint64_t x = (uint64_t)(uintptr_t)s;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33;
    return (uint32_t)x;
}

static void const_cache_grow(void) {
    uint32_t ncap = g_const_cache_cap ? g_const_cache_cap * 2 : 256;
    const char **nk = calloc(ncap, sizeof(const char*));
    Value *nv = calloc(ncap, sizeof(Value));
    for (uint32_t i = 0; i < g_const_cache_cap; i++) {
        if (!g_const_cache_keys[i]) continue;
        uint32_t j = const_cache_hash(g_const_cache_keys[i]) & (ncap - 1);
        while (nk[j]) j = (j + 1) & (ncap - 1);
        nk[j] = g_const_cache_keys[i]; nv[j] = g_const_cache_vals[i];
    }
    free(g_const_cache_keys); free(g_const_cache_vals);
    g_const_cache_keys = nk; g_const_cache_vals = nv; g_const_cache_cap = ncap;
}

static Value vm_get_const_string(const char *s) {
    if (g_const_cache_cap) {
        uint32_t j = const_cache_hash(s) & (g_const_cache_cap - 1);
        while (g_const_cache_keys[j]) {
            if (g_const_cache_keys[j] == s) return g_const_cache_vals[j];
            j = (j + 1) & (g_const_cache_cap - 1);
        }
    }
    Value v = value_string_copy(s);
    gc_register_constant_root(v);
    if ((g_const_cache_len + 1) * 2 > g_const_cache_cap) const_cache_grow();
    uint32_t j = const_cache_hash(s) & (g_const_cache_cap - 1);
    while (g_const_cache_keys[j]) j = (j + 1) & (g_const_cache_cap - 1);
    g_const_cache_keys[j] = s;
    g_const_cache_vals[j] = v;
    g_const_cache_len++;
    return v;
}

/* Forward declarations for the numeric-tower helpers defined below (used by
   val_eq / comparisons which appear earlier in this file). */
static inline int value_exact_int64(Value v, int64_t *out);
static inline int is_fractional_double(Value v);
static inline BigInt* value_to_bigint(Value v);
static inline int value_cmp(Value a, Value b);

int val_eq(Value a, Value b) {
    /* Every NaN is CANON_NAN, and a NaN equals nothing, not even itself. */
    if (a == b) return a != CANON_NAN;
    if (IS_FLOAT_BITS(a) && IS_FLOAT_BITS(b)) return AS_FLOAT(a) == AS_FLOAT(b);
    if (is_bigint(a) || is_bigint(b)) {
        int64_t ia, ib;
        if (value_exact_int64(a, &ia) && value_exact_int64(b, &ib)) return ia == ib;
        if (is_bigint(a) && is_bigint(b)) return bigint_cmp(AS_BIGINT(a), AS_BIGINT(b)) == 0;
        return 0;
    }
    uint64_t ta = IS_FLOAT_BITS(a) ? VAL_FLOAT : ((a) & MASK_TYPE) >> 48;
    uint64_t tb = IS_FLOAT_BITS(b) ? VAL_FLOAT : ((b) & MASK_TYPE) >> 48;
    if (ta == VAL_INT && tb == VAL_FLOAT) return (double)AS_INT(a) == AS_FLOAT(b);
    if (ta == VAL_FLOAT && tb == VAL_INT) return AS_FLOAT(a) == (double)AS_INT(b);
    if (ta == VAL_STRING && tb == VAL_STRING) {
        size_t l1 = rope_length(a);
        size_t l2 = rope_length(b);
        return string_equal_simd(AS_STRING(a), l1, AS_STRING(b), l2);
    }
    /* Lists and dicts compare by contents ([1, 2] == [1, 2]); the depth
       limit stops self-referencing structures from recursing forever. */
    static THREAD_LOCAL int eq_depth = 0;
    if (eq_depth > 200) return 0;
    if (ta == VAL_LIST && tb == VAL_LIST) {
        List *la = AS_LIST(a), *lb = AS_LIST(b);
        if (la->size != lb->size) return 0;
        eq_depth++;
        int same = 1;
        for (int i = 0; i < la->size && same; i++) same = val_eq(la->items[i], lb->items[i]);
        eq_depth--;
        return same;
    }
    if (ta == VAL_DICT && tb == VAL_DICT) {
        Dict *da = AS_DICT(a), *db = AS_DICT(b);
        if (da->size != db->size) return 0;
        eq_depth++;
        int same = 1;
        for (int i = 0; i < da->capacity && same; i++) {
            if (GET_TYPE(da->keys[i]) == VAL_NONE) continue;
            const char *k = (const char*)(uintptr_t)(da->keys[i] & MASK_PAYLOAD);
            int j = dict_get_idx(db, k);
            same = (j >= 0) && val_eq(da->values[i], db->values[j]);
        }
        eq_depth--;
        return same;
    }
    return 0;
}



int value_truthy(Value v) {
    if (IS_FLOAT_BITS(v)) return AS_FLOAT(v) != 0.0;
    uint64_t type = (v & MASK_TYPE) >> 48;
    if (type == VAL_INT) return AS_INT(v) != 0;
    if (type == VAL_BOOL) return AS_BOOL(v);
    if (type == VAL_STRING) return AS_STRING(v) && AS_STRING(v)[0] != '\0';
    if (type == VAL_LIST) return AS_LIST(v) && AS_LIST(v)->size > 0;
    return 0;
}

/* ---- Numeric tower (stage 1): keep exact integers inline when they fit the
   48-bit payload, otherwise promote to a double. Never truncates. ---- */
static inline Value num_from_double(double r) {
    if (isfinite(r) && r == floor(r) && fabs(r) < 9.0e18) {
        int64_t i = (int64_t)r;
        if (fits_int48(i)) return value_int(i);
    }
    return value_float(r);
}
static inline int64_t num_payload_int(Value v) {
    return (int64_t)((v & MASK_PAYLOAD) << 16) >> 16;
}
/* 2^53: the largest integer a double represents exactly. Beyond this we keep
   the value exact with a heap big-int instead of an (inexact) double. */
#define INT53_MAX ((int64_t)9007199254740992LL)

/* Is v an exact integer that fits in int64 (inline int, small bigint, or
   integer-valued double)? */
static inline int value_exact_int64(Value v, int64_t *out) {
    if (GET_TYPE(v) == VAL_INT) { *out = num_payload_int(v); return 1; }
    if (is_bigint(v)) return bigint_fits_int64(AS_BIGINT(v), out);
    if (GET_TYPE(v) == VAL_FLOAT) {
        double d = AS_FLOAT(v);
        if (isfinite(d) && d == floor(d) && fabs(d) < 9.2233720368547758e18)
            { *out = (int64_t)d; return 1; }
    }
    return 0;
}
static inline int is_fractional_double(Value v) {
    if (GET_TYPE(v) == VAL_FLOAT) {
        double d = AS_FLOAT(v);
        return !(isfinite(d) && d == floor(d));
    }
    return 0;
}
static inline BigInt* value_to_bigint(Value v) {
    if (is_bigint(v)) return AS_BIGINT(v);
    int64_t i;
    if (value_exact_int64(v, &i)) return bigint_from_int64(i);
    return bigint_from_int64((int64_t)val_to_double(v));
}
/* Exact three-way comparison of two values (int/bigint vs double). */
int glow_value_cmp(Value a, Value b);
/* Can a and b be ordered with < and >? Numbers (and booleans) with numbers,
   strings with strings, lists with lists. */
static int values_comparable(Value a, Value b) {
    ValueType ta = GET_TYPE(a), tb = GET_TYPE(b);
    int na = ta == VAL_INT || ta == VAL_FLOAT || ta == VAL_BOOL || is_bigint(a);
    int nb = tb == VAL_INT || tb == VAL_FLOAT || tb == VAL_BOOL || is_bigint(b);
    if (na && nb) return 1;
    if (ta == VAL_STRING && tb == VAL_STRING) return 1;
    if (ta == VAL_LIST && tb == VAL_LIST) return 1;
    return 0;
}
static inline int value_cmp(Value a, Value b) {
    int64_t ia, ib;
    if (GET_TYPE(a) == VAL_STRING && GET_TYPE(b) == VAL_STRING) {
        size_t la = rope_length(a), lb = rope_length(b);
        int c = memcmp(AS_STRING(a), AS_STRING(b), la < lb ? la : lb);
        if (c) return c < 0 ? -1 : 1;
        return (la > lb) - (la < lb);
    }
    if (GET_TYPE(a) == VAL_LIST && GET_TYPE(b) == VAL_LIST) {
        List *x = AS_LIST(a), *y = AS_LIST(b);
        int n = x->size < y->size ? x->size : y->size;
        for (int i = 0; i < n; i++) {
            if (val_eq(x->items[i], y->items[i])) continue;
            return values_comparable(x->items[i], y->items[i]) ? value_cmp(x->items[i], y->items[i]) : 0;
        }
        return (x->size > y->size) - (x->size < y->size);
    }
    if (GET_TYPE(a) == VAL_BOOL) a = value_int(AS_BOOL(a));
    if (GET_TYPE(b) == VAL_BOOL) b = value_int(AS_BOOL(b));
    if (value_exact_int64(a, &ia) && value_exact_int64(b, &ib))
        return (ia > ib) ? 1 : (ia < ib) ? -1 : 0;
    if (is_bigint(a) || is_bigint(b))
        return bigint_cmp(value_to_bigint(a), value_to_bigint(b));
    double da = val_to_double(a), db = val_to_double(b);
    return (da > db) ? 1 : (da < db) ? -1 : 0;
}
/* <= and >= are false when either side is NaN (value_cmp says "equal" for
   values it cannot order). */
static inline int either_nan(Value a, Value b) { return a == CANON_NAN || b == CANON_NAN; }
/* Ordering used by sorted(), min() and max(). */
int glow_value_cmp(Value a, Value b) { return value_cmp(a, b); }

// Numeric tower: inline int (48-bit) -> double (exact to 2^53) -> heap big-int
// (exact beyond 2^53).
static inline Value num_add(Value a, Value b) {
    int64_t ia, ib;
    if (value_exact_int64(a, &ia) && value_exact_int64(b, &ib)) {
        int64_t r;
        if (__builtin_add_overflow(ia, ib, &r))
            return value_bigint(bigint_add(bigint_from_int64(ia), bigint_from_int64(ib)));
        if (r >= (int64_t)INT48_MIN && r <= (int64_t)INT48_MAX) return value_int(r);
        if (r >= -INT53_MAX && r <= INT53_MAX) return value_float((double)r);
        return value_bigint(bigint_from_int64(r));
    }
    if (is_fractional_double(a) || is_fractional_double(b))
        return num_from_double(val_to_double(a) + val_to_double(b));
    return value_bigint(bigint_add(value_to_bigint(a), value_to_bigint(b)));
}
static inline Value num_sub(Value a, Value b) {
    int64_t ia, ib;
    if (value_exact_int64(a, &ia) && value_exact_int64(b, &ib)) {
        int64_t r;
        if (__builtin_sub_overflow(ia, ib, &r))
            return value_bigint(bigint_sub(bigint_from_int64(ia), bigint_from_int64(ib)));
        if (r >= (int64_t)INT48_MIN && r <= (int64_t)INT48_MAX) return value_int(r);
        if (r >= -INT53_MAX && r <= INT53_MAX) return value_float((double)r);
        return value_bigint(bigint_from_int64(r));
    }
    if (is_fractional_double(a) || is_fractional_double(b))
        return num_from_double(val_to_double(a) - val_to_double(b));
    return value_bigint(bigint_sub(value_to_bigint(a), value_to_bigint(b)));
}
static inline Value num_mul(Value a, Value b) {
    int64_t ia, ib;
    if (value_exact_int64(a, &ia) && value_exact_int64(b, &ib)) {
        int64_t r;
        if (__builtin_mul_overflow(ia, ib, &r))
            return value_bigint(bigint_mul(bigint_from_int64(ia), bigint_from_int64(ib)));
        if (r >= (int64_t)INT48_MIN && r <= (int64_t)INT48_MAX) return value_int(r);
        if (r >= -INT53_MAX && r <= INT53_MAX) return value_float((double)r);
        return value_bigint(bigint_from_int64(r));
    }
    if (is_fractional_double(a) || is_fractional_double(b))
        return num_from_double(val_to_double(a) * val_to_double(b));
    return value_bigint(bigint_mul(value_to_bigint(a), value_to_bigint(b)));
}
static inline Value num_div(Value a, Value b) { return num_from_double(val_to_double(a) / val_to_double(b)); }
/* `a ** b`: exact for integer bases and non-negative integer exponents
   (repeated squaring through the numeric tower, so 2 ** 100 is a big
   integer); otherwise floating point, as before. */
static Value num_pow(Value a, Value b) {
    int64_t base, e;
    if (value_exact_int64(a, &base) && value_exact_int64(b, &e) && e >= 0) {
        if (base == 0 || base == 1) return value_int(e == 0 ? 1 : base);
        if (base == -1) return value_int((e & 1) ? -1 : 1);
        if (e <= 65536) {
            Value result = value_int(1), sq = a;
            while (e > 0) {
                if (e & 1) result = num_mul(result, sq);
                e >>= 1;
                if (e) sq = num_mul(sq, sq);
            }
            return result;
        }
    }
    return num_from_double(pow(val_to_double(a), val_to_double(b)));
}

static inline int is_numeric(Value v) {
    ValueType t = GET_TYPE(v);
    return t == VAL_INT || t == VAL_FLOAT || is_bigint(v);
}

/* Arithmetic on values that are not numbers raises an error instead of
   treating them as 0 (`none * 2` used to give 0). */
static void arith_type_error(const char *op, Value a, Value b) {
    Value ta = native_type(1, &a), tb = native_type(1, &b);
    char msg[160];
    snprintf(msg, sizeof(msg), "Cannot apply '%s' to %s and %s", op, AS_STRING(ta), AS_STRING(tb));
    pending_exception = value_string_copy(msg);
    has_exception = 1;
}

static void compare_type_error(const char *op, Value a, Value b) {
    Value ta = native_type(1, &a), tb = native_type(1, &b);
    char msg[160];
    snprintf(msg, sizeof(msg), "Cannot compare %s and %s with '%s'", AS_STRING(ta), AS_STRING(tb), op);
    pending_exception = value_string_copy(msg);
    has_exception = 1;
}

/* `text * n` and `list * n` repeat the sequence; returns 0 when a and b are
   not a sequence and an integer. */
static int seq_repeat(Value a, Value b, Value *out) {
    int64_t n;
    Value seq;
    if ((GET_TYPE(a) == VAL_STRING || GET_TYPE(a) == VAL_LIST) && GET_TYPE(b) == VAL_INT) { seq = a; n = AS_INT(b); }
    else if ((GET_TYPE(b) == VAL_STRING || GET_TYPE(b) == VAL_LIST) && GET_TYPE(a) == VAL_INT) { seq = b; n = AS_INT(a); }
    else return 0;
    if (n < 0) n = 0;
    if (GET_TYPE(seq) == VAL_STRING) {
        size_t len = rope_length(seq);
        if (len && (uint64_t)n > (size_t)0x7fffffff / len) { pending_exception = value_string_copy("Repeated string is too long"); has_exception = 1; *out = value_none(); return 1; }
        char *buf = malloc(len * (size_t)n + 1);
        const char *src = AS_STRING(seq);
        for (int64_t i = 0; i < n; i++) memcpy(buf + (size_t)i * len, src, len);
        buf[len * (size_t)n] = '\0';
        *out = value_string_copy(buf);
        free(buf);
    } else {
        List *src = AS_LIST(seq);
        if (src->size && (uint64_t)n > (uint64_t)0x7fffffff / (uint64_t)src->size) { pending_exception = value_string_copy("Repeated list is too long"); has_exception = 1; *out = value_none(); return 1; }
        int count = src->size;
        List *l = list_new((int)(count * n) + 1);
        Value lv = value_list(l);
        for (int64_t i = 0; i < n; i++) for (int j = 0; j < count; j++) list_push(l, src->items[j]);
        *out = lv;
    }
    return 1;
}

/* Floor modulo, like Python: the result has the sign of the divisor
   (-7 % 3 == 2). Exact for integers. */
static Value num_mod(Value a, Value b) {
    int64_t ia, ib;
    if (value_exact_int64(a, &ia) && value_exact_int64(b, &ib) && ib != 0 &&
        !is_fractional_double(a) && !is_fractional_double(b)) {
        int64_t r = (ib == -1) ? 0 : ia % ib;
        if (r != 0 && ((r < 0) != (ib < 0))) r += ib;
        if (r >= (int64_t)INT48_MIN && r <= (int64_t)INT48_MAX) return value_int(r);
        return value_bigint(bigint_from_int64(r));
    }
    double x = val_to_double(a), y = val_to_double(b);
    double r = fmod(x, y);
    if (r != 0 && ((r < 0) != (y < 0))) r += y;
    return num_from_double(r);
}

/* JIT helpers (defined here, called from jit.c). */
Value jit_int_add(Value a, Value b) {
    if (GET_TYPE(a) == VAL_STRING || GET_TYPE(b) == VAL_STRING) return glow_string_concat(a, b);
    if (GET_TYPE(a) == VAL_LIST && GET_TYPE(b) == VAL_LIST) {
        List *la = AS_LIST(a), *lb = AS_LIST(b);
        List *rl = list_new(la->size + lb->size);
        for (int i=0; i<la->size; i++) list_push(rl, la->items[i]);
        for (int i=0; i<lb->size; i++) list_push(rl, lb->items[i]);
        return value_list(rl);
    }
    if (!is_numeric(a) || !is_numeric(b)) return VAL_DEOPT_SENTINEL;
    return num_add(a, b);
}
Value jit_int_sub(Value a, Value b) {
    if (!is_numeric(a) || !is_numeric(b)) return VAL_DEOPT_SENTINEL;
    return num_sub(a, b);
}
/* Division for compiled code. Division by zero is declined (deopt sentinel)
   so the interpreter raises the same error it always does. */
Value jit_div_helper(Value a, Value b) {
    if (!is_numeric(a) || !is_numeric(b) || val_to_double(b) == 0.0) return VAL_DEOPT_SENTINEL;
    return num_div(a, b);
}
Value jit_int_mul(Value a, Value b) {
    if (!is_numeric(a) || !is_numeric(b)) return VAL_DEOPT_SENTINEL;
    return num_mul(a, b);
}
Value jit_int_lt(Value a, Value b) { if (!values_comparable(a, b)) return VAL_DEOPT_SENTINEL; return value_bool(value_cmp(a, b) < 0); }
Value jit_int_gt(Value a, Value b) { if (!values_comparable(a, b)) return VAL_DEOPT_SENTINEL; return value_bool(value_cmp(a, b) > 0); }
Value jit_int_lte(Value a, Value b) { if (!values_comparable(a, b)) return VAL_DEOPT_SENTINEL; return value_bool(!either_nan(a, b) && value_cmp(a, b) <= 0); }
Value jit_int_gte(Value a, Value b) { if (!values_comparable(a, b)) return VAL_DEOPT_SENTINEL; return value_bool(!either_nan(a, b) && value_cmp(a, b) >= 0); }
Value jit_int_eq(Value a, Value b)  { return value_bool(val_eq(a, b)); }

/* Generic value comparison helpers for JIT (non-specialized paths). */
Value jit_val_lt(Value a, Value b) { if (!values_comparable(a, b)) return VAL_DEOPT_SENTINEL; return value_bool(value_cmp(a, b) < 0); }
Value jit_val_gt(Value a, Value b) { if (!values_comparable(a, b)) return VAL_DEOPT_SENTINEL; return value_bool(value_cmp(a, b) > 0); }
Value jit_val_lte(Value a, Value b) { if (!values_comparable(a, b)) return VAL_DEOPT_SENTINEL; return value_bool(!either_nan(a, b) && value_cmp(a, b) <= 0); }
Value jit_val_gte(Value a, Value b) { if (!values_comparable(a, b)) return VAL_DEOPT_SENTINEL; return value_bool(!either_nan(a, b) && value_cmp(a, b) >= 0); }
Value jit_val_eq(Value a, Value b)  { return value_bool(val_eq(a, b)); }
Value jit_val_neq(Value a, Value b) { return value_bool(!val_eq(a, b)); }


pthread_mutex_t gc_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t threads_mutex = PTHREAD_MUTEX_INITIALIZER;
ThreadStack active_stacks[128];
int active_stack_count = 0;
volatile int threads_ever_spawned = 0;

void print_error_trace(Value exception);
void vm_stack_init(void) {
    glow_init_debug_flags();
    glow_thread_ctx_init();
    const char *vs = getenv("GLOW_VM_STACK");
    /* 1M slots = 8MB. The stack never moves (JIT frames hold raw pointers into
       it); frame pushes check the bound and raise a recursion error instead. */
    vm_stack_size = vs ? atoi(vs) : (1024 * 1024);
    if (vm_stack_size < 65536) vm_stack_size = 65536;
    vm_stack = calloc(vm_stack_size, sizeof(Value));
    sp = 0;
    vm_stack_top = 0;
    pthread_mutex_lock(&threads_mutex);
    if (active_stack_count > 0) threads_ever_spawned = 1;
    if (active_stack_count < 128) {
        active_stacks[active_stack_count].vm_stack_ptr = &vm_stack;
        active_stacks[active_stack_count].sp_ptr = &sp;
        active_stacks[active_stack_count].top_ptr = &vm_stack_top;
        active_stacks[active_stack_count].pending_exc_ptr = &pending_exception;
        active_stacks[active_stack_count].ctx = &glow_thread_ctx;
        active_stack_count++;
    }
    pthread_mutex_unlock(&threads_mutex);
}

void vm_stack_cleanup(void) {
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
    /* All VM and heap state is shared, so a spawned thread runs only while it
       holds the GIL, exactly like the main thread. */
    glow_gil_acquire();
    vm_stack_init();
    // Push arguments onto the new thread's vm_stack
    if (GET_TYPE(td->callee) == VAL_OBJECT && (td->callee & MASK_PAYLOAD) && GET_OBJ_TYPE(td->callee) == OBJ_BOUND_METHOD) {
        BoundMethod *bm = AS_BOUND_METHOD(td->callee);
        if (GET_TYPE(bm->instance) != VAL_NONE) {
            vm_stack[0] = bm->instance;
            for (int i = 0; i < td->argc; i++) {
                vm_stack[i + 1] = td->args[i];
            }
        } else {
            for (int i = 0; i < td->argc; i++) {
                vm_stack[i] = td->args[i];
            }
        }
    } else {
        for (int i = 0; i < td->argc; i++) {
            vm_stack[i] = td->args[i];
        }
    }
    /* The callee and arguments are now rooted by this thread's VM stack (and
       the callee by td, which stays on our C stack), so stop treating them as
       pending roots. */
    vm_stack[td->argc + 1] = td->callee;
    vm_stack_top = td->argc + 2;
    pending_spawn_remove(td);

    if (GET_TYPE(td->callee) == VAL_OBJECT && (td->callee & MASK_PAYLOAD)) {
        ObjType ot = GET_OBJ_TYPE(td->callee);
        if (ot == OBJ_NATIVE_FUNC) {
            glow_call_native(AS_NATIVE_FUNC(td->callee), td->argc, td->args);
        } else if (ot == OBJ_BOUND_METHOD) {
            BoundMethod *bm = AS_BOUND_METHOD(td->callee);
            if (GET_TYPE(bm->instance) != VAL_NONE) {
                var_set("self", bm->instance);
            }
            if (bm->method && bm->method->body) {
                run_bytecode(bm->method->body, NULL, NULL, 0);
            }
        } else if (ot == OBJ_CLOSURE) {
            Closure *c = AS_CLOSURE(td->callee);
            if (c->body) {
                run_bytecode(c->body, NULL, c, 0);
            }
        }
    } else if (td->name) {
        FuncEntry *fe = func_get(td->name);
        if (fe && fe->body) {
            run_bytecode(fe->body, NULL, NULL, 0);
        }
    }

    if (__builtin_expect(has_exception, 0)) {
        print_error_trace(pending_exception);
        runtime_error_thrown = 1;
        has_exception = 0;
    }
    fflush(stdout);

    int counted = !td->daemon;
    free(td->args); free(td);
    vm_stack_cleanup();
    pthread_mutex_lock(&threads_done_mutex);
    threads_alive -= counted;
    pthread_cond_broadcast(&threads_done_cond);
    pthread_mutex_unlock(&threads_done_mutex);
    glow_gil_release();
    return NULL;
}


char* format_stack_trace(void) {
    int max_frames = 30;
    int frames_to_show = call_stack_ptr;
    if (frames_to_show > max_frames) frames_to_show = max_frames;

    char *buf = malloc((frames_to_show + 3) * 256);
    buf[0] = '\0';
    strcat(buf, "Stack Trace:\n");
    for (int i = call_stack_ptr - 1; i >= call_stack_ptr - frames_to_show; i--) {
        FuncEntry *fe = call_stack[i].fe;
        Instruction *pc = call_stack[i].pc;
        int line = pc ? pc->line : 0;
        const char *file = (pc && pc->file) ? pc->file : "<unknown>";
        const char *func_name = (fe && fe->name) ? fe->name : "<unknown>";
        char frame[256];
        snprintf(frame, 256, "  at %s (%s:%d)\n", func_name, file, line);
        strcat(buf, frame);
    }
    if (call_stack_ptr > max_frames) {
        char extra[128];
        snprintf(extra, 128, "  ... and %d more frames\n", call_stack_ptr - max_frames);
        strcat(buf, extra);
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
    fprintf(stderr, "\033[31mRuntimeError:\033[0m ");
    if (exception == 0) { fprintf(stderr, "none"); }
    else if (IS_FLOAT_BITS(exception)) { char _b[64]; format_double(_b, sizeof(_b), AS_FLOAT(exception)); fprintf(stderr, "%s", _b); }
    else {
        uint64_t type = ((exception) & MASK_TYPE) >> 48;
        if (type == VAL_INT) fprintf(stderr, "%lld", (long long)AS_INT(exception));
        else if (type == VAL_BOOL) fprintf(stderr, AS_BOOL(exception) ? "true" : "false");
        else if (type == VAL_STRING) fprintf(stderr, "%s", AS_STRING(exception));
        else if (type == VAL_OBJECT && (exception & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(exception);
            if (ot == OBJ_CLASS) fprintf(stderr, "<class %s>", AS_CLASS(exception)->name);
            else if (ot == OBJ_INSTANCE) fprintf(stderr, "<instance of %s>", AS_INSTANCE(exception)->klass->name);
            else fprintf(stderr, "<object>");
        } else {
            fprintf(stderr, "<unknown value type>");
        }
    }
    fprintf(stderr, "\n\n");

    // Print source context for the actual error location
    if (error_file && error_line > 0) {
        print_error_line(error_file, error_line);
        fprintf(stderr, "\n  in %s:%d\n\n", error_file, error_line);
    } else {
        fprintf(stderr, "\n");
    }

    if (exception_stack_trace) {
        fprintf(stderr, "%s", exception_stack_trace);
        free(exception_stack_trace);
        exception_stack_trace = NULL;
    } else {
        print_stack_trace();
    }
}

const char *op_names[] = {
    "CONST_INT", "CONST_FLOAT", "CONST_STRING", "CONST_BOOL", "LOAD_VAR", "STORE_VAR",
    "ADD", "SUB", "MUL", "DIV", "MOD", "POW", "NEG", "LT", "GT", "LTE", "GTE", "EQ", "NEQ",
    "AND", "OR", "NOT", "JUMP", "JUMP_IF_FALSE", "JUMP_IF_TRUE", "LEN", "RANGE", "TYPE",
    "APPEND", "INPUT", "SHOW", "WRITE", "LIST_LITERAL", "DICT_LITERAL", "INDEX", "SLICE",
    "STORE_INDEX", "FUNC_DEF", "CALL", "CALL_SELF_FAST", "TAIL_CALL", "RETURN",
    "FOR_EACH", "FOR_EACH_PAIR", "STOP", "SKIP", "POP", "PUSH_SCOPE", "POP_SCOPE",
    "STORE_LET", "LOAD_LOCAL", "STORE_LOCAL", "DICT_KEYS", "DICT_VALUES", "DICT_HAS",
    "DICT_REMOVE", "DICT_MERGE", "MATH_ABS", "MATH_MIN", "MATH_MAX", "MATH_ROUND",
    "MATH_FLOOR", "MATH_CEIL", "MATH_SQRT", "STR_UPPER", "STR_LOWER", "STR_TRIM",
    "STR_FIND", "STR_REPLACE", "STR_STARTSWITH", "STR_ENDSWITH", "STR_SPLIT",
    "LIST_PUSH", "LIST_POP", "LIST_INSERT", "LIST_REMOVE", "LIST_CONTAINS", "LIST_SORT",
    "LIST_REVERSE", "FILE_READ", "FILE_WRITE", "FILE_APPEND", "FSTRING", "LIST_COMP",
    "THROW", "TRY_START", "TRY_END", "LAMBDA", "CALL_VALUE", "CLASS_DEF", "METHOD_DEF",
    "TRAIT_DEF", "USE_TRAIT", "GET_PROPERTY", "SET_PROPERTY", "SPAWN", "MOVE", "NOP",
    "TRACE_ENTER", "PACKED_ADD", "PACKED_SUB", "PACKED_MUL", "PACKED_LOAD", "PACKED_STORE",
    "PACKED_FUSED_MUL_ADD", "MATRIX_MUL", "SIMD_ADD", "USE_FILE", "ITER_START", "ITER_NEXT",
    "IN", "ADD_INT", "SUB_INT", "MUL_INT", "LT_INT", "GT_INT", "LTE_INT", "GTE_INT", "EQ_INT",
    "CALL_METHOD"
};

void print_bytecode(Bytecode *bc) {
    if (!bc) return;
    for (int i = 0; i < bc->count; i++) {
        Instruction *pc = &bc->code[i];
        const char *name = (pc->op < sizeof(op_names)/sizeof(op_names[0])) ? op_names[pc->op] : "UNKNOWN";
        printf("%04d: %-15s r0:%d r1:%d r2:%d arg:%d ptr:%p\n", i, name, pc->r0, pc->r1, pc->r2, pc->arg, (void*)pc->ptr);
    }
}

#define VM_STACK_HEADROOM_ENTRY 512
#define GLOW_MAX_VM_FRAMES 65536

/* Run a callee's compiled code from the interpreter. The callee gets a fresh
   frame above vm_stack_top (compiled code may use slots past the caller's
   argument window) and *frame_base reports where it ran, so a deopt can be
   resumed in that same frame. When compiled code must not run (interpreter-only
   mode, machine stack nearly exhausted, VM stack full) this returns the deopt
   sentinel without running anything and without touching *deopt_pc; the
   interpreter then makes the call itself, which needs no native stack. */
Value vm_call_jit_direct(Value (*f)(Value *, int *), FuncEntry *fe, int callee_base,
                         int *deopt_pc, int *frame_base) {
    if (glow_nojitexec || glow_stack_low()) return VAL_DEOPT_SENTINEL;
    int new_base = vm_stack_top;
    int need = new_base + fe->body->local_count + 4;
    if (call_depth >= 65500 || need + VM_STACK_HEADROOM_ENTRY >= vm_stack_size) return VAL_DEOPT_SENTINEL;
    int saved_top = vm_stack_top;
    if (fe->param_count > 0) memmove(&vm_stack[new_base], &vm_stack[callee_base], (size_t)fe->param_count * sizeof(Value));
    vm_stack_top = need;
    *frame_base = new_base;
    call_stack_push(fe, fe->body->code);
    call_depth++;
    Value res = f(&vm_stack[new_base], deopt_pc);
    call_depth--;
    call_stack_pop();
    vm_stack_top = saved_top;
    return res;
}

THREAD_LOCAL int debug_mode = 0;
static THREAD_LOCAL int dbg_single_step = 1;
static THREAD_LOCAL int dbg_breakpoints[64];
static THREAD_LOCAL int dbg_bp_count = 0;

static void glow_debug_hook(Bytecode *bc, Instruction *pc, int base_reg) {
    if (!debug_mode) return;
    int line = pc->line;
    int is_bp = 0;
    for (int i = 0; i < dbg_bp_count; i++) {
        if (dbg_breakpoints[i] == line) { is_bp = 1; break; }
    }
    if (!dbg_single_step && !is_bp) return;

    printf("\n[DEBUG] Line %d | PC %04d: %-14s (r0:%d r1:%d r2:%d arg:%d)\n",
           line, (int)(pc - bc->code), op_names[pc->op], pc->r0, pc->r1, pc->r2, pc->arg);
    while (1) {
        printf("(glowdb) ");
        fflush(stdout);
        char cmd[128];
        if (!fgets(cmd, sizeof(cmd), stdin)) exit(0);
        cmd[strcspn(cmd, "\r\n")] = '\0';
        if (strlen(cmd) == 0 || strcmp(cmd, "s") == 0 || strcmp(cmd, "step") == 0) {
            dbg_single_step = 1;
            break;
        } else if (strcmp(cmd, "c") == 0 || strcmp(cmd, "continue") == 0) {
            dbg_single_step = 0;
            break;
        } else if (strcmp(cmd, "r") == 0 || strcmp(cmd, "regs") == 0) {
            printf("--- Virtual Registers (Base %d) ---\n", base_reg);
            for (int r = 0; r < 8 && r < bc->local_count; r++) {
                printf("  R%d = ", r);
                print_value(vm_stack[base_reg + r]);
                printf("\n");
            }
        } else if (strncmp(cmd, "b ", 2) == 0) {
            int bp = atoi(cmd + 2);
            if (bp > 0 && dbg_bp_count < 64) {
                dbg_breakpoints[dbg_bp_count++] = bp;
                printf("Breakpoint set at line %d\n", bp);
            }
        } else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
            printf("Exiting debugger.\n");
            exit(0);
        } else {
            printf("Commands: s (step), c (continue), r (print regs), b <line> (set breakpoint), q (quit)\n");
        }
    }
}

/* A callable stored in a dict under `name` (module functions such as
   fs.remove or os.exit live in dicts) takes precedence over the built-in
   dict methods of the same name. */
static int dict_member_is_callable(Dict *d, const char *name, Value *out) {
    Value v = dict_get(d, name);
    if (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD)) {
        ObjType ot = GET_OBJ_TYPE(v);
        if (ot == OBJ_NATIVE_FUNC || ot == OBJ_CLOSURE || ot == OBJ_BOUND_METHOD || ot == OBJ_CLASS) { *out = v; return 1; }
    }
    return 0;
}


/* Directory part of a path ("" when there is none), with a trailing slash. */
int glow_self_exe_path(char *out, size_t outsz) {
    if (!out || outsz < 2) return -1;
    out[0] = '\0';
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)outsz);
    if (n == 0 || n >= outsz) { out[0] = '\0'; return -1; }
    return (int)n;
#elif defined(__APPLE__)
    char raw[PATH_MAX], real[PATH_MAX];
    uint32_t sz = sizeof(raw);
    if (_NSGetExecutablePath(raw, &sz) != 0) return -1;
    const char *p = realpath(raw, real) ? real : raw;  /* resolve symlinks, like /proc/self/exe */
    size_t n = strlen(p);
    if (n >= outsz) return -1;
    memcpy(out, p, n + 1);
    return (int)n;
#else
    ssize_t n = readlink("/proc/self/exe", out, outsz - 1);
    if (n <= 0) { out[0] = '\0'; return -1; }
    out[n] = '\0';
    return (int)n;
#endif
}

static void path_dir(const char *path, char *out, size_t outsz) {
    out[0] = '\0';
    if (!path) return;
    const char *slash = NULL;
    for (const char *c = path; *c; c++) if (*c == '/' || *c == '\\') slash = c;
    if (!slash) return;
    size_t n = (size_t)(slash - path) + 1;
    if (n >= outsz) n = outsz - 1;
    memcpy(out, path, n);
    out[n] = '\0';
}

/* Find a module for `use`/`import`. A relative path is tried, in order:
   next to the importing file, from the current directory, under lib/, and
   under the lib/ folder beside the glowlang executable (so the standard
   libraries work from any directory). *found gets the path that opened. */
/* fopen for reading that refuses folders: on Linux and macOS fopen opens a
   directory, so lib/bridge/ was taken for the module "bridge". */
static FILE *open_module_file(const char *p) {
    struct stat st;
    if (stat(p, &st) != 0 || (st.st_mode & S_IFMT) == S_IFDIR) return NULL;
    return fopen(p, "r");
}

static FILE *glow_open_module_path(const char *path, const char *importer, const char **found) {
    char cand[1024], dir[768];
    int absolute = path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':');
    if (!absolute) {
        path_dir(importer, dir, sizeof(dir));
        if (dir[0]) {
            snprintf(cand, sizeof(cand), "%s%s", dir, path);
            FILE *f = open_module_file(cand);
            if (f) { *found = intern_string(cand); return f; }
        }
    }
    FILE *f = open_module_file(path);
    if (f) { *found = path; return f; }
    if (absolute) return NULL;
    snprintf(cand, sizeof(cand), "lib/%s", path);
    if ((f = open_module_file(cand))) { *found = intern_string(cand); return f; }
    char exe[768] = "";
    glow_self_exe_path(exe, sizeof(exe));
    path_dir(exe, dir, sizeof(dir));
    if (dir[0]) {
        const char *rel = strncmp(path, "lib/", 4) == 0 ? path + 4 : path;
        snprintf(cand, sizeof(cand), "%s../lib/%s", dir, rel);
        if ((f = open_module_file(cand))) { *found = intern_string(cand); return f; }
        snprintf(cand, sizeof(cand), "%slib/%s", dir, rel);
        if ((f = open_module_file(cand))) { *found = intern_string(cand); return f; }
    }
    return NULL;
}

/* `use "name"` without the .glow extension looks for, in order:
   name.glow next to the importing file or in the current directory; a
   package that `gdu install name` placed in .glow_pkg/; then name.glow in
   the standard library folders. So the program's own files come first and
   an installed package wins over the copy bundled with GlowLang. */
static FILE *glow_open_module(const char *path, const char *importer, const char **found) {
    FILE *f = glow_open_module_path(path, importer, found);
    if (f) return f;
    size_t n = strlen(path);
    if (n == 0 || n > 400 || (n > 5 && strcmp(path + n - 5, ".glow") == 0)) return NULL;
    char cand[1024], dir[768];
    path_dir(importer, dir, sizeof(dir));
    if (dir[0]) {
        snprintf(cand, sizeof(cand), "%s%s.glow", dir, path);
        if ((f = fopen(cand, "r"))) { *found = intern_string(cand); return f; }
    }
    snprintf(cand, sizeof(cand), "%s.glow", path);
    if ((f = fopen(cand, "r"))) { *found = intern_string(cand); return f; }
    snprintf(cand, sizeof(cand), ".glow_pkg/%s.glow", path);
    if ((f = glow_open_module_path(cand, importer, found))) { *found = intern_string(*found); return f; }
    snprintf(cand, sizeof(cand), "%s.glow", path);
    if ((f = glow_open_module_path(cand, importer, found))) { *found = intern_string(*found); return f; }
    return NULL;
}

/* An index as a whole number (ints and integral floats). */
static int index_value(Value v, int64_t *out) {
    if (GET_TYPE(v) == VAL_INT) { *out = AS_INT(v); return 1; }
    if (GET_TYPE(v) == VAL_FLOAT) {
        double d = AS_FLOAT(v);
        if (d == floor(d) && fabs(d) < 9.0e15) { *out = (int64_t)d; return 1; }
    }
    return 0;
}

/* Raise "<what>: index i, size n" (or just <what> when size is 0 and i 0). */
static void index_error(const char *what, int64_t i, int64_t size) {
    char msg[160];
    if (i == 0 && size == 0) snprintf(msg, sizeof(msg), "%s", what);
    else snprintf(msg, sizeof(msg), "%s (index %lld, length %lld)", what, (long long)i, (long long)size);
    pending_exception = value_string_copy(msg);
    has_exception = 1;
}

/* Lets compiled code ask whether a runtime call it just made raised. */
int jit_exception_pending(void) { return has_exception; }

/* Back-edge threshold after which a loop is compiled for on-stack entry. */
#define GLOW_OSR_THRESHOLD 1000

 Value run_bytecode(Bytecode *bc, Instruction *start_pc, Closure *current_closure, int base_reg) {
     #ifdef _WIN32
     glow_ensure_veh();
     #endif
     if (glow_entrydbg) { fprintf(stderr, "[ENTER] owner=%s start_pc=%p count=%d jf=%d local_count=%d base_reg=%d\n", (bc->owner&&bc->owner->name)?bc->owner->name:"(null)", (void*)start_pc, bc->count, bc->jit_failed, bc->local_count, base_reg); fflush(stderr); }
     if (++call_depth > 65500 || glow_stack_low() || base_reg + bc->local_count + VM_STACK_HEADROOM_ENTRY >= vm_stack_size) {
        pending_exception = value_string_copy("Maximum recursion depth exceeded");
        has_exception = 1;
        call_depth--;
        return value_none();
    }

    call_stack_push(bc->owner, start_pc ? start_pc : bc->code);
    int my_frame_idx = call_stack_ptr - 1;
    int entry_count = bc->count;
    if (entry_count <= 0 || entry_count > 100000) {
        fprintf(stderr, "[ENTRY-CORRUPT] bc->count=%d owner=%s call_depth=%d\n", entry_count,
                (bc->owner && bc->owner->name) ? bc->owner->name : "?", call_depth);
        for (int _s = call_stack_ptr - 1; _s >= 0; _s--)
            fprintf(stderr, "  frame[%d] fe=%s\n", _s,
                    (call_stack[_s].fe && call_stack[_s].fe->name) ? call_stack[_s].fe->name : "?");
        abort();
    }

    if (glow_topprobe) {
        if (vm_stack_top < 0 || vm_stack_top > vm_stack_size) {
            fprintf(stderr, "[TOPCORRUPT] vm_stack_top=%d size=%d owner=%s base_reg=%d lc=%d\n",
                    vm_stack_top, vm_stack_size, (bc->owner&&bc->owner->name)?bc->owner->name:"?", base_reg, bc->local_count);
            abort();
        }
    }

    size_t old_arena_used = stack_arena_used;
    int old_top = vm_stack_top;

    // Ensure the entire call frame and its incoming arguments are rooted for the GC
    if (base_reg + bc->local_count + 16 > vm_stack_top) {
        vm_stack_top = base_reg + bc->local_count + 16;
    }

    // static THREAD_LOCAL long total_instructions = 0;
    // static THREAD_LOCAL Trace *recording_trace = NULL;


    typedef struct {
        Bytecode *bc;
        Instruction *pc;
        Closure *closure;
        int base_reg;
        int ret_reg;
        size_t old_arena_used;
        int my_frame_idx;
        int old_top;
    } VMFrame;
    /* Frames of the calls this activation makes without recursing in C. All
       run_bytecode activations on a thread share one pool; each uses the
       slice that starts where the activations below it stopped, so a nested
       activation (native code calling back, a runtime helper finishing a
       deopt) never overwrites its caller's frames. */
    static THREAD_LOCAL VMFrame frame_pool[GLOW_MAX_VM_FRAMES];
    static THREAD_LOCAL int frame_pool_used = 0;
    int frame_base = frame_pool_used;
    VMFrame *frames = &frame_pool[frame_base];
    int internal_depth = 0;
    /* Tail-call reuse keeps internal_depth flat, so count tail recursions
     * separately to still enforce the recursion-depth guard on infinite
     * tail recursion (e.g. `func r(): r() end; r()`). Reset on every
     * real frame push/pop. */
    int tail_depth = 0;
    /* Value being returned by l_DO_RETURN (OP_RETURN, or a native OSR body
       that ran to completion). */
    Value vm_ret_val = 0;

    /* The VM register stack never moves (compiled code holds pointers into
       it), so running out of it is reported like any other deep recursion. */
#define VM_STACK_HEADROOM 512
/* Parameters the caller did not pass start as none, not as whatever an
   earlier call left in those stack slots. abs_base is the callee's first
   slot, filled the number of slots the caller wrote (self included) and
   total the callee's self and parameter slots. */
#define CLEAR_MISSING_ARGS(abs_base, filled, total) do { \
    for (int _k = (filled); _k < (total); _k++) vm_stack[(abs_base) + _k] = value_none(); \
} while (0)
#define DO_CALL(target_bc, target_closure, new_base_reg, target_ret_reg) \
    DO_CALL_AT(target_bc, target_closure, new_base_reg, target_ret_reg, 0)
/* Push a frame and continue in the callee at instruction start_idx. A
   non-zero start_idx resumes a call that compiled code began, so the
   callee's locals already hold live values and are not cleared. */
#define DO_CALL_AT(target_bc, target_closure, new_base_reg, target_ret_reg, start_idx) do { \
    int _eval_new_base = (new_base_reg); \
    int _start_idx = (start_idx); \
    if (__builtin_expect(call_depth + internal_depth >= 65500 || \
                         frame_base + internal_depth >= GLOW_MAX_VM_FRAMES - 1 || \
                         _eval_new_base + (target_bc)->local_count + VM_STACK_HEADROOM >= vm_stack_size, 0)) { \
        pending_exception = value_string_copy("Maximum recursion depth exceeded"); has_exception = 1; goto l_OP_THROW; } \
    GLOW_GIL_POLL(); \
    tail_depth = 0; \
    frames[internal_depth].bc = bc; \
    frames[internal_depth].pc = pc; \
    frames[internal_depth].closure = current_closure; \
    frames[internal_depth].base_reg = base_reg; \
    frames[internal_depth].ret_reg = target_ret_reg; \
    frames[internal_depth].old_arena_used = old_arena_used; \
    frames[internal_depth].my_frame_idx = my_frame_idx; \
    frames[internal_depth].old_top = old_top; \
    internal_depth++; \
    frame_pool_used = frame_base + internal_depth; \
    call_stack_push((target_bc)->owner, (target_bc)->code + _start_idx); \
    bc = (target_bc); \
    pc = (target_bc)->code + _start_idx; \
    current_closure = (target_closure); \
    base_reg = _eval_new_base; \
    my_frame_idx = call_stack_ptr - 1; \
    old_arena_used = stack_arena_used; \
    old_top = vm_stack_top; \
    if (__builtin_expect(base_reg + bc->local_count > vm_stack_top, 0)) vm_stack_top = base_reg + bc->local_count; \
    if (_start_idx == 0) \
        for (int _i = bc->preserved_locals; _i < bc->local_count; _i++) vm_stack[base_reg + _i] = value_none(); \
    goto l_DISPATCH; \
} while(0)

/* Call `cfe` (a named function) through its compiled code `jf`. If the
   compiled code declines to run, the interpreter makes the call itself; if it
   stops part-way (deopt, or an exception it leaves to the interpreter), the
   interpreter resumes it in the frame it was using. Neither case nests a C
   level run_bytecode, so recursion depth is bounded by the VM stack only. */
#define CALL_VIA_JIT(jf, cfe) do { \
    int _dpc = -1, _fbase = 0; \
    Value _res = vm_call_jit_direct((Value (*)(Value *, int *))(jf), (cfe), base_reg + pc->r2, &_dpc, &_fbase); \
    if (__builtin_expect(IS_DEOPT(_res), 0)) { \
        if (_dpc < 0) DO_CALL((cfe)->body, NULL, base_reg + pc->r2, pc->r0); \
        DO_CALL_AT((cfe)->body, NULL, _fbase, pc->r0, _dpc); \
    } \
    if (has_exception) { R(pc->r0) = pending_exception; goto l_OP_THROW; } \
    R(pc->r0) = _res; \
} while (0)

/* Loop back-edge: let a waiting thread run, count iterations, and once the
   loop is hot switch into compiled code at the loop header (on-stack
   replacement). Closures with captured variables stay interpreted because
   compiled LOAD_VAR/STORE_VAR address globals directly. */
#define BACKEDGE(target_idx) do { \
    GLOW_GIL_POLL(); \
    if (__builtin_expect(++bc->loop_iterations > GLOW_OSR_THRESHOLD, 0) && !bc->osr_failed && !glow_nojitexec && \
        !(trace_mode | debug_mode | profiling_active) && \
        !(current_closure && current_closure->captured_count > 0)) { \
        if (!bc->osr_entries) jit_compile_osr(bc); \
        void *_osr = bc->osr_entries ? bc->osr_entries[(target_idx)] : NULL; \
        if (_osr) { \
            int _deopt_pc = 0; \
            Value _res = ((Value (*)(Value *, int *))_osr)(&vm_stack[base_reg], &_deopt_pc); \
            if (IS_DEOPT(_res)) { \
                /* Exiting before doing any work: this entry cannot help. */ \
                if (_deopt_pc == (target_idx)) bc->osr_entries[(target_idx)] = NULL; \
                pc = &bc->code[_deopt_pc]; DISPATCH(); \
            } \
            vm_ret_val = _res; goto l_DO_RETURN; \
        } \
    } \
} while (0)

    Instruction *pc = start_pc ? start_pc : bc->code;

    l_RESUME_INTERP:;

    if (!start_pc && bc->count > 0) {
        bc->exec_count++;
        if (bc->jit_tier == JIT_TIER_INTERP && !bc->baseline_jit_code && !bc->jit_failed) {
            if (glow_no_baseline) bc->jit_failed = 1;
            const char *only = glow_base_only;
            if (only) { fprintf(stderr, "[GATE] owner=%s only=%s jit_failed_before=%d\n", (bc->owner&&bc->owner->name)?bc->owner->name:"(null)", only, bc->jit_failed); fflush(stderr); }
            if (only && bc->owner && bc->owner->name && strcmp(only, bc->owner->name) != 0) bc->jit_failed = 1;
            if (bc->exec_count > 5) {
                if (bc->count <= 0 || bc->count > 100000) {
                    fprintf(stderr, "[CORRUPT] bc->count=%d (entry=%d) owner=%s exec_count=%d\n",
                            bc->count, entry_count, (bc->owner && bc->owner->name) ? bc->owner->name : "?", bc->exec_count);
                    for (int _s = call_stack_ptr - 1; _s >= 0; _s--)
                        fprintf(stderr, "  frame[%d] fe=%s\n", _s,
                                (call_stack[_s].fe && call_stack[_s].fe->name) ? call_stack[_s].fe->name : "?");
                    abort();
                }
                if (glow_optgate) jit_compile(bc); else jit_compile_baseline(bc);
            }
        }
        if (bc->jit_tier == JIT_TIER_BASELINE && !bc->jit_code && !bc->jit_failed) {
            if (bc->exec_count > 50) {
                jit_compile(bc);
            }
        }
    }

    /* Native entry: run the compiled body if there is one. A deopt hands back
       the instruction to resume at; the JIT has already written every local
       it computed into the register file, so those must not be cleared. */
    /* Resuming at instruction 0 is a fresh call (callers use it when the JIT
       declines to run), so its locals still start out as none. */
    int resumed_mid_function = (start_pc != NULL && start_pc != bc->code);
    /* Compiled LOAD_VAR/STORE_VAR address globals directly, so a closure
       with captured variables must run in the interpreter. */
    int closure_needs_interp = current_closure && current_closure->captured_count > 0;
    if (!start_pc && !glow_nojitexec && !closure_needs_interp) {
        void *entry = bc->jit_code ? bc->jit_code : (glow_nobaseexec ? NULL : bc->baseline_jit_code);
        if (entry) {
            typedef Value (*JITFunc)(Value *R, int *pc_idx);
            int deopt_pc = 0;
            Value res = ((JITFunc)entry)(&vm_stack[base_reg], &deopt_pc);
            if (__builtin_expect(IS_DEOPT(res), 0)) {
                pc = &bc->code[deopt_pc];
                resumed_mid_function = 1;
            } else {
                /* Always leave through l_FINISH so the call-stack entry, the
                   recursion depth and vm_stack_top are restored. */
                vm_stack[base_reg] = res;
                goto l_FINISH;
            }
        }
    }
    if (__builtin_expect(base_reg + bc->local_count > vm_stack_top, 0)) vm_stack_top = base_reg + bc->local_count;

    // Clear locals but preserve parameters and 'self' (which are the first local_count registers)
    if (!resumed_mid_function) {
        for (int i = bc->preserved_locals; i < bc->local_count; i++) {
            vm_stack[base_reg + i] = value_none();
        }
    }

#ifdef GLOW_COMPUTED_GOTO
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
        &&l_OP_ITER_NEXT,
        &&l_OP_IN,
        &&l_OP_ADD_INT, &&l_OP_SUB_INT, &&l_OP_MUL_INT,
        &&l_OP_LT_INT, &&l_OP_GT_INT, &&l_OP_LTE_INT, &&l_OP_GTE_INT, &&l_OP_EQ_INT,
        &&l_OP_CALL_METHOD
    };
#endif

/* Maximum register index relative to base_reg that is considered valid.
 * The compiler/fuzzer sets local_count ≤ 256, but call opcodes can reference
 * argument windows past that.  We pick 4096 as a generous ceiling — any index
 * beyond that in random bytecode is surely garbage.  */
#define VM_REG_LIMIT 4096

/* Clamp loop-count + base so that base+count never exceeds vm_stack_size
 * relative to base_reg.  Used by OP_SHOW, OP_WRITE, OP_LIST_LITERAL, etc. */
#define CLAMP_REG_COUNT(base, count) do { \
    if ((count) < 0) (count) = 0; \
    int _max = vm_stack_size - base_reg - (base); \
    if (_max < 0) _max = 0; \
    if ((count) > _max) (count) = _max; \
} while(0)

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
/* Bounds-checked register access via GCC statement expression (lvalue-safe).
 * OOB indices are redirected to slot 0 which is always valid. */
#define R(idx) vm_stack[({ int _ri = base_reg + (idx); (_ri >= 0 && _ri < vm_stack_size) ? _ri : 0; })]
#else
#define R(idx) vm_stack[base_reg + (idx)]
#endif

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
#define CHECK_FUZZ_TIMEOUT() if (__builtin_expect(in_fuzz_timeout, 0)) { pending_exception = value_string_copy("Fuzzer Timeout"); has_exception = 1; goto l_OP_THROW; }
#else
#define CHECK_FUZZ_TIMEOUT()
#endif

#ifndef GLOW_COMPUTED_GOTO
#define DISPATCH_SWITCH() \
    switch(pc->op) { \
        case OP_CONST_INT: goto l_OP_CONST_INT; \
        case OP_CONST_FLOAT: goto l_OP_CONST_FLOAT; \
        case OP_CONST_STRING: goto l_OP_CONST_STRING; \
        case OP_CONST_BOOL: goto l_OP_CONST_BOOL; \
        case OP_LOAD_VAR: goto l_OP_LOAD_VAR; \
        case OP_STORE_VAR: goto l_OP_STORE_VAR; \
        case OP_ADD: goto l_OP_ADD; \
        case OP_SUB: goto l_OP_SUB; \
        case OP_MUL: goto l_OP_MUL; \
        case OP_DIV: goto l_OP_DIV; \
        case OP_MOD: goto l_OP_MOD; \
        case OP_POW: goto l_OP_POW; \
        case OP_NEG: goto l_OP_NEG; \
        case OP_LT: goto l_OP_LT; \
        case OP_GT: goto l_OP_GT; \
        case OP_LTE: goto l_OP_LTE; \
        case OP_GTE: goto l_OP_GTE; \
        case OP_EQ: goto l_OP_EQ; \
        case OP_NEQ: goto l_OP_NEQ; \
        case OP_AND: goto l_OP_AND; \
        case OP_OR: goto l_OP_OR; \
        case OP_NOT: goto l_OP_NOT; \
        case OP_JUMP: goto l_OP_JUMP; \
        case OP_JUMP_IF_FALSE: goto l_OP_JUMP_IF_FALSE; \
        case OP_JUMP_IF_TRUE: goto l_OP_JUMP_IF_TRUE; \
        case OP_LEN: goto l_OP_LEN; \
        case OP_RANGE: goto l_OP_RANGE; \
        case OP_TYPE: goto l_OP_TYPE; \
        case OP_APPEND: goto l_OP_APPEND; \
        case OP_INPUT: goto l_OP_INPUT; \
        case OP_SHOW: goto l_OP_SHOW; \
        case OP_WRITE: goto l_OP_WRITE; \
        case OP_LIST_LITERAL: goto l_OP_LIST_LITERAL; \
        case OP_DICT_LITERAL: goto l_OP_DICT_LITERAL; \
        case OP_INDEX: goto l_OP_INDEX; \
        case OP_SLICE: goto l_OP_SLICE; \
        case OP_STORE_INDEX: goto l_OP_STORE_INDEX; \
        case OP_FUNC_DEF: goto l_OP_FUNC_DEF; \
        case OP_CALL: goto l_OP_CALL; \
        case OP_CALL_SELF_FAST: goto l_OP_CALL_SELF_FAST; \
        case OP_TAIL_CALL: goto l_OP_TAIL_CALL; \
        case OP_RETURN: goto l_OP_RETURN; \
        case OP_FOR_EACH: goto l_OP_FOR_EACH; \
        case OP_FOR_EACH_PAIR: goto l_OP_FOR_EACH_PAIR; \
        case OP_STOP: goto l_OP_STOP; \
        case OP_SKIP: goto l_OP_SKIP; \
        case OP_POP: goto l_OP_POP; \
        case OP_PUSH_SCOPE: goto l_OP_PUSH_SCOPE; \
        case OP_POP_SCOPE: goto l_OP_POP_SCOPE; \
        case OP_STORE_LET: goto l_OP_STORE_LET; \
        case OP_LOAD_LOCAL: goto l_OP_LOAD_LOCAL; \
        case OP_STORE_LOCAL: goto l_OP_STORE_LOCAL; \
        case OP_DICT_KEYS: goto l_OP_DICT_KEYS; \
        case OP_DICT_VALUES: goto l_OP_DICT_VALUES; \
        case OP_DICT_HAS: goto l_OP_DICT_HAS; \
        case OP_DICT_REMOVE: goto l_OP_DICT_REMOVE; \
        case OP_DICT_MERGE: goto l_OP_DICT_MERGE; \
        case OP_MATH_ABS: goto l_OP_MATH_ABS; \
        case OP_MATH_MIN: goto l_OP_MATH_MIN; \
        case OP_MATH_MAX: goto l_OP_MATH_MAX; \
        case OP_MATH_ROUND: goto l_OP_MATH_ROUND; \
        case OP_MATH_FLOOR: goto l_OP_MATH_FLOOR; \
        case OP_MATH_CEIL: goto l_OP_MATH_CEIL; \
        case OP_MATH_SQRT: goto l_OP_MATH_SQRT; \
        case OP_STR_UPPER: goto l_OP_STR_UPPER; \
        case OP_STR_LOWER: goto l_OP_STR_LOWER; \
        case OP_STR_TRIM: goto l_OP_STR_TRIM; \
        case OP_STR_FIND: goto l_OP_STR_FIND; \
        case OP_STR_REPLACE: goto l_OP_STR_REPLACE; \
        case OP_STR_STARTSWITH: goto l_OP_STR_STARTSWITH; \
        case OP_STR_ENDSWITH: goto l_OP_STR_ENDSWITH; \
        case OP_STR_SPLIT: goto l_OP_STR_SPLIT; \
        case OP_LIST_PUSH: goto l_OP_LIST_PUSH; \
        case OP_LIST_POP: goto l_OP_LIST_POP; \
        case OP_LIST_INSERT: goto l_OP_LIST_INSERT; \
        case OP_LIST_REMOVE: goto l_OP_LIST_REMOVE; \
        case OP_LIST_CONTAINS: goto l_OP_LIST_CONTAINS; \
        case OP_LIST_SORT: goto l_OP_LIST_SORT; \
        case OP_LIST_REVERSE: goto l_OP_LIST_REVERSE; \
        case OP_FILE_READ: goto l_OP_FILE_READ; \
        case OP_FILE_WRITE: goto l_OP_FILE_WRITE; \
        case OP_FILE_APPEND: goto l_OP_FILE_APPEND; \
        case OP_FSTRING: goto l_OP_FSTRING; \
        case OP_LIST_COMP: goto l_OP_LIST_COMP; \
        case OP_THROW: goto l_OP_THROW; \
        case OP_TRY_START: goto l_OP_TRY_START; \
        case OP_TRY_END: goto l_OP_TRY_END; \
        case OP_LAMBDA: goto l_OP_LAMBDA; \
        case OP_CALL_VALUE: goto l_OP_CALL_VALUE; \
        case OP_CLASS_DEF: goto l_OP_CLASS_DEF; \
        case OP_METHOD_DEF: goto l_OP_METHOD_DEF; \
        case OP_TRAIT_DEF: goto l_OP_TRAIT_DEF; \
        case OP_USE_TRAIT: goto l_OP_USE_TRAIT; \
        case OP_GET_PROPERTY: goto l_OP_GET_PROPERTY; \
        case OP_SET_PROPERTY: goto l_OP_SET_PROPERTY; \
        case OP_SPAWN: goto l_OP_SPAWN; \
        case OP_MOVE: goto l_OP_MOVE; \
        case OP_NOP: goto l_OP_NOP; \
        case OP_TRACE_ENTER: goto l_OP_TRACE_ENTER; \
        case OP_PACKED_ADD: goto l_OP_PACKED_ADD; \
        case OP_PACKED_SUB: goto l_OP_PACKED_SUB; \
        case OP_PACKED_MUL: goto l_OP_PACKED_MUL; \
        case OP_PACKED_LOAD: goto l_OP_PACKED_LOAD; \
        case OP_PACKED_STORE: goto l_OP_PACKED_STORE; \
        case OP_PACKED_FUSED_MUL_ADD: goto l_OP_PACKED_FUSED_MUL_ADD; \
        case OP_MATRIX_MUL: goto l_OP_MATRIX_MUL; \
        case OP_SIMD_ADD: goto l_OP_SIMD_ADD; \
        case OP_USE_FILE: goto l_OP_USE_FILE; \
        case OP_ITER_START: goto l_OP_ITER_START; \
        case OP_ITER_NEXT: goto l_OP_ITER_NEXT; \
        case OP_IN: goto l_OP_IN; \
        case OP_ADD_INT: goto l_OP_ADD_INT; \
        case OP_SUB_INT: goto l_OP_SUB_INT; \
        case OP_MUL_INT: goto l_OP_MUL_INT; \
        case OP_LT_INT: goto l_OP_LT_INT; \
        case OP_GT_INT: goto l_OP_GT_INT; \
        case OP_LTE_INT: goto l_OP_LTE_INT; \
        case OP_GTE_INT: goto l_OP_GTE_INT; \
        case OP_EQ_INT: goto l_OP_EQ_INT; \
        case OP_CALL_METHOD: goto l_OP_CALL_METHOD; \
    }

#define DISPATCH() { \
    if (__builtin_expect(has_exception | profiling_active | trace_mode | debug_mode, 0)) { \
        g_dbg_bc = bc; g_dbg_pc = pc; \
        CHECK_FUZZ_TIMEOUT(); \
        if (profiling_active) pc->exec_count++; \
        if (trace_mode) { \
            printf("DEBUG %04d: %-15s r0:%d r1:%d r2:%d arg:%d\n", (int)(pc - bc->code), op_names[pc->op], pc->r0, pc->r1, pc->r2, pc->arg); \
            visualize_stack(base_reg, bc->local_count); \
        } \
        if (debug_mode) { \
            glow_debug_hook(bc, pc, base_reg); \
        } \
        if (has_exception) goto l_OP_THROW; \
    } \
    DISPATCH_SWITCH(); \
}
#else
#define DISPATCH() { \
    if (__builtin_expect(has_exception | profiling_active | trace_mode | debug_mode, 0)) { \
        g_dbg_bc = bc; g_dbg_pc = pc; \
        CHECK_FUZZ_TIMEOUT(); \
    if (profiling_active) pc->exec_count++; \
    if (trace_mode) { \
        printf("DEBUG %04d: %-15s r0:%d r1:%d r2:%d arg:%d\n", (int)(pc - bc->code), op_names[pc->op], pc->r0, pc->r1, pc->r2, pc->arg); \
        visualize_stack(base_reg, bc->local_count); \
    } \
    if (debug_mode) { \
        glow_debug_hook(bc, pc, base_reg); \
    } \
        if (has_exception) goto l_OP_THROW; \
    } \
    goto *dt[pc->op]; \
}
#endif
#ifndef GLOW_COMPUTED_GOTO
/* Re-run the current (just rewritten) instruction without counting it again. */
#define DISPATCH_CURRENT() { DISPATCH_SWITCH(); }
#else
#define DISPATCH_CURRENT() goto *dt[pc->op]
#endif

    l_DISPATCH: DISPATCH();

    l_OP_CONST_INT: R(pc->r0) = value_int(pc->arg); pc++; goto l_DISPATCH;
    l_OP_CONST_FLOAT: R(pc->r0) = *(Value*)pc->ptr; pc++; goto l_DISPATCH;
    l_OP_CONST_STRING: R(pc->r0) = vm_get_const_string((const char*)pc->ptr); pc++; goto l_DISPATCH;
    l_OP_CONST_BOOL: R(pc->r0) = value_bool(pc->arg); pc++; goto l_DISPATCH;
    l_OP_LOAD_VAR: {
        int slot = pc->arg;
        if (slot < 0 || slot >= global_table.count) { pending_exception = value_string_copy("LOAD_VAR slot out of bounds"); has_exception = 1; goto l_OP_THROW; }

        bool found_in_closure = false;
        if (current_closure) {
            const char *var_name = global_table.names[slot];
            if (var_name) {
                for (int i = 0; i < current_closure->captured_count; i++) {
                    if (strcmp(current_closure->captured_names[i], var_name) == 0) {
                        R(pc->r0) = current_closure->captured_values[i];
                        found_in_closure = true;
                        break;
                    }
                }
            }
        }

        if (!found_in_closure) {
            Value v = global_table.values[slot];
            if (GET_TYPE(v) == VAL_NONE) {
                const char *var_name = global_table.names[slot];
                if (var_name) {
                    FuncEntry *fe = func_get(var_name);
                    if (fe) {
                        v = bound_method_new(value_none(), fe);
                    }
                }
            }
            R(pc->r0) = v;
        }
        pc++; goto l_DISPATCH;
    }
    l_OP_STORE_VAR: {
        int slot = pc->arg;
        if (slot < 0 || slot >= global_table.count) { pending_exception = value_string_copy("STORE_VAR slot out of bounds"); has_exception = 1; goto l_OP_THROW; }

        bool found_in_closure = false;
        if (current_closure) {
            const char *var_name = global_table.names[slot];
            if (var_name) {
                for (int i = 0; i < current_closure->captured_count; i++) {
                    if (strcmp(current_closure->captured_names[i], var_name) == 0) {
                        current_closure->captured_values[i] = R(pc->r0);
                        found_in_closure = true;
                        break;
                    }
                }
            }
        }

        if (!found_in_closure) {
            global_table.values[slot] = R(pc->r0);
        }
        pc++; goto l_DISPATCH;
    }
    l_OP_MOVE: R(pc->r0) = R(pc->r1); pc++; DISPATCH_CURRENT();
    l_OP_ADD: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (IS_INT(a) && IS_INT(b)) {
            pc->op = OP_ADD_INT;
            int64_t ia = AS_INT(a), ib = AS_INT(b);
            int64_t sum = ia + ib;
            if (__builtin_expect(sum >= -140737488355328LL && sum <= 140737488355327LL, 1)) {
                R(pc->r0) = value_int(sum);
                pc++;
                goto l_DISPATCH;
            }
            R(pc->r0) = num_add(a, b);
            pc++;
            goto l_DISPATCH;
        } else if (GET_TYPE(a) == VAL_STRING || GET_TYPE(b) == VAL_STRING) {
            R(pc->r0) = glow_string_concat(a, b);
            pc++;
            goto l_DISPATCH;
        } else if (GET_TYPE(a) == VAL_LIST && GET_TYPE(b) == VAL_LIST) {
            pc->type = 5;
            List *la = AS_LIST(a), *lb = AS_LIST(b);
            List *rl = list_new(la->size + lb->size);
            for (int i=0; i<la->size; i++) list_push(rl, la->items[i]);
            for (int i=0; i<lb->size; i++) list_push(rl, lb->items[i]);
            R(pc->r0) = value_list(rl);
            pc++;
            goto l_DISPATCH;
        } else {
            if (!is_numeric(a) || !is_numeric(b)) { arith_type_error("+", a, b); goto l_OP_THROW; }
            if (GET_TYPE(a) == VAL_FLOAT || GET_TYPE(b) == VAL_FLOAT) pc->type = 2;
            R(pc->r0) = num_add(a, b);
            pc++;
            goto l_DISPATCH;
        }
    }
    l_OP_SUB: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (IS_INT(a) && IS_INT(b)) {
            pc->op = OP_SUB_INT;
            int64_t ia = AS_INT(a), ib = AS_INT(b);
            int64_t res = ia - ib;
            if (__builtin_expect(res >= -140737488355328LL && res <= 140737488355327LL, 1)) {
                R(pc->r0) = value_int(res);
                pc++;
                goto l_DISPATCH;
            }
        }
        if (!is_numeric(a) || !is_numeric(b)) { arith_type_error("-", a, b); goto l_OP_THROW; }
        R(pc->r0) = num_sub(a, b);
        pc++;
        goto l_DISPATCH;
    }
    l_OP_MUL: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (IS_INT(a) && IS_INT(b)) {
            pc->op = OP_MUL_INT;
            int64_t ia = AS_INT(a), ib = AS_INT(b);
            int64_t res = ia * ib;
            if (__builtin_expect((ib == 0 || res / ib == ia) && (res >= -140737488355328LL && res <= 140737488355327LL), 1)) {
                R(pc->r0) = value_int(res);
                pc++;
                goto l_DISPATCH;
            }
        }
        if (!is_numeric(a) || !is_numeric(b)) {
            Value rep_v;
            if (seq_repeat(a, b, &rep_v)) {
                if (has_exception) goto l_OP_THROW;
                R(pc->r0) = rep_v; pc++; goto l_DISPATCH;
            }
            arith_type_error("*", a, b); goto l_OP_THROW;
        }
        R(pc->r0) = num_mul(a, b);
        pc++;
        goto l_DISPATCH;
    }
    l_OP_DIV: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (!is_numeric(a) || !is_numeric(b)) { arith_type_error("/", a, b); goto l_OP_THROW; }
        if (val_to_double(b) == 0.0) {
            pending_exception = value_string_copy("Division by zero");
            has_exception = 1;
            goto l_OP_THROW;
        }
        if (GET_TYPE(a)==VAL_INT && GET_TYPE(b)==VAL_INT) pc->type = 1; else pc->type = 2;
        R(pc->r0) = num_div(a, b);
        pc++; goto l_DISPATCH;
    }
    l_OP_LT: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (IS_INT(a) && IS_INT(b)) {
            pc->op = OP_LT_INT;
            R(pc->r0) = value_bool(AS_INT(a) < AS_INT(b));
        } else {
            if (!values_comparable(a, b)) { compare_type_error("<", a, b); goto l_OP_THROW; }
            R(pc->r0) = value_bool(value_cmp(a, b) < 0);
        }
        pc++;
        goto l_DISPATCH;
    }
    l_OP_GT: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (IS_INT(a) && IS_INT(b)) {
            pc->op = OP_GT_INT;
            R(pc->r0) = value_bool(AS_INT(a) > AS_INT(b));
        } else {
            if (!values_comparable(a, b)) { compare_type_error(">", a, b); goto l_OP_THROW; }
            R(pc->r0) = value_bool(value_cmp(a, b) > 0);
        }
        pc++;
        goto l_DISPATCH;
    }
    l_OP_LTE: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (IS_INT(a) && IS_INT(b)) {
            pc->op = OP_LTE_INT;
            R(pc->r0) = value_bool(AS_INT(a) <= AS_INT(b));
        } else {
            if (!values_comparable(a, b)) { compare_type_error("<=", a, b); goto l_OP_THROW; }
            R(pc->r0) = value_bool(!either_nan(a, b) && value_cmp(a, b) <= 0);
        }
        pc++;
        goto l_DISPATCH;
    }
    l_OP_GTE: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (IS_INT(a) && IS_INT(b)) {
            pc->op = OP_GTE_INT;
            R(pc->r0) = value_bool(AS_INT(a) >= AS_INT(b));
        } else {
            if (!values_comparable(a, b)) { compare_type_error(">=", a, b); goto l_OP_THROW; }
            R(pc->r0) = value_bool(!either_nan(a, b) && value_cmp(a, b) >= 0);
        }
        pc++;
        goto l_DISPATCH;
    }
    l_OP_EQ: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (IS_INT(a) && IS_INT(b)) {
            pc->op = OP_EQ_INT;
            R(pc->r0) = value_bool(AS_INT(a) == AS_INT(b));
        } else {
            R(pc->r0) = value_bool(val_eq(a, b));
        }
        pc++;
        goto l_DISPATCH;
    }
    l_OP_NEQ: R(pc->r0) = value_bool(!val_eq(R(pc->r1), R(pc->r2))); pc++; goto l_DISPATCH;
    l_OP_AND: R(pc->r0) = value_bool(value_truthy(R(pc->r1)) && value_truthy(R(pc->r2))); pc++; goto l_DISPATCH;
    l_OP_OR: R(pc->r0) = value_bool(value_truthy(R(pc->r1)) || value_truthy(R(pc->r2))); pc++; goto l_DISPATCH;
    l_OP_NOT: R(pc->r0) = value_bool(!value_truthy(R(pc->r1))); pc++; goto l_DISPATCH;
    l_OP_JUMP: {
        if (pc->arg < 0 || pc->arg >= bc->count) { pending_exception = value_string_copy("JUMP out of bounds"); has_exception = 1; goto l_OP_THROW; }
        if (pc->arg <= (int)(pc - bc->code)) {
            if (__builtin_expect(bc->jit_tier != JIT_TIER_OPTIMIZING && !bc->jit_failed, 0)) {
                bc->exec_count++;
                if (bc->jit_tier == JIT_TIER_INTERP && !bc->baseline_jit_code && bc->exec_count > 10) {
                    jit_compile_baseline(bc);
                }
                if (bc->jit_tier == JIT_TIER_BASELINE && !bc->jit_code && bc->exec_count > 50) {
                    jit_compile(bc);
                }
            }
            int _target = pc->arg;
            BACKEDGE(_target);
        }
        pc = bc->code + pc->arg; DISPATCH();
    }
    l_OP_JUMP_IF_FALSE: {
        if(!value_truthy(R(pc->r0))) {
            if (pc->arg < 0 || pc->arg >= bc->count) { pending_exception = value_string_copy("JUMP out of bounds"); has_exception = 1; goto l_OP_THROW; }
            if (pc->arg <= (int)(pc - bc->code)) {
                if (__builtin_expect(bc->jit_tier != JIT_TIER_OPTIMIZING && !bc->jit_failed, 0)) {
                    bc->exec_count++;
                    if (bc->jit_tier == JIT_TIER_INTERP && !bc->baseline_jit_code && bc->exec_count > 10) {
                        jit_compile_baseline(bc);
                    }
                    if (bc->jit_tier == JIT_TIER_BASELINE && !bc->jit_code && bc->exec_count > 50) {
                        jit_compile(bc);
                    }
                }
                int _target = pc->arg;
                BACKEDGE(_target);
            }
            pc = bc->code + pc->arg;
        } else {
            pc++;
        }
        DISPATCH();
    }
    l_OP_JUMP_IF_TRUE: {
        if(value_truthy(R(pc->r0))) {
            if (pc->arg < 0 || pc->arg >= bc->count) { pending_exception = value_string_copy("JUMP out of bounds"); has_exception = 1; goto l_OP_THROW; }
            if (pc->arg <= (int)(pc - bc->code)) {
                if (__builtin_expect(bc->jit_tier != JIT_TIER_OPTIMIZING && !bc->jit_failed, 0)) {
                    bc->exec_count++;
                    if (bc->jit_tier == JIT_TIER_INTERP && !bc->baseline_jit_code && bc->exec_count > 10) {
                        jit_compile_baseline(bc);
                    }
                    if (bc->jit_tier == JIT_TIER_BASELINE && !bc->jit_code && bc->exec_count > 50) {
                        jit_compile(bc);
                    }
                }
                int _target = pc->arg;
                BACKEDGE(_target);
            }
            pc = bc->code + pc->arg;
        } else {
            pc++;
        }
        DISPATCH();
    }
    l_OP_FUNC_DEF: pc++; goto l_DISPATCH;
    l_OP_CALL: {
        int ac = pc->arg; const char *n = (const char*)pc->ptr;
        if (!n) {
            pending_exception = value_string_copy("Attempted to call a null function name");
            has_exception = 1; goto l_OP_THROW;
        }
        FuncEntry *fe = func_get(n);
        if (fe && fe->body) CLEAR_MISSING_ARGS(base_reg + pc->r2, ac, fe->body->preserved_locals);
        call_stack[my_frame_idx].pc = pc; // Update current frame PC
        /* A nested function calling itself keeps its captured variables. */
        if (fe && fe->body && current_closure && current_closure->body == fe->body) {
            DO_CALL(fe->body, current_closure, base_reg + pc->r2, pc->r0);
        }
        if (fe && fe->body) {
            TypeSignature sig = compute_signature(&vm_stack[base_reg + pc->r2], fe->param_count);

            void *jit = func_get_specialization(fe, sig);
            // 1. PIC Lookup
            if (pc->cache[0].key == (void*)sig && pc->cache[0].val) {
                bool cache_valid = false;
                for (int i = 0; i < fe->spec_count; i++) {
                    if (fe->specs[i].jit_code == (void*)pc->cache[0].val) {
                        cache_valid = true;
                        break;
                    }
                }
                if (!cache_valid) {
                    pc->cache[0].key = NULL;
                    pc->cache[0].val = 0;
                } else {
                    if (glow_jitdbg) { fprintf(stderr, "[JITRUN] owner=%s SPEC base=%d\n", (fe&&fe->name)?fe->name:"?", base_reg + pc->r2); fflush(stderr); }
                    CALL_VIA_JIT(jit, fe);
                    pc++; goto l_DISPATCH;
                }
            }

            if (jit && jit != (void*)1) {
                // Update Cache
                pc->cache[0].key = (void*)sig;
                pc->cache[0].val = (intptr_t)jit;

                CALL_VIA_JIT(jit, fe);
            } else {
                fe->used++;
                func_maybe_tier_up(fe);
                if (fe->body && fe->body->jit_code) {
                    CALL_VIA_JIT(fe->body->jit_code, fe);
                } else if (fe->body && fe->body->baseline_jit_code && !fe->body->jit_code) {
                    if (glow_jitdbg) { fprintf(stderr, "[JITRUN] owner=%s BASELINE base=%d\n", (fe&&fe->name)?fe->name:"?", base_reg + pc->r2); fflush(stderr); }
                    CALL_VIA_JIT(fe->body->baseline_jit_code, fe);
                } else {
                    if (fe->used > 10 && fe->spec_count < 8 && !fe->body->jit_failed) {
                        void *jit_check = func_get_specialization(fe, sig);
                        if (!jit_check) {
                            jit_compile_specialized(fe, sig);
                            if (!func_get_specialization(fe, sig)) {
                                func_add_specialization(fe, sig, (void*)1, 0);
                            }
                        }
                    }
                    DO_CALL(fe->body, NULL, base_reg + pc->r2, pc->r0);
                }
            }
        } else {
            Value v = var_get(n);
            if (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD)) {
                ObjType ot = GET_OBJ_TYPE(v);
                if (ot == OBJ_CLASS) {
                    Value inst = value_instance(instance_new(AS_CLASS(v)));
                    R(pc->r0) = inst;
                    Value init_method = class_get_method(AS_CLASS(v), "init");
                    if (GET_TYPE(init_method) == VAL_OBJECT && (init_method & MASK_PAYLOAD)) {
                        Value temp[256];
                        int count = (ac > 255) ? 255 : ac;
                        for (int i=0; i<count; i++) temp[i] = R(pc->r2 + i);

                        int callee_base = base_reg + pc->r2;
                        if (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 + 512 >= vm_stack_size) { /* VM stack is fixed-size; never realloc it */
                            pending_exception = value_string_copy("Maximum recursion depth exceeded"); has_exception = 1; goto l_OP_THROW;
                        }

                        FuncEntry *ife = AS_BOUND_METHOD(init_method)->method;
                        vm_stack[callee_base] = inst; // self
                        for (int i=0; i<count && (!ife || i<ife->param_count); i++) vm_stack[callee_base + i + 1] = temp[i];
                        if (ife) {
                            for (int i=count; i<ife->param_count; i++) vm_stack[callee_base + i + 1] = value_none();
                        }

                        Value old_self = var_get("self");
                        var_set("self", inst);
                        if (ife && ife->body) {
                            DO_CALL(ife->body, NULL, callee_base, -1);
                        }
                        var_set("self", old_self);
                    }
                }
                else if (ot == OBJ_BOUND_METHOD) {
                    BoundMethod *bm = AS_BOUND_METHOD(v);
                    if (bm->native_method) {
                        Value args[256];
                        args[0] = bm->instance;
                        int count = ac;
                        if (count > 254) count = 254;
                        int start_offset = pc->is_method ? 1 : 0;
                        for (int i = 0; i < count; i++) {
                            args[i + 1] = R(pc->r2 + start_offset + i);
                        }
                        R(pc->r0) = bm->native_method(count + 1, args);
                    } else if (bm->method && bm->method->body) {
                        // Calling a bound method held as a value: the method body
                        // expects self at R0 and args at R1.., so lay out the callee
                        // frame the same way the direct obj.method() path does rather
                        // than starting args at R0 (which dropped the first argument).
                        int has_self = (GET_TYPE(bm->instance) != VAL_NONE);
                        int count = ac; if (count > 254) count = 254;
                        int start_offset = pc->is_method ? 1 : 0;
                        Value temp[256];
                        for (int i = 0; i < count; i++) temp[i] = R(pc->r2 + start_offset + i);
                        int callee_base = base_reg + pc->r2;
                        if (callee_base + bm->method->body->local_count + 16 + 512 >= vm_stack_size) { /* VM stack is fixed-size; never realloc it */
                            pending_exception = value_string_copy("Maximum recursion depth exceeded"); has_exception = 1; goto l_OP_THROW;
                        }
                        int slot = callee_base;
                        if (has_self) { vm_stack[slot] = bm->instance; slot++; }
                        for (int i = 0; i < count; i++) vm_stack[slot + i] = temp[i];
                        CLEAR_MISSING_ARGS(callee_base, slot - callee_base + count, bm->method->body->preserved_locals);
                        DO_CALL(bm->method->body, NULL, callee_base, pc->r0);
                    }
                }
                else if (ot == OBJ_CLOSURE) {
                    Closure *c = AS_CLOSURE(v);
                    if (c->body) {
                        CLEAR_MISSING_ARGS(base_reg + pc->r2, ac, c->body->preserved_locals);
                        DO_CALL(c->body, c, base_reg + pc->r2, pc->r0);
                    } else R(pc->r0) = value_none();
                }
                else if (ot == OBJ_NATIVE_FUNC) {
                    Value *args = &R(pc->r2);
                    int argc = ac;
                    if (pc->is_method) {
                        args++;
                    }
                    R(pc->r0) = glow_call_native(AS_NATIVE_FUNC(v), argc, args);
                }
                else R(pc->r0) = value_none();
            } else {
                char msg[256]; snprintf(msg, 256, "Function '%s' not found", n);
                pending_exception = value_string_copy(msg);
                has_exception = 1;
                goto l_OP_THROW;
            }
        }
        pc++; goto l_DISPATCH;
    }
    l_OP_CALL_SELF_FAST: {
        FuncEntry *fe = bc->owner;
        if (fe && fe->body) CLEAR_MISSING_ARGS(base_reg + pc->r2, pc->arg, fe->body->preserved_locals);
        if (fe && fe->body && current_closure && current_closure->body == fe->body) {
            call_stack[my_frame_idx].pc = pc;
            DO_CALL(fe->body, current_closure, base_reg + pc->r2, pc->r0);
        }
        if (fe && fe->body) {
            call_stack[my_frame_idx].pc = pc;
            TypeSignature sig = compute_signature(&vm_stack[base_reg + pc->r2], fe->param_count);

            void *jit = func_get_specialization(fe, sig);
            if (jit && jit != (void*)1) {
                pc->cache[0].key = (void*)sig;
                pc->cache[0].val = (intptr_t)jit;
                CALL_VIA_JIT(jit, fe);
            } else {
                fe->used++;
                func_maybe_tier_up(fe);
                if (fe->body && fe->body->jit_code) {
                    CALL_VIA_JIT(fe->body->jit_code, fe);
                } else if (fe->body && fe->body->baseline_jit_code && !fe->body->jit_code) {
                    if (glow_jitdbg) { fprintf(stderr, "[JITRUN] owner=%s BASELINE base=%d\n", (fe&&fe->name)?fe->name:"?", base_reg + pc->r2); fflush(stderr); }
                    CALL_VIA_JIT(fe->body->baseline_jit_code, fe);
                } else {
                    if (fe->used > 10 && fe->spec_count < 8 && !fe->body->jit_failed) {
                        void *jit_check = func_get_specialization(fe, sig);
                        if (!jit_check) {
                            jit_compile_specialized(fe, sig);
                            if (!func_get_specialization(fe, sig)) {
                                func_add_specialization(fe, sig, (void*)1, 0);
                            }
                        }
                    }
                    DO_CALL(fe->body, NULL, base_reg + pc->r2, pc->r0);
                }
            }
        }
        pc++; goto l_DISPATCH;
    }
    l_OP_TAIL_CALL: {
        int ac = pc->arg; const char *n = (const char*)pc->ptr;
        FuncEntry *fe = NULL;
        Value callee = 0;
        bool is_bound_method = false;
        Value bound_instance = value_none();
        if (n) fe = func_get(n);
        else {
            callee = R(pc->r1);
            if (GET_TYPE(callee) == VAL_OBJECT && (callee & MASK_PAYLOAD)) {
                ObjType ot = GET_OBJ_TYPE(callee);
                if (ot == OBJ_BOUND_METHOD) {
                    BoundMethod *bm = AS_BOUND_METHOD(callee);
                    if (bm->native_method) {
                        Value args[256];
                        args[0] = bm->instance;
                        int count = ac;
                        if (count > 254) count = 254;
                        int start_offset = pc->is_method ? 1 : 0;
                        for (int i = 0; i < count; i++) {
                            args[i + 1] = R(pc->r2 + start_offset + i);
                        }
                        R(pc->r0) = bm->native_method(count + 1, args);
                        goto l_OP_RETURN;
                    } else {
                        fe = bm->method;
                        is_bound_method = true;
                        bound_instance = bm->instance;
                    }
                }
                else if (ot == OBJ_NATIVE_FUNC) {
                    Value *args = &R(pc->r2);
                    int argc = ac;
                    if (pc->is_method) {
                        args++;
                    }
                    R(pc->r0) = glow_call_native(AS_NATIVE_FUNC(callee), argc, args);
                    goto l_OP_RETURN;
                }
                else if (ot == OBJ_CLASS) {
                    Value inst = value_instance(instance_new(AS_CLASS(callee)));
                    Value init_method = class_get_method(AS_CLASS(callee), "init");
                    if (GET_TYPE(init_method) == VAL_OBJECT && (init_method & MASK_PAYLOAD)) {
                        Value temp[256];
                        int count = (ac > 255) ? 255 : ac;
                        for (int i=0; i<count; i++) temp[i] = R(pc->r2 + i);

                        int callee_base = base_reg + pc->r2;
                        if (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 + 512 >= vm_stack_size) { /* VM stack is fixed-size; never realloc it */
                            pending_exception = value_string_copy("Maximum recursion depth exceeded"); has_exception = 1; goto l_OP_THROW;
                        }
                        FuncEntry *ife = AS_BOUND_METHOD(init_method)->method;
                        vm_stack[callee_base] = inst; // self at R0
                        for (int i=0; i<count && (!ife || i<ife->param_count); i++) vm_stack[callee_base + i + 1] = temp[i];
                        if (ife) {
                            for (int i=count; i<ife->param_count; i++) vm_stack[callee_base + i + 1] = value_none();
                        }

                        Value old_self = var_get("self");
                        var_set("self", inst);
                        if (ife && ife->body) {
                            DO_CALL(ife->body, NULL, callee_base, -1);
                        }
                        var_set("self", old_self);
                    }
                    R(pc->r0) = inst;
                    goto l_OP_RETURN;
                }
            }
        }

        if (fe && fe->body) {
            int needed = fe->param_count;
            if (fe->body->local_count > needed) needed = fe->body->local_count;
            if (base_reg + needed + 512 >= vm_stack_size) { /* VM stack is fixed-size; never realloc it */
                pending_exception = value_string_copy("Maximum recursion depth exceeded"); has_exception = 1; goto l_OP_THROW;
            }
            call_stack[my_frame_idx].fe = fe;
            call_stack[my_frame_idx].pc = fe->body->code;
            int arg_base = base_reg + pc->r2;
            if (arg_base + ac >= vm_stack_size) {
                 goto l_OP_CALL;
            }
            if (pc->is_method) {
                vm_stack[base_reg] = vm_stack[arg_base];
                for (int i=0; i<ac && i<fe->param_count; i++) vm_stack[base_reg + i + 1] = vm_stack[arg_base + i + 1];
                for (int i=ac; i<fe->param_count; i++) vm_stack[base_reg + i + 1] = value_none();
            } else if (is_bound_method && GET_TYPE(bound_instance) != VAL_NONE) {
                vm_stack[base_reg] = bound_instance;
                for (int i=0; i<ac && i<fe->param_count; i++) vm_stack[base_reg + i + 1] = vm_stack[arg_base + i];
                for (int i=ac; i<fe->param_count; i++) vm_stack[base_reg + i + 1] = value_none();
            } else {
                for (int i=0; i<ac && i<fe->param_count; i++) vm_stack[base_reg + i] = vm_stack[arg_base + i];
                for (int i=ac; i<fe->param_count; i++) vm_stack[base_reg + i] = value_none();
            }
            /* A closure keeps its captured variables only when it calls itself;
               any other callee runs without them. */
            if (current_closure && current_closure->body != fe->body) current_closure = NULL;
            bc = fe->body; pc = bc->code;

            if (stack_arena_used > old_arena_used) gc_free_stack_arena_range(old_arena_used, stack_arena_used);
            stack_arena_used = old_arena_used;
            if (bc->jit_code && !(current_closure && current_closure->captured_count > 0) && !glow_nojitexec) {
                typedef Value (*JITFunc)(Value *R, int *pc_idx);
                int deopt_pc = 0;
                Value res = ((JITFunc)bc->jit_code)(&vm_stack[base_reg], &deopt_pc);
                if (__builtin_expect(IS_DEOPT(res), 0)) { pc = &bc->code[deopt_pc]; goto l_DISPATCH; }
                vm_ret_val = res; goto l_DO_RETURN;
            }
            goto l_DISPATCH;
        }
        goto l_OP_CALL;
    }

    l_OP_RETURN: {
        vm_ret_val = R(pc->r0);
    l_DO_RETURN:;
        Value ret_val = vm_ret_val;
        if (internal_depth == 0) {
            R(0) = ret_val;
            goto l_FINISH;
        } else {
            call_stack_pop();
            if (stack_arena_used > old_arena_used) gc_free_stack_arena_range(old_arena_used, stack_arena_used);
            stack_arena_used = old_arena_used;
            vm_stack_top = old_top;

            internal_depth--;
            frame_pool_used = frame_base + internal_depth;
            tail_depth = 0;
            bc = frames[internal_depth].bc;
            pc = frames[internal_depth].pc;
            current_closure = frames[internal_depth].closure;
            base_reg = frames[internal_depth].base_reg;
            int ret_reg = frames[internal_depth].ret_reg;
            old_arena_used = frames[internal_depth].old_arena_used;
            my_frame_idx = frames[internal_depth].my_frame_idx;
            old_top = frames[internal_depth].old_top;

            if (ret_reg >= 0) vm_stack[base_reg + ret_reg] = ret_val;
            pc++; goto l_DISPATCH;
        }
    }

    l_OP_POP: pc++; goto l_DISPATCH;
    l_OP_SHOW: { int n = pc->arg; int base = pc->r0; CLAMP_REG_COUNT(base, n); for (int i=0; i<n; i++) { print_value(R(base+i)); if(i<n-1) printf(" "); } printf("\n"); fflush(stdout); pc++; goto l_DISPATCH; }
    l_OP_STORE_LET: {
        int slot = pc->arg;
        if (slot < 0 || slot >= global_table.count) { pending_exception = value_string_copy("STORE_LET slot out of bounds"); has_exception = 1; goto l_OP_THROW; }
        global_table.values[slot] = R(pc->r0);
        pc++; goto l_DISPATCH;
    }
    l_OP_TRY_START: pc++; goto l_DISPATCH;
    l_OP_TRY_END: pc++; goto l_DISPATCH;

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
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
        int is_fuzz_timeout = __builtin_expect(in_fuzz_timeout, 0);
#else
        int is_fuzz_timeout = 0;
#endif
        if (!is_fuzz_timeout) {
            for (int i = 0; i < bc->exception_count; i++) {
                if (current_pc_idx >= bc->exception_table[i].start_pc &&
                    current_pc_idx < bc->exception_table[i].end_pc) {
                    R(bc->exception_table[i].exc_reg) = exc;
                    has_exception = 0;
                    pending_exception = 0;
                    if (exception_stack_trace) { free(exception_stack_trace); exception_stack_trace = NULL; }
                    pc = bc->code + bc->exception_table[i].handler_pc;
                    DISPATCH();
                }
            }
        }

        // If we reach here, no exception handler was found in the current frame.
        if (internal_depth > 0) {
            call_stack_pop();
            if (stack_arena_used > old_arena_used) gc_free_stack_arena_range(old_arena_used, stack_arena_used);
            stack_arena_used = old_arena_used;
            vm_stack_top = old_top;

            internal_depth--;
            frame_pool_used = frame_base + internal_depth;
            tail_depth = 0;
            bc = frames[internal_depth].bc;
            pc = frames[internal_depth].pc;
            current_closure = frames[internal_depth].closure;
            base_reg = frames[internal_depth].base_reg;
            old_arena_used = frames[internal_depth].old_arena_used;
            my_frame_idx = frames[internal_depth].my_frame_idx;
            old_top = frames[internal_depth].old_top;
            goto l_OP_THROW; // Re-evaluate in caller
        }

        goto l_FINISH;
    }

    l_OP_CLASS_DEF: {
        const char *n = (const char*)pc->ptr; Class *p = NULL;
        if (pc->arg == 1) { Value pv = R(pc->r1); if (GET_TYPE(pv) == VAL_OBJECT && (pv & MASK_PAYLOAD)) p = AS_CLASS(pv); }
        Value c = value_class(class_new(n, p)); var_set(n, c); vm_last_class = AS_CLASS(c); pc++; goto l_DISPATCH;
    }
    l_OP_METHOD_DEF: {
        FuncEntry *fe = func_get((const char*)pc->ptr);
        if (fe) {
            const char *mname = fe->name;
            const char *colon = strstr(mname, "::");
            if (colon) mname = colon + 2;
            if(vm_last_class) dict_set(vm_last_class->methods, mname, bound_method_new(value_none(), fe));
            else if(vm_last_trait) dict_set(vm_last_trait->methods, mname, bound_method_new(value_none(), fe));
        }
        pc++; goto l_DISPATCH;
    }
    l_OP_GET_PROPERTY: {
        const char *n = (const char*)pc->ptr; Value o = R(pc->r1);
        if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(o);
            if(ot==OBJ_INSTANCE) {
                Instance *inst = AS_INSTANCE(o);
                Dict *d = inst->properties;
                intptr_t idx = pc->cache[0].val - 1;
                Value v;
                if (idx >= 0 && idx < d->capacity && GET_TYPE(d->keys[idx]) == VAL_STRING && pc->cache[0].key == (void*)(uintptr_t)(d->keys[idx] & MASK_PAYLOAD)) {
                    v = d->values[idx];
                } else {
                    int new_idx = dict_get_idx(d, n);
                    if (new_idx != -1) {
                        pc->cache[0].val = new_idx + 1;
                        pc->cache[0].key = (void*)(uintptr_t)(d->keys[new_idx] & MASK_PAYLOAD);
                        v = d->values[new_idx];
                    } else {
                        v = class_get_method(inst->klass, n);
                    }
                }
                if (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD) && GET_OBJ_TYPE(v) == OBJ_BOUND_METHOD && GET_TYPE(AS_BOUND_METHOD(v)->instance) == VAL_NONE) {
                    v = bound_method_new(o, AS_BOUND_METHOD(v)->method);
                }
                R(pc->r0) = v;
            } else R(pc->r0) = value_none();
        } else if(GET_TYPE(o)==VAL_LIST) {
            NativeFuncPtr bfn = glow_builtin_method(o, n);
            R(pc->r0) = bfn ? bound_native_method_new(o, bfn) : value_none();
        } else if(GET_TYPE(o)==VAL_DICT) {
            Value v = dict_get(AS_DICT(o), n);
            NativeFuncPtr bfn;
            if (GET_TYPE(v) == VAL_NONE && (bfn = glow_builtin_method(o, n)) != NULL) {
                v = bound_native_method_new(o, bfn);
            } else if (GET_TYPE(v) == VAL_NONE) {
                if (strcmp(n, "has") == 0) {
                    v = bound_native_method_new(o, native_dict_has);
                } else if (strcmp(n, "keys") == 0) {
                    v = bound_native_method_new(o, native_dict_keys);
                } else if (strcmp(n, "values") == 0) {
                    v = bound_native_method_new(o, native_dict_values);
                } else if (strcmp(n, "remove") == 0) {
                    v = bound_native_method_new(o, native_dict_remove);
                } else if (strcmp(n, "merge") == 0) {
                    v = bound_native_method_new(o, native_dict_merge);
                }
            }
            R(pc->r0) = v;
        } else if(GET_TYPE(o)==VAL_STRING) {
            Value v = value_none();
            NativeFuncPtr bfn = glow_builtin_method(o, n);
            if (bfn) {
                v = bound_native_method_new(o, bfn);
            } else if (strcmp(n, "contains") == 0) {
                v = bound_native_method_new(o, native_str_contains);
            } else if (strcmp(n, "find") == 0) {
                v = bound_native_method_new(o, native_str_find);
            } else if (strcmp(n, "slice") == 0) {
                v = bound_native_method_new(o, native_str_slice);
            } else if (strcmp(n, "split") == 0) {
                v = bound_native_method_new(o, native_str_split);
            }
            R(pc->r0) = v;
        }
        else R(pc->r0) = value_none(); pc++; goto l_DISPATCH;
    }
    l_OP_SET_PROPERTY: {
        const char *n = (const char*)pc->ptr; Value o = R(pc->r0); Value v = R(pc->r1);
        if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD) && GET_OBJ_TYPE(o)==OBJ_INSTANCE) {
            Instance *inst = AS_INSTANCE(o);
            Dict *d = inst->properties;
            intptr_t idx = pc->cache[0].val - 1;
            if (idx >= 0 && idx < d->capacity && GET_TYPE(d->keys[idx]) == VAL_STRING && pc->cache[0].key == (void*)(uintptr_t)(d->keys[idx] & MASK_PAYLOAD)) {
                d->values[idx] = v;
                gc_write_barrier((GCObject*)inst, v);
            } else {
                int new_idx = dict_get_idx(d, n);
                if (new_idx != -1) {
                    pc->cache[0].val = new_idx + 1;
                    pc->cache[0].key = (void*)(uintptr_t)(d->keys[new_idx] & MASK_PAYLOAD);
                    d->values[new_idx] = v;
                    gc_write_barrier((GCObject*)inst, v);
                } else {
                    instance_set(inst, n, v);
                }
            }
        }
        else if(GET_TYPE(o)==VAL_DICT) dict_set(AS_DICT(o), n, v);
        pc++; goto l_DISPATCH;
    }
    l_OP_CALL_VALUE: {
        int ac = pc->arg; Value callee = R(pc->r1);
        call_stack[my_frame_idx].pc = pc; // Update current frame PC
        if(GET_TYPE(callee)==VAL_OBJECT && (callee & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(callee);
            if(ot==OBJ_CLASS) {
                Value inst = value_instance(instance_new(AS_CLASS(callee)));
                R(pc->r0) = inst;
                Value init_method = class_get_method(AS_CLASS(callee), "init");
                if (GET_TYPE(init_method) == VAL_OBJECT && (init_method & MASK_PAYLOAD)) {
                    Value temp[256];
                    int count = (ac > 255) ? 255 : ac;
                    for (int i=0; i<count; i++) temp[i] = R(pc->r2 + i);

                    int callee_base = base_reg + pc->r2;
                    if (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 + 512 >= vm_stack_size) { /* VM stack is fixed-size; never realloc it */
                        pending_exception = value_string_copy("Maximum recursion depth exceeded"); has_exception = 1; goto l_OP_THROW;
                    }
                    FuncEntry *ife = AS_BOUND_METHOD(init_method)->method;
                    vm_stack[callee_base] = inst; // self at R0
                    for (int i=0; i<count && (!ife || i<ife->param_count); i++) vm_stack[callee_base + i + 1] = temp[i];
                    if (ife) {
                        for (int i=count; i<ife->param_count; i++) vm_stack[callee_base + i + 1] = value_none();
                    }

                    Value old_self = var_get("self");
                    var_set("self", inst);
                    if (ife && ife->body) {
                        DO_CALL(ife->body, NULL, callee_base, -1);
                    }
                    var_set("self", old_self);
                }
            }
            else if(ot==OBJ_BOUND_METHOD) {
                BoundMethod *bm = AS_BOUND_METHOD(callee);
                if (bm->native_method) {
                    Value args[256];
                    args[0] = bm->instance;
                    int count = ac;
                    if (count > 254) count = 254;
                    int start_offset = pc->is_method ? 1 : 0;
                    for (int i = 0; i < count; i++) {
                        args[i + 1] = R(pc->r2 + start_offset + i);
                    }
                    R(pc->r0) = bm->native_method(count + 1, args);
                } else if(bm->method && bm->method->body) {
                    // Copy incoming args before we overwrite the stack: when the
                    // bound method carries a self, it goes in R0 and the args must
                    // shift up one slot (R1, R2, ...). Skipping this shift makes
                    // `let f = obj.method; f(a, b)` drop the first arg.
                    int has_self = (GET_TYPE(bm->instance) != VAL_NONE);
                    int count = ac; if (count > 254) count = 254;
                    int start_offset = pc->is_method ? 1 : 0;
                    Value temp[256];
                    for (int i = 0; i < count; i++) temp[i] = R(pc->r2 + start_offset + i);

                    int callee_base = base_reg + pc->r2;
                    if (callee_base + bm->method->body->local_count + 16 + 512 >= vm_stack_size) { /* VM stack is fixed-size; never realloc it */
                        pending_exception = value_string_copy("Maximum recursion depth exceeded"); has_exception = 1; goto l_OP_THROW;
                    }
                    int slot = callee_base;
                    if (has_self) { vm_stack[slot] = bm->instance; slot++; } // self at R0
                    for (int i = 0; i < count; i++) vm_stack[slot + i] = temp[i];
                    CLEAR_MISSING_ARGS(callee_base, slot - callee_base + count, bm->method->body->preserved_locals);
                    DO_CALL(bm->method->body, NULL, callee_base, pc->r0);
                } else R(pc->r0) = value_none();
            } else if(ot==OBJ_CLOSURE) {
                Closure *c = AS_CLOSURE(callee);
                if(c->body) {
                    CLEAR_MISSING_ARGS(base_reg + pc->r2, ac + (pc->is_method ? 1 : 0), c->body->preserved_locals);
                    DO_CALL(c->body, c, base_reg + pc->r2, pc->r0);
                } else R(pc->r0) = value_none();
            } else if (ot == OBJ_NATIVE_FUNC) {
                Value *args = &R(pc->r2);
                int argc = ac;
                if (pc->is_method) {
                    args++;
                }
                R(pc->r0) = glow_call_native(AS_NATIVE_FUNC(callee), argc, args);
            } else {
                pending_exception = value_string_copy("Object is not callable");
                has_exception = 1;
                goto l_OP_THROW;
            }
        } else {
            // The callee is not a callable value at all (e.g. none from an
            // undefined name, or a number/string). This previously fell through
            // silently, so `undefined_func()` was a no-op; throw to match the
            // OP_CALL "not found" and object-not-callable paths.
            pending_exception = value_string_copy("Attempted to call a non-callable value");
            has_exception = 1;
            goto l_OP_THROW;
        }
        pc++; goto l_DISPATCH;
    }
    l_OP_CALL_METHOD: {
        int ac = pc->arg;
        Value obj = R(pc->r1);
        Value dict_member = value_none();

        FuncEntry *fe = NULL;
        NativeFuncPtr native_fe = NULL;
        int is_native_dict_str_method = 0;
        Value fallback_callee = value_none();

        const char *mname = (const char*)pc->ptr;
        if (mname && strcmp(mname, "type") == 0) {
            native_fe = native_val_type;
            is_native_dict_str_method = 1;
        } else if ((native_fe = glow_builtin_method(obj, mname)) != NULL) {
            is_native_dict_str_method = 1;
        } else if(GET_TYPE(obj)==VAL_OBJECT && (obj & MASK_PAYLOAD)) {
            ObjType ot = GET_OBJ_TYPE(obj);
            if(ot==OBJ_INSTANCE) {
                Instance *inst = AS_INSTANCE(obj);
                if (pc->cache[0].key == inst->klass) {
                    fe = (FuncEntry*)pc->cache[0].val;
                } else {
                    Value m = class_get_method(inst->klass, mname);
                    if (GET_TYPE(m) == VAL_OBJECT && GET_OBJ_TYPE(m) == OBJ_BOUND_METHOD) {
                        fe = AS_BOUND_METHOD(m)->method;
                        pc->cache[0].key = inst->klass;
                        pc->cache[0].val = (intptr_t)fe;
                    }
                }
                if (!fe) {
                    // No method by this name: fall back to a callable instance field
                    // (e.g. a handler stored on the object and invoked as obj.handler(...)).
                    Value field = dict_get(inst->properties, mname);
                    if (GET_TYPE(field) != VAL_NONE) fallback_callee = field;
                }
            }
        } else if(GET_TYPE(obj)==VAL_LIST) {
            if (strcmp(mname, "append") == 0 || strcmp(mname, "push") == 0) { native_fe = native_list_append; is_native_dict_str_method = 1; }
            else if (strcmp(mname, "pop") == 0) { native_fe = native_list_pop; is_native_dict_str_method = 1; }
            else if (strcmp(mname, "len") == 0) { native_fe = native_list_len; is_native_dict_str_method = 1; }
        } else if(GET_TYPE(obj)==VAL_DICT) {
            if (dict_member_is_callable(AS_DICT(obj), mname, &dict_member)) fallback_callee = dict_member;
            else if (strcmp(mname, "has") == 0) { native_fe = native_dict_has; is_native_dict_str_method = 1; }
            else if (strcmp(mname, "keys") == 0) { native_fe = native_dict_keys; is_native_dict_str_method = 1; }
            else if (strcmp(mname, "values") == 0) { native_fe = native_dict_values; is_native_dict_str_method = 1; }
            else if (strcmp(mname, "remove") == 0) { native_fe = native_dict_remove; is_native_dict_str_method = 1; }
            else if (strcmp(mname, "merge") == 0) { native_fe = native_dict_merge; is_native_dict_str_method = 1; }
            else if (strcmp(mname, "len") == 0) { native_fe = native_len; is_native_dict_str_method = 1; }
            else {
                fallback_callee = dict_get(AS_DICT(obj), mname);
            }
        } else if(GET_TYPE(obj)==VAL_STRING) {
            if (strcmp(mname, "contains") == 0) { native_fe = native_str_contains; is_native_dict_str_method = 1; }
            else if (strcmp(mname, "find") == 0) { native_fe = native_str_find; is_native_dict_str_method = 1; }
            else if (strcmp(mname, "slice") == 0) { native_fe = native_str_slice; is_native_dict_str_method = 1; }
            else if (strcmp(mname, "split") == 0) { native_fe = native_str_split; is_native_dict_str_method = 1; }
            else if (strcmp(mname, "len") == 0) { native_fe = native_len; is_native_dict_str_method = 1; }
            else {
                fallback_callee = value_none();
            }
        }

        if (fe && fe->body) {
            int callee_base = base_reg + pc->r2;
            if (callee_base + fe->body->local_count + 16 + 512 >= vm_stack_size) { /* VM stack is fixed-size; never realloc it */
                pending_exception = value_string_copy("Maximum recursion depth exceeded"); has_exception = 1; goto l_OP_THROW;
            }
            vm_stack[callee_base] = obj; // SET SELF
            CLEAR_MISSING_ARGS(callee_base, 1 + ac, fe->body->preserved_locals);

            DO_CALL(fe->body, NULL, callee_base, pc->r0);
        } else if (native_fe && is_native_dict_str_method) {
            Value args[257];
            args[0] = obj;
            int count = ac;
            if (count > 254) count = 254;
            int start_offset = pc->is_method ? 1 : 0;
            for (int i = 0; i < count; i++) {
                args[i + 1] = R(pc->r2 + start_offset + i);
            }
            R(pc->r0) = native_fe(count + 1, args);
        } else if (GET_TYPE(fallback_callee) != VAL_NONE) {
            // Fallback: it's not a devirtualized method, just call it normally as a value
            if (GET_TYPE(fallback_callee) == VAL_OBJECT && (fallback_callee & MASK_PAYLOAD)) {
                ObjType ot = GET_OBJ_TYPE(fallback_callee);
                if (ot == OBJ_NATIVE_FUNC) {
                    NativeFunc *nf = (NativeFunc*)(fallback_callee & MASK_PAYLOAD);
                    Value *args = &R(pc->r2);
                    if (pc->is_method) args++;
                    R(pc->r0) = glow_call_native(nf, ac, args);
                } else if (ot == OBJ_CLOSURE) {
                    Closure *c = AS_CLOSURE(fallback_callee);
                    if (c->body) {
                        CLEAR_MISSING_ARGS(base_reg + pc->r2 + (pc->is_method ? 1 : 0), ac, c->body->preserved_locals);
                        // A callable field/value is a plain function, so its arguments
                        // begin after the receiver slot on a method-style call.
                        DO_CALL(c->body, c, base_reg + pc->r2 + (pc->is_method ? 1 : 0), pc->r0);
                    } else R(pc->r0) = value_none();
                } else if (ot == OBJ_BOUND_METHOD) {
                    BoundMethod *bm = AS_BOUND_METHOD(fallback_callee);
                    if (bm->native_method) {
                        Value args[257];
                        args[0] = bm->instance;
                        int count = ac;
                        if (count > 254) count = 254;
                        int start_offset = pc->is_method ? 1 : 0;
                        for (int i = 0; i < count; i++) args[i + 1] = R(pc->r2 + start_offset + i);
                        R(pc->r0) = bm->native_method(count + 1, args);
                    } else if (bm->method && bm->method->body) {
                        // A real bound method reuses the receiver slot for self; a bare
                        // function reference (no bound instance) is a plain function, so
                        // skip that slot and start its args after self on a method call.
                        int has_self = (GET_TYPE(bm->instance) != VAL_NONE);
                        int callee_base = base_reg + pc->r2 + (has_self ? 0 : (pc->is_method ? 1 : 0));
                        if (callee_base + bm->method->body->local_count + 16 + 512 >= vm_stack_size) { /* VM stack is fixed-size; never realloc it */
                            pending_exception = value_string_copy("Maximum recursion depth exceeded"); has_exception = 1; goto l_OP_THROW;
                        }
                        if (has_self) vm_stack[callee_base] = bm->instance;
                        /* Missing arguments are none (they used to keep values left in
                           those registers, so a default parameter saw garbage). */
                        CLEAR_MISSING_ARGS(callee_base, (has_self ? 1 : 0) + ac, bm->method->body->preserved_locals);
                        DO_CALL(bm->method->body, NULL, callee_base, pc->r0);
                    } else R(pc->r0) = value_none();
                } else if (ot == OBJ_CLASS) {
                    Value inst = value_instance(instance_new(AS_CLASS(fallback_callee)));
                    R(pc->r0) = inst;
                    Value init_method = class_get_method(AS_CLASS(fallback_callee), "init");
                    if (GET_TYPE(init_method) == VAL_OBJECT && (init_method & MASK_PAYLOAD)) {
                        Value temp[256];
                        int count = (ac > 255) ? 255 : ac;
                        int start_offset = pc->is_method ? 1 : 0;
                        for (int i = 0; i < count; i++) temp[i] = R(pc->r2 + start_offset + i);

                        int callee_base = base_reg + pc->r2 + start_offset;
                        if (callee_base + AS_BOUND_METHOD(init_method)->method->body->local_count + 16 + 512 >= vm_stack_size) { /* VM stack is fixed-size; never realloc it */
                            pending_exception = value_string_copy("Maximum recursion depth exceeded"); has_exception = 1; goto l_OP_THROW;
                        }
                        FuncEntry *ife = AS_BOUND_METHOD(init_method)->method;
                        vm_stack[callee_base] = inst; // self at R0
                        for (int i=0; i<count && (!ife || i<ife->param_count); i++) vm_stack[callee_base + i + 1] = temp[i];
                        if (ife) {
                            for (int i=count; i<ife->param_count; i++) vm_stack[callee_base + i + 1] = value_none();
                        }

                        Value old_self = var_get("self");
                        var_set("self", inst);
                        if (ife && ife->body) {
                            DO_CALL(ife->body, NULL, callee_base, -1);
                        }
                        var_set("self", old_self);
                    }
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
        } else {
            pending_exception = value_string_copy("Attempted to call a non-method value");
            has_exception = 1;
            goto l_OP_THROW;
        }
        pc++; goto l_DISPATCH;
    }
    l_OP_LIST_LITERAL: {
        int n = pc->arg; CLAMP_REG_COUNT(pc->r1, n);
        List *l = pc->is_stack_alloc ? gc_alloc_stack(sizeof(List), OBJ_LIST) : gc_alloc(sizeof(List), OBJ_LIST);
        l->size = 0; l->capacity = n; l->items = n > 0 ? calloc(n, sizeof(Value)) : NULL;
        for (int i=0; i<n; i++) list_push(l, R(pc->r1 + i));
        R(pc->r0) = value_list(l); pc++; goto l_DISPATCH;
    }
    l_OP_INDEX: {
        Value coll = R(pc->r1), idx_arg = R(pc->r2);
        if (GET_TYPE(coll) == VAL_LIST) {
            int64_t i;
            if (!index_value(idx_arg, &i)) { index_error("List index must be a whole number", 0, 0); goto l_OP_THROW; }
            int64_t size = AS_LIST(coll)->size;
            if (i < 0) i += size;
            if (i < 0 || i >= size) { index_error("List index out of range", i < 0 ? i - size : i, size); goto l_OP_THROW; }
            R(pc->r0) = AS_LIST(coll)->items[i];
        } else if (GET_TYPE(coll) == VAL_OBJECT) {
            if ((coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_PACKED_ARRAY) {
                PackedArray *pa = AS_PACKED_ARRAY(coll);
                int i = (int)AS_INT(idx_arg); if (i < 0) i += pa->size;
                if ((size_t)i < (size_t)pa->size) R(pc->r0) = packed_array_get(pa, i);
                else R(pc->r0) = value_none();
            } else R(pc->r0) = value_none();
        } else if (GET_TYPE(coll) == VAL_DICT) {
            Value v = dict_get(AS_DICT(coll), AS_STRING(idx_arg));
            if (GET_TYPE(v) == VAL_NONE && GET_TYPE(idx_arg) == VAL_STRING) {
                const char *mname = AS_STRING(idx_arg);
                if (strcmp(mname, "has") == 0) {
                    v = bound_native_method_new(coll, native_dict_has);
                } else if (strcmp(mname, "keys") == 0) {
                    v = bound_native_method_new(coll, native_dict_keys);
                } else if (strcmp(mname, "values") == 0) {
                    v = bound_native_method_new(coll, native_dict_values);
                } else if (strcmp(mname, "remove") == 0) {
                    v = bound_native_method_new(coll, native_dict_remove);
                } else if (strcmp(mname, "merge") == 0) {
                    v = bound_native_method_new(coll, native_dict_merge);
                }
            } else if (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD)) {
                ObjType ot = GET_OBJ_TYPE(v);
                if (ot == OBJ_NATIVE_FUNC || ot == OBJ_BOUND_METHOD) {
                    // For dicts, we don't bind 'self' unless it's explicitly done.
                    // But we must return the callable.
                }
            }
            R(pc->r0) = v;
        } else if (GET_TYPE(coll) == VAL_STRING) {
            Value v = value_none();
            if (GET_TYPE(idx_arg) == VAL_STRING) {
                const char *mname = AS_STRING(idx_arg);
                if (strcmp(mname, "contains") == 0) {
                    v = bound_native_method_new(coll, native_str_contains);
                } else if (strcmp(mname, "find") == 0) {
                    v = bound_native_method_new(coll, native_str_find);
                } else if (strcmp(mname, "slice") == 0) {
                    v = bound_native_method_new(coll, native_str_slice);
                } else if (strcmp(mname, "split") == 0) {
                    v = bound_native_method_new(coll, native_str_split);
                }
            } else {
                int64_t i;
                if (!index_value(idx_arg, &i)) { index_error("String index must be a whole number", 0, 0); goto l_OP_THROW; }
                int64_t len = (int64_t)rope_length(coll);
                if (i < 0) i += len;
                if (i < 0 || i >= len) { index_error("String index out of range", i < 0 ? i - len : i, len); goto l_OP_THROW; }
                char char_str[2] = { AS_STRING(coll)[i], '\0' };
                v = value_string_copy(char_str);
            }
            R(pc->r0) = v;
        } else R(pc->r0) = value_none();
        pc++; goto l_DISPATCH;
    }
    l_OP_STORE_INDEX: {
        Value coll = R(pc->r0), idx = R(pc->r1), val = R(pc->r2);
        if (GET_TYPE(coll) == VAL_LIST) {
            int64_t i;
            if (!index_value(idx, &i)) { index_error("List index must be a whole number", 0, 0); goto l_OP_THROW; }
            int64_t size = AS_LIST(coll)->size;
            if (i < 0) i += size;
            if (i < 0 || i >= size) { index_error("List index out of range", i < 0 ? i - size : i, size); goto l_OP_THROW; }
            gc_write_barrier((GCObject*)AS_LIST(coll), val);
            AS_LIST(coll)->items[i] = val;
        } else if (GET_TYPE(coll) == VAL_OBJECT) {
            if ((coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_PACKED_ARRAY) {
                PackedArray *pa = AS_PACKED_ARRAY(coll);
                int i = (int)AS_INT(idx); if (i < 0) i += pa->size;
                if ((size_t)i < (size_t)pa->size) {
                    packed_array_set(pa, i, val);
                }
            }
        } else if (GET_TYPE(coll) == VAL_DICT) {
            dict_set(AS_DICT(coll), AS_STRING(idx), val);
        }
        pc++; goto l_DISPATCH;
    }
    l_OP_SPAWN: {
        threads_ever_spawned = 1;
        int ac = pc->arg; Value callee = R(pc->r0);
        if (ac < 0 || ac > 1024) {
            pending_exception = value_string_copy("Runtime Error: Invalid number of arguments for OP_SPAWN");
            has_exception = 1;
            goto l_OP_THROW;
        }
        if (pending_spawn_count >= MAX_PENDING_SPAWNS || active_stack_count >= 120) {
            pending_exception = value_string_copy("Runtime Error: too many threads");
            has_exception = 1;
            goto l_OP_THROW;
        }
        ThreadData *td = malloc(sizeof(ThreadData)); td->callee = callee; td->argc = ac; td->name = (const char*)pc->ptr;
        td->daemon = pc->r2 == 1;
        CLAMP_REG_COUNT(pc->r1, ac);
        td->args = malloc(sizeof(Value)*(ac > 0 ? ac : 1)); for(int i=0; i<ac; i++) td->args[i] = R(pc->r1 + i);
        pending_spawns[pending_spawn_count++] = td;
        int counted = !td->daemon;
        pthread_mutex_lock(&threads_done_mutex);
        threads_alive += counted;
        pthread_mutex_unlock(&threads_done_mutex);
        pthread_t t;
        if (pthread_create(&t, NULL, thread_wrapper, td) != 0) {
            pending_spawn_remove(td);
            free(td->args); free(td);
            pthread_mutex_lock(&threads_done_mutex);
            threads_alive -= counted;
            pthread_mutex_unlock(&threads_done_mutex);
            pending_exception = value_string_copy("Runtime Error: could not start thread");
            has_exception = 1;
            goto l_OP_THROW;
        }
        pthread_detach(t); pc++; goto l_DISPATCH;
    }
    l_OP_LEN: { Value v = R(pc->r1); if(GET_TYPE(v)==VAL_STRING) R(pc->r0) = value_int((int64_t)rope_length(v)); else if(GET_TYPE(v)==VAL_LIST) R(pc->r0) = value_int(AS_LIST(v)->size); else R(pc->r0) = value_int(0); pc++; goto l_DISPATCH; }
    l_OP_RANGE: { int n = (int)AS_INT(R(pc->r1)); if (n < 0 || n > 10000000) n = 0; List *l = list_new(n); for(int i=0; i<n; i++) list_push(l, value_int(i)); R(pc->r0) = value_list(l); pc++; goto l_DISPATCH; }
    l_OP_APPEND: { Value v = R(pc->r1), l = R(pc->r0); if(GET_TYPE(l)==VAL_LIST) list_push(AS_LIST(l), v); pc++; goto l_DISPATCH; }
    l_OP_TYPE: {
        Value v = R(pc->r1);
        R(pc->r0) = native_type(1, &v);
        pc++; goto l_DISPATCH;
    }
    l_OP_MOD: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (!is_numeric(a) || !is_numeric(b)) { arith_type_error("%", a, b); goto l_OP_THROW; }
        if (val_to_double(b) == 0.0) { pending_exception = value_string_copy("Modulo by zero"); has_exception = 1; goto l_OP_THROW; }
        R(pc->r0) = num_mod(a, b); pc++; goto l_DISPATCH;
    }
    l_OP_POW: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (!is_numeric(a) || !is_numeric(b)) { arith_type_error("**", a, b); goto l_OP_THROW; }
        R(pc->r0) = num_pow(a, b); pc++; goto l_DISPATCH;
    }
    l_OP_NEG: { Value v = R(pc->r1);
        if (!is_numeric(v)) {
            Value t = native_type(1, &v); char msg[96];
            snprintf(msg, sizeof(msg), "Cannot negate %s", AS_STRING(t));
            pending_exception = value_string_copy(msg); has_exception = 1; goto l_OP_THROW;
        }
        if (GET_TYPE(v) == VAL_INT) { int64_t x = AS_INT(v); if (x == INT48_MIN) R(pc->r0) = value_float(-(double)x); else R(pc->r0) = value_int(-x); } else if (is_bigint(v)) R(pc->r0) = value_bigint(bigint_negate(AS_BIGINT(v)));  /* exact, not through a double */ else R(pc->r0) = num_from_double(-val_to_double(v)); pc++; goto l_DISPATCH; }
    l_OP_STOP: {
        if (pc->arg > 0) {
            if (pc->arg < 0 || pc->arg >= bc->count) { pending_exception = value_string_copy("STOP out of bounds"); has_exception = 1; goto l_OP_THROW; }
            pc = bc->code + pc->arg;
        } else {
            goto l_FINISH;
        }
        DISPATCH();
    }
    l_OP_SKIP: {
        if (pc->arg > 0) {
            if (pc->arg < 0 || pc->arg >= bc->count) { pending_exception = value_string_copy("SKIP out of bounds"); has_exception = 1; goto l_OP_THROW; }
            pc = bc->code + pc->arg;
        } else {
            pc++;
        }
        DISPATCH();
    }
    l_OP_TRAIT_DEF: { const char *n = (const char*)pc->ptr; Value t = value_trait(trait_new(n)); var_set(n, t); vm_last_trait = AS_TRAIT(t); pc++; goto l_DISPATCH; }
    l_OP_USE_TRAIT: {
        Value tv = var_get((const char*)pc->ptr);
        if (GET_TYPE(tv) == VAL_OBJECT && (tv & MASK_PAYLOAD) && GET_OBJ_TYPE(tv) == OBJ_TRAIT && vm_last_class) {
            Trait *t = AS_TRAIT(tv);
            for (int i = 0; i < t->methods->capacity; i++) {
                if (t->methods->keys[i] != 0 && GET_TYPE(t->methods->keys[i]) != VAL_NONE) {
                    dict_set(vm_last_class->methods, (const char*)(uintptr_t)(t->methods->keys[i] & MASK_PAYLOAD), t->methods->values[i]);
                }
            }
        }
        pc++; goto l_DISPATCH;
    }
    l_OP_INPUT: pc++; goto l_DISPATCH;
    l_OP_WRITE: { int n = pc->arg; int base = pc->r0; CLAMP_REG_COUNT(base, n); for (int i=0; i<n; i++) { print_value(R(base+i)); if(i<n-1) printf(" "); } pc++; goto l_DISPATCH; }
    l_OP_DICT_LITERAL: {
        int n = pc->arg; if (n < 0) n = 0;
        /* Each dict entry reads 2 registers: key at r1+i*2, value at r1+i*2+1 */
        int dict_reg_span = n * 2; CLAMP_REG_COUNT(pc->r1, dict_reg_span); n = dict_reg_span / 2;
        Dict *d = pc->is_stack_alloc ? gc_alloc_stack(sizeof(Dict), OBJ_DICT) : gc_alloc(sizeof(Dict), OBJ_DICT);
        dict_init(d, n);
        for (int i=0; i<n; i++) {
            Value k = R(pc->r1 + i * 2);
            Value v = R(pc->r1 + i * 2 + 1);
            if (GET_TYPE(k) == VAL_STRING) {
                dict_set(d, AS_STRING(k), v);
            }
        }
        R(pc->r0) = value_dict(d); pc++; goto l_DISPATCH;
    }
    l_OP_SLICE: {
        Value obj = R(pc->r1);
        Value startV = R(pc->r2);
        Value endV = R(pc->r2 + 1);
        int slen = 0;
        if (GET_TYPE(obj) == VAL_STRING) {
            slen = strlen(AS_STRING(obj));
        } else if (GET_TYPE(obj) == VAL_LIST) {
            slen = AS_LIST(obj)->size;
        }
        int s = 0;
        if (GET_TYPE(startV) == VAL_INT) {
            int val = (int)AS_INT(startV);
            if (val == -999999) s = 0;
            else if (val < 0) s = val + slen;
            else s = val;
        }
        int e = slen;
        if (GET_TYPE(endV) == VAL_INT) {
            int val = (int)AS_INT(endV);
            if (val == -999999) e = slen;
            else if (val < 0) e = val + slen;
            else e = val;
        }
        if (s < 0) s = 0;
        if (e > slen) e = slen;
        if (s > e) s = e;
        if (GET_TYPE(obj) == VAL_STRING) {
            int rlen = e - s;
            char *sub = malloc(rlen + 1);
            strncpy(sub, AS_STRING(obj) + s, rlen);
            sub[rlen] = 0;
            R(pc->r0) = value_string_copy(sub);
            free(sub);
        } else if (GET_TYPE(obj) == VAL_LIST) {
            List *rl = list_new(e - s);
            for (int i = s; i < e; i++) {
                list_push(rl, AS_LIST(obj)->items[i]);
            }
            R(pc->r0) = value_list(rl);
        } else {
            R(pc->r0) = value_none();
        }
        pc++; goto l_DISPATCH;
    }
    l_OP_FOR_EACH: pc++; goto l_DISPATCH;
    l_OP_FOR_EACH_PAIR: pc++; goto l_DISPATCH;
    l_OP_PUSH_SCOPE: pc++; goto l_DISPATCH;
    l_OP_POP_SCOPE: pc++; goto l_DISPATCH;
    l_OP_LOAD_LOCAL: pc++; DISPATCH_CURRENT();
    l_OP_STORE_LOCAL: pc++; DISPATCH_CURRENT();
    l_OP_DICT_KEYS: pc++; goto l_DISPATCH;
    l_OP_DICT_VALUES: pc++; goto l_DISPATCH;
    l_OP_DICT_HAS: pc++; goto l_DISPATCH;
    l_OP_DICT_REMOVE: pc++; goto l_DISPATCH;
    l_OP_DICT_MERGE: pc++; goto l_DISPATCH;
    l_OP_MATH_ABS: pc++; goto l_DISPATCH;
    l_OP_MATH_MIN: pc++; goto l_DISPATCH;
    l_OP_MATH_MAX: pc++; goto l_DISPATCH;
    l_OP_MATH_ROUND: pc++; goto l_DISPATCH;
    l_OP_MATH_FLOOR: pc++; goto l_DISPATCH;
    l_OP_MATH_CEIL: pc++; goto l_DISPATCH;
    l_OP_MATH_SQRT: pc++; goto l_DISPATCH;
    l_OP_STR_UPPER: pc++; goto l_DISPATCH;
    l_OP_STR_LOWER: pc++; goto l_DISPATCH;
    l_OP_STR_TRIM: pc++; goto l_DISPATCH;
    l_OP_STR_FIND: pc++; goto l_DISPATCH;
    l_OP_STR_REPLACE: pc++; goto l_DISPATCH;
    l_OP_STR_STARTSWITH: pc++; goto l_DISPATCH;
    l_OP_STR_ENDSWITH: pc++; goto l_DISPATCH;
    l_OP_STR_SPLIT: pc++; goto l_DISPATCH;
    l_OP_LIST_PUSH: { Value v = R(pc->r1), l = R(pc->r0); if(GET_TYPE(l)==VAL_LIST) list_push(AS_LIST(l), v); pc++; goto l_DISPATCH; }
    l_OP_LIST_POP: { Value l = R(pc->r1); if(GET_TYPE(l)==VAL_LIST) R(pc->r0) = list_pop(AS_LIST(l)); else R(pc->r0) = value_none(); pc++; goto l_DISPATCH; }
    l_OP_LIST_INSERT: pc++; goto l_DISPATCH;
    l_OP_LIST_REMOVE: pc++; goto l_DISPATCH;
    l_OP_LIST_CONTAINS: pc++; goto l_DISPATCH;
    l_OP_LIST_SORT: pc++; goto l_DISPATCH;
    l_OP_LIST_REVERSE: pc++; goto l_DISPATCH;
    l_OP_FILE_READ: pc++; goto l_DISPATCH;
    l_OP_FILE_WRITE: pc++; goto l_DISPATCH;
    l_OP_FILE_APPEND: pc++; goto l_DISPATCH;
    l_OP_FSTRING: pc++; goto l_DISPATCH;
    l_OP_LIST_COMP: pc++; goto l_DISPATCH;
    l_OP_LAMBDA: {
        FuncEntry *fe = func_get((const char*)pc->ptr);
        if (!fe) { pending_exception = value_string_copy("LAMBDA function not found"); has_exception = 1; goto l_OP_THROW; }
        Closure *c = pc->is_stack_alloc ? gc_alloc_stack(sizeof(Closure), OBJ_CLOSURE) : gc_alloc(sizeof(Closure), OBJ_CLOSURE);
        c->param_count = fe->param_count; c->params = fe->params; c->body = fe->body;
        FuncEntry *parent_fe = (FuncEntry*)pc->cache[0].key;
        int capture_size = 0;
        if (parent_fe) capture_size += parent_fe->local_count;
        if (current_closure) capture_size += current_closure->captured_count;
        c->captured_count = capture_size;
        if (capture_size > 0) {
            c->captured_names = malloc(sizeof(const char*) * capture_size);
            c->captured_values = malloc(sizeof(Value) * capture_size);
            /* Lookups take the first match, so the enclosing function's own
               variables come before the ones it inherited. */
            int idx = 0;
            if (parent_fe) {
                for (int i=0; i<parent_fe->local_count; i++) {
                    c->captured_names[idx] = parent_fe->local_names[i];
                    c->captured_values[idx] = vm_stack[base_reg + (parent_fe->local_regs ? parent_fe->local_regs[i] : i)];
                    idx++;
                }
            }
            if (current_closure) {
                for (int i=0; i<current_closure->captured_count; i++) {
                    c->captured_names[idx] = current_closure->captured_names[i];
                    c->captured_values[idx] = current_closure->captured_values[i];
                    idx++;
                }
            }
            /* A named nested function can call itself recursively. */
            Value self_v = value_closure(c);
            for (int i = 0; i < idx; i++)
                if (fe->name && strcmp(c->captured_names[i], fe->name) == 0) c->captured_values[i] = self_v;
        } else {
            c->captured_names = NULL;
            c->captured_values = NULL;
        }
        R(pc->r0) = value_closure(c);
        pc++; goto l_DISPATCH;
    }

    l_OP_TRACE_ENTER: {
        Trace *t = (Trace*)pc->trace;
        if (!t || !t->jit_code) {
            pc->op = pc->original_op;
            DISPATCH_CURRENT();
        }
        typedef Value (*TraceFunc)(Value *R, int *pc_off);
        int off = 0;
        Value res = ((TraceFunc)t->jit_code)(&vm_stack[base_reg], &off);
        if (IS_TRACE_EXIT(res)) { pc = bc->code + off; DISPATCH_CURRENT(); }
        pc->op = pc->original_op; DISPATCH_CURRENT();
    }
    l_OP_USE_FILE: {
        /* Count nested imports, not call depth: a `use` inside a deeply
           recursive function is not a circular import. */
        static THREAD_LOCAL int import_depth = 0;
        if (import_depth >= 50) {
            pending_exception = value_string_copy("Maximum import depth exceeded (circular import?)");
            has_exception = 1; goto l_OP_THROW;
        }
        const char *path = (const char*)pc->ptr;
        if (!path) path = "";
        const char *resolved_path = path;
        FILE *lock = fopen("Glow.lock", "r");
        if (lock) {
            char line[512];
            while (fgets(line, sizeof(line), lock)) {
                char url[256], hash[256], local[256];
                if (sscanf(line, "\"%255[^\"]\" \"%255[^\"]\" \"%255[^\"]\"", url, hash, local) == 3) {
                    if (strcmp(url, path) == 0) {
                        resolved_path = intern_string(local);
                        break;
                    }
                }
            }
            fclose(lock);
        }
        if (resolved_path == path) {
            FILE *deps = fopen("Glow.deps", "r");
            if (deps) {
                char line[512];
                while (fgets(line, sizeof(line), deps)) {
                    char url[256], local[256];
                    if (sscanf(line, "\"%255[^\"]\" \"%255[^\"]\"", url, local) == 2) {
                        if (strcmp(url, path) == 0) {
                            resolved_path = intern_string(local);
                            break;
                        }
                    }
                }
                fclose(deps);
            }
        }
        FILE *f = glow_open_module(resolved_path, pc->file, &resolved_path);
        if (!f) {
            char err[512]; snprintf(err, sizeof(err), "Could not open file: %s", path);
            pending_exception = value_string_copy(err); has_exception = 1; goto l_OP_THROW;
        }
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz < 0 || sz > 64 * 1024 * 1024) {
            fclose(f);
            char err[512]; snprintf(err, sizeof(err), "File too large or invalid: %s", path);
            pending_exception = value_string_copy(err); has_exception = 1; goto l_OP_THROW;
        }
        char *buf = malloc(sz + 1);
        size_t nread = fread(buf, 1, sz, f); buf[nread] = '\0'; fclose(f);

        Node *old_root = root;
        const char *old_file = compiling_file;
        Bytecode *old_bc = compiling_bc;
        int old_reg = next_free_reg;
        FuncEntry *old_func = compiling_func;
        Class *old_class = last_compiling_class; Trait *old_trait = last_compiling_trait;

        compiling_file = resolved_path;
        int old_line = line_num; line_num = 1;
        YY_BUFFER_STATE old_bp = get_current_buffer();
        YY_BUFFER_STATE bp = yy_scan_string(buf);
        root = NULL; int parse_res = yyparse();
        yy_delete_buffer(bp);
        if (old_bp) yy_switch_to_buffer(old_bp);
        line_num = old_line;
        if (parse_res != 0) {
            root = old_root; compiling_file = old_file; compiling_bc = old_bc;
            next_free_reg = old_reg; compiling_func = old_func;
            last_compiling_class = old_class; last_compiling_trait = old_trait;
            char err[512]; snprintf(err, sizeof(err), "Failed to load module: %s (syntax error)", path);
            pending_exception = value_string_copy(err); has_exception = 1;
            free(buf); goto l_OP_THROW;
        }
        if (root) {
            FuncEntry *fe = arena_alloc(sizeof(FuncEntry)); memset(fe, 0, sizeof(FuncEntry));
            fe->name = "use_file";
            Bytecode *use_bc = bc_new(fe); compiling_bc = use_bc; next_free_reg = 0;
            last_compiling_class = NULL; last_compiling_trait = NULL;
            /* A module's top level is top-level code: its `let`s are globals
               its functions (and the importer) can see. */
            compiling_func = NULL;
            int old_peak = compiler_reg_peak(); compiler_set_reg_peak(0);
            compile(root); bc_emit(use_bc, OP_RETURN, 0, 0);
            use_bc->local_count = compiler_reg_peak();
            compiler_set_reg_peak(old_peak);
            import_depth++;
            run_bytecode(use_bc, NULL, NULL, vm_stack_top);
            import_depth--;
            bc_free(use_bc);
        }
        root = old_root; compiling_file = old_file; compiling_bc = old_bc;
        next_free_reg = old_reg; compiling_func = old_func;
        last_compiling_class = old_class; last_compiling_trait = old_trait;
        free(buf);
        if (__builtin_expect(has_exception, 0)) goto l_OP_THROW;
        pc++; goto l_DISPATCH;
    }
    l_OP_PACKED_ADD: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (GET_TYPE(a) == VAL_OBJECT && (a & MASK_PAYLOAD) && GET_OBJ_TYPE(a) == OBJ_PACKED_ARRAY &&
            GET_TYPE(b) == VAL_OBJECT && (b & MASK_PAYLOAD) && GET_OBJ_TYPE(b) == OBJ_PACKED_ARRAY) {
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
        pc++; goto l_DISPATCH;
    }
    l_OP_PACKED_SUB: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (GET_TYPE(a) == VAL_OBJECT && (a & MASK_PAYLOAD) && GET_OBJ_TYPE(a) == OBJ_PACKED_ARRAY &&
            GET_TYPE(b) == VAL_OBJECT && (b & MASK_PAYLOAD) && GET_OBJ_TYPE(b) == OBJ_PACKED_ARRAY) {
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
        pc++; goto l_DISPATCH;
    }
    l_OP_SIMD_ADD: {
        Value a = R(pc->r1), b = R(pc->r2), target = R(pc->r0);
        simd_add_helper(a, b, target);
        pc++; goto l_DISPATCH;
    }
    l_OP_PACKED_MUL:
    l_OP_PACKED_LOAD:
    l_OP_PACKED_STORE:
    l_OP_PACKED_FUSED_MUL_ADD:
    l_OP_MATRIX_MUL:
        pc++; goto l_DISPATCH;
    l_OP_ITER_START: {
        Value coll = R(pc->r1);
        if (GET_TYPE(coll) == VAL_OBJECT && (coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_SQLITE_ITER) {
            R(pc->r0) = coll;
        } else {
            Iterator *it = gc_alloc(sizeof(Iterator), OBJ_ITERATOR);
            it->iterable = coll;
            it->index = 0;
            R(pc->r0) = make_value(VAL_OBJECT, (uintptr_t)it);
        }
        pc++; goto l_DISPATCH;
    }
    l_OP_ITER_NEXT: {
        Value vit = R(pc->r1);
        if (GET_TYPE(vit) != VAL_OBJECT || !(vit & MASK_PAYLOAD)) {
            if (pc->arg < 0 || pc->arg >= bc->count) { pending_exception = value_string_copy("ITER_NEXT jump out of bounds"); has_exception = 1; goto l_OP_THROW; }
            pc = bc->code + pc->arg; DISPATCH();
        }
        GCObject *obj = (GCObject*)(uintptr_t)(vit & MASK_PAYLOAD);

        if (obj->type == OBJ_ITERATOR) {
            Iterator *it = (Iterator*)obj;
            Value coll = it->iterable;
            int type = GET_TYPE(coll);
            if (type == VAL_LIST) {
                 List *l = AS_LIST(coll);
                 if (it->index < l->size) {
                     R(pc->r0) = l->items[it->index++];
                     pc++; goto l_DISPATCH;
                 }
            } else if (type == VAL_DICT) {
                 Dict *d = AS_DICT(coll);
                 while (it->index < d->capacity && (d->keys[it->index] == 0 || GET_TYPE(d->keys[it->index]) == VAL_NONE)) it->index++;
                 if (it->index < d->capacity) {
                     R(pc->r0) = value_string_copy((const char*)(uintptr_t)(d->keys[it->index++] & MASK_PAYLOAD));
                     pc++; goto l_DISPATCH;
                 }
            } else if (type == VAL_STRING) {
                 const char *s = AS_STRING(coll);
                 int len = strlen(s);
                 if (it->index < len) {
                     char buf[2] = { s[it->index++], '\0' };
                     R(pc->r0) = value_string_copy(buf);
                     pc++; goto l_DISPATCH;
                 }
            }
        } else if (obj->type == OBJ_SQLITE_ITER) {
            SQLiteIter *it = (SQLiteIter*)obj;
            if (!it->done) {
                int rc = sqlite3_step(it->stmt);
                if (rc == SQLITE_ROW) {
                    R(pc->r0) = glow_sqlite_row(it->stmt);
                    pc++; goto l_DISPATCH;
                } else {
                    it->done = 1;
                    sqlite3_finalize(it->stmt);
                    it->stmt = NULL;
                }
            }
        }

        if (pc->arg < 0 || pc->arg >= bc->count) { pending_exception = value_string_copy("ITER_NEXT jump out of bounds"); has_exception = 1; goto l_OP_THROW; }
        pc = bc->code + pc->arg; DISPATCH();
    }
    l_OP_IN: {
        Value elem = R(pc->r1);
        Value coll = R(pc->r2);
        int found = 0;
        if (GET_TYPE(coll) == VAL_LIST) {
            List *l = AS_LIST(coll);
            for (int i = 0; i < l->size; i++) {
                if (val_eq(l->items[i], elem)) {
                    found = 1;
                    break;
                }
            }
        } else if (GET_TYPE(coll) == VAL_DICT) {
            if (GET_TYPE(elem) == VAL_STRING) {
                Value val = dict_get(AS_DICT(coll), AS_STRING(elem));
                if (GET_TYPE(val) != VAL_NONE) {
                    found = 1;
                }
            }
        } else if (GET_TYPE(coll) == VAL_STRING) {
            if (GET_TYPE(elem) == VAL_STRING) {
                const char *s = AS_STRING(coll);
                const char *sub = AS_STRING(elem);
                if (strstr(s, sub) != NULL) {
                    found = 1;
                }
            }
        }
        R(pc->r0) = value_bool(found);
        pc++; goto l_DISPATCH;
    }

    l_OP_ADD_INT: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (__builtin_expect(IS_INT(a) && IS_INT(b), 1)) {
            int64_t ia = AS_INT(a), ib = AS_INT(b);
            int64_t res = ia + ib;
            if (__builtin_expect(res >= -140737488355328LL && res <= 140737488355327LL, 1)) {
                R(pc->r0) = value_int(res);
                pc++;
                goto l_DISPATCH;
            }
        }
        pc->op = OP_ADD;
        goto l_OP_ADD;
    }
    l_OP_SUB_INT: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (__builtin_expect(IS_INT(a) && IS_INT(b), 1)) {
            int64_t ia = AS_INT(a), ib = AS_INT(b);
            int64_t res = ia - ib;
            if (__builtin_expect(res >= -140737488355328LL && res <= 140737488355327LL, 1)) {
                R(pc->r0) = value_int(res);
                pc++;
                goto l_DISPATCH;
            }
        }
        pc->op = OP_SUB;
        goto l_OP_SUB;
    }
    l_OP_MUL_INT: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (__builtin_expect(IS_INT(a) && IS_INT(b), 1)) {
            int64_t ia = AS_INT(a), ib = AS_INT(b);
            int64_t res = ia * ib;
            if (__builtin_expect((ib == 0 || res / ib == ia) && (res >= -140737488355328LL && res <= 140737488355327LL), 1)) {
                R(pc->r0) = value_int(res);
                pc++;
                goto l_DISPATCH;
            }
        }
        pc->op = OP_MUL;
        goto l_OP_MUL;
    }
    l_OP_LT_INT: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (__builtin_expect(IS_INT(a) && IS_INT(b), 1)) {
            R(pc->r0) = value_bool(AS_INT(a) < AS_INT(b));
            pc++;
            goto l_DISPATCH;
        }
        pc->op = OP_LT;
        goto l_OP_LT;
    }
    l_OP_GT_INT: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (__builtin_expect(IS_INT(a) && IS_INT(b), 1)) {
            R(pc->r0) = value_bool(AS_INT(a) > AS_INT(b));
            pc++;
            goto l_DISPATCH;
        }
        pc->op = OP_GT;
        goto l_OP_GT;
    }
    l_OP_LTE_INT: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (__builtin_expect(IS_INT(a) && IS_INT(b), 1)) {
            R(pc->r0) = value_bool(AS_INT(a) <= AS_INT(b));
            pc++;
            goto l_DISPATCH;
        }
        pc->op = OP_LTE;
        goto l_OP_LTE;
    }
    l_OP_GTE_INT: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (__builtin_expect(IS_INT(a) && IS_INT(b), 1)) {
            R(pc->r0) = value_bool(AS_INT(a) >= AS_INT(b));
            pc++;
            goto l_DISPATCH;
        }
        pc->op = OP_GTE;
        goto l_OP_GTE;
    }
    l_OP_EQ_INT: {
        Value a = R(pc->r1), b = R(pc->r2);
        if (__builtin_expect(IS_INT(a) && IS_INT(b), 1)) {
            R(pc->r0) = value_bool(AS_INT(a) == AS_INT(b));
            pc++;
            goto l_DISPATCH;
        }
        pc->op = OP_EQ;
        goto l_OP_EQ;
    }
    l_OP_NOP: pc++; goto l_DISPATCH;

l_FINISH:
    frame_pool_used = frame_base;
    if (has_exception && call_depth == 1) {
        print_error_trace(pending_exception);
        runtime_error_thrown = 1;
        has_exception = 0; // Prevent double report
    }
    call_stack_pop();
    vm_stack_top = old_top;
    gc_free_stack_arena_range(old_arena_used, stack_arena_used);
    stack_arena_used = old_arena_used;
    call_depth--;
    return (has_exception) ? value_none() : R(0);
}
