#include "glowlang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <process.h>
#include <direct.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stddef.h>
#ifndef _WIN32
#include <sanitizer/asan_interface.h>
#endif
#define sleep(x) Sleep((x)*1000)
#define usleep(x) Sleep((x)/1000)
typedef int socklen_t;
#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED) && !defined(__MINGW32__) && !defined(__MINGW64__)
typedef int ssize_t;
#endif
#define close(s) closesocket(s)
#define read(s, b, n) recv(s, (char*)(b), n, 0)
#define write(s, b, n) send(s, (const char*)(b), n, 0)

static inline int posix_memalign(void **memptr, size_t alignment, size_t size) {
    *memptr = _aligned_malloc(size, alignment);
    return (*memptr == NULL) ? 1 : 0;
}

#ifndef _TIMEZONE_DEFINED
#define _TIMEZONE_DEFINED
struct timezone {
    int  tz_minuteswest;
    int  tz_dsttime;
};
#endif

static inline int gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (tv) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        unsigned long long t = ft.dwHighDateTime;
        t <<= 32;
        t |= ft.dwLowDateTime;
        t /= 10;
        t -= 11644473600000000ULL;
        tv->tv_sec = (long)(t / 1000000UL);
        tv->tv_usec = (long)(t % 1000000UL);
    }
    return 0;
}

#pragma comment(lib, "ws2_32.lib")

struct dirent {
    char d_name[1024];
    int d_type;
};
#define DT_DIR 1
#define DT_REG 2

typedef struct {
    HANDLE hFind;
    WIN32_FIND_DATAA fd;
    struct dirent entry;
    int first;
} DIR;

static DIR *opendir(const char *name) {
    DIR *dir = (DIR *)malloc(sizeof(DIR));
    char search_path[1024];
    snprintf(search_path, sizeof(search_path), "%s\\*", name);
    dir->hFind = FindFirstFileA(search_path, &dir->fd);
    if (dir->hFind == INVALID_HANDLE_VALUE) { free(dir); return NULL; }
    dir->first = 1;
    return dir;
}

static struct dirent *readdir(DIR *dir) {
    if (!dir->first) { if (!FindNextFileA(dir->hFind, &dir->fd)) return NULL; }
    dir->first = 0;
    strncpy(dir->entry.d_name, dir->fd.cFileName, 1023);
    dir->entry.d_name[1023] = '\0';
    dir->entry.d_type = (dir->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
    return &dir->entry;
}

static void closedir(DIR *dir) {
    if (dir) { FindClose(dir->hFind); free(dir); }
}

#define stat _stat
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#define mkdir(path, mode) _mkdir(path)
#define rmdir _rmdir
#define unlink _unlink
#else
#include <sys/time.h>
#include <sys/select.h>
#include <sys/wait.h>
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
#include <ctype.h>

/* ---- Runtime-dispatched SIMD kernels -----------------------------------
   The binary is built for baseline x86-64. The AVX2/FMA kernels below are
   compiled for those extensions individually and only called after CPUID
   confirms the CPU (and OS) support them; otherwise the scalar loops run.
   Each kernel processes whole 4-lane groups and returns how many elements
   it handled, leaving the tail to the calling scalar loop. */
#if defined(__x86_64__) || defined(_M_X64)
#define GLOW_X86_SIMD 1
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#if defined(__GNUC__) || defined(__clang__)
#define GLOW_TARGET_AVX2 __attribute__((target("avx2,fma")))
#else
#define GLOW_TARGET_AVX2
#endif

static void glow_cpuid(int leaf, int sub, unsigned int out[4]) {
#if defined(_MSC_VER)
    int r[4]; __cpuidex(r, leaf, sub);
    out[0] = (unsigned)r[0]; out[1] = (unsigned)r[1]; out[2] = (unsigned)r[2]; out[3] = (unsigned)r[3];
#else
    unsigned int a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(leaf), "c"(sub));
    out[0] = a; out[1] = b; out[2] = c; out[3] = d;
#endif
}

static unsigned long long glow_xgetbv0(void) {
#if defined(__GNUC__) || defined(__clang__)
    unsigned int lo, hi;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return ((unsigned long long)hi << 32) | lo;
#else
    return _xgetbv(0);
#endif
}

int glow_cpu_has_avx2_fma(void) {
    static int cached = -1;
    if (cached >= 0) return cached;
    int ok = 0;
    if (!getenv("GLOW_NO_SIMD")) {
        unsigned int r[4];
        glow_cpuid(0, 0, r);
        if (r[0] >= 7) {
            glow_cpuid(1, 0, r);
            int osxsave = (r[2] >> 27) & 1, avx = (r[2] >> 28) & 1, fma = (r[2] >> 12) & 1;
            if (osxsave && avx && fma && (glow_xgetbv0() & 6) == 6) {
                glow_cpuid(7, 0, r);
                ok = (r[1] >> 5) & 1;
            }
        }
    }
    cached = ok;
    return ok;
}

GLOW_TARGET_AVX2 static int simd_add_f64(double *r, const double *a, const double *b, int n) {
    int i = 0;
    for (; i + 4 <= n; i += 4) _mm256_storeu_pd(&r[i], _mm256_add_pd(_mm256_loadu_pd(&a[i]), _mm256_loadu_pd(&b[i])));
    return i;
}
GLOW_TARGET_AVX2 static int simd_sub_f64(double *r, const double *a, const double *b, int n) {
    int i = 0;
    for (; i + 4 <= n; i += 4) _mm256_storeu_pd(&r[i], _mm256_sub_pd(_mm256_loadu_pd(&a[i]), _mm256_loadu_pd(&b[i])));
    return i;
}
GLOW_TARGET_AVX2 static int simd_mul_f64(double *r, const double *a, const double *b, int n) {
    int i = 0;
    for (; i + 4 <= n; i += 4) _mm256_storeu_pd(&r[i], _mm256_mul_pd(_mm256_loadu_pd(&a[i]), _mm256_loadu_pd(&b[i])));
    return i;
}
GLOW_TARGET_AVX2 static int simd_add_i64(int64_t *r, const int64_t *a, const int64_t *b, int n) {
    int i = 0;
    for (; i + 4 <= n; i += 4) _mm256_storeu_si256((__m256i*)&r[i], _mm256_add_epi64(_mm256_loadu_si256((const __m256i*)&a[i]), _mm256_loadu_si256((const __m256i*)&b[i])));
    return i;
}
GLOW_TARGET_AVX2 static int simd_sub_i64(int64_t *r, const int64_t *a, const int64_t *b, int n) {
    int i = 0;
    for (; i + 4 <= n; i += 4) _mm256_storeu_si256((__m256i*)&r[i], _mm256_sub_epi64(_mm256_loadu_si256((const __m256i*)&a[i]), _mm256_loadu_si256((const __m256i*)&b[i])));
    return i;
}
/* Fused multiply-add dot product over the 4-lane prefix; *sum gets the
   partial result. */
GLOW_TARGET_AVX2 static int simd_dot_f64(const double *a, const double *b, int n, double *sum) {
    __m256d acc = _mm256_setzero_pd();
    int i = 0;
    for (; i + 4 <= n; i += 4) acc = _mm256_fmadd_pd(_mm256_loadu_pd(&a[i]), _mm256_loadu_pd(&b[i]), acc);
    double t[4];
    _mm256_storeu_pd(t, acc);
    *sum = t[0] + t[1] + t[2] + t[3];
    return i;
}
/* One output cell of a row-major matmul: dot(row of A, column j of B). */
GLOW_TARGET_AVX2 static int simd_matmul_cell(const double *row_a, const double *db, int cA, int cB, int j, double *sum) {
    __m256d acc = _mm256_setzero_pd();
    int k = 0;
    for (; k + 4 <= cA; k += 4) {
        __m256d va = _mm256_loadu_pd(&row_a[k]);
        __m256d vb = _mm256_set_pd(db[(k+3) * cB + j], db[(k+2) * cB + j], db[(k+1) * cB + j], db[k * cB + j]);
        acc = _mm256_fmadd_pd(va, vb, acc);
    }
    double t[4];
    _mm256_storeu_pd(t, acc);
    *sum = t[0] + t[1] + t[2] + t[3];
    return k;
}
#else
int glow_cpu_has_avx2_fma(void) { return 0; }
static int simd_add_f64(double *r, const double *a, const double *b, int n) { (void)r; (void)a; (void)b; (void)n; return 0; }
static int simd_sub_f64(double *r, const double *a, const double *b, int n) { (void)r; (void)a; (void)b; (void)n; return 0; }
static int simd_mul_f64(double *r, const double *a, const double *b, int n) { (void)r; (void)a; (void)b; (void)n; return 0; }
static int simd_add_i64(int64_t *r, const int64_t *a, const int64_t *b, int n) { (void)r; (void)a; (void)b; (void)n; return 0; }
static int simd_sub_i64(int64_t *r, const int64_t *a, const int64_t *b, int n) { (void)r; (void)a; (void)b; (void)n; return 0; }
static int simd_dot_f64(const double *a, const double *b, int n, double *sum) { (void)a; (void)b; (void)n; *sum = 0; return 0; }
static int simd_matmul_cell(const double *row_a, const double *db, int cA, int cB, int j, double *sum) { (void)row_a; (void)db; (void)cA; (void)cB; (void)j; *sum = 0; return 0; }
#endif

Class *vm_last_class = NULL;
Trait *vm_last_trait = NULL;
GlobalTable global_table = {NULL, NULL, 0, 0, NULL, 0};

FuncEntry *func_table = NULL;
int func_table_size = 1024;
int func_table_capacity = 0;
FuncEntry main_fe;

const char *compiling_file = NULL;

GLOW_PLAIN_LOOPS Value jit_call_helper(FuncEntry *fe, int argc, Value *args, Instruction *pc) {
      (void)argc;
      if (pc && call_stack_ptr > 0) call_stack[call_stack_ptr - 1].pc = pc;
      if (glow_stack_low()) {
          pending_exception = value_string_copy("Maximum recursion depth exceeded");
          has_exception = 1;
          return value_none();
      }
      if (fe && fe->body) {
          /* Allocate a fresh register frame for the callee instead of reusing the
           * caller's window via an offset (R + r2*8). The offset model lets nested
           * JIT calls accumulate past the allocated frame and clobber neighbouring
           * vm_stack slots / heap on mutual or deep recursion (heap corruption
           * surfacing as a later calloc crash). This mirrors the interpreter's
           * DO_CALL frame chaining, so each level's register window resets. */
 int new_base = vm_stack_top;
           int needed = new_base + fe->body->local_count + 16;
           if (needed + 512 >= vm_stack_size) {
               pending_exception = value_string_copy("Maximum recursion depth exceeded");
               has_exception = 1;
               return value_none();
           }
           Value temp_args[16];
          int copy_count = fe->param_count < argc ? fe->param_count : argc;
          if (copy_count > 16) copy_count = 16;
          for (int _i = 0; _i < copy_count; _i++)
              temp_args[_i] = args[_i];
          for (int _i = 0; _i < copy_count; _i++)
              vm_stack[new_base + _i] = temp_args[_i];
          /* Parameters the call gave no argument for are none (the callee
             fills in its defaults), like every other local. */
          for (int _i = copy_count; _i < fe->body->local_count; _i++)
              vm_stack[new_base + _i] = value_none();
         int saved_top = vm_stack_top;
         vm_stack_top = needed;

         /* Only functions with type specializations need the argument types;
         the rest run their one compiled body, so skip the signature (and the
         interpreter's call cache, which only ever holds specializations). */
         int has_sig = fe->spec_count > 0;
         TypeSignature sig = has_sig ? compute_signature(&vm_stack[new_base], fe->param_count) : 0;
         void *jit = has_sig ? func_get_specialization(fe, sig) : NULL;
         typedef Value (*JITFunc)(Value *R, int *pc_idx);
         Value res;
         if (jit && jit != (void*)1) {
             if (pc) { pc->cache[0].key = (void*)sig; pc->cache[0].val = (intptr_t)jit; }
             int deopt_pc = 0;
              call_stack_push(fe, NULL);
              res = ((JITFunc)jit)(&vm_stack[new_base], &deopt_pc);
              call_stack_pop();
              if (IS_DEOPT(res)) res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, new_base);
         } else if (fe->body->jit_code) {
             if (pc && has_sig) { pc->cache[0].key = (void*)sig; pc->cache[0].val = (intptr_t)fe->body->jit_code; }
             int deopt_pc = 0;
             call_stack_push(fe, NULL);
             res = ((JITFunc)fe->body->jit_code)(&vm_stack[new_base], &deopt_pc);
             call_stack_pop();
             if (IS_DEOPT(res)) res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, new_base);
         } else if (fe->body->baseline_jit_code) {
             if (pc && has_sig) { pc->cache[0].key = (void*)sig; pc->cache[0].val = (intptr_t)fe->body->baseline_jit_code; }
             int deopt_pc = 0;
             call_stack_push(fe, NULL);
             res = ((JITFunc)fe->body->baseline_jit_code)(&vm_stack[new_base], &deopt_pc);
             call_stack_pop();
             if (IS_DEOPT(res)) res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, new_base);
          } else {
              res = run_bytecode(fe->body, NULL, NULL, new_base);
          }
           vm_stack_top = saved_top;
           return res;
      }
return value_none();
}

/* Self-recursion from compiled code (OP_CALL_SELF_FAST). The compiled caller
   stores the result straight into its register, so this always finishes the
   call: a deopt inside the callee is completed by the interpreter, and running
   out of machine or VM stack raises the usual recursion error. It returns the
   deopt sentinel only when an exception is pending; the caller then leaves
   compiled code at the call and the interpreter unwinds it. */
static GLOW_NOINLINE void *self_call_specialization(FuncEntry *fe, Value *args) {
    void *spec = func_get_specialization(fe, compute_signature(args, fe->param_count));
    return spec != (void*)1 ? spec : NULL;
}

Value jit_self_call_direct(FuncEntry *fe, Value *args) {
    typedef Value (*JITFunc)(Value *R, int *pc_idx);
    Bytecode *body = fe->body;
    int new_base = vm_stack_top;
    int needed = new_base + body->local_count + 4;
    if (call_depth >= 65500 || glow_stack_low() || needed + 512 >= vm_stack_size) {
        pending_exception = value_string_copy("Maximum recursion depth exceeded");
        has_exception = 1;
        return VAL_DEOPT_SENTINEL;
    }
    for (int i = 0; i < fe->param_count; i++) vm_stack[new_base + i] = args[i];
    vm_stack_top = needed;

    void *code = body->jit_code ? body->jit_code : body->baseline_jit_code;
    if (!code && fe->spec_count > 0) code = self_call_specialization(fe, &vm_stack[new_base]);
    Value res;
    if (code && !glow_nojitexec) {
        int deopt_pc = 0;
        call_stack_push(fe, body->code);
        call_depth++;
        res = ((JITFunc)code)(&vm_stack[new_base], &deopt_pc);
        call_depth--;
        call_stack_pop();
        if (IS_DEOPT(res)) res = run_bytecode(body, &body->code[deopt_pc], NULL, new_base);
    } else {
        res = run_bytecode(body, NULL, NULL, new_base);
    }
    vm_stack_top = new_base;
    return has_exception ? VAL_DEOPT_SENTINEL : res;
}

/* Direct calls (emit_self_call_direct in jit.c). Compiled code asks for this
   once on entry and passes it down to the functions it calls directly. */
static THREAD_LOCAL JitThreadState jit_ts;
JitThreadState *jit_thread_state(void) {
    jit_ts.stack = vm_stack;
    jit_ts.top = &vm_stack_top;
    jit_ts.size = vm_stack_size;
    jit_ts.depth = &call_depth;
    jit_ts.frames_len = &call_stack_ptr;
    jit_ts.frames = call_stack;
    jit_ts.has_exception = &has_exception;
    jit_ts.machine_limit = (char*)glow_thread_ctx.stack_limit;
    return &jit_ts;
}

/* A directly called function left compiled code part-way (its frame starts
   at vm_stack[new_base], the instruction to resume at is *pc_idx): finish
   it in the interpreter and drop its frame, as jit_self_call_direct does. */
Value jit_direct_finish(FuncEntry *fe, int new_base, int *pc_idx) {
    Bytecode *body = fe->body;
    Value res = run_bytecode(body, &body->code[*pc_idx], NULL, new_base);
    vm_stack_top = new_base;
    return res;
}

 Value jit_call_helper_name(const char *name, int argc, Value *args, Instruction *pc) {
    if (pc && call_stack_ptr > 0) call_stack[call_stack_ptr - 1].pc = pc;
    FuncEntry *fe = func_get(name);
    if (fe) return jit_call_helper(fe, argc, args, pc);
    Value v = var_get(name);
    return jit_call_helper_val(v, argc, args, pc);
 }

/* Klass(args...) from compiled code, as the interpreter's OP_CALL does it:
   a new instance, then `init` (if the class has one) with self in register 0
   and the arguments after it, missing ones none and extra ones dropped. The
   result is the instance. An init that is not a compiled-language method
   (a native or a value stored as init) is left to the interpreter: this
   returns the deopt sentinel before doing anything. An exception raised by
   init stays pending, and the compiled caller leaves at the call. */
extern void func_maybe_tier_up(FuncEntry *fe);
static Value jit_construct(Class *k, int argc, Value *args, Instruction *pc) {
    typedef Value (*JITFunc)(Value *R, int *pc_idx);
    Value init = class_init_method(k);
    FuncEntry *ife = NULL;
    if (GET_TYPE(init) == VAL_OBJECT && (init & MASK_PAYLOAD)) {
        if (GET_OBJ_TYPE(init) != OBJ_BOUND_METHOD) return VAL_DEOPT_SENTINEL;
        ife = AS_BOUND_METHOD(init)->method;
        if (!ife || !ife->body || AS_BOUND_METHOD(init)->native_method) return VAL_DEOPT_SENTINEL;
    }
    if (!ife) return value_instance(instance_new(k));

    Bytecode *body = ife->body;
    int new_base = vm_stack_top;
    int frame = body->local_count > ife->param_count + 1 ? body->local_count : ife->param_count + 1;
    int needed = new_base + frame + 16;
    if (glow_stack_low() || call_depth >= 65500 || needed + 512 >= vm_stack_size) {
        pending_exception = value_string_copy("Maximum recursion depth exceeded");
        has_exception = 1;
        return value_none();
    }
    /* The arguments live in the caller's frame, below new_base. The new
       instance goes straight into the callee frame, where the GC sees it,
       before anything else can allocate. */
    Value inst = value_instance(instance_new(k));
    vm_stack[new_base] = inst;
    for (int i = 0; i < ife->param_count; i++) vm_stack[new_base + 1 + i] = i < argc ? args[i] : value_none();
    for (int i = ife->param_count + 1; i < frame; i++) vm_stack[new_base + i] = value_none();
    int saved_top = vm_stack_top;
    vm_stack_top = needed;

    static int self_slot = -1;
    if (self_slot < 0) self_slot = global_get_slot("self");
    Value old_self = global_table.values[self_slot];
    global_table.values[self_slot] = inst;
    global_mark_declared(self_slot);

    func_maybe_tier_up(ife);
    void *code = body->jit_code ? body->jit_code : body->baseline_jit_code;
    if (pc && call_stack_ptr > 0) call_stack[call_stack_ptr - 1].pc = pc;
    if (code && !glow_nojitexec) {
        int deopt_pc = 0;
        call_stack_push(ife, NULL);
        call_depth++;
        Value res = ((JITFunc)code)(&vm_stack[new_base], &deopt_pc);
        call_depth--;
        call_stack_pop();
        if (IS_DEOPT(res)) run_bytecode(body, &body->code[deopt_pc], NULL, new_base);
    } else {
        run_bytecode(body, NULL, NULL, new_base);
    }
    global_table.values[self_slot] = old_self;
    vm_stack_top = saved_top;
    return inst;
}

GLOW_PLAIN_LOOPS Value jit_call_helper_val(Value callee, int argc, Value *args, Instruction *pc) {
    if (pc && call_stack_ptr > 0) call_stack[call_stack_ptr - 1].pc = pc;
    /* Anything this helper cannot complete exactly like the interpreter
       (calling a non-callable raises; see also jit_construct) returns the
       deopt sentinel before doing any work, and the compiled
       code then lets the interpreter execute the call instruction. */
    if (!(GET_TYPE(callee) == VAL_OBJECT && (callee & MASK_PAYLOAD))) return VAL_DEOPT_SENTINEL;
    if (GET_TYPE(callee) == VAL_OBJECT && (callee & MASK_PAYLOAD)) {
        ObjType ot = GET_OBJ_TYPE(callee);
        if (ot == OBJ_CLASS) return jit_construct(AS_CLASS(callee), argc, args, pc);
        if (ot == OBJ_BOUND_METHOD) {
             BoundMethod *bm = AS_BOUND_METHOD(callee);
             if (glow_calldbg) { fprintf(stderr, "[CALLVAL] bm=%p bm->method=%p bm->native=%p argc=%d args=%p callee=%lx ot=%d\n", (void*)bm, (void*)bm->method, (void*)bm->native_method, argc, (void*)args, (unsigned long)callee, (int)ot); fflush(stderr); }
             if (((uintptr_t)bm & 7) || (uintptr_t)bm < 0x1000 || (uintptr_t)bm > 0x7fffffffffffUL) { fprintf(stderr, "[CALLVAL-BAD] callee=%lx bm=%p\n", (unsigned long)callee, (void*)bm); fflush(stderr); }
             if (bm->method == NULL || ((uintptr_t)bm->method & 7)) { fprintf(stderr, "[CALLVAL-BADMETHOD] callee=%lx bm=%p bm->method=%p\n", (unsigned long)callee, (void*)bm, (void*)bm->method); fflush(stderr); }
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
            if (!c->body) return value_none();
            if (c->captured_count == 0) return jit_call_helper(c->body->owner, argc, args, pc);
            /* Run with the closure attached so captured variables resolve. */
            int new_base = vm_stack_top;
            int needed = new_base + c->body->local_count + 16;
            if (needed + 512 >= vm_stack_size) {
                pending_exception = value_string_copy("Maximum recursion depth exceeded");
                has_exception = 1;
                return value_none();
            }
            int nparams = c->param_count;
            for (int i = 0; i < nparams; i++) vm_stack[new_base + i] = (i < argc) ? args[i] : value_none();
            int saved_top = vm_stack_top;
            vm_stack_top = needed;
            Value res = run_bytecode(c->body, NULL, c, new_base);
            vm_stack_top = saved_top;
            return res;
        }
        if (ot == OBJ_NATIVE_FUNC) {
            NativeFunc *nf = AS_NATIVE_FUNC(callee);
            if (pc && pc->is_method) {
                args++;
            }
            return glow_call_native(AS_NATIVE_FUNC(callee), argc, args);
        }
    }
    return VAL_DEOPT_SENTINEL;
}

/* o.n from JIT code: glow_get_property, shared with the interpreter. A
   missing property returns the deopt sentinel, so the interpreter re-runs
   the instruction and raises the error. */
Value jit_get_property_helper(Value o, const char *n, Instruction *pc) {
    int missing = 0;
    Value v = glow_get_property(o, n, pc, &missing);
    return missing ? VAL_DEOPT_SENTINEL : v;
}

Value jit_set_property_helper(Value o, const char *n, Value v, Instruction *pc) {
    if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD) && GET_OBJ_TYPE(o)==OBJ_INSTANCE) {
        instance_set_cached(AS_INSTANCE(o), n, v, pc);
    } else if(GET_TYPE(o)==VAL_DICT) dict_set(AS_DICT(o), n, v);
    return v;
}

GLOW_PLAIN_LOOPS Value jit_call_method_helper(Value obj, const char *n, int argc, Value *args, Instruction *pc) {
    extern THREAD_LOCAL Bytecode *g_dbg_bc;
    extern THREAD_LOCAL Instruction *g_dbg_pc;
    extern THREAD_LOCAL int vm_stack_top;
    FuncEntry *fe = NULL;
    NativeFuncPtr native_fe = NULL;
    int is_native_dict_str_method = 0;
    Value fallback_callee = value_none();

    /* Resolve the target exactly as the interpreter's OP_CALL_METHOD does.
       If there is nothing this helper can run, return the deopt sentinel
       before touching any state so the interpreter executes the instruction
       (and raises the proper error). */
    if (n && strcmp(n, "type") == 0) {
        native_fe = native_val_type; is_native_dict_str_method = 1;
    } else if ((native_fe = glow_method_lookup(obj, n, pc, &fallback_callee)) != NULL) {
        is_native_dict_str_method = 1;
    } else if(GET_TYPE(obj)==VAL_OBJECT && (obj & MASK_PAYLOAD)) {
        ObjType ot = GET_OBJ_TYPE(obj);
        if(ot==OBJ_INSTANCE) {
            Instance *inst = AS_INSTANCE(obj);
            if (pc && pc->cache[0].key == inst->klass && pc->cache[1].key == (void*)1) {
                if (call_stack_ptr > 0) call_stack[call_stack_ptr - 1].pc = pc;
                g_dbg_pc = pc;
                int old_top = vm_stack_top;
                int curr_offset = (int)(args - vm_stack);
                if (curr_offset + 256 > vm_stack_top) vm_stack_top = curr_offset + 256;
                FuncEntry *cached_fe = (FuncEntry*)pc->cache[0].val;
                /* Parameters the caller left out are none, as in the
                   interpreter (they used to keep whatever the registers
                   held, so a default parameter saw garbage). */
                for (int k = 1 + argc; k < cached_fe->body->preserved_locals; k++) args[k] = value_none();
                typedef Value (*JITFunc)(Value *R, int *pc_idx);
                /* A method cached at its baseline tier keeps counting
                   towards the optimizing tier, and switches to it once it
                   exists. */
                if (pc->cache[1].val == (intptr_t)cached_fe->body->baseline_jit_code) {
                    if (!cached_fe->body->jit_code) func_maybe_tier_up(cached_fe);
                    if (cached_fe->body->jit_code) pc->cache[1].val = (intptr_t)cached_fe->body->jit_code;
                }
                JITFunc cached_jit = (JITFunc)pc->cache[1].val;
                int deopt_pc = 0;
                call_stack_push(cached_fe, NULL);
                static int self_slot = -1;
                if (self_slot < 0) self_slot = global_get_slot("self");
                Value old_self = (self_slot >= 0 && self_slot < global_table.count) ? global_table.values[self_slot] : value_none();
                if (self_slot >= 0 && self_slot < global_table.count) global_table.values[self_slot] = obj;
                Value res = cached_jit(args, &deopt_pc);
                call_stack_pop();
                /* Compiled code can bail out mid-method; finish in the interpreter. */
                if (IS_DEOPT(res)) res = run_bytecode(cached_fe->body, &cached_fe->body->code[deopt_pc], NULL, (int)(args - vm_stack));
                if (self_slot >= 0 && self_slot < global_table.count) global_table.values[self_slot] = old_self;
                vm_stack_top = old_top;
                return res;
            }
            if (pc && pc->cache[0].key == inst->klass) {
                fe = (FuncEntry*)pc->cache[0].val;
            } else {
                Value m = class_get_method(inst->klass, n);
                if (GET_TYPE(m) == VAL_OBJECT && (m & MASK_PAYLOAD) && GET_OBJ_TYPE(m) == OBJ_BOUND_METHOD) {
                    fe = AS_BOUND_METHOD(m)->method;
                    if (pc) {
                        pc->cache[0].key = inst->klass;
                        pc->cache[0].val = (intptr_t)fe;
                    }
                }
            }
            if (!fe) {
                /* A callable stored on the object, invoked as obj.handler(...). */
                Value field = dict_get(inst->properties, n);
                if (GET_TYPE(field) != VAL_NONE) fallback_callee = field;
            }
        }
    }

    if (!(fe && fe->body) && !(native_fe && is_native_dict_str_method)) {
        /* Only plain native functions are completed here; closures, bound
           methods and classes stored as fields go back to the interpreter. */
        if (!(GET_TYPE(fallback_callee) == VAL_OBJECT && (fallback_callee & MASK_PAYLOAD) &&
              GET_OBJ_TYPE(fallback_callee) == OBJ_NATIVE_FUNC)) return VAL_DEOPT_SENTINEL;
    }

    if (pc && call_stack_ptr > 0) call_stack[call_stack_ptr - 1].pc = pc;
    g_dbg_pc = pc;
    int old_top = vm_stack_top;
    int curr_offset = (int)(args - vm_stack);
    if (curr_offset + 256 > vm_stack_top) vm_stack_top = curr_offset + 256;

    Value final_res = value_none();
    if (fe && fe->body) {
        g_dbg_bc = fe->body;
        static int self_slot = -1;
        if (self_slot < 0) self_slot = global_get_slot("self");
        Value old_self = (self_slot >= 0 && self_slot < global_table.count) ? global_table.values[self_slot] : value_none();
        if (self_slot >= 0 && self_slot < global_table.count) global_table.values[self_slot] = obj;
        for (int k = 1 + argc; k < fe->body->preserved_locals; k++) args[k] = value_none();
        TypeSignature sig = compute_signature(args, fe->param_count);
        void *jit = func_get_specialization(fe, sig);
        if (!(jit && jit != (void*)1)) {
            /* No specialization: run the method's own compiled body, the
               optimizing tier once it exists (this used to fall back to the
               interpreter whenever the method had one). */
            func_maybe_tier_up(fe);
            jit = fe->body->jit_code ? fe->body->jit_code : NULL;
        }
        if (jit && jit != (void*)1) {
            typedef Value (*JITFunc)(Value *R, int *pc_idx);
            int deopt_pc = 0;
            if (pc) {
                pc->cache[1].key = (void*)1;
                pc->cache[1].val = (intptr_t)jit;
            }
            call_stack_push(fe, NULL);
            final_res = ((JITFunc)jit)(args, &deopt_pc);
            call_stack_pop();
            if (IS_DEOPT(final_res)) final_res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, (int)(args - vm_stack));
        } else if (fe->body->baseline_jit_code && !fe->body->jit_code) {
            typedef Value (*JITFunc)(Value *R, int *pc_idx);
            int deopt_pc = 0;
            if (pc) {
                pc->cache[1].key = (void*)1;
                pc->cache[1].val = (intptr_t)fe->body->baseline_jit_code;
            }
            call_stack_push(fe, NULL);
            final_res = ((JITFunc)fe->body->baseline_jit_code)(args, &deopt_pc);
            call_stack_pop();
            if (IS_DEOPT(final_res)) final_res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, (int)(args - vm_stack));
        } else {
            final_res = run_bytecode(fe->body, NULL, NULL, (int)(args - vm_stack));
        }
        if (self_slot >= 0 && self_slot < global_table.count) global_table.values[self_slot] = old_self;
    } else if (native_fe && is_native_dict_str_method) {
        Value temp_args[257];
        temp_args[0] = obj;
        int count = argc;
        if (count > 254) count = 254;
        int start_offset = (pc && pc->is_method) ? 1 : 0;
        for (int i=0; i<count; i++) temp_args[i+1] = args[start_offset + i];
        final_res = native_fe(count + 1, temp_args);
    } else {
        NativeFunc *nf = AS_NATIVE_FUNC(fallback_callee);
        Value *fargs = args;
        if (pc && pc->is_method) fargs++;
        final_res = glow_call_native(nf, argc, fargs);
    }

    vm_stack_top = old_top;
    return final_res;
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

/* Indexing from compiled code. Out-of-range or non-integer list/string
   indexes return the deopt sentinel so the interpreter raises the error. */
Value glow_index_jit(Value coll, Value v_idx) {
    if (GET_TYPE(coll) == VAL_LIST) {
        if (GET_TYPE(v_idx) != VAL_INT) return VAL_DEOPT_SENTINEL;
        int64_t i = AS_INT(v_idx);
        List *l = AS_LIST(coll);
        if (i < 0) i += l->size;
        if (i >= 0 && i < l->size) return l->items[i];
        return VAL_DEOPT_SENTINEL;
    } else if (GET_TYPE(coll) == VAL_OBJECT) {
        if ((coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_PACKED_ARRAY) {
            return packed_array_get_jit(coll, v_idx);
        }
        return value_none();
    } else if (GET_TYPE(coll) == VAL_DICT) {
        Value v = dict_get(AS_DICT(coll), AS_STRING(v_idx));
        /* Missing keys that name dict methods produce bound methods in the
           interpreter; let it handle those. */
        if (GET_TYPE(v) == VAL_NONE && GET_TYPE(v_idx) == VAL_STRING) return VAL_DEOPT_SENTINEL;
        return v;
    } else if (GET_TYPE(coll) == VAL_STRING) {
        if (GET_TYPE(v_idx) != VAL_INT) return VAL_DEOPT_SENTINEL;
        int64_t len = glow_str_len(coll);
        int64_t i = AS_INT(v_idx);
        if (i < 0) i += len;
        if (i < 0 || i >= len) return VAL_DEOPT_SENTINEL;
        return glow_str_char_at(coll, i);
    }
    return value_none();
}

/* `coll[idx] = val` from compiled code. Returns the deopt sentinel, before
   changing anything, for cases the interpreter must handle (errors). */
Value glow_store_index_jit(Value coll, Value v_idx, Value v_val) {
    if (GET_TYPE(coll) == VAL_LIST) {
        if (GET_TYPE(v_idx) != VAL_INT) return VAL_DEOPT_SENTINEL;
        int64_t i = AS_INT(v_idx);
        List *l = AS_LIST(coll);
        if (i < 0) i += l->size;
        if (i < 0 || i >= l->size) return VAL_DEOPT_SENTINEL;
        l->items[i] = v_val;
        gc_write_barrier((GCObject*)l, v_val);
    } else if (GET_TYPE(coll) == VAL_OBJECT && (coll & MASK_PAYLOAD) && GET_OBJ_TYPE(coll) == OBJ_PACKED_ARRAY) {
        packed_array_set_jit(coll, v_idx, v_val);
    } else if (GET_TYPE(coll) == VAL_DICT && GET_TYPE(v_idx) == VAL_STRING) {
        dict_set(AS_DICT(coll), AS_STRING(v_idx), v_val);
    } else {
        return VAL_DEOPT_SENTINEL;
    }
    return value_none();
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
     if (IS_FLOAT_BITS(v)) return AS_FLOAT(v);
     uint64_t type = ((v) & MASK_TYPE) >> 48;
     if (type == VAL_INT) return (double)AS_INT(v);
     if (type == VAL_BOOL) return (double)AS_BOOL(v);
     if (is_bigint(v)) return bigint_to_double(AS_BIGINT(v));
     return 0.0;
 }

/* A number result from C: a whole number that fits an inline (48-bit) int
   becomes one, anything else stays a double. It used to box any whole
   double as an inline int, so math.floor(1e15) wrapped to -125899906842624,
   and converted NaN to an int64 (undefined behaviour). */
Value value_from_double(double d) {
    if (d >= (double)INT48_MIN && d <= (double)INT48_MAX && d == (double)(int64_t)d) return value_int((int64_t)d);
    return value_float(d);
}

#define MAX_CALL_STACK 65536

Value value_string_copy(const char *s) {
    if (!s) s = "";
    size_t len = strlen(s);
    GCString *gs = gc_alloc(sizeof(GCString) + len + 1, OBJ_STRING);
    gs->size = (int)len;
    /* memcpy is already vectorised by the C runtime for whatever the CPU
       supports; hand-written AVX2 here made every build require AVX2. */
    memcpy(gs->chars, s, len + 1);
    return make_value(VAL_STRING, (uint64_t)(uintptr_t)gs);
}

/* Build a GCString directly from a length-delimited span (which need not be
   NUL-terminated), allocating the string once. Lets callers that already know
   the length (slice, split, JSON keys) avoid a temp malloc + second copy. */
Value value_string_copy_len(const char *s, size_t len) {
    if (!s) { s = ""; len = 0; }
    GCString *gs = gc_alloc(sizeof(GCString) + len + 1, OBJ_STRING);
    gs->size = (int)len;
    memcpy(gs->chars, s, len);
    gs->chars[len] = '\0';
    return make_value(VAL_STRING, (uint64_t)(uintptr_t)gs);
}

/* A new string of len bytes (plus the terminator) for the caller to fill:
   *chars points at its buffer. */
Value value_string_alloc(size_t len, char **chars) {
    GCString *gs = gc_alloc(sizeof(GCString) + len + 1, OBJ_STRING);
    gs->size = (int)len;
    gs->chars[len] = '\0';
    *chars = gs->chars;
    return make_value(VAL_STRING, (uint64_t)(uintptr_t)gs);
}

/* ---- UTF-8: character positions ---- */

/* The byte index just past the character starting at s[i] (i < n). A lead
   byte followed by the right number of continuation bytes is one character;
   any other byte stands alone. */
size_t glow_utf8_step(const char *s, size_t i, size_t n) {
    unsigned char b = (unsigned char)s[i];
    size_t len = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
    if (len == 1) return i + 1;
    if (i + len > n) return i + 1;
    for (size_t k = 1; k < len; k++)
        if (((unsigned char)s[i + k] & 0xC0) != 0x80) return i + 1;
    return i + len;
}

size_t glow_utf8_count(const char *s, size_t n) {
    size_t count = 0, i = 0;
    while (i < n) {
        /* Runs of ASCII are the common case: eight bytes at a time. */
        if (i + 8 <= n) {
            uint64_t w;
            memcpy(&w, s + i, 8);
            if (!(w & 0x8080808080808080ULL)) { i += 8; count += 8; continue; }
        }
        if ((unsigned char)s[i] < 0x80) { i++; count++; continue; }
        i = glow_utf8_step(s, i, n);
        count++;
    }
    return count;
}

/* The last non-ASCII position looked up (see glow_str_byte_offset).
   One for all threads: only the GIL holder runs, and the collector resets it. */
static struct { GCString *g; int64_t ch; size_t byte; } str_cursor;

/* The flat GCString holding v's bytes (ropes are flattened once). */
static GCString *str_flat_object(Value v) {
    GCObject *o = (GCObject*)(uintptr_t)(v & MASK_PAYLOAD);
    if (o->type == OBJ_STRING) return (GCString*)o;
    rope_flatten(v);
    return (GCString*)(uintptr_t)(((RopeNode*)o)->leaf.flat_string & MASK_PAYLOAD);
}

/* The number of characters in v, counted once and cached. */
int64_t glow_str_len(Value v) {
    if (GET_TYPE(v) != VAL_STRING || !(v & MASK_PAYLOAD)) return 0;
    GCObject *o = (GCObject*)(uintptr_t)(v & MASK_PAYLOAD);
    if (o->type == OBJ_ROPE && ((RopeNode*)o)->nchars1) return ((RopeNode*)o)->nchars1 - 1;
    GCString *g = str_flat_object(v);
    if (!g->nchars1) g->nchars1 = (int)glow_utf8_count(g->chars, (size_t)g->size) + 1;
    return g->nchars1 - 1;
}

/* Is v known to be pure ASCII (counted, and as many characters as bytes)? */
int glow_str_is_ascii(Value v) {
    if (GET_TYPE(v) != VAL_STRING || !(v & MASK_PAYLOAD)) return 0;
    GCObject *o = (GCObject*)(uintptr_t)(v & MASK_PAYLOAD);
    if (o->type != OBJ_STRING) return 0;
    GCString *g = (GCString*)o;
    return g->nchars1 == g->size + 1;
}

/* Record that a new flat string is pure ASCII, sparing the count. */
void glow_str_mark_ascii(Value v) {
    GCObject *o = (GCObject*)(uintptr_t)(v & MASK_PAYLOAD);
    if (o->type == OBJ_STRING) ((GCString*)o)->nchars1 = ((GCString*)o)->size + 1;
}

/* The byte offset where character char_index starts (the byte length for
   an index at or past the end). */
size_t glow_str_byte_offset(Value v, int64_t char_index) {
    if (GET_TYPE(v) != VAL_STRING || !(v & MASK_PAYLOAD) || char_index <= 0) return 0;
    GCString *g = str_flat_object(v);
    size_t n = (size_t)g->size;
    if (glow_str_len(v) == (int64_t)n) return (size_t)char_index < n ? (size_t)char_index : n;
    /* Continue from the last position looked up in this string when it is
       not past this one, so walking a string with s[i] is not quadratic. */
    int64_t k = 0;
    size_t i = 0;
    if (str_cursor.g == g && str_cursor.ch <= char_index) { k = str_cursor.ch; i = str_cursor.byte; }
    for (; k < char_index && i < n; k++) i = glow_utf8_step(g->chars, i, n);
    str_cursor.g = g; str_cursor.ch = k; str_cursor.byte = i;
    return i;
}

/* The collector frees strings; forget the cursor so it never refers to a
   string reallocated at the same address. */
void glow_str_cursor_reset(void) { str_cursor.g = NULL; }

/* The character index of byte offset byte_offset. */
int64_t glow_str_char_index(Value v, size_t byte_offset) {
    if (GET_TYPE(v) != VAL_STRING || !(v & MASK_PAYLOAD)) return 0;
    GCString *g = str_flat_object(v);
    size_t n = (size_t)g->size;
    if (byte_offset > n) byte_offset = n;
    if (glow_str_len(v) == (int64_t)n) return (int64_t)byte_offset;
    return (int64_t)glow_utf8_count(g->chars, byte_offset);
}

/* Character char_index of v (0 <= char_index < length) as a string. */
Value glow_str_char_at(Value v, int64_t char_index) {
    GCString *g = str_flat_object(v);
    size_t n = (size_t)g->size;
    size_t a = glow_str_byte_offset(v, char_index);
    if (a >= n) return value_string_copy("");
    g = str_flat_object(v);   /* value_string_copy_len may collect; v keeps g alive */
    return value_string_copy_len(g->chars + a, glow_utf8_step(g->chars, a, n) - a);
}

/* v's character count, if it is known or cheap to get (a short string). */
static int64_t str_chars_if_cheap(Value v) {
    if (!(v & MASK_PAYLOAD)) return 0;
    GCObject *o = (GCObject*)(uintptr_t)(v & MASK_PAYLOAD);
    if (o->type == OBJ_ROPE) return ((RopeNode*)o)->nchars1 - 1;
    GCString *g = (GCString*)o;
    if (!g->nchars1 && g->size <= 256) g->nchars1 = (int)glow_utf8_count(g->chars, (size_t)g->size) + 1;
    return g->nchars1 - 1;
}

Value rope_new(Value left, Value right) {
    int64_t cl = str_chars_if_cheap(left), cr = str_chars_if_cheap(right);
    RopeNode *rn = gc_alloc(sizeof(RopeNode), OBJ_ROPE);
    /* Counted now so len() of a long concatenation needs no flattening. */
    if (cl >= 0 && cr >= 0) rn->nchars1 = cl + cr + 1;
    rn->length = rope_length(left) + rope_length(right);
    rn->is_leaf = false;
    rn->internal.left = (RopeNode*)(uintptr_t)(left & MASK_PAYLOAD);
    rn->internal.right = (RopeNode*)(uintptr_t)(right & MASK_PAYLOAD);
    return make_value(VAL_STRING, (uint64_t)(uintptr_t)rn);
}

static void rope_flatten_recursive(RopeNode *rn, char *dest, size_t *offset) {
    if (rn->obj.type == OBJ_STRING) {
        GCString *gs = (GCString*)rn;
        /* Use the stored size (O(1)) instead of strlen (O(n)); memcpy the exact
           span rather than strcpy scanning for the NUL. The final terminator is
           written once by rope_flatten. */
        memcpy(dest + *offset, gs->chars, (size_t)gs->size);
        *offset += (size_t)gs->size;
    } else if (rn->obj.type == OBJ_ROPE) {
        if (rn->is_leaf) {
            GCString *gs = (GCString*)(uintptr_t)(rn->leaf.flat_string & MASK_PAYLOAD);
            /* Copy exactly rn->length bytes of the view. strcpy would run to the
               source NUL, which for a substring view can exceed rn->length and
               overflow the destination buffer. */
            memcpy(dest + *offset, gs->chars + rn->leaf.offset, rn->length);
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
        if (rn->is_leaf && rn->leaf.offset == 0) {
            GCString *gs = (GCString*)(uintptr_t)(rn->leaf.flat_string & MASK_PAYLOAD);
            if ((size_t)gs->size == rn->length) return gs->chars;
        }
        /* Flatten straight into the final string and keep it on the node, so
           the rope is copied once however often it is read. */
        GCString *gs = gc_alloc(sizeof(GCString) + rn->length + 1, OBJ_STRING);
        gs->size = (int)rn->length;
        size_t offset = 0;
        rope_flatten_recursive(rn, gs->chars, &offset);
        gs->chars[rn->length] = '\0';
        rn->is_leaf = true;
        rn->leaf.flat_string = make_value(VAL_STRING, (uint64_t)(uintptr_t)gs);
        rn->leaf.offset = 0;
        return gs->chars;
    }
    return "";
}

/* The bytes of a string value without copying, or of a small int formatted
   into buf. Returns 0 for anything else (including unflattened ropes). */
static int concat_piece(Value v, char *buf, const char **p, size_t *len) {
    if (GET_TYPE(v) == VAL_STRING && (v & MASK_PAYLOAD)) {
        GCObject *o = (GCObject*)(uintptr_t)(v & MASK_PAYLOAD);
        if (o->type == OBJ_STRING) { GCString *gs = (GCString*)o; *p = gs->chars; *len = (size_t)gs->size; return 1; }
        RopeNode *rn = (RopeNode*)o;
        if (o->type == OBJ_ROPE && rn->is_leaf) {
            GCString *gs = (GCString*)(uintptr_t)(rn->leaf.flat_string & MASK_PAYLOAD);
            *p = gs->chars + rn->leaf.offset; *len = rn->length;
            return 1;
        }
        return 0;
    }
    if (buf && GET_TYPE(v) == VAL_INT) {
        int64_t x = AS_INT(v);
        uint64_t u = x < 0 ? (uint64_t)0 - (uint64_t)x : (uint64_t)x;
        char tmp[24]; int n = 0;
        do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
        int k = 0;
        if (x < 0) buf[k++] = '-';
        while (n) buf[k++] = tmp[--n];
        *p = buf; *len = (size_t)k;
        return 1;
    }
    return 0;
}

/* `a + b` where either side is a string. Short results are built as one flat
   string: one allocation and nothing to flatten later. Longer results become
   a rope node, so a string grown by repeated appends is copied once, not on
   every append. */
#define CONCAT_FLAT_MAX 256
Value glow_string_concat(Value a, Value b) {
    char abuf[24], bbuf[24];
    const char *pa, *pb;
    size_t la, lb;
    if (!concat_piece(a, abuf, &pa, &la)) {
        a = value_to_string_value(a);
        if (!concat_piece(a, NULL, &pa, &la)) goto rope;
    }
    if (!concat_piece(b, bbuf, &pb, &lb)) {
        b = value_to_string_value(b);
        if (!concat_piece(b, NULL, &pb, &lb)) goto rope;
        /* Converting b may have allocated; a's bytes did not move (the
           collector never moves objects) and a is still referenced here. */
    }
    if (la + lb <= CONCAT_FLAT_MAX) {
        GCString *gs = gc_alloc(sizeof(GCString) + la + lb + 1, OBJ_STRING);
        gs->size = (int)(la + lb);
        memcpy(gs->chars, pa, la);
        memcpy(gs->chars + la, pb, lb);
        gs->chars[la + lb] = '\0';
        return make_value(VAL_STRING, (uint64_t)(uintptr_t)gs);
    }
rope:
    a = value_to_string_value(a);
    b = value_to_string_value(b);
    return rope_new(a, b);
}

static uint32_t hash_string(const char *s);

/* Insert an existing slot into the open-addressed name->slot index.
   index_cap must be a power of two with a free bucket available. */
static void global_index_insert_slot(int slot) {
    uint32_t mask = (uint32_t)(global_table.index_cap - 1);
    uint32_t h = hash_string(global_table.names[slot]) & mask;
    while (global_table.index[h] != -1) h = (h + 1) & mask;
    global_table.index[h] = slot;
}

/* (Re)allocate the index to new_cap buckets and reinsert every live slot. */
static void global_index_rebuild(int new_cap) {
    global_table.index = realloc(global_table.index, sizeof(int) * new_cap);
    global_table.index_cap = new_cap;
    for (int i = 0; i < new_cap; i++) global_table.index[i] = -1;
    for (int s = 0; s < global_table.count; s++) {
        if (global_table.names[s]) global_index_insert_slot(s);
    }
}

/* Which global slots have actually been declared (assigned at module level,
   defined as a function, or registered as a builtin), as opposed to merely
   referenced. The compiler uses this to decide whether an assignment inside a
   function targets a global or creates a new local. */
static unsigned char *global_declared = NULL;
static int global_declared_cap = 0;

void global_mark_declared(int slot) {
    if (slot < 0) return;
    if (slot >= global_declared_cap) {
        int ncap = global_declared_cap ? global_declared_cap : 1024;
        while (ncap <= slot) ncap *= 2;
        global_declared = realloc(global_declared, (size_t)ncap);
        memset(global_declared + global_declared_cap, 0, (size_t)(ncap - global_declared_cap));
        global_declared_cap = ncap;
    }
    global_declared[slot] = 1;
}

/* Look a global up without creating its slot. */
int global_find_slot(const char *name) {
    if (!name || global_table.index_cap == 0) return -1;
    uint32_t mask = (uint32_t)(global_table.index_cap - 1);
    uint32_t h = hash_string(name) & mask;
    while (global_table.index[h] != -1) {
        int s = global_table.index[h];
        if (global_table.names[s] && strcmp(global_table.names[s], name) == 0) return s;
        h = (h + 1) & mask;
    }
    return -1;
}

int global_slot_declared(int slot) {
    return slot >= 0 && slot < global_declared_cap && global_declared[slot];
}

int global_is_declared(const char *name) {
    int slot = global_find_slot(name);
    return slot >= 0 && slot < global_declared_cap && global_declared[slot];
}

int global_get_slot(const char *name) {
    if (!name) return -1;

    // Keep the index at <0.75 load factor. Slot indices are stable, so growing
    // the parallel names/values arrays never requires a rehash; only growth in
    // the number of entries does.
    if (global_table.index_cap == 0 ||
        (global_table.count + 1) * 4 >= global_table.index_cap * 3) {
        global_index_rebuild(global_table.index_cap ? global_table.index_cap * 2 : 2048);
    }

    uint32_t mask = (uint32_t)(global_table.index_cap - 1);
    uint32_t h = hash_string(name) & mask;
    while (global_table.index[h] != -1) {
        int s = global_table.index[h];
        // strcmp-confirm preserves the original content-equality semantics.
        if (global_table.names[s] && strcmp(global_table.names[s], name) == 0) return s;
        h = (h + 1) & mask;
    }
    // Not found — `h` is the empty bucket where the new slot belongs.

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
    /* Interned (permanently): callers may pass a dict key or other string
       that does not outlive this table. */
    global_table.names[slot] = intern_string(name);
    global_table.index[h] = slot;

    // Check if this is a builtin or already defined function
    FuncEntry *fe = func_get(name);
    if (fe) global_table.values[slot] = bound_method_new(value_none(), fe);
    else global_table.values[slot] = value_none();

    return slot;
}

void var_set(const char *name, Value value) {
    int slot = global_get_slot(name);
    if (slot >= 0) { global_table.values[slot] = value; global_mark_declared(slot); }
}

Value var_get(const char *name) {
    int slot = global_get_slot(name);
    if (slot >= 0) return global_table.values[slot];
    return value_none();
}

void var_table_init(void) {
    // No longer needed
}

static unsigned int hash_str(const char *s) {
    /* FNV-1a: portable (the previous CRC32 version required SSE4.2). */
    unsigned int h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

 void func_table_init(void) {
     func_table = calloc(func_table_size, sizeof(FuncEntry));
     func_table_capacity = func_table_size;
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
            /* Use the O(1) hashed global index instead of linear scan. */
            int gslot = global_get_slot(name);
            if (gslot >= 0) {
                global_table.values[gslot] = bound_method_new(value_none(), &func_table[idx]);
                global_mark_declared(gslot);
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
    /* Runs on every compiled call. Vectorised (as clang would), it makes each
       caller save and restore the SSE registers, which costs more than the
       loop over a few arguments. */
#if defined(__clang__)
#pragma clang loop vectorize(disable) interleave(disable)
#endif
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

/* A dict's keys, values and index live in one block: cap keys, cap values,
   then 2*cap int32 index slots. Small dicts keep that block inside the Dict
   object itself (DICT_INLINE_CAP entries), so `{}` or a two-field instance
   costs one GC allocation and no malloc. */
#define DICT_INLINE_CAP 4
#define DICT_BLOCK_SIZE(cap) ((size_t)(cap) * 2 * sizeof(Value) + (size_t)(cap) * 2 * sizeof(int32_t))

static void dict_set_block(Dict *d, void *block, int cap) {
    if (block != (void*)(d + 1)) gc_note_external_storage(&d->obj);
    d->keys = (Value*)block;
    d->values = d->keys + cap;
    d->index = (int32_t*)(d->values + cap);
    d->capacity = cap;
    d->index_cap = cap * 2;
    for (int i=0; i<cap; i++) d->keys[i] = value_none();
    memset(d->values, 0, sizeof(Value) * (size_t)cap);
    memset(d->index, 0xff, sizeof(int32_t) * (size_t)d->index_cap); /* DICT_EMPTY */
}

static int dict_is_inline(Dict *d) { return (void*)d->keys == (void*)(d + 1); }

/* Free a dict's storage block unless it lives inside the Dict. */
void dict_free_storage(Dict *d) {
    if (d->keys && !dict_is_inline(d)) free(d->keys);
    d->keys = NULL; d->values = NULL; d->index = NULL;
}

/* Fill a freshly allocated Dict with room for about sz entries. */
void dict_init(Dict *d, int sz) {
    int cap = 8; while (cap < sz) cap *= 2;
    d->size = 0; d->used = 0;
    void *block = malloc(DICT_BLOCK_SIZE(cap));
    if (!block) { fprintf(stderr, "Fatal: out of memory\n"); abort(); }
    dict_set_block(d, block, cap);
}

/* A new dict for about sz entries; small ones carry their storage inline. */
Dict* dict_alloc(int sz, int stack) {
    if (sz <= DICT_INLINE_CAP) {
        size_t bytes = sizeof(Dict) + DICT_BLOCK_SIZE(DICT_INLINE_CAP);
        Dict *d = stack ? gc_alloc_stack(bytes, OBJ_DICT) : gc_alloc(bytes, OBJ_DICT);
        d->size = 0; d->used = 0;
        dict_set_block(d, d + 1, DICT_INLINE_CAP);
        return d;
    }
    Dict *d = stack ? gc_alloc_stack(sizeof(Dict), OBJ_DICT) : gc_alloc(sizeof(Dict), OBJ_DICT);
    dict_init(d, sz);
    return d;
}

Dict* dict_new(int sz) { return dict_alloc(sz, 0); }

#define DICT_KEY_PTR(d, e) ((const char*)(uintptr_t)((d)->keys[e] & MASK_PAYLOAD))

/* The index slot holding interned key k (hash h), or -1. */
static int dict_find_slot(Dict *d, const char *k, uint32_t h) {
    uint32_t mask = (uint32_t)(d->index_cap - 1);
    uint32_t s = h & mask;
    for (;;) {
        int32_t e = d->index[s];
        if (e == DICT_EMPTY) return -1;
        if (e >= 0 && DICT_KEY_PTR(d, e) == k) return (int)s;
        s = (s + 1) & mask;
    }
}

/* Put entry e (key hash h) into the index. */
static void dict_index_insert(Dict *d, int e, uint32_t h) {
    uint32_t mask = (uint32_t)(d->index_cap - 1);
    uint32_t s = h & mask;
    while (d->index[s] >= 0) s = (s + 1) & mask;
    d->index[s] = e;
}

/* Make room for one more entry: drop removed entries (keeping order),
   grow if the dict is more than half full, and rebuild the index. */
static void dict_grow(Dict *d) {
    int cap = d->capacity;
    if ((d->size + 1) * 2 > cap) cap *= 2;
    Value *old_keys = d->keys, *old_values = d->values;
    int old_used = d->used, was_inline = dict_is_inline(d);
    void *block;
    Value saved[2 * DICT_INLINE_CAP];
    if (cap == d->capacity && was_inline) {
        /* Compacting in place: copy the live entries out first. */
        memcpy(saved, old_keys, sizeof(Value) * (size_t)old_used);
        memcpy(saved + DICT_INLINE_CAP, old_values, sizeof(Value) * (size_t)old_used);
        old_keys = saved; old_values = saved + DICT_INLINE_CAP;
        block = d + 1;
    } else {
        block = malloc(DICT_BLOCK_SIZE(cap));
        if (!block) { fprintf(stderr, "Fatal: out of memory\n"); abort(); }
    }
    dict_set_block(d, block, cap);
    int n = 0;
    for (int i=0; i<old_used; i++) {
        if (GET_TYPE(old_keys[i]) == VAL_NONE) continue;
        d->keys[n] = old_keys[i]; d->values[n] = old_values[i]; n++;
    }
    if (!was_inline) free(old_keys);
    d->used = n; d->size = n;
    for (int e=0; e<n; e++) dict_index_insert(d, e, intern_cached_hash(DICT_KEY_PTR(d, e)));
}

static void dict_set_interned(Dict *d, const char *interned, uint32_t h, Value val);
void dict_set(Dict *d, const char *key, Value val) {
    if (!key) return;
    uint32_t h;
    const char *interned = intern_string_hash(key, &h);
    dict_set_interned(d, interned, h, val);
}

static void dict_set_interned(Dict *d, const char *interned, uint32_t h, Value val) {
    gc_write_barrier((GCObject*)d, val);
    int s = dict_find_slot(d, interned, h);
    if (s >= 0) { d->values[d->index[s]] = val; return; }
    if (d->used >= d->capacity) dict_grow(d);
    int e = d->used++;
    d->keys[e] = make_value(VAL_STRING, (uintptr_t)interned);
    d->values[e] = val;
    d->size++;
    dict_index_insert(d, e, h);
}

/* inst.n = v for OP_SET_PROPERTY, shared by the interpreter and compiled
   code. pc->cache[0] remembers the entry number the field had last time
   (fields of one class usually sit at the same entry in every instance);
   pc->cache[1].key holds the interned name, looked up once, so adding a
   field to a new object hashes nothing. */
void instance_set_cached(Instance *inst, const char *n, Value v, Instruction *pc) {
    Dict *d = inst->properties;
    intptr_t idx = pc ? pc->cache[0].val - 1 : -1;
    if (idx >= 0 && idx < d->capacity && GET_TYPE(d->keys[idx]) == VAL_STRING &&
        pc->cache[0].key == (void*)(uintptr_t)(d->keys[idx] & MASK_PAYLOAD)) {
        d->values[idx] = v;
        gc_write_barrier((GCObject*)inst, v);
        return;
    }
    if (!pc || !n) { instance_set(inst, n, v); return; }
    if (!pc->cache[1].key || pc->cache[1].val != (intptr_t)n) {
        pc->cache[1].key = (void*)intern_string(n);
        pc->cache[1].val = (intptr_t)n;
    }
    const char *k = (const char*)pc->cache[1].key;
    uint32_t h = intern_cached_hash(k);
    int s = d->capacity ? dict_find_slot(d, k, h) : -1;
    int e;
    if (s >= 0) {
        e = d->index[s];
        d->values[e] = v;
    } else {
        if (d->used >= d->capacity) dict_grow(d);
        e = d->used++;
        d->keys[e] = make_value(VAL_STRING, (uintptr_t)k);
        d->values[e] = v;
        d->size++;
        dict_index_insert(d, e, h);
        if (d->size > inst->klass->field_hint) inst->klass->field_hint = d->size;
    }
    pc->cache[0].val = e + 1;
    pc->cache[0].key = (void*)k;
    gc_write_barrier((GCObject*)inst, v);
}

/* coll["name"] and coll["name"] = v from compiled code, where the key is a
   string literal: ikey is that literal, interned when the code was compiled,
   so nothing is hashed or compared here. Anything that is not a dict, and a
   missing key, goes the general way. */
Value glow_index_key_jit(Value coll, Value v_idx, const char *ikey) {
    if (GET_TYPE(coll) != VAL_DICT) return glow_index_jit(coll, v_idx);
    Dict *d = AS_DICT(coll);
    if (d->capacity) {
        int s = dict_find_slot(d, ikey, intern_cached_hash(ikey));
        if (s >= 0) {
            Value v = d->values[d->index[s]];
            if (GET_TYPE(v) != VAL_NONE) return v;
        }
    }
    return VAL_DEOPT_SENTINEL;
}

Value glow_store_index_key_jit(Value coll, Value v_idx, Value v_val, const char *ikey) {
    if (GET_TYPE(coll) != VAL_DICT) return glow_store_index_jit(coll, v_idx, v_val);
    dict_set_interned(AS_DICT(coll), ikey, intern_cached_hash(ikey), v_val);
    return value_none();
}

Value dict_get(Dict *d, const char *key) {
    int e = dict_get_idx(d, key);
    return e >= 0 ? d->values[e] : value_none();
}

/* The entry number of key (stable until the dict next grows), or -1. */
int dict_get_idx(Dict *d, const char *key) {
    if (!d || d->capacity == 0 || !key) return -1;
    uint32_t h;
    const char *interned = intern_string_find(key, &h);
    if (!interned) return -1;
    int s = dict_find_slot(d, interned, h);
    return s >= 0 ? d->index[s] : -1;
}

/* Remove key; returns 1 and its value in *out if it was there. */
int dict_remove(Dict *d, const char *key, Value *out) {
    if (!d || d->capacity == 0 || !key) return 0;
    uint32_t h;
    const char *interned = intern_string_find(key, &h);
    if (!interned) return 0;
    int s = dict_find_slot(d, interned, h);
    if (s < 0) return 0;
    int e = d->index[s];
    if (out) *out = d->values[e];
    d->index[s] = DICT_REMOVED;
    d->keys[e] = value_none();
    d->values[e] = value_none();
    d->size--;
    return 1;
}
/* Lists of up to LIST_INLINE_CAP items keep them inside the List object,
   so a small list costs one GC allocation and no malloc; the first push
   past that moves them to a malloc'd array. */
#define LIST_INLINE_CAP 4
static int list_is_inline(List *l) { return (void*)l->items == (void*)(l + 1); }

List* list_alloc(int sz, int stack) {
    if (sz < 0) sz = 0;
    List *l;
    if (sz <= LIST_INLINE_CAP) {
        size_t bytes = sizeof(List) + sizeof(Value) * LIST_INLINE_CAP;
        l = stack ? gc_alloc_stack(bytes, OBJ_LIST) : gc_alloc(bytes, OBJ_LIST);
        l->items = (Value*)(l + 1);
        l->capacity = LIST_INLINE_CAP;
    } else {
        l = stack ? gc_alloc_stack(sizeof(List), OBJ_LIST) : gc_alloc(sizeof(List), OBJ_LIST);
        l->items = calloc((size_t)sz, sizeof(Value));
        if (!l->items) { fprintf(stderr, "Fatal: out of memory\n"); abort(); }
        gc_note_external_storage(&l->obj);
        l->capacity = sz;
    }
    l->size = 0;
    return l;
}

List* list_new(int sz) { return list_alloc(sz, 0); }

/* Free a list's item array unless it lives inside the List. */
void list_free_storage(List *l) {
    if (l->items && !list_is_inline(l)) free(l->items);
    l->items = NULL;
}
void list_push(List *l, Value v) {
    gc_write_barrier((GCObject*)l, v);
    if (l->size >= l->capacity) {
        int cap = l->capacity < 4 ? 8 : l->capacity * 2;
        Value *items;
        if (list_is_inline(l) || !l->items) {
            items = malloc(sizeof(Value) * (size_t)cap);
            if (items && l->size) memcpy(items, l->items, sizeof(Value) * (size_t)l->size);
            gc_note_external_storage(&l->obj);
        } else {
            items = realloc(l->items, sizeof(Value) * (size_t)cap);
        }
        if (!items) { fprintf(stderr, "Fatal: out of memory\n"); abort(); }
        l->items = items; l->capacity = cap;
    }
    l->items[l->size++] = v;
}

Value list_pop(List *l) {
    if (!l || l->size <= 0) return value_none();
    return l->items[--l->size];
}

Value native_list_append(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_LIST) return value_none();
    list_push(AS_LIST(args[0]), args[1]);
    return args[0];
}

Value native_list_pop(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_LIST) return value_none();
    return list_pop(AS_LIST(args[0]));
}

Value native_list_len(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_LIST) return value_int(0);
    return value_int(AS_LIST(args[0])->size);
}

Value native_val_type(int argc, Value *args) {
    if (argc < 1) return value_string_copy("none");
    Value arg = args[0];
    return native_type(1, &arg);
}

#define TENSOR_POOL_MAX_BINS 32
typedef struct FreeBlock {
    struct FreeBlock *next;
} FreeBlock;

static FreeBlock *tensor_pool[TENSOR_POOL_MAX_BINS];
static pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;

static int get_bin(size_t size) {
    int bin = 0;
    size_t s = 32;
    while (s < size && bin < TENSOR_POOL_MAX_BINS - 1) {
        s *= 2;
        bin++;
    }
    return bin;
}

void* tensor_malloc(size_t size) {
    if (size == 0) return NULL;
    int bin = get_bin(size);
    pthread_mutex_lock(&pool_mutex);
    if (tensor_pool[bin]) {
        void *ptr = tensor_pool[bin];
        tensor_pool[bin] = tensor_pool[bin]->next;
        pthread_mutex_unlock(&pool_mutex);
        return ptr;
    }
    pthread_mutex_unlock(&pool_mutex);
    size_t alloc_size = 32;
    for (int i = 0; i < bin; i++) alloc_size *= 2;
    void *ptr = NULL;
    if (posix_memalign(&ptr, 32, alloc_size) != 0) return NULL;
    return ptr;
}

#ifdef GLOW_USE_CUDA
extern void* cuda_tensor_malloc(size_t size);
extern void cuda_tensor_free(void* ptr);
extern void cuda_tensor_to_device(void* dst, const void* src, size_t size);
extern void cuda_tensor_to_host(void* dst, const void* src, size_t size);
extern void cuda_tensor_matmul(const double* A, int rA, int cA, const double* B, int cB, double* C);
#endif

void tensor_free(void *ptr, size_t size, const char *device) {
    if (!ptr || size == 0) return;
#ifdef GLOW_USE_CUDA
    if (strcmp(device, "cuda") == 0) {
        cuda_tensor_free(ptr);
        return;
    }
#endif
    int bin = get_bin(size);
    pthread_mutex_lock(&pool_mutex);
    FreeBlock *block = (FreeBlock*)ptr;
    block->next = tensor_pool[bin];
    tensor_pool[bin] = block;
    pthread_mutex_unlock(&pool_mutex);
}

/* Internal constructor. When zero_init is 0 the element buffer is left
   UNINITIALIZED (it comes from the tensor pool and may hold stale bytes), so
   callers MUST fully overwrite every element before any read. Use the uninit
   path only for result buffers proven to write all `capacity` elements. */
static PackedArray* packed_array_new_ex(PackedType type, int capacity, int zero_init) {
    PackedArray *pa = gc_alloc(sizeof(PackedArray), OBJ_PACKED_ARRAY);
    pa->type = type;
    size_t elem_sz = (type == PACKED_INT64) ? sizeof(int64_t) : sizeof(double);
    if (capacity > 0) {
        pa->data = tensor_malloc(capacity * elem_sz);
        if (pa->data) {
            pa->size = capacity;
            pa->capacity = capacity;
            if (zero_init) memset(pa->data, 0, capacity * elem_sz);
        } else {
            pa->size = 0;
            pa->capacity = 0;
        }
    } else {
        pa->data = NULL;
        pa->size = 0;
        pa->capacity = 0;
    }
    pa->ndims = 1;
    pa->shape[0] = capacity;
    pa->strides[0] = 1;
    pa->offset = 0;
    pa->is_view = false;
    pa->base = NULL;
    strcpy(pa->device, "cpu");
    return pa;
}

PackedArray* packed_array_new(PackedType type, int capacity) {
    return packed_array_new_ex(type, capacity, 1);
}

/* Result buffer that will be fully overwritten by the caller — skips the
   O(n) zeroing pass. Only for sites proven to write every element. */
static PackedArray* packed_array_new_uninit(PackedType type, int capacity) {
    return packed_array_new_ex(type, capacity, 0);
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
    int cap = DICT_INLINE_CAP;
    while (cap < k->field_hint) cap *= 2;
    Instance *i = gc_alloc(sizeof(Instance) + DICT_BLOCK_SIZE(cap), OBJ_INSTANCE);
    i->klass = k;
    i->properties = &i->fields;
    i->fields.obj.type = OBJ_DICT;
    i->fields.obj.embedded = 1;
    i->fields.obj.block_ptr = i;
    i->fields.size = 0; i->fields.used = 0;
    dict_set_block(&i->fields, &i->fields + 1, cap);
    k->instances_created++;
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

unsigned glow_method_epoch = 1;

/* The class's init method (or none), looked up once per class instead of on
   every construction. Methods are only added while classes and traits are
   defined, and each addition bumps glow_method_epoch. The cached value is
   also stored in the class's methods dict, so the GC keeps it alive. */
Value class_init_method(Class *k) {
    if (k->init_epoch != glow_method_epoch) {
        k->init_method = class_get_method(k, "init");
        k->init_epoch = glow_method_epoch;
    }
    return k->init_method;
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
    if (inst->properties->size > inst->klass->field_hint) inst->klass->field_hint = inst->properties->size;
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
THREAD_LOCAL NativeFunc *glow_current_native = NULL;
Value native_func_new(const char *name, NativeFuncPtr ptr) { return native_func_new_data(name, ptr, NULL, NULL); }
Value native_func_new_data(const char *name, NativeFuncPtr ptr, void *data, void (*finalize)(void *data)) {
    NativeFunc *nf = gc_alloc(sizeof(NativeFunc), OBJ_NATIVE_FUNC);
    nf->name = name; nf->func = ptr; nf->data = data; nf->finalize = finalize;
    return value_native_func(nf);
}


/* Render an integer-valued double without exponent/decimal so integers that
   were promoted out of the 48-bit inline range still print like integers. */
void format_double(char *buf, size_t n, double d) {
    if (isnan(d)) { snprintf(buf, n, "nan"); return; }
    if (isinf(d)) { snprintf(buf, n, d < 0 ? "-inf" : "inf"); return; }
    if (d == floor(d) && fabs(d) < 1e16) { snprintf(buf, n, "%.0f", d); return; }
    /* Shortest text that reads back as the same double (what Python and
       JavaScript print). Plain %g kept only 6 significant digits, so e.g. a
       timestamp printed as 1.79297e+09. */
    for (int prec = 1; prec <= 17; prec++) {
        snprintf(buf, n, "%.*g", prec, d);
        if (strtod(buf, NULL) == d) return;
    }
}

/* Growable output buffer used to render values as text. */
typedef struct { char *p; size_t n; size_t cap; } DStr;

static void ds_putn(DStr *d, const char *s, size_t len) {
    if (d->n + len + 1 > d->cap) {
        size_t ncap = d->cap ? d->cap : 64;
        while (d->n + len + 1 > ncap) ncap *= 2;
        d->p = realloc(d->p, ncap);
        d->cap = ncap;
    }
    memcpy(d->p + d->n, s, len);
    d->n += len;
    d->p[d->n] = '\0';
}
static void ds_puts(DStr *d, const char *s) { ds_putn(d, s, strlen(s)); }

static void ds_value(DStr *d, Value v, int depth) {
    char nb[64];
    if (depth > 64) { ds_puts(d, "..."); return; }
    /* All-zero bits are the float +0.0 (none has its own tag); printing them
       as "none" made `show 0.0` and tensor.dot of zeros print none. */
    if (IS_FLOAT_BITS(v)) { format_double(nb, sizeof(nb), AS_FLOAT(v)); ds_puts(d, nb); return; }
    uint64_t type = (v & MASK_TYPE) >> 48;
    if (type == VAL_INT) { snprintf(nb, sizeof(nb), "%lld", (long long)AS_INT(v)); ds_puts(d, nb); }
    else if (type == VAL_BOOL) ds_puts(d, AS_BOOL(v) ? "true" : "false");
    else if (type == VAL_STRING) { if (v & MASK_PAYLOAD) ds_puts(d, AS_STRING(v)); }
    else if (type == VAL_LIST) {
        List *l = AS_LIST(v);
        ds_puts(d, "[");
        for (int i = 0; i < l->size; i++) { ds_value(d, l->items[i], depth + 1); if (i < l->size - 1) ds_puts(d, ", "); }
        ds_puts(d, "]");
    }
    else if (type == VAL_DICT) {
        Dict *dict = AS_DICT(v);
        ds_puts(d, "{");
        int printed = 0;
        for (int i = 0; i < dict->capacity; i++) {
            if (GET_TYPE(dict->keys[i]) != VAL_NONE) {
                if (printed > 0) ds_puts(d, ", ");
                ds_puts(d, "\"");
                ds_puts(d, (const char*)(uintptr_t)(dict->keys[i] & MASK_PAYLOAD));
                ds_puts(d, "\": ");
                ds_value(d, dict->values[i], depth + 1);
                printed++;
            }
        }
        ds_puts(d, "}");
    }
    else if (type == VAL_OBJECT) {
        if (!(v & MASK_PAYLOAD)) { ds_puts(d, "none"); return; }
        ObjType ot = GET_OBJ_TYPE(v);
        if (ot == OBJ_CLASS) { ds_puts(d, "<class "); ds_puts(d, AS_CLASS(v)->name); ds_puts(d, ">"); }
        else if (ot == OBJ_INSTANCE) { ds_puts(d, "<instance of "); ds_puts(d, AS_INSTANCE(v)->klass->name); ds_puts(d, ">"); }
        else if (ot == OBJ_PACKED_ARRAY) {
            PackedArray *pa = AS_PACKED_ARRAY(v);
            ds_puts(d, "[");
            for (int i = 0; i < pa->size; i++) {
                if (pa->type == PACKED_INT64) snprintf(nb, sizeof(nb), "%lld", (long long)((int64_t*)pa->data)[i]);
                else format_double(nb, sizeof(nb), ((double*)pa->data)[i]);
                ds_puts(d, nb);
                if (i < pa->size - 1) ds_puts(d, ", ");
            }
            ds_puts(d, "]");
        }
        else if (ot == OBJ_BIGINT) { char *bb = bigint_to_new_string(AS_BIGINT(v)); ds_puts(d, bb); free(bb); }
        else ds_puts(d, "<object>");
    } else ds_puts(d, "none");
}

/* Render any value as a newly malloc'd C string (caller frees). */
char *value_to_new_cstring(Value v) {
    DStr d = {0};
    ds_putn(&d, "", 0);
    ds_value(&d, v, 0);
    return d.p;
}

/* Render any value as a GlowLang string value. */
Value value_to_string_value(Value v) {
    if (GET_TYPE(v) == VAL_STRING && (v & MASK_PAYLOAD)) return v;
    if (GET_TYPE(v) == VAL_INT) {
        /* str(i) is common enough (keys, labels, f-strings) to skip the
           general formatter and its two allocations. */
        char buf[24], *p = buf + sizeof(buf);
        int64_t i = AS_INT(v);
        uint64_t u = i < 0 ? 0 - (uint64_t)i : (uint64_t)i;
        do { *--p = (char)('0' + u % 10); u /= 10; } while (u);
        if (i < 0) *--p = '-';
        return value_string_copy_len(p, (size_t)(buf + sizeof(buf) - p));
    }
    char *s = value_to_new_cstring(v);
    Value r = value_string_copy(s);
    free(s);
    return r;
}

/* pairs(coll): [index, item] for each element of a list or character of a
   string, [key, value] for each entry of a dict. Backs `for each a, b in x`. */
static Value native_pairs(int argc, Value *args) {
    if (argc < 1) return value_list(list_new(0));
    Value coll = args[0];
    List *out = list_new(8);
    Value out_v = value_list(out);
    if (GET_TYPE(coll) == VAL_LIST) {
        List *l = AS_LIST(coll);
        for (int i = 0; i < l->size; i++) {
            List *p = list_new(2); Value pv = value_list(p);
            list_push(p, value_int(i)); list_push(p, l->items[i]);
            list_push(out, pv);
        }
    } else if (GET_TYPE(coll) == VAL_DICT) {
        Dict *d = AS_DICT(coll);
        for (int i = 0; i < d->capacity; i++) {
            if (GET_TYPE(d->keys[i]) == VAL_NONE) continue;
            List *p = list_new(2); Value pv = value_list(p);
            list_push(p, value_string_copy((const char*)(uintptr_t)(d->keys[i] & MASK_PAYLOAD)));
            list_push(p, d->values[i]);
            list_push(out, pv);
        }
    } else if (GET_TYPE(coll) == VAL_STRING) {
        const char *s = AS_STRING(coll);
        size_t n = rope_length(coll);
        int64_t ci = 0;
        for (size_t i = 0; i < n; ci++) {
            size_t next = glow_utf8_step(s, i, n);
            List *p = list_new(2); Value pv = value_list(p);
            list_push(p, value_int(ci)); list_push(p, value_string_copy_len(s + i, next - i));
            list_push(out, pv);
            i = next;
            s = AS_STRING(coll);
        }
    } else {
        pending_exception = value_string_copy("pairs() needs a list, dict or string");
        has_exception = 1;
        return value_none();
    }
    return out_v;
}

/* What `for each a, b in coll` walks: the same pairs as pairs(coll), laid
   out flat in one list, [a0, b0, a1, b1, ...], so a pass of the loop builds
   no list of its own. Like pairs(), it is a snapshot of coll. */
static Value native_pairs_flat(int argc, Value *args) {
    if (argc < 1) return value_list(list_new(0));
    Value coll = args[0];
    if (GET_TYPE(coll) == VAL_LIST) {
        List *l = AS_LIST(coll);
        List *out = list_new(l->size * 2 + 2);
        for (int i = 0; i < l->size; i++) { list_push(out, value_int(i)); list_push(out, l->items[i]); }
        return value_list(out);
    }
    if (GET_TYPE(coll) == VAL_DICT) {
        Dict *d = AS_DICT(coll);
        List *out = list_new(d->size * 2 + 2);
        Value out_v = value_list(out);
        for (int i = 0; i < d->capacity; i++) {
            if (GET_TYPE(d->keys[i]) == VAL_NONE) continue;
            Value k = value_string_copy((const char*)(uintptr_t)(d->keys[i] & MASK_PAYLOAD));
            list_push(out, k);
            list_push(out, d->values[i]);
        }
        return out_v;
    }
    if (GET_TYPE(coll) == VAL_STRING) {
        List *out = list_new(8);
        Value out_v = value_list(out);
        const char *s = AS_STRING(coll);
        size_t n = rope_length(coll);
        int64_t ci = 0;
        for (size_t i = 0; i < n; ci++) {
            size_t next = glow_utf8_step(s, i, n);
            Value ch = value_string_copy_len(s + i, next - i);
            list_push(out, value_int(ci));
            list_push(out, ch);
            i = next;
            s = AS_STRING(coll);
        }
        return out_v;
    }
    pending_exception = value_string_copy("pairs() needs a list, dict or string");
    has_exception = 1;
    return value_none();
}

/* input(prompt) / ask(prompt): print the prompt, read one line from stdin
   (without its line ending). Returns none at end of input. */
static Value native_input(int argc, Value *args) {
    if (argc >= 1 && GET_TYPE(args[0]) != VAL_NONE) { print_value(args[0]); fflush(stdout); }
    size_t cap = 256, len = 0;
    char *buf = malloc(cap);
    int c, got = 0, newline = 0;
    glow_gil_release();
    while ((c = fgetc(stdin)) != EOF) {
        got = 1;
        if (c == '\n') { newline = 1; break; }
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = (char)c;
    }
    glow_gil_acquire();
    if (!got) { free(buf); return value_none(); }
    /* Drop a UTF-8 byte-order mark some tools put at the start of the stream. */
    static int first_line = 1;
    if (first_line && len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, len - 3); len -= 3;
    }
    first_line = 0;
    /* Only a byte-order mark before the end of input: no line was read. */
    if (len == 0 && !newline) { free(buf); return value_none(); }
    if (len > 0 && buf[len - 1] == '\r') len--;
    buf[len] = '\0';
    Value v = value_string_copy(buf);
    free(buf);
    return v;
}

static Value native_str(int argc, Value *args) {
    if (argc < 1) return value_string_copy("");
    return value_to_string_value(args[0]);
}

void print_value(Value v) {
    if (GET_TYPE(v) == VAL_STRING && (v & MASK_PAYLOAD)) { fputs(AS_STRING(v), stdout); return; }
    char *s = value_to_new_cstring(v);
    fputs(s, stdout);
    free(s);
}

GlowError *glow_errors = NULL; int glow_error_count = 0; static int glow_error_capacity = 0;
void glow_clear_errors(void) { for (int i = 0; i < glow_error_count; i++) if(glow_errors[i].message) free(glow_errors[i].message); glow_error_count = 0; }
void glow_add_error(int line, const char *msg) {
    if (glow_error_count >= glow_error_capacity) { glow_error_capacity = (glow_error_capacity == 0) ? 8 : glow_error_capacity * 2; glow_errors = realloc(glow_errors, sizeof(GlowError) * glow_error_capacity); }
    glow_errors[glow_error_count].line = line; glow_errors[glow_error_count].message = strdup(msg); glow_error_count++;
}

Value native_len(int argc, Value *args) {
    if (argc < 1) return value_int(0);
    Value v = args[0];
    if (GET_TYPE(v) == VAL_STRING) return value_int(glow_str_len(v));
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
    /* Keep our buffered output ahead of the child's, and let other GlowLang
       threads run while the command executes. */
    fflush(stdout); fflush(stderr);
    glow_gil_release();
    int status = system(cmd);
    glow_gil_acquire();
#ifndef _WIN32
    /* POSIX system() returns a wait status; report the exit code. */
    if (status != -1 && WIFEXITED(status)) status = WEXITSTATUS(status);
#endif
    return value_int(status);
}
#include <math.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

static Value math_sin(int argc, Value *args) { return argc > 0 ? value_from_double(sin(val_to_double(args[0]))) : value_none(); }
static Value math_cos(int argc, Value *args) { return argc > 0 ? value_from_double(cos(val_to_double(args[0]))) : value_none(); }
static Value math_sqrt(int argc, Value *args) { return argc > 0 ? value_from_double(sqrt(val_to_double(args[0]))) : value_none(); }
static Value math_pow(int argc, Value *args) { return argc > 1 ? value_from_double(pow(val_to_double(args[0]), val_to_double(args[1]))) : value_none(); }
/* floor/ceil/round go through value_from_double: casting a large result to
   int64 (math.floor(1e20)) was undefined behaviour. */
static Value math_floor(int argc, Value *args) { return argc > 0 ? value_from_double(floor(val_to_double(args[0]))) : value_none(); }
static Value math_ceil(int argc, Value *args)  { return argc > 0 ? value_from_double(ceil(val_to_double(args[0])))  : value_none(); }
static Value math_round(int argc, Value *args) { return argc > 0 ? value_from_double(round(val_to_double(args[0]))) : value_none(); }

/* More of math (corelib.c adds exp, log, the trigonometry and pi, e, inf). */
#define MATH_FN1(name, expr) static Value math_##name(int argc, Value *args) { \
    if (argc < 1) return value_none(); double x = val_to_double(args[0]); return value_from_double(expr); }
MATH_FN1(log2, log2(x))
MATH_FN1(sinh, sinh(x))
MATH_FN1(cosh, cosh(x))
MATH_FN1(tanh, tanh(x))
MATH_FN1(trunc, trunc(x))
#undef MATH_FN1
static Value math_isnan(int argc, Value *args) { return value_bool(argc > 0 && GET_TYPE(args[0]) == VAL_FLOAT && isnan(AS_FLOAT(args[0]))); }
static Value math_isinf(int argc, Value *args) { return value_bool(argc > 0 && GET_TYPE(args[0]) == VAL_FLOAT && isinf(AS_FLOAT(args[0]))); }
/* An integer value for i: inline up to 48 bits, an exact double up to 53,
   a big integer beyond. */
static Value int_value_from_int64(int64_t i) {
    if (fits_int48(i)) return value_int(i);
    if (i >= -INT53_MAX && i <= INT53_MAX) return value_float((double)i);
    return value_bigint(bigint_from_int64(i));
}

static Value int_error(const char *s, int base, int too_large) {
    char m[200];
    if (too_large) snprintf(m, sizeof(m), "Cannot convert \"%.100s\" to an int: too large for base %d", s, base);
    else if (base != 10) snprintf(m, sizeof(m), "Cannot convert \"%.100s\" to an int in base %d", s, base);
    else snprintf(m, sizeof(m), "Cannot convert \"%.100s\" to an int", s);
    pending_exception = value_string_copy(m); has_exception = 1;
    return value_none();
}

/* int(x) and int(text, base). Strings are parsed (they used to all give 0):
   optional spaces and sign, then digits in the base (10 unless given; base
   16 also takes a 0x prefix). Floats are truncated toward zero. */
static Value native_int(int argc, Value *args) {
    if (argc < 1) return value_int(0);
    Value v = args[0];
    if (GET_TYPE(v) == VAL_INT || (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD) && is_bigint(v))) return v;
    if (GET_TYPE(v) == VAL_BOOL) return value_int(AS_BOOL(v) ? 1 : 0);
    if (GET_TYPE(v) == VAL_FLOAT) {
        double d = AS_FLOAT(v);
        if (isnan(d) || isinf(d)) { pending_exception = value_string_copy("int() of nan or infinity"); has_exception = 1; return value_none(); }
        d = trunc(d);
        if (d >= -9.2e18 && d <= 9.2e18) return int_value_from_int64((int64_t)d);
        char buf[400];
        snprintf(buf, sizeof(buf), "%.0f", d);
        return value_bigint(bigint_from_string(buf));
    }
    if (GET_TYPE(v) != VAL_STRING) {
        pending_exception = value_string_copy("int() needs a number or a string"); has_exception = 1;
        return value_none();
    }
    const char *s = AS_STRING(v);
    int base = 10;
    if (argc > 1) {
        if (GET_TYPE(args[1]) != VAL_INT || AS_INT(args[1]) < 2 || AS_INT(args[1]) > 36) {
            pending_exception = value_string_copy("int(): the base must be from 2 to 36"); has_exception = 1;
            return value_none();
        }
        base = (int)AS_INT(args[1]);
    }
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    int neg = 0;
    if (*p == '+' || *p == '-') { neg = *p == '-'; p++; }
    if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    const char *digits = p;
    while (*p) {
        int c = tolower((unsigned char)*p), dv;
        if (c >= '0' && c <= '9') dv = c - '0';
        else if (c >= 'a' && c <= 'z') dv = c - 'a' + 10;
        else break;
        if (dv >= base) break;
        p++;
    }
    const char *after = p;
    while (isspace((unsigned char)*p)) p++;
    if (after == digits || *p) return int_error(s, base, 0);
    while (digits < after - 1 && *digits == '0') digits++;
    size_t n = (size_t)(after - digits);
    if (n <= 18 || base != 10) {
        char buf[80];
        if (n >= sizeof(buf)) return int_error(s, base, 1);
        memcpy(buf, digits, n); buf[n] = '\0';
        errno = 0;
        unsigned long long u = strtoull(buf, NULL, base);
        if (errno == ERANGE || u > (unsigned long long)INT64_MAX + (neg ? 1ULL : 0ULL)) return int_error(s, base, 1);
        int64_t i = neg ? (int64_t)(0ULL - u) : (int64_t)u;
        return int_value_from_int64(i);
    }
    char *buf = malloc(n + 2);
    buf[0] = '-';
    memcpy(buf + 1, digits, n); buf[n + 1] = '\0';
    Value out = value_bigint(bigint_from_string(neg ? buf : buf + 1));
    free(buf);
    return out;
}

static void hex_encode(const unsigned char *in, size_t n, char *out) {
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2]     = hex_chars[in[i] >> 4];
        out[i * 2 + 1] = hex_chars[in[i] & 0x0F];
    }
    out[n * 2] = '\0';
}

/* crypto.sha256(data): the hash as 64 hex digits. The whole string is hashed,
   including any zero bytes (strlen used to stop at the first one). */
static Value crypto_sha256(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_none();
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)AS_STRING(args[0]), rope_length(args[0]), hash);
    char out[SHA256_DIGEST_LENGTH * 2 + 1];
    hex_encode(hash, SHA256_DIGEST_LENGTH, out);
    return value_string_copy(out);
}

/* crypto.hmac(key, data): HMAC-SHA256 as 64 hex digits. */
static Value crypto_hmac_sha256(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_STRING || GET_TYPE(args[1]) != VAL_STRING) return value_none();
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    if (!HMAC(EVP_sha256(), AS_STRING(args[0]), (int)rope_length(args[0]),
              (const unsigned char*)AS_STRING(args[1]), rope_length(args[1]), md, &md_len)) {
        return value_none();
    }
    char out[EVP_MAX_MD_SIZE * 2 + 1];
    hex_encode(md, md_len, out);
    return value_string_copy(out);
}

static int random_count_arg(int argc, Value *args, const char *fn, int64_t *n) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT || AS_INT(args[0]) < 1 || AS_INT(args[0]) > 1048576) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: expected a number of bytes from 1 to 1048576", fn);
        pending_exception = value_string_copy(msg);
        has_exception = 1;
        return 0;
    }
    *n = AS_INT(args[0]);
    return 1;
}

/* crypto.random_bytes(n): n bytes from the operating system's secure random
   source (through OpenSSL), as a string of raw bytes. */
static Value crypto_random_bytes(int argc, Value *args) {
    int64_t n;
    if (!random_count_arg(argc, args, "crypto.random_bytes", &n)) return value_none();
    unsigned char *buf = malloc((size_t)n);
    if (!buf || RAND_bytes(buf, (int)n) != 1) {
        free(buf);
        pending_exception = value_string_copy("crypto.random_bytes: the secure random source failed");
        has_exception = 1;
        return value_none();
    }
    Value v = value_string_copy_len((const char*)buf, (size_t)n);
    OPENSSL_cleanse(buf, (size_t)n);
    free(buf);
    return v;
}

/* crypto.random_hex(n): n secure random bytes as 2n hex digits (keys, tokens). */
static Value crypto_random_hex(int argc, Value *args) {
    int64_t n;
    if (!random_count_arg(argc, args, "crypto.random_hex", &n)) return value_none();
    unsigned char *buf = malloc((size_t)n);
    char *out = malloc((size_t)n * 2 + 1);
    if (!buf || !out || RAND_bytes(buf, (int)n) != 1) {
        free(buf); free(out);
        pending_exception = value_string_copy("crypto.random_hex: the secure random source failed");
        has_exception = 1;
        return value_none();
    }
    hex_encode(buf, (size_t)n, out);
    Value v = value_string_copy(out);
    OPENSSL_cleanse(buf, (size_t)n);
    OPENSSL_cleanse(out, (size_t)n * 2);
    free(buf); free(out);
    return v;
}

/* crypto.equal(a, b): whether two strings are equal, taking the same time
   wherever they differ, so comparing a secret (a signature, a token) does not
   leak how much of it matched. */
static Value crypto_equal(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_STRING || GET_TYPE(args[1]) != VAL_STRING) return value_bool(0);
    size_t la = rope_length(args[0]), lb = rope_length(args[1]);
    const char *a = AS_STRING(args[0]), *b = AS_STRING(args[1]);
    if (la != lb) {
        /* Still do the work of a comparison, against a itself. */
        CRYPTO_memcmp(a, a, la);
        return value_bool(0);
    }
    return value_bool(la == 0 || CRYPTO_memcmp(a, b, la) == 0);
}

/* crypto.uuid4(): a random (version 4) UUID. */
static Value crypto_uuid4(int argc, Value *args) {
    (void)argc; (void)args;
    unsigned char b[16];
    if (RAND_bytes(b, 16) != 1) {
        pending_exception = value_string_copy("crypto.uuid4: the secure random source failed");
        has_exception = 1;
        return value_none();
    }
    b[6] = (unsigned char)((b[6] & 0x0F) | 0x40);
    b[8] = (unsigned char)((b[8] & 0x3F) | 0x80);
    char out[37];
    snprintf(out, sizeof(out), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return value_string_copy(out);
}

/* crypto.blake3(text): the 256-bit BLAKE3 hash as 64 hex digits. (This used
   to be a 64-bit FNV-style hash under the BLAKE3 name.) */
extern void glow_blake3(const void *input, size_t len, uint8_t *out, size_t out_len);
static Value native_crypto_blake_fast(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_none();
    uint8_t digest[32];
    glow_blake3(AS_STRING(args[0]), rope_length(args[0]), digest, sizeof(digest));
    char out[65];
    for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", digest[i]);
    return value_string_copy(out);
}

static Value native_tensor_dequant_int4(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_OBJECT || GET_OBJ_TYPE(args[0]) != OBJ_PACKED_ARRAY) return value_none();
    PackedArray *src = AS_PACKED_ARRAY(args[0]);
    int sz = src->size * 2;
    PackedArray *pa = packed_array_new(PACKED_FLOAT64, sz);
    if (pa->data && sz > 0 && src->data) {
        double *out = (double*)pa->data;
        /* Each element holds one byte (0-255) packing two 4-bit weights; the
           tensor stores doubles or int64s, not raw bytes. */
        for (int i = 0; i < src->size; i++) {
            uint8_t b = src->type == PACKED_FLOAT64 ? (uint8_t)(int64_t)((double*)src->data)[i]
                                                     : (uint8_t)((int64_t*)src->data)[i];
            out[i * 2] = (double)(b & 0x0F) - 8.0;
            out[i * 2 + 1] = (double)((b >> 4) & 0x0F) - 8.0;
        }
    }
    return value_packed(pa);
}

static Value native_fast_json_scan(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_none();
    const char *json = AS_STRING(args[0]);
    const char *k_start = strchr(json, '"');
    if (!k_start) return value_none();
    const char *k_end = strchr(k_start + 1, '"');
    if (!k_end) return value_none();
    int k_len = k_end - (k_start + 1);
    char key[128];
    if (k_len >= 128) k_len = 127;
    memcpy(key, k_start + 1, k_len);
    key[k_len] = '\0';
    const char *colon = strchr(k_end, ':');
    if (!colon) return value_none();
    const char *v_start = colon + 1;
    while (*v_start == ' ' || *v_start == '\t' || *v_start == '\n' || *v_start == '\r') v_start++;
    /* Only the first value: a quoted string without its quotes, otherwise
       the text up to the next ',' or '}'. */
    int v_len;
    if (*v_start == '"') {
        const char *e = v_start + 1;
        while (*e && *e != '"') { if (*e == '\\' && e[1]) e++; e++; }
        v_start++;
        v_len = (int)(e - v_start);
    } else {
        const char *e = v_start;
        while (*e && *e != ',' && *e != '}') e++;
        while (e > v_start && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) e--;
        v_len = (int)(e - v_start);
    }
    char val[128];
    if (v_len >= 128) v_len = 127;
    memcpy(val, v_start, v_len);
    val[v_len] = '\0';
    Dict *d = dict_new(2);
    dict_set(d, "key", value_string_copy(key));
    dict_set(d, "val", value_string_copy(val));
    return value_dict(d);
}

/* net_fast.poll_fast(socket, timeout_ms): true when the socket has data (or
   a connection) waiting, waiting up to timeout_ms (default 0). It used to
   return true without looking at anything. */
static Value native_net_poll_fast(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT || AS_INT(args[0]) < 0) return value_bool(0);
    int fd = (int)AS_INT(args[0]);
    double ms = argc >= 2 ? val_to_double(args[1]) : 0;
    if (!(ms > 0)) ms = 0;
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(fd, &readable);
    struct timeval tv;
    tv.tv_sec = (long)(ms / 1000);
    tv.tv_usec = (long)((ms - (double)tv.tv_sec * 1000) * 1000);
    glow_gil_release();
    int r = select(fd + 1, &readable, NULL, NULL, &tv);
    glow_gil_acquire();
    return value_bool(r > 0 && FD_ISSET(fd, &readable));
}

static Value native_media_png_dims(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_OBJECT || GET_OBJ_TYPE(args[0]) != OBJ_PACKED_ARRAY) return value_none();
    PackedArray *pa = AS_PACKED_ARRAY(args[0]);
    if (pa->size < 24 || !pa->data) return value_none();
    uint8_t b[24];
    if (pa->type == PACKED_FLOAT64) {
        double *db = (double*)pa->data;
        for (int i=0; i<24; i++) b[i] = (uint8_t)db[i];
    } else {
        memcpy(b, pa->data, 24);
    }
    if (b[0] != 0x89 || b[1] != 0x50 || b[2] != 0x4E || b[3] != 0x47) return value_none();
    uint32_t w = ((uint32_t)b[16] << 24) | ((uint32_t)b[17] << 16) | ((uint32_t)b[18] << 8) | b[19];
    uint32_t h = ((uint32_t)b[20] << 24) | ((uint32_t)b[21] << 16) | ((uint32_t)b[22] << 8) | b[23];
    Dict *d = dict_new(2);
    dict_set(d, "width", value_float(w));
    dict_set(d, "height", value_float(h));
    return value_dict(d);
}



/* ---- JSON (RFC 8259) ----
   stringify escapes strings and writes non-finite numbers and values JSON
   has no form for (functions, instances) as null. parse is strict: invalid
   input raises an error naming the line and column. Integers stay exact
   (big ones become big-ints); nesting deeper than JSON_MAX_DEPTH is refused,
   which also stops stringify on a list that contains itself. */
#define JSON_MAX_DEPTH 1000

typedef struct { char *p; size_t n, cap; int failed; } JsonBuf;

static void jb_add(JsonBuf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->n + n + 1) cap *= 2;
        b->p = realloc(b->p, cap);
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
}
static void jb_str(JsonBuf *b, const char *s) { jb_add(b, s, strlen(s)); }

static void json_write_string(JsonBuf *b, const char *s, size_t n) {
    jb_add(b, "\"", 1);
    size_t run = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        const char *esc = NULL;
        char u[8];
        switch (c) {
            case '"': esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n"; break;
            case '\r': esc = "\\r"; break;
            case '\t': esc = "\\t"; break;
            case '\b': esc = "\\b"; break;
            case '\f': esc = "\\f"; break;
            default:
                if (c < 0x20) { snprintf(u, sizeof(u), "\\u%04x", c); esc = u; }
        }
        if (esc) {
            if (run) jb_add(b, s + i - run, run);
            run = 0;
            jb_str(b, esc);
        } else run++;
    }
    if (run) jb_add(b, s + n - run, run);
    jb_add(b, "\"", 1);
}

static void json_write(Value v, JsonBuf *b, int depth) {
    if (depth > JSON_MAX_DEPTH) { b->failed = 1; return; }
    if (IS_FLOAT_BITS(v)) {
        double d = AS_FLOAT(v);
        if (isnan(d) || isinf(d)) { jb_str(b, "null"); return; }
        char tmp[64]; format_double(tmp, sizeof(tmp), d); jb_str(b, tmp); return;
    }
    if (is_bigint(v)) { char *s = bigint_to_new_string(AS_BIGINT(v)); jb_str(b, s); free(s); return; }
    uint64_t type = (v & MASK_TYPE) >> 48;
    if (type == VAL_INT) {
        char tmp[64]; snprintf(tmp, sizeof(tmp), "%lld", (long long)AS_INT(v)); jb_str(b, tmp);
    } else if (type == VAL_BOOL) {
        jb_str(b, AS_BOOL(v) ? "true" : "false");
    } else if (type == VAL_STRING && (v & MASK_PAYLOAD)) {
        json_write_string(b, AS_STRING(v), rope_length(v));
    } else if (type == VAL_LIST) {
        List *l = AS_LIST(v);
        jb_add(b, "[", 1);
        for (int i = 0; i < l->size && !b->failed; i++) {
            if (i) jb_add(b, ", ", 2);
            json_write(l->items[i], b, depth + 1);
        }
        jb_add(b, "]", 1);
    } else if (type == VAL_DICT) {
        Dict *d = AS_DICT(v);
        int printed = 0;
        jb_add(b, "{", 1);
        for (int i = 0; i < d->capacity && !b->failed; i++) {
            if (GET_TYPE(d->keys[i]) == VAL_NONE) continue;
            if (printed++) jb_add(b, ", ", 2);
            const char *key = (const char *)(uintptr_t)(d->keys[i] & MASK_PAYLOAD);
            json_write_string(b, key, strlen(key));
            jb_add(b, ": ", 2);
            json_write(d->values[i], b, depth + 1);
        }
        jb_add(b, "}", 1);
    } else {
        jb_str(b, "null");
    }
}

typedef struct { const char *start, *p, *end; const char *err; int depth; } JsonIn;

static void json_ws(JsonIn *in) {
    while (in->p < in->end && (*in->p == ' ' || *in->p == '\t' || *in->p == '\n' || *in->p == '\r')) in->p++;
}

static int json_hex4(const char *p, const char *end, unsigned *out) {
    if (end - p < 4) return 0;
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

static void json_utf8(JsonBuf *b, unsigned cp) {
    char u[4];
    if (cp < 0x80) { u[0] = (char)cp; jb_add(b, u, 1); }
    else if (cp < 0x800) { u[0] = (char)(0xC0 | (cp >> 6)); u[1] = (char)(0x80 | (cp & 0x3F)); jb_add(b, u, 2); }
    else if (cp < 0x10000) { u[0] = (char)(0xE0 | (cp >> 12)); u[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); u[2] = (char)(0x80 | (cp & 0x3F)); jb_add(b, u, 3); }
    else { u[0] = (char)(0xF0 | (cp >> 18)); u[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); u[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); u[3] = (char)(0x80 | (cp & 0x3F)); jb_add(b, u, 4); }
}

/* Read a string (in->p at the opening quote) into b. */
static int json_read_string(JsonIn *in, JsonBuf *b) {
    in->p++;
    b->n = 0;
    jb_add(b, "", 0);
    for (;;) {
        const char *s = in->p;
        while (in->p < in->end && *in->p != '"' && *in->p != '\\') in->p++;
        jb_add(b, s, (size_t)(in->p - s));
        if (in->p >= in->end) { in->err = "unterminated string"; return 0; }
        if (*in->p == '"') { in->p++; return 1; }
        in->p++;
        if (in->p >= in->end) { in->err = "unterminated string"; return 0; }
        char c = *in->p++;
        switch (c) {
            case '"': jb_add(b, "\"", 1); break;
            case '\\': jb_add(b, "\\", 1); break;
            case '/': jb_add(b, "/", 1); break;
            case 'b': jb_add(b, "\b", 1); break;
            case 'f': jb_add(b, "\f", 1); break;
            case 'n': jb_add(b, "\n", 1); break;
            case 'r': jb_add(b, "\r", 1); break;
            case 't': jb_add(b, "\t", 1); break;
            case 'u': {
                unsigned cp;
                if (!json_hex4(in->p, in->end, &cp)) { in->p--; in->err = "bad \\u escape"; return 0; }
                in->p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    unsigned lo;
                    if (in->end - in->p >= 6 && in->p[0] == '\\' && in->p[1] == 'u' && json_hex4(in->p + 2, in->end, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        in->p += 6;
                    } else cp = 0xFFFD;
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) cp = 0xFFFD;
                json_utf8(b, cp);
                break;
            }
            default: in->p -= 2; in->err = "bad escape in string"; return 0;
        }
    }
}

static Value json_read_value(JsonIn *in, JsonBuf *tmp);

static Value json_read_number(JsonIn *in) {
    const char *s = in->p;
    int is_int = 1;
    if (in->p < in->end && *in->p == '-') in->p++;
    if (in->p >= in->end || !isdigit((unsigned char)*in->p)) { in->err = "bad number"; return value_none(); }
    if (*in->p == '0') in->p++;
    else while (in->p < in->end && isdigit((unsigned char)*in->p)) in->p++;
    if (in->p < in->end && *in->p == '.') {
        is_int = 0; in->p++;
        if (in->p >= in->end || !isdigit((unsigned char)*in->p)) { in->err = "bad number"; return value_none(); }
        while (in->p < in->end && isdigit((unsigned char)*in->p)) in->p++;
    }
    if (in->p < in->end && (*in->p == 'e' || *in->p == 'E')) {
        is_int = 0; in->p++;
        if (in->p < in->end && (*in->p == '+' || *in->p == '-')) in->p++;
        if (in->p >= in->end || !isdigit((unsigned char)*in->p)) { in->err = "bad number"; return value_none(); }
        while (in->p < in->end && isdigit((unsigned char)*in->p)) in->p++;
    }
    size_t n = (size_t)(in->p - s);
    char buf[64], *num = n < sizeof(buf) ? buf : malloc(n + 1);
    memcpy(num, s, n);
    num[n] = '\0';
    Value v;
    if (is_int && n <= 18) {
        int64_t i = strtoll(num, NULL, 10);
        if (fits_int48(i)) v = value_int(i);
        else if (i >= -INT53_MAX && i <= INT53_MAX) v = value_float((double)i);
        else v = value_bigint(bigint_from_int64(i));
    } else if (is_int) {
        v = value_bigint(bigint_from_string(num));
    } else {
        v = value_from_double(strtod(num, NULL));
    }
    if (num != buf) free(num);
    return v;
}

static Value json_read_value(JsonIn *in, JsonBuf *tmp) {
    json_ws(in);
    if (in->p >= in->end) { in->err = "unexpected end of input"; return value_none(); }
    char c = *in->p;
    if (c == '{' || c == '[') {
        if (++in->depth > JSON_MAX_DEPTH) { in->err = "nested too deeply"; return value_none(); }
        in->p++;
        json_ws(in);
        Value out;
        if (c == '{') {
            Dict *d = dict_new(8);
            out = value_dict(d);
            if (in->p < in->end && *in->p == '}') { in->p++; in->depth--; return out; }
            for (;;) {
                json_ws(in);
                if (in->p >= in->end || *in->p != '"') { in->err = "expected a string key"; return value_none(); }
                if (!json_read_string(in, tmp)) return value_none();
                const char *key = intern_string(tmp->p);
                json_ws(in);
                if (in->p >= in->end || *in->p != ':') { in->err = "expected ':'"; return value_none(); }
                in->p++;
                Value item = json_read_value(in, tmp);
                if (in->err) return value_none();
                dict_set(d, key, item);
                json_ws(in);
                if (in->p < in->end && *in->p == ',') { in->p++; continue; }
                if (in->p < in->end && *in->p == '}') { in->p++; break; }
                in->err = "expected ',' or '}'";
                return value_none();
            }
        } else {
            List *l = list_new(8);
            out = value_list(l);
            if (in->p < in->end && *in->p == ']') { in->p++; in->depth--; return out; }
            for (;;) {
                Value item = json_read_value(in, tmp);
                if (in->err) return value_none();
                list_push(l, item);
                json_ws(in);
                if (in->p < in->end && *in->p == ',') { in->p++; continue; }
                if (in->p < in->end && *in->p == ']') { in->p++; break; }
                in->err = "expected ',' or ']'";
                return value_none();
            }
        }
        in->depth--;
        return out;
    }
    if (c == '"') {
        if (!json_read_string(in, tmp)) return value_none();
        return value_string_copy_len(tmp->p, tmp->n);
    }
    if (c == '-' || isdigit((unsigned char)c)) return json_read_number(in);
    if (in->end - in->p >= 4 && !memcmp(in->p, "true", 4)) { in->p += 4; return value_bool(1); }
    if (in->end - in->p >= 5 && !memcmp(in->p, "false", 5)) { in->p += 5; return value_bool(0); }
    if (in->end - in->p >= 4 && !memcmp(in->p, "null", 4)) { in->p += 4; return value_none(); }
    in->err = "unexpected character";
    return value_none();
}

static Value json_error(const char *msg) {
    pending_exception = value_string_copy(msg);
    has_exception = 1;
    return value_none();
}

static Value native_json_parse(int argc, Value *args) {
    if (argc >= 2 && GET_TYPE(args[0]) == VAL_DICT) { argc--; args++; }
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING || !(args[0] & MASK_PAYLOAD)) return json_error("json.parse: expected a string");
    JsonIn in;
    in.start = in.p = AS_STRING(args[0]);
    in.end = in.start + rope_length(args[0]);
    in.err = NULL;
    in.depth = 0;
    /* Skip a UTF-8 byte-order mark. */
    if (in.end - in.p >= 3 && (unsigned char)in.p[0] == 0xEF && (unsigned char)in.p[1] == 0xBB && (unsigned char)in.p[2] == 0xBF) in.p += 3;
    JsonBuf tmp = { 0 };
    Value v = json_read_value(&in, &tmp);
    if (!in.err) { json_ws(&in); if (in.p < in.end) in.err = "unexpected text after the JSON value"; }
    free(tmp.p);
    if (in.err) {
        int line = 1, col = 1;
        for (const char *q = in.start; q < in.p; q++) { if (*q == '\n') { line++; col = 1; } else col++; }
        char msg[160];
        snprintf(msg, sizeof(msg), "json.parse: %s at line %d column %d", in.err, line, col);
        return json_error(msg);
    }
    return v;
}

static Value native_json_stringify(int argc, Value *args) {
    if (argc >= 2 && GET_TYPE(args[0]) == VAL_DICT) { argc--; args++; }
    if (argc < 1) return json_error("json.stringify: expected a value");
    JsonBuf b = { 0 };
    jb_add(&b, "", 0);
    json_write(args[0], &b, 0);
    if (b.failed) { free(b.p); return json_error("json.stringify: nested too deeply (does a list or dict contain itself?)"); }
    Value res = value_string_copy_len(b.p, b.n);
    free(b.p);
    return res;
}

Value native_dict_has(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_DICT || GET_TYPE(args[1]) != VAL_STRING) {
        return value_bool(0);
    }
    /* A key whose value is none (JSON null) is still in the dict. */
    return value_bool(dict_get_idx(AS_DICT(args[0]), AS_STRING(args[1])) >= 0);
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
    Value val = value_none();
    dict_remove(AS_DICT(args[0]), AS_STRING(args[1]), &val);
    return val;
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
        return value_int(glow_str_char_index(args[0], (size_t)(found - s)));
    }
    return value_int(-1);
}

/* text.slice(start, end): characters start..end-1; negative indexes count
   from the end, and both are clamped to the string. */
Value native_str_slice(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_STRING) {
        return value_none();
    }
    int64_t len = glow_str_len(args[0]);

    int64_t start = 0;
    if (GET_TYPE(args[1]) == VAL_INT) {
        start = AS_INT(args[1]);
    } else if (GET_TYPE(args[1]) == VAL_FLOAT) {
        start = (int64_t)AS_FLOAT(args[1]);
    }
    if (start < 0) {
        start = len + start;
        if (start < 0) start = 0;
    }
    if (start > len) start = len;

    int64_t end = len;
    if (argc >= 3 && GET_TYPE(args[2]) != VAL_NONE) {
        if (GET_TYPE(args[2]) == VAL_INT) {
            end = AS_INT(args[2]);
        } else if (GET_TYPE(args[2]) == VAL_FLOAT) {
            end = (int64_t)AS_FLOAT(args[2]);
        }
        if (end < 0) {
            end = len + end;
            if (end < 0) end = 0;
        }
        if (end > len) end = len;
    }

    if (start >= end) {
        return value_string_copy("");
    }
    size_t a = glow_str_byte_offset(args[0], start);
    size_t b = glow_str_byte_offset(args[0], end);
    return value_string_copy_len(AS_STRING(args[0]) + a, b - a);
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

    List *l = list_new(0);

    // Pause GC during split to protect the unrooted list 'l' and its items
    size_t old_gc_thresh = gc_threshold;
    gc_threshold = (size_t)-1;

    size_t delim_len = strlen(delim);
    if (delim_len == 0) {
        size_t len = strlen(s);
        for (size_t i = 0; i < len; ) {
            size_t next = glow_utf8_step(s, i, len);
            list_push(l, value_string_copy_len(s + i, next - i));
            i = next;
        }
        gc_threshold = old_gc_thresh;
        return value_list(l);
    }

    /* Pieces of an ASCII string are ASCII: count them now, for free. */
    int ascii = glow_str_len(args[0]) == (int64_t)rope_length(args[0]);   /* counted once, then cached */
    const char *current = s;
    const char *next = strstr(current, delim);
    while (next != NULL) {
        size_t part_len = next - current;
        Value piece = value_string_copy_len(current, part_len);
        if (ascii) glow_str_mark_ascii(piece);
        list_push(l, piece);
        current = next + delim_len;
        next = strstr(current, delim);
    }
    Value last = value_string_copy(current);
    if (ascii) glow_str_mark_ascii(last);
    list_push(l, last);

    gc_threshold = old_gc_thresh;
    return value_list(l);
}

/* ---- HTTP client: http.request(method, url, options) ----
 *
 * options: headers (dict), body (string), timeout (seconds, default 30),
 * follow_redirects (default true), max_redirects (default 10) and verify
 * (default true: check the server's TLS certificate and host name).
 * Returns {status, body, headers, url}; a network, TLS or protocol
 * failure raises an error saying what went wrong.
 *
 * HTTPS uses OpenSSL with SNI and certificate verification. Trusted roots
 * come from the Windows certificate store on Windows and from the system
 * CA bundle elsewhere. Bodies sent with chunked transfer encoding are
 * decoded; redirects (301, 302, 303, 307, 308) are followed.
 */
#ifdef _WIN32
#include <wincrypt.h>
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
typedef SOCKET http_sock_t;
#define HTTP_BAD_SOCK INVALID_SOCKET
#define http_closesock closesocket
#else
#include <fcntl.h>
#include <errno.h>
typedef int http_sock_t;
#define HTTP_BAD_SOCK (-1)
#define http_closesock close
#endif

typedef struct { char *p; size_t n, cap; } HttpBuf;
static void hb_add(HttpBuf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
}

typedef struct {
    int ssl;
    char host[256];
    int port;
    char *path;          /* includes the query; malloc'd */
} HttpUrl;

/* Splits http(s)://host[:port][/path?query]; returns an error or NULL. */
static const char *http_parse_url(const char *url, HttpUrl *u) {
    memset(u, 0, sizeof(*u));
    const char *p;
    if (strncmp(url, "https://", 8) == 0) { u->ssl = 1; p = url + 8; u->port = 443; }
    else if (strncmp(url, "http://", 7) == 0) { p = url + 7; u->port = 80; }
    else return "the URL must start with http:// or https://";
    const char *host_end;
    if (*p == '[') {   /* [IPv6] */
        const char *close_br = strchr(p, ']');
        if (!close_br) return "bad IPv6 address in the URL";
        size_t n = (size_t)(close_br - p - 1);
        if (n >= sizeof(u->host)) return "host name too long";
        memcpy(u->host, p + 1, n); u->host[n] = '\0';
        host_end = close_br + 1;
    } else {
        host_end = p;
        while (*host_end && *host_end != ':' && *host_end != '/' && *host_end != '?' && *host_end != '#') host_end++;
        size_t n = (size_t)(host_end - p);
        if (n == 0) return "the URL has no host";
        if (n >= sizeof(u->host)) return "host name too long";
        memcpy(u->host, p, n); u->host[n] = '\0';
    }
    p = host_end;
    if (*p == ':') {
        p++;
        long port = strtol(p, (char**)&p, 10);
        if (port <= 0 || port > 65535) return "bad port in the URL";
        u->port = (int)port;
    }
    const char *path_end = strchr(p, '#');
    size_t plen = path_end ? (size_t)(path_end - p) : strlen(p);
    int need_slash = plen == 0 || p[0] != '/';
    u->path = malloc(plen + 2);
    snprintf(u->path, plen + 2, "%s%.*s", need_slash ? "/" : "", (int)plen, p);
    return NULL;
}

/* The URL a Location header points to, made absolute against base. */
static char *http_resolve_location(const HttpUrl *base, const char *loc) {
    if (strncmp(loc, "http://", 7) == 0 || strncmp(loc, "https://", 8) == 0) return strdup(loc);
    size_t n = strlen(base->host) + strlen(loc) + strlen(base->path) + 32;
    char *out = malloc(n);
    const char *scheme = base->ssl ? "https" : "http";
    int default_port = base->port == (base->ssl ? 443 : 80);
    char hostport[300];
    if (strchr(base->host, ':')) snprintf(hostport, sizeof(hostport), "[%s]", base->host);
    else snprintf(hostport, sizeof(hostport), "%s", base->host);
    if (!default_port) { size_t l = strlen(hostport); snprintf(hostport + l, sizeof(hostport) - l, ":%d", base->port); }
    if (strncmp(loc, "//", 2) == 0) snprintf(out, n, "%s:%s", scheme, loc);
    else if (loc[0] == '/') snprintf(out, n, "%s://%s%s", scheme, hostport, loc);
    else {
        /* Relative to the directory of the current path. */
        const char *q = strchr(base->path, '?');
        size_t dir = q ? (size_t)(q - base->path) : strlen(base->path);
        while (dir > 0 && base->path[dir - 1] != '/') dir--;
        snprintf(out, n, "%s://%s%.*s%s", scheme, hostport, (int)dir, base->path, loc);
    }
    return out;
}

/* A monotonic clock in seconds. */
static double http_now(void) {
#ifdef _WIN32
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

static void http_set_timeouts(http_sock_t s, double seconds) {
#ifdef _WIN32
    DWORD ms = (DWORD)(seconds * 1000);   /* Windows takes milliseconds, not a timeval */
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof(ms));
#else
    struct timeval tv;
    tv.tv_sec = (long)seconds;
    tv.tv_usec = (long)((seconds - (double)tv.tv_sec) * 1e6);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

/* Connects to host:port within `seconds`, trying each address. */
static http_sock_t http_connect(const char *host, int port, double seconds, char *err, size_t errsz) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    int rc = getaddrinfo(host, portstr, &hints, &res);
    if (rc != 0 || !res) { snprintf(err, errsz, "cannot resolve host %s", host); return HTTP_BAD_SOCK; }
    http_sock_t s = HTTP_BAD_SOCK;
    int timed_out = 0;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == HTTP_BAD_SOCK) continue;
        /* Non-blocking connect, so the timeout applies to connecting too. */
#ifdef _WIN32
        u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
#else
        int fl = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
        int c = connect(s, ai->ai_addr, (int)ai->ai_addrlen);
        int ok = c == 0;
        if (!ok) {
            fd_set w, e;
            FD_ZERO(&w); FD_SET(s, &w);
            FD_ZERO(&e); FD_SET(s, &e);
            struct timeval tv;
            tv.tv_sec = (long)seconds;
            tv.tv_usec = (long)((seconds - (double)tv.tv_sec) * 1e6);
            int sel = select((int)s + 1, NULL, &w, &e, &tv);
            if (sel == 0) timed_out = 1;
            if (sel > 0 && FD_ISSET(s, &w)) {
                int soerr = 0;
                socklen_t len = sizeof(soerr);
                getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soerr, &len);
                ok = soerr == 0;
            }
        }
        if (ok) {
#ifdef _WIN32
            u_long b = 0; ioctlsocket(s, FIONBIO, &b);
#else
            fcntl(s, F_SETFL, fl);
#endif
            http_set_timeouts(s, seconds);
            break;
        }
        http_closesock(s);
        s = HTTP_BAD_SOCK;
    }
    freeaddrinfo(res);
    if (s == HTTP_BAD_SOCK) snprintf(err, errsz, timed_out ? "connecting to %s:%d timed out" : "cannot connect to %s:%d", host, port);
    return s;
}

/* One client TLS context for the whole program, trusting the system's
   root certificates. */
static SSL_CTX *http_ssl_ctx = NULL;
static SSL_CTX *http_get_ssl_ctx(void) {
    if (http_ssl_ctx) return http_ssl_ctx;
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return NULL;
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
#ifdef _WIN32
    X509_STORE *store = SSL_CTX_get_cert_store(ctx);
    const wchar_t *stores[] = { L"ROOT", L"CA" };
    for (int k = 0; k < 2; k++) {
        HCERTSTORE hs = CertOpenSystemStoreW(0, stores[k]);
        if (!hs) continue;
        PCCERT_CONTEXT cc = NULL;
        while ((cc = CertEnumCertificatesInStore(hs, cc)) != NULL) {
            const unsigned char *der = cc->pbCertEncoded;
            X509 *x = d2i_X509(NULL, &der, (long)cc->cbCertEncoded);
            if (x) { X509_STORE_add_cert(store, x); X509_free(x); }
        }
        CertCloseStore(hs, 0);
    }
#else
    SSL_CTX_set_default_verify_paths(ctx);
    /* A statically linked OpenSSL may look in its build machine's folder;
       add the usual system bundles as well. */
    const char *bundles[] = { "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
                              "/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem", "/usr/local/etc/openssl@3/cert.pem",
                              "/opt/homebrew/etc/openssl@3/cert.pem", NULL };
    for (int k = 0; bundles[k]; k++) {
        FILE *f = fopen(bundles[k], "r");
        if (f) { fclose(f); SSL_CTX_load_verify_locations(ctx, bundles[k], NULL); }
    }
#endif
    http_ssl_ctx = ctx;
    return ctx;
}

typedef struct { int status; HttpBuf headers_raw; HttpBuf body; } HttpResult;

/* Decodes a chunked body in place; returns 0 if it is malformed. */
static int http_dechunk(HttpBuf *b) {
    size_t in = 0, out = 0;
    for (;;) {
        char *end;
        unsigned long size = strtoul(b->p + in, &end, 16);
        if (end == b->p + in) return 0;
        char *line_end = strstr(end, "\r\n");
        if (!line_end) return 0;
        in = (size_t)(line_end - b->p) + 2;
        if (size == 0) break;
        if (in + size > b->n) return 0;
        memmove(b->p + out, b->p + in, size);
        out += size;
        in += size;
        if (in + 2 <= b->n && b->p[in] == '\r' && b->p[in + 1] == '\n') in += 2;
    }
    b->n = out;
    b->p[out] = '\0';
    return 1;
}

/* A header's value (case-insensitive name) from the raw header block. */
static int http_header(const char *raw, const char *name, char *out, size_t outsz) {
    size_t nl = strlen(name);
    for (const char *line = raw; line && *line; ) {
        const char *eol = strstr(line, "\r\n");
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        if (len > nl && line[nl] == ':' && strncasecmp(line, name, nl) == 0) {
            const char *v = line + nl + 1;
            while (*v == ' ' || *v == '\t') v++;
            size_t vl = len - (size_t)(v - line);
            if (vl >= outsz) vl = outsz - 1;
            memcpy(out, v, vl); out[vl] = '\0';
            return 1;
        }
        line = eol ? eol + 2 : NULL;
    }
    return 0;
}

/* One request/response exchange (no redirects). Runs without the GIL. */
static int http_exchange(const HttpUrl *u, const char *method, const char *extra_headers,
                         const char *body, size_t body_len, double timeout, int verify,
                         HttpResult *r, char *err, size_t errsz) {
    http_sock_t s = http_connect(u->host, u->port, timeout, err, errsz);
    if (s == HTTP_BAD_SOCK) return 0;
    SSL *ssl = NULL;
    if (u->ssl) {
        SSL_CTX *ctx = http_get_ssl_ctx();
        if (!ctx || !(ssl = SSL_new(ctx))) { http_closesock(s); snprintf(err, errsz, "cannot start TLS"); return 0; }
        SSL_set_fd(ssl, (int)s);
        SSL_set_tlsext_host_name(ssl, u->host);
        if (verify) {
            SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
            SSL_set1_host(ssl, u->host);
        }
        if (SSL_connect(ssl) <= 0) {
            long vr = SSL_get_verify_result(ssl);
            if (verify && vr != X509_V_OK) snprintf(err, errsz, "TLS certificate of %s not trusted: %s", u->host, X509_verify_cert_error_string(vr));
            else snprintf(err, errsz, "TLS handshake with %s failed", u->host);
            SSL_free(ssl); http_closesock(s);
            return 0;
        }
    }
    HttpBuf req = { NULL, 0, 0 };
    char line[1024];
    int default_port = u->port == (u->ssl ? 443 : 80);
    int v6 = strchr(u->host, ':') != NULL;
    if (default_port) snprintf(line, sizeof(line), "%s %s HTTP/1.1\r\nHost: %s%s%s\r\n", method, u->path, v6 ? "[" : "", u->host, v6 ? "]" : "");
    else snprintf(line, sizeof(line), "%s %s HTTP/1.1\r\nHost: %s%s%s:%d\r\n", method, u->path, v6 ? "[" : "", u->host, v6 ? "]" : "", u->port);
    hb_add(&req, line, strlen(line));
    hb_add(&req, extra_headers, strlen(extra_headers));
    if (body) { snprintf(line, sizeof(line), "Content-Length: %zu\r\n", body_len); hb_add(&req, line, strlen(line)); }
    hb_add(&req, "\r\n", 2);
    if (body && body_len) hb_add(&req, body, body_len);
    size_t sent = 0;
    while (sent < req.n) {
        int w = ssl ? SSL_write(ssl, req.p + sent, (int)(req.n - sent)) : send(s, req.p + sent, (int)(req.n - sent), 0);
        if (w <= 0) break;
        sent += (size_t)w;
    }
    free(req.p);
    if (sent < req.n) { snprintf(err, errsz, "sending the request to %s failed", u->host); if (ssl) SSL_free(ssl); http_closesock(s); return 0; }
    HttpBuf resp = { NULL, 0, 0 };
    char chunk[16384];
    int timed_out = 0;
    for (;;) {
        double waited_from = http_now();
        int n = ssl ? SSL_read(ssl, chunk, sizeof(chunk)) : recv(s, chunk, sizeof(chunk), 0);
        if (n > 0) { hb_add(&resp, chunk, (size_t)n); continue; }
        /* A read that failed after waiting the whole timeout timed out
           (the error codes for that differ between systems and TLS). */
        if (n < 0 || (ssl && SSL_get_error(ssl, n) != SSL_ERROR_ZERO_RETURN))
            timed_out = http_now() - waited_from >= timeout * 0.9;
        break;
    }
    if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
    http_closesock(s);
    char *hend = resp.p ? strstr(resp.p, "\r\n\r\n") : NULL;
    if (!hend) {
        free(resp.p);
        snprintf(err, errsz, timed_out ? "%s did not answer within the %g-second timeout" : "%s sent no valid HTTP response", u->host, timeout);
        return 0;
    }
    if (strncmp(resp.p, "HTTP/", 5) != 0 || !strchr(resp.p, ' ')) { free(resp.p); snprintf(err, errsz, "%s sent no valid HTTP response", u->host); return 0; }
    r->status = atoi(strchr(resp.p, ' ') + 1);
    size_t hlen = (size_t)(hend - resp.p);
    const char *first_eol = strstr(resp.p, "\r\n");
    size_t skip = (size_t)(first_eol - resp.p) + 2;
    r->headers_raw.n = 0;
    if (hlen > skip) hb_add(&r->headers_raw, resp.p + skip, hlen - skip);
    else hb_add(&r->headers_raw, "", 0);
    size_t body_start = hlen + 4;
    hb_add(&r->body, resp.p + body_start, resp.n - body_start);
    free(resp.p);
    char val[256];
    if (http_header(r->headers_raw.p, "Transfer-Encoding", val, sizeof(val)) && strstr(val, "chunked")) {
        if (!http_dechunk(&r->body)) { snprintf(err, errsz, "%s sent a malformed chunked body", u->host); return 0; }
    } else if (http_header(r->headers_raw.p, "Content-Length", val, sizeof(val))) {
        size_t cl = (size_t)strtoull(val, NULL, 10);
        if (cl < r->body.n) { r->body.n = cl; r->body.p[cl] = '\0'; }
        else if (cl > r->body.n && timed_out) { snprintf(err, errsz, "%s stopped sending before the end of the body (timed out)", u->host); return 0; }
    }
    return 1;
}

static Value http_request_native(int argc, Value *args) {
    char err[512];
    if (argc < 2 || GET_TYPE(args[0]) != VAL_STRING || GET_TYPE(args[1]) != VAL_STRING) {
        pending_exception = value_string_copy("http.request(method, url, options) needs a method and a URL");
        has_exception = 1;
        return value_none();
    }
    char method[16];
    snprintf(method, sizeof(method), "%s", AS_STRING(args[0]));
    for (char *c = method; *c; c++) *c = (char)toupper((unsigned char)*c);
    Value options = argc > 2 ? args[2] : value_none();
    double timeout = 30;
    int follow = 1, max_redirects = 10, verify = 1;
    HttpBuf hdrs = { NULL, 0, 0 };
    char *body = NULL;
    size_t body_len = 0;
    int has_ua = 0;
    if (GET_TYPE(options) == VAL_DICT) {
        Dict *o = AS_DICT(options);
        Value v = dict_get(o, "timeout");
        if (GET_TYPE(v) == VAL_INT || GET_TYPE(v) == VAL_FLOAT) timeout = val_to_double(v);
        if (timeout <= 0) timeout = 30;
        v = dict_get(o, "follow_redirects"); if (GET_TYPE(v) == VAL_BOOL) follow = AS_BOOL(v);
        v = dict_get(o, "max_redirects"); if (GET_TYPE(v) == VAL_INT) max_redirects = (int)AS_INT(v);
        v = dict_get(o, "verify"); if (GET_TYPE(v) == VAL_BOOL) verify = AS_BOOL(v);
        v = dict_get(o, "headers");
        if (GET_TYPE(v) == VAL_DICT) {
            Dict *h = AS_DICT(v);
            for (int i = 0; i < h->capacity; i++) {
                if (GET_TYPE(h->keys[i]) != VAL_STRING) continue;
                const char *k = (const char*)(uintptr_t)(h->keys[i] & MASK_PAYLOAD);
                char *val = value_to_new_cstring(h->values[i]);
                if (strpbrk(k, "\r\n:") || (val && strpbrk(val, "\r\n"))) {   /* no header injection */
                    free(val); free(hdrs.p);
                    pending_exception = value_string_copy("http.request: a header name or value contains a line break");
                    has_exception = 1;
                    return value_none();
                }
                if (strcasecmp(k, "User-Agent") == 0) has_ua = 1;
                hb_add(&hdrs, k, strlen(k)); hb_add(&hdrs, ": ", 2);
                hb_add(&hdrs, val ? val : "", val ? strlen(val) : 0); hb_add(&hdrs, "\r\n", 2);
                free(val);
            }
        }
        v = dict_get(o, "body");
        if (GET_TYPE(v) == VAL_STRING) { body_len = rope_length(v); body = malloc(body_len + 1); memcpy(body, AS_STRING(v), body_len); body[body_len] = '\0'; }
        else if (GET_TYPE(v) != VAL_NONE) { char *s = value_to_new_cstring(v); body = s; body_len = s ? strlen(s) : 0; }
    }
    if (!has_ua) { const char *ua = "User-Agent: GlowLang\r\n"; hb_add(&hdrs, ua, strlen(ua)); }
    hb_add(&hdrs, "Connection: close\r\nAccept-Encoding: identity\r\n", 46);

    char *url = strdup(AS_STRING(args[1]));
    HttpResult r;
    memset(&r, 0, sizeof(r));
    int ok = 0, redirects = 0;
    glow_gil_release();
    for (;;) {
        HttpUrl u;
        const char *perr = http_parse_url(url, &u);
        if (perr) { snprintf(err, sizeof(err), "%s", perr); break; }
        free(r.headers_raw.p); free(r.body.p);
        memset(&r, 0, sizeof(r));
        ok = http_exchange(&u, method, hdrs.p, body, body_len, timeout, verify, &r, err, sizeof(err));
        if (!ok) { free(u.path); break; }
        char loc[2048];
        int is_redirect = r.status == 301 || r.status == 302 || r.status == 303 || r.status == 307 || r.status == 308;
        if (!follow || !is_redirect || !http_header(r.headers_raw.p, "Location", loc, sizeof(loc))) { free(u.path); break; }
        if (++redirects > max_redirects) { snprintf(err, sizeof(err), "more than %d redirects", max_redirects); ok = 0; free(u.path); break; }
        char *next = http_resolve_location(&u, loc);
        free(u.path);
        free(url);
        url = next;
        /* 303, and 301/302 after a POST, turn into a GET without a body. */
        if (r.status == 303 || ((r.status == 301 || r.status == 302) && strcmp(method, "POST") == 0)) {
            snprintf(method, sizeof(method), "GET");
            free(body); body = NULL; body_len = 0;
        }
    }
    glow_gil_acquire();
    free(hdrs.p);
    free(body);
    if (!ok) {
        free(url); free(r.headers_raw.p); free(r.body.p);
        char msg[600];
        snprintf(msg, sizeof(msg), "http.request: %s", err);
        pending_exception = value_string_copy(msg);
        has_exception = 1;
        return value_none();
    }
    Dict *headers = dict_new(16);
    for (char *line = r.headers_raw.p; line && *line; ) {
        char *eol = strstr(line, "\r\n");
        if (eol) *eol = '\0';
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char *v = colon + 1;
            while (*v == ' ' || *v == '\t') v++;
            dict_set(headers, line, value_string_copy(v));
        }
        line = eol ? eol + 2 : NULL;
    }
    Dict *result = dict_new(4);
    dict_set(result, "status", value_int(r.status));
    dict_set(result, "body", value_string_copy_len(r.body.p ? r.body.p : "", r.body.n));
    dict_set(result, "headers", value_dict(headers));
    dict_set(result, "url", value_string_copy(url));
    free(url); free(r.headers_raw.p); free(r.body.p);
    return value_dict(result);
}

/* ---- PostgreSQL client: pg.connect / pg.query / pg.script / pg.close ----
 *
 * A native client for the PostgreSQL frontend/backend protocol (version 3),
 * so no libpq is needed. TLS uses the same OpenSSL setup as http.request.
 * Logins: SCRAM-SHA-256, MD5 and cleartext passwords, and trust.
 *
 * pg.query always uses the extended protocol with $1, $2 ... parameters
 * sent apart from the SQL text, so a value can never change the statement.
 * Network waits run without the GIL. The wire work is done into plain C
 * buffers and only turned into GlowLang values once the GIL is held again.
 *
 * lib/postgres.glow is the friendly layer on top of these.
 */
#include <openssl/evp.h>
#include <openssl/rand.h>

#define PG_MAX_CONNS 256
#define PG_MAX_MESSAGE (512u * 1024u * 1024u)

typedef struct {
    http_sock_t s;
    SSL *ssl;
    int busy;          /* a call on another thread is using it */
    int broken;        /* the connection failed; only close() is allowed */
    char tx;           /* last ReadyForQuery status: I, T or E */
    int server_version;
    double timeout;    /* seconds per wait for the server; 0 = no limit */
} PgConn;

static PgConn *pg_conns[PG_MAX_CONNS];

static Value pg_fail(const char *msg) {
    char buf[1200];
    snprintf(buf, sizeof(buf), "postgres: %s", msg);
    pending_exception = value_string_copy(buf);
    has_exception = 1;
    return value_none();
}

/* Big-endian helpers. */
static void pg_put32(HttpBuf *b, int32_t v) {
    unsigned char x[4] = { (unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v };
    hb_add(b, (const char*)x, 4);
}
static void pg_put16(HttpBuf *b, int16_t v) {
    unsigned char x[2] = { (unsigned char)(v >> 8), (unsigned char)v };
    hb_add(b, (const char*)x, 2);
}
static int32_t pg_get32(const unsigned char *p) {
    return (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
}
static int16_t pg_get16(const unsigned char *p) { return (int16_t)(((unsigned)p[0] << 8) | (unsigned)p[1]); }

/* Starts a message: type byte (0 for the startup message) and a length
   placeholder. pg_end fills the length in. */
static size_t pg_begin(HttpBuf *b, char type) {
    if (type) hb_add(b, &type, 1);
    size_t at = b->n;
    pg_put32(b, 0);
    return at;
}
static void pg_end(HttpBuf *b, size_t at) {
    int32_t len = (int32_t)(b->n - at);
    unsigned char *p = (unsigned char*)b->p + at;
    p[0] = (unsigned char)(len >> 24); p[1] = (unsigned char)(len >> 16); p[2] = (unsigned char)(len >> 8); p[3] = (unsigned char)len;
}
static void pg_cstr(HttpBuf *b, const char *s) { hb_add(b, s, strlen(s) + 1); }

static int pg_send(PgConn *c, const char *p, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        int chunk = (int)((n - sent) > (1u << 30) ? (1u << 30) : (n - sent));
        int w = c->ssl ? SSL_write(c->ssl, p + sent, chunk) : send(c->s, p + sent, chunk, 0);
        if (w <= 0) return 0;
        sent += (size_t)w;
    }
    return 1;
}
static int pg_recv(PgConn *c, unsigned char *p, size_t n) {
    size_t got = 0;
    while (got < n) {
        int chunk = (int)((n - got) > (1u << 30) ? (1u << 30) : (n - got));
        int r = c->ssl ? SSL_read(c->ssl, p + got, chunk) : recv(c->s, (char*)p + got, chunk, 0);
        if (r <= 0) return 0;
        got += (size_t)r;
    }
    return 1;
}

/* Reads one backend message. body is malloc'd (may be empty). */
static int pg_read_msg(PgConn *c, char *type, unsigned char **body, size_t *len, char *err, size_t errsz) {
    unsigned char hdr[5];
    if (!pg_recv(c, hdr, 5)) { snprintf(err, errsz, "the connection to the server was lost"); return 0; }
    int32_t l = pg_get32(hdr + 1);
    if (l < 4 || (uint32_t)l > PG_MAX_MESSAGE) { snprintf(err, errsz, "the server sent a malformed message"); return 0; }
    *type = (char)hdr[0];
    *len = (size_t)l - 4;
    *body = malloc(*len + 1);
    if (!*body) { snprintf(err, errsz, "out of memory"); return 0; }
    if (*len && !pg_recv(c, *body, *len)) { free(*body); *body = NULL; snprintf(err, errsz, "the connection to the server was lost"); return 0; }
    (*body)[*len] = 0;
    return 1;
}

/* "message [SQLSTATE]" plus detail and hint, from an ErrorResponse body. */
static void pg_error_text(const unsigned char *b, size_t n, char *out, size_t outsz) {
    const char *msg = "unknown error", *code = "", *detail = NULL, *hint = NULL;
    size_t i = 0;
    while (i < n && b[i]) {
        char f = (char)b[i++];
        const char *v = (const char*)b + i;
        size_t vl = strnlen(v, n - i);
        if (f == 'M') msg = v; else if (f == 'C') code = v; else if (f == 'D') detail = v; else if (f == 'H') hint = v;
        i += vl + 1;
    }
    int k = snprintf(out, outsz, "%s [%s]", msg, code);
    if (detail && k > 0 && (size_t)k < outsz) k += snprintf(out + k, outsz - (size_t)k, "\nDETAIL: %s", detail);
    if (hint && k > 0 && (size_t)k < outsz) snprintf(out + k, outsz - (size_t)k, "\nHINT: %s", hint);
}

static void pg_b64(const unsigned char *in, int n, char *out) { EVP_EncodeBlock((unsigned char*)out, in, n); }
static int pg_unb64(const char *in, unsigned char *out, int outmax) {
    int n = (int)strlen(in);
    if (n % 4 || n / 4 * 3 > outmax) return -1;
    int r = EVP_DecodeBlock(out, (const unsigned char*)in, n);
    if (r < 0) return -1;
    if (n >= 1 && in[n - 1] == '=') r--;
    if (n >= 2 && in[n - 2] == '=') r--;
    return r;
}
static void pg_hmac(const unsigned char *key, int keylen, const char *msg, size_t msglen, unsigned char out[32]) {
    unsigned int ol = 32;
    HMAC(EVP_sha256(), key, keylen, (const unsigned char*)msg, msglen, out, &ol);
}
/* The value of "k=" in a SCRAM message, copied into out. */
static int pg_scram_attr(const char *msg, char k, char *out, size_t outsz) {
    const char *p = msg;
    while (p && *p) {
        if (p[0] == k && p[1] == '=') {
            const char *e = strchr(p, ',');
            size_t n = e ? (size_t)(e - p - 2) : strlen(p + 2);
            if (n >= outsz) return 0;
            memcpy(out, p + 2, n); out[n] = 0;
            return 1;
        }
        p = strchr(p, ',');
        if (p) p++;
    }
    return 0;
}

typedef struct {
    char host[256];
    int port;
    char *user, *password, *database, *appname;
    int sslmode;       /* 0 disable, 1 prefer, 2 require, 3 verify-full */
    double connect_timeout, timeout;
} PgOpts;

/* Logs in. Runs without the GIL. On success the connection is ready. */
static int pg_open(PgConn *c, const PgOpts *o, char *err, size_t errsz) {
    c->s = http_connect(o->host, o->port, o->connect_timeout, err, errsz);
    if (c->s == HTTP_BAD_SOCK) return 0;
    http_set_timeouts(c->s, o->connect_timeout);
    if (o->sslmode > 0) {
        HttpBuf b = { NULL, 0, 0 };
        pg_put32(&b, 8); pg_put32(&b, 80877103);   /* SSLRequest */
        int ok = pg_send(c, b.p, b.n);
        free(b.p);
        unsigned char answer;
        if (!ok || !pg_recv(c, &answer, 1)) { snprintf(err, errsz, "the server closed the connection during TLS negotiation"); return 0; }
        if (answer == 'S') {
            SSL_CTX *ctx = http_get_ssl_ctx();
            if (!ctx || !(c->ssl = SSL_new(ctx))) { snprintf(err, errsz, "cannot start TLS"); return 0; }
            SSL_set_fd(c->ssl, (int)c->s);
            SSL_set_tlsext_host_name(c->ssl, o->host);
            if (o->sslmode == 3) { SSL_set_verify(c->ssl, SSL_VERIFY_PEER, NULL); SSL_set1_host(c->ssl, o->host); }
            else SSL_set_verify(c->ssl, SSL_VERIFY_NONE, NULL);
            if (SSL_connect(c->ssl) <= 0) {
                long vr = SSL_get_verify_result(c->ssl);
                if (o->sslmode == 3 && vr != X509_V_OK) snprintf(err, errsz, "TLS certificate of %s not trusted: %s", o->host, X509_verify_cert_error_string(vr));
                else snprintf(err, errsz, "TLS handshake with %s failed", o->host);
                return 0;
            }
        } else if (answer == 'N') {
            if (o->sslmode >= 2) { snprintf(err, errsz, "the server does not support TLS, but sslmode requires it"); return 0; }
        } else { snprintf(err, errsz, "the server did not answer the TLS request properly"); return 0; }
    }

    HttpBuf b = { NULL, 0, 0 };
    size_t at = pg_begin(&b, 0);
    pg_put32(&b, 196608);   /* protocol 3.0 */
    pg_cstr(&b, "user"); pg_cstr(&b, o->user);
    pg_cstr(&b, "database"); pg_cstr(&b, o->database);
    pg_cstr(&b, "application_name"); pg_cstr(&b, o->appname);
    pg_cstr(&b, "client_encoding"); pg_cstr(&b, "UTF8");
    pg_cstr(&b, "DateStyle"); pg_cstr(&b, "ISO");
    hb_add(&b, "\0", 1);
    pg_end(&b, at);
    int ok = pg_send(c, b.p, b.n);
    free(b.p);
    if (!ok) { snprintf(err, errsz, "sending the login to the server failed"); return 0; }

    char client_first_bare[128] = "", nonce[64] = "", server_first[1024] = "";
    unsigned char salted[32];
    char auth_message[2048] = "";
    int scram_done = 0;
    for (;;) {
        char type; unsigned char *body; size_t len;
        if (!pg_read_msg(c, &type, &body, &len, err, errsz)) return 0;
        if (type == 'E') { pg_error_text(body, len, err, errsz); free(body); return 0; }
        if (type == 'Z') { c->tx = len ? (char)body[0] : 'I'; free(body); break; }
        if (type == 'S' && len) {
            if (strcmp((char*)body, "server_version") == 0) {   /* "17.2 (Debian ...)" -> 170002; "9.6.24" -> 90624 */
                const char *v = (char*)body + strlen((char*)body) + 1;
                int major = 0, minor = 0, patch = 0;
                int k = sscanf(v, "%d.%d.%d", &major, &minor, &patch);
                c->server_version = major >= 10 ? major * 10000 + (k >= 2 ? minor : 0) : major * 10000 + minor * 100 + (k >= 3 ? patch : 0);
            }
            free(body); continue;
        }
        if (type != 'R') { free(body); continue; }   /* BackendKeyData, notices ... */
        if (len < 4) { free(body); snprintf(err, errsz, "malformed login message"); return 0; }
        int32_t code = pg_get32(body);
        HttpBuf out = { NULL, 0, 0 };
        if (code == 0) { free(body); continue; }   /* AuthenticationOk */
        if (code == 3) {   /* cleartext password */
            size_t a = pg_begin(&out, 'p'); pg_cstr(&out, o->password); pg_end(&out, a);
        } else if (code == 5) {   /* MD5 */
            if (len < 8) { free(body); snprintf(err, errsz, "malformed MD5 request"); return 0; }
            unsigned char d[16]; char hex1[33], hex2[33];
            size_t pl = strlen(o->password), ul = strlen(o->user);
            char *pu = malloc(pl + ul + 1); memcpy(pu, o->password, pl); memcpy(pu + pl, o->user, ul);
            EVP_Digest(pu, pl + ul, d, NULL, EVP_md5(), NULL); free(pu);
            for (int i = 0; i < 16; i++) snprintf(hex1 + 2 * i, 3, "%02x", d[i]);
            unsigned char salt_in[36]; memcpy(salt_in, hex1, 32); memcpy(salt_in + 32, body + 4, 4);
            EVP_Digest(salt_in, 36, d, NULL, EVP_md5(), NULL);
            for (int i = 0; i < 16; i++) snprintf(hex2 + 2 * i, 3, "%02x", d[i]);
            char resp[40]; snprintf(resp, sizeof(resp), "md5%s", hex2);
            size_t a = pg_begin(&out, 'p'); pg_cstr(&out, resp); pg_end(&out, a);
        } else if (code == 10) {   /* SASL: pick SCRAM-SHA-256 */
            int has = 0;
            for (size_t i = 4; i < len && body[i]; ) { if (strcmp((char*)body + i, "SCRAM-SHA-256") == 0) has = 1; i += strlen((char*)body + i) + 1; }
            if (!has) { free(body); snprintf(err, errsz, "the server asks for a login method GlowLang does not support (SCRAM channel binding)"); return 0; }
            unsigned char rnd[18];
            if (RAND_bytes(rnd, sizeof(rnd)) != 1) { free(body); snprintf(err, errsz, "no secure random numbers available"); return 0; }
            pg_b64(rnd, sizeof(rnd), nonce);
            snprintf(client_first_bare, sizeof(client_first_bare), "n=,r=%s", nonce);
            char first[160]; snprintf(first, sizeof(first), "n,,%s", client_first_bare);
            size_t a = pg_begin(&out, 'p');
            pg_cstr(&out, "SCRAM-SHA-256");
            pg_put32(&out, (int32_t)strlen(first));
            hb_add(&out, first, strlen(first));
            pg_end(&out, a);
        } else if (code == 11) {   /* SASLContinue: server-first */
            size_t sl = len - 4;
            if (!client_first_bare[0] || sl >= sizeof(server_first)) { free(body); snprintf(err, errsz, "unexpected SCRAM message"); return 0; }
            memcpy(server_first, body + 4, sl); server_first[sl] = 0;
            char rnonce[256], salt64[256], iters[32];
            if (!pg_scram_attr(server_first, 'r', rnonce, sizeof(rnonce)) || !pg_scram_attr(server_first, 's', salt64, sizeof(salt64)) ||
                !pg_scram_attr(server_first, 'i', iters, sizeof(iters)) || strncmp(rnonce, nonce, strlen(nonce)) != 0) {
                free(body); snprintf(err, errsz, "the server's SCRAM reply is invalid"); return 0;
            }
            unsigned char salt[192];
            int saltlen = pg_unb64(salt64, salt, sizeof(salt));
            long iter = strtol(iters, NULL, 10);
            if (saltlen <= 0 || iter < 1 || iter > 10000000) { free(body); snprintf(err, errsz, "the server's SCRAM reply is invalid"); return 0; }
            PKCS5_PBKDF2_HMAC(o->password, (int)strlen(o->password), salt, saltlen, (int)iter, EVP_sha256(), 32, salted);
            unsigned char client_key[32], stored_key[32], sig[32], proof[32];
            pg_hmac(salted, 32, "Client Key", 10, client_key);
            SHA256(client_key, 32, stored_key);
            char without_proof[300];
            snprintf(without_proof, sizeof(without_proof), "c=biws,r=%s", rnonce);
            snprintf(auth_message, sizeof(auth_message), "%s,%s,%s", client_first_bare, server_first, without_proof);
            pg_hmac(stored_key, 32, auth_message, strlen(auth_message), sig);
            for (int i = 0; i < 32; i++) proof[i] = client_key[i] ^ sig[i];
            char proof64[64]; pg_b64(proof, 32, proof64);
            char final_msg[400]; snprintf(final_msg, sizeof(final_msg), "%s,p=%s", without_proof, proof64);
            size_t a = pg_begin(&out, 'p'); hb_add(&out, final_msg, strlen(final_msg)); pg_end(&out, a);
        } else if (code == 12) {   /* SASLFinal: check the server knows the password too */
            char v64[128]; unsigned char v[64], server_key[32], expect[32];
            char *sf = malloc(len - 4 + 1); memcpy(sf, body + 4, len - 4); sf[len - 4] = 0;
            int got = pg_scram_attr(sf, 'v', v64, sizeof(v64));
            free(sf);
            if (!got || pg_unb64(v64, v, sizeof(v)) != 32 || !auth_message[0]) { free(body); snprintf(err, errsz, "the server's SCRAM signature is missing"); return 0; }
            pg_hmac(salted, 32, "Server Key", 10, server_key);
            pg_hmac(server_key, 32, auth_message, strlen(auth_message), expect);
            if (CRYPTO_memcmp(v, expect, 32) != 0) { free(body); snprintf(err, errsz, "the server's SCRAM signature is wrong; it may not be the real server"); return 0; }
            scram_done = 1;
            free(body); continue;
        } else {
            free(body);
            snprintf(err, errsz, "the server asks for a login method GlowLang does not support (code %d)", (int)code);
            return 0;
        }
        free(body);
        int sent = pg_send(c, out.p, out.n);
        free(out.p);
        if (!sent) { snprintf(err, errsz, "the connection to the server was lost during login"); return 0; }
    }
    (void)scram_done;
    http_set_timeouts(c->s, o->timeout);
    c->timeout = o->timeout;
    return 1;
}

static void pg_shutdown(PgConn *c) {
    if (c->ssl) { SSL_free(c->ssl); c->ssl = NULL; }
    if (c->s != HTTP_BAD_SOCK) { http_closesock(c->s); c->s = HTTP_BAD_SOCK; }
}

static char *pg_opt_str(Dict *o, const char *key, const char *dflt) {
    Value v = dict_get(o, key);
    if (GET_TYPE(v) == VAL_NONE) return strdup(dflt);
    return value_to_new_cstring(v);
}

/* pg.connect({host, port, user, password, database, sslmode,
               connect_timeout, timeout, application_name}) -> handle */
static Value pg_connect_native(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_DICT) return pg_fail("pg.connect needs a dict of options");
    Dict *d = AS_DICT(args[0]);
    PgOpts o;
    memset(&o, 0, sizeof(o));
    char *host = pg_opt_str(d, "host", "localhost");
    snprintf(o.host, sizeof(o.host), "%s", host);
    free(host);
    Value v = dict_get(d, "port");
    o.port = GET_TYPE(v) == VAL_INT ? (int)AS_INT(v) : 5432;
    if (o.port <= 0 || o.port > 65535) return pg_fail("port must be between 1 and 65535");
    o.user = pg_opt_str(d, "user", "postgres");
    o.password = pg_opt_str(d, "password", "");
    o.database = pg_opt_str(d, "database", o.user);
    o.appname = pg_opt_str(d, "application_name", "glowlang");
    char *mode = pg_opt_str(d, "sslmode", "prefer");
    if (strcmp(mode, "disable") == 0) o.sslmode = 0;
    else if (strcmp(mode, "prefer") == 0 || strcmp(mode, "allow") == 0) o.sslmode = 1;
    else if (strcmp(mode, "require") == 0) o.sslmode = 2;
    else if (strcmp(mode, "verify-full") == 0 || strcmp(mode, "verify-ca") == 0) o.sslmode = 3;
    else o.sslmode = -1;
    free(mode);
    v = dict_get(d, "connect_timeout");
    o.connect_timeout = (GET_TYPE(v) == VAL_INT || GET_TYPE(v) == VAL_FLOAT) ? val_to_double(v) : 10;
    if (o.connect_timeout <= 0) o.connect_timeout = 10;
    v = dict_get(d, "timeout");
    o.timeout = (GET_TYPE(v) == VAL_INT || GET_TYPE(v) == VAL_FLOAT) ? val_to_double(v) : 0;
    if (o.timeout < 0) o.timeout = 0;
    Value result = value_none();
    if (o.sslmode < 0) { result = pg_fail("sslmode must be disable, prefer, require or verify-full"); goto done; }
    int slot = -1;
    for (int i = 1; i < PG_MAX_CONNS; i++) if (!pg_conns[i]) { slot = i; break; }
    if (slot < 0) { result = pg_fail("too many open connections"); goto done; }
    PgConn *c = calloc(1, sizeof(PgConn));
    c->s = HTTP_BAD_SOCK;
    c->busy = 1;
    pg_conns[slot] = c;   /* reserve the slot before letting other threads run */
    char err[1024] = "";
    glow_gil_release();
    int ok = pg_open(c, &o, err, sizeof(err));
    glow_gil_acquire();
    if (!ok) {
        pg_shutdown(c);
        free(c);
        pg_conns[slot] = NULL;
        char msg[1200];
        if (strncmp(err, "cannot connect", 14) == 0 || strncmp(err, "cannot resolve", 14) == 0) snprintf(msg, sizeof(msg), "%s", err);
        else snprintf(msg, sizeof(msg), "cannot connect to %s:%d: %s", o.host, o.port, err);
        result = pg_fail(msg);
        goto done;
    }
    c->busy = 0;
    result = value_int(slot);
done:
    free(o.user); free(o.password); free(o.database); free(o.appname);
    return result;
}

static PgConn *pg_take(Value h, Value *errv) {
    if (GET_TYPE(h) != VAL_INT || AS_INT(h) <= 0 || AS_INT(h) >= PG_MAX_CONNS || !pg_conns[AS_INT(h)]) { *errv = pg_fail("the connection is closed"); return NULL; }
    PgConn *c = pg_conns[AS_INT(h)];
    if (c->broken) { *errv = pg_fail("the connection was lost; open a new one"); return NULL; }
    if (c->busy) { *errv = pg_fail("the connection is in use by another thread; give each thread its own connection"); return NULL; }
    c->busy = 1;
    return c;
}

/* Sends req and reads every message up to ReadyForQuery into raw, as
   [type][int32 length][body] records. Runs without the GIL. */
static int pg_roundtrip(PgConn *c, const HttpBuf *req, HttpBuf *raw, char *err, size_t errsz) {
    if (!pg_send(c, req->p, req->n)) { snprintf(err, errsz, "the connection to the server was lost"); return 0; }
    char last_error[600] = "";
    for (;;) {
        char type; unsigned char *body; size_t len;
        double waited_from = http_now();
        if (!pg_read_msg(c, &type, &body, &len, err, errsz)) {
            if (c->timeout > 0 && http_now() - waited_from >= c->timeout * 0.95)
                snprintf(err, errsz, "the server did not answer within %g second%s; the connection is closed", c->timeout, c->timeout == 1 ? "" : "s");
            else if (last_error[0])   /* the server said why before closing, e.g. an admin ended the session */
                snprintf(err, errsz, "the connection to the server was lost: %s", last_error);
            return 0;
        }
        if (type == 'E') pg_error_text(body, len, last_error, sizeof(last_error));
        if (type == 'N' || type == 'A' || type == 'S') { free(body); continue; }   /* notices, notifications, settings */
        hb_add(raw, &type, 1);
        HttpBuf tmp = { NULL, 0, 0 };
        pg_put32(&tmp, (int32_t)(len + 4));
        hb_add(raw, tmp.p, 4);
        free(tmp.p);
        if (len) hb_add(raw, (const char*)body, len);
        int ready = type == 'Z';
        if (ready) c->tx = len ? (char)body[0] : 'I';
        free(body);
        if (ready) return 1;
    }
}

/* Text of one parameter value, or NULL for SQL NULL. Arrays use the
   PostgreSQL array literal; dicts become JSON. */
static void pg_param_text(Value v, HttpBuf *b, int in_array);
static void pg_array_elem(Value v, HttpBuf *b) {
    int t = GET_TYPE(v);
    if (t == VAL_NONE) { hb_add(b, "NULL", 4); return; }
    if (t == VAL_LIST) { pg_param_text(v, b, 1); return; }
    HttpBuf s = { NULL, 0, 0 };
    pg_param_text(v, &s, 1);
    hb_add(b, "\"", 1);
    for (size_t i = 0; i < s.n; i++) { if (s.p[i] == '"' || s.p[i] == '\\') hb_add(b, "\\", 1); hb_add(b, s.p + i, 1); }
    hb_add(b, "\"", 1);
    free(s.p);
}
static void pg_param_text(Value v, HttpBuf *b, int in_array) {
    int t = GET_TYPE(v);
    if (t == VAL_BOOL) { hb_add(b, AS_BOOL(v) ? "true" : "false", AS_BOOL(v) ? 4 : 5); return; }
    if (t == VAL_FLOAT) {
        double d = AS_FLOAT(v);
        char num[40];
        if (d != d) snprintf(num, sizeof(num), "NaN");
        else if (isinf(d)) snprintf(num, sizeof(num), d > 0 ? "Infinity" : "-Infinity");
        else snprintf(num, sizeof(num), "%.17g", d);
        hb_add(b, num, strlen(num));
        return;
    }
    if (t == VAL_STRING) { hb_add(b, AS_STRING(v), rope_length(v)); return; }
    if (t == VAL_LIST) {
        List *l = AS_LIST(v);
        hb_add(b, "{", 1);
        for (int i = 0; i < l->size; i++) { if (i) hb_add(b, ",", 1); pg_array_elem(l->items[i], b); }
        hb_add(b, "}", 1);
        return;
    }
    if (t == VAL_DICT) {
        JsonBuf jb = { 0 };
        jb_add(&jb, "", 0);
        json_write(v, &jb, 0);
        hb_add(b, jb.p ? jb.p : "null", jb.p ? jb.n : 4);
        free(jb.p);
        return;
    }
    (void)in_array;
    char *s = value_to_new_cstring(v);   /* ints, big ints and anything else */
    if (s) { hb_add(b, s, strlen(s)); free(s); }
}

/* An int column value, exact even past 48 bits. */
static Value pg_int_value(const char *s) {
    errno = 0;
    char *end;
    long long i = strtoll(s, &end, 10);
    if (errno || *end) return value_bigint(bigint_from_string(s));
    if (fits_int48(i)) return value_int(i);
    return value_bigint(bigint_from_int64(i));
}

static Value pg_scalar(uint32_t oid, const char *s, size_t n);

/* A one-dimensional text-format array: {1,2,NULL,"a b"}. Returns 0 if the
   text is not one (for example a multi-dimensional array). */
static int pg_parse_array(uint32_t elem, const char *s, size_t n, Value *out) {
    if (n < 2 || s[0] != '{' || s[n - 1] != '}') return 0;
    List *l = list_new(0);
    Value result = value_list(l);
    size_t i = 1;
    char *buf = malloc(n + 1);
    while (i < n - 1) {
        size_t k = 0;
        int quoted = 0;
        if (s[i] == '{') { free(buf); return 0; }
        if (s[i] == '"') {
            quoted = 1; i++;
            while (i < n - 1 && s[i] != '"') { if (s[i] == '\\' && i + 1 < n - 1) i++; buf[k++] = s[i++]; }
            i++;
        } else {
            while (i < n - 1 && s[i] != ',') buf[k++] = s[i++];
        }
        buf[k] = 0;
        if (!quoted && k == 4 && strcmp(buf, "NULL") == 0) list_push(l, value_none());
        else list_push(l, pg_scalar(elem, buf, k));
        if (i < n - 1 && s[i] == ',') i++;
    }
    free(buf);
    *out = result;
    return 1;
}

static uint32_t pg_array_elem_oid(uint32_t oid) {
    switch (oid) {
        case 1000: return 16;   case 1005: return 21;  case 1007: return 23;  case 1016: return 20;
        case 1021: return 700;  case 1022: return 701; case 1009: return 25;  case 1015: return 1043;
        case 1231: return 1700; case 199: return 114;  case 3807: return 3802; case 2951: return 2950;
        default: return 0;
    }
}

static Value pg_scalar(uint32_t oid, const char *s, size_t n) {
    switch (oid) {
        case 16: return value_bool(n > 0 && s[0] == 't');
        case 20: case 21: case 23: case 26: return pg_int_value(s);
        case 700: case 701:
            if (strcmp(s, "NaN") == 0) return value_float(NAN);
            if (strcmp(s, "Infinity") == 0) return value_float(INFINITY);
            if (strcmp(s, "-Infinity") == 0) return value_float(-INFINITY);
            return value_from_double(strtod(s, NULL));
        case 114: case 3802: {
            Value str = value_string_copy_len(s, n);
            Value parsed = native_json_parse(1, &str);
            if (has_exception) { has_exception = 0; pending_exception = value_none(); return str; }
            return parsed;
        }
        default: {
            uint32_t elem = pg_array_elem_oid(oid);
            Value arr;
            if (elem && pg_parse_array(elem, s, n, &arr)) return arr;
            return value_string_copy_len(s, n);
        }
    }
}

/* Turns the raw messages of one exchange into a result dict, or raises. */
static Value pg_build_result(PgConn *c, HttpBuf *raw) {
    List *columns = list_new(0);
    Value columns_v = value_list(columns);
    List *rows = list_new(0);
    Value rows_v = value_list(rows);
    uint32_t oids[1600];
    int ncols = 0;
    char command[128] = "";
    int64_t affected = -1;
    char errtext[1100] = "";
    const unsigned char *p = (const unsigned char*)raw->p, *end = p + raw->n;
    (void)c;
    int malformed = 0;
    while (p + 5 <= end && !malformed) {
        char type = (char)p[0];
        size_t len = (size_t)pg_get32(p + 1) - 4;
        const unsigned char *b = p + 5;
        p = b + len;
        const unsigned char *msg_end = p;   /* every read below stays before this */
        if (type == 'E' && !errtext[0]) pg_error_text(b, len, errtext, sizeof(errtext));
        else if (type == 'T') {
            if (len < 2) { malformed = 1; break; }
            ncols = pg_get16(b);
            if (ncols < 0 || ncols > 1600) { malformed = 1; break; }
            const unsigned char *q = b + 2;
            columns->size = 0;
            for (int i = 0; i < ncols; i++) {
                const char *name = (const char*)q;
                size_t nl = strnlen(name, (size_t)(msg_end - q));
                if (q + nl + 1 + 18 > msg_end) { malformed = 1; break; }
                q += nl + 1;
                oids[i] = (uint32_t)pg_get32(q + 6);
                q += 18;
                list_push(columns, value_string_copy(name));
            }
        } else if (type == 'D') {
            if (len < 2) { malformed = 1; break; }
            int nf = pg_get16(b);
            const unsigned char *q = b + 2;
            List *row = list_new(0);
            Value row_v = value_list(row);
            list_push(rows, row_v);
            for (int i = 0; i < nf; i++) {
                if (q + 4 > msg_end) { malformed = 1; break; }
                int32_t fl = pg_get32(q); q += 4;
                if (fl < 0) { list_push(row, value_none()); continue; }
                if ((size_t)fl > (size_t)(msg_end - q)) { malformed = 1; break; }
                char save = (char)q[fl];
                ((unsigned char*)q)[fl] = 0;   /* the next record's type byte; restored below */
                list_push(row, pg_scalar(i < ncols ? oids[i] : 25, (const char*)q, (size_t)fl));
                ((unsigned char*)q)[fl] = (unsigned char)save;
                q += fl;
            }
        } else if (type == 'C') {
            snprintf(command, sizeof(command), "%.*s", (int)strnlen((const char*)b, len), (const char*)b);
            const char *last = strrchr(command, ' ');
            if (last && isdigit((unsigned char)last[1])) affected = strtoll(last + 1, NULL, 10);
        }
    }
    if (malformed) return pg_fail("the server sent a malformed result");
    if (errtext[0]) return pg_fail(errtext);
    Dict *r = dict_new(4);
    dict_set(r, "columns", columns_v);
    dict_set(r, "rows", rows_v);
    dict_set(r, "command", value_string_copy(command));
    dict_set(r, "affected", value_int(affected));
    return value_dict(r);
}

static Value pg_finish(PgConn *c, HttpBuf *req, int ok_build) {
    if (!ok_build) { free(req->p); c->busy = 0; return value_none(); }
    HttpBuf raw = { NULL, 0, 0 };
    char err[512] = "";
    glow_gil_release();
    int ok = pg_roundtrip(c, req, &raw, err, sizeof(err));
    glow_gil_acquire();
    free(req->p);
    c->busy = 0;
    if (!ok) {
        free(raw.p);
        c->broken = 1;
        pg_shutdown(c);
        return pg_fail(err);
    }
    size_t old_gc_thresh = gc_threshold;
    gc_threshold = (size_t)-1;   /* no collection while the result is half built */
    Value r = pg_build_result(c, &raw);
    gc_threshold = old_gc_thresh;
    free(raw.p);
    return r;
}

/* pg.query(handle, sql, params) -> {columns, rows, command, affected} */
static Value pg_query_native(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[1]) != VAL_STRING) return pg_fail("query needs SQL text");
    Value e;
    PgConn *c = pg_take(args[0], &e);
    if (!c) return e;
    const char *sql = AS_STRING(args[1]);
    if (strlen(sql) != rope_length(args[1])) { c->busy = 0; return pg_fail("the SQL text contains a NUL character"); }
    List *params = (argc > 2 && GET_TYPE(args[2]) == VAL_LIST) ? AS_LIST(args[2]) : NULL;
    if (argc > 2 && GET_TYPE(args[2]) != VAL_LIST && GET_TYPE(args[2]) != VAL_NONE) { c->busy = 0; return pg_fail("parameters must be a list"); }
    int np = params ? params->size : 0;
    if (np > 65535) { c->busy = 0; return pg_fail("too many parameters (at most 65535)"); }
    HttpBuf req = { NULL, 0, 0 };
    size_t a = pg_begin(&req, 'P');   /* Parse, unnamed statement, types inferred */
    pg_cstr(&req, ""); pg_cstr(&req, sql); pg_put16(&req, 0);
    pg_end(&req, a);
    a = pg_begin(&req, 'B');          /* Bind: all parameters and results in text */
    pg_cstr(&req, ""); pg_cstr(&req, "");
    pg_put16(&req, 0);
    pg_put16(&req, (int16_t)np);
    for (int i = 0; i < np; i++) {
        Value v = params->items[i];
        if (GET_TYPE(v) == VAL_NONE) { pg_put32(&req, -1); continue; }
        HttpBuf t = { NULL, 0, 0 };
        hb_add(&t, "", 0);
        pg_param_text(v, &t, 0);
        if (memchr(t.p, 0, t.n)) { free(t.p); free(req.p); c->busy = 0; return pg_fail("a parameter contains a NUL character"); }
        pg_put32(&req, (int32_t)t.n);
        hb_add(&req, t.p, t.n);
        free(t.p);
    }
    pg_put16(&req, 0);
    pg_end(&req, a);
    a = pg_begin(&req, 'D'); hb_add(&req, "P", 1); pg_cstr(&req, ""); pg_end(&req, a);
    a = pg_begin(&req, 'E'); pg_cstr(&req, ""); pg_put32(&req, 0); pg_end(&req, a);
    a = pg_begin(&req, 'S'); pg_end(&req, a);
    return pg_finish(c, &req, 1);
}

/* pg.script(handle, sql): runs several statements (simple protocol), no
   parameters. For migrations and setup. */
static Value pg_script_native(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[1]) != VAL_STRING) return pg_fail("script needs SQL text");
    Value e;
    PgConn *c = pg_take(args[0], &e);
    if (!c) return e;
    if (strlen(AS_STRING(args[1])) != rope_length(args[1])) { c->busy = 0; return pg_fail("the SQL text contains a NUL character"); }
    HttpBuf req = { NULL, 0, 0 };
    size_t a = pg_begin(&req, 'Q'); pg_cstr(&req, AS_STRING(args[1])); pg_end(&req, a);
    return pg_finish(c, &req, 1);
}

/* pg.status(handle) -> "idle", "transaction" or "failed" */
static Value pg_status_native(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT || AS_INT(args[0]) <= 0 || AS_INT(args[0]) >= PG_MAX_CONNS || !pg_conns[AS_INT(args[0])]) return value_string_copy("closed");
    PgConn *c = pg_conns[AS_INT(args[0])];
    if (c->broken) return value_string_copy("broken");
    return value_string_copy(c->tx == 'T' ? "transaction" : c->tx == 'E' ? "failed" : "idle");
}

static Value pg_server_version_native(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT || AS_INT(args[0]) <= 0 || AS_INT(args[0]) >= PG_MAX_CONNS || !pg_conns[AS_INT(args[0])]) return value_none();
    return value_int(pg_conns[AS_INT(args[0])]->server_version);
}

/* pg.close(handle): says goodbye and closes. Closing twice is fine. */
static Value pg_close_native(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT || AS_INT(args[0]) <= 0 || AS_INT(args[0]) >= PG_MAX_CONNS) return value_none();
    int h = (int)AS_INT(args[0]);
    PgConn *c = pg_conns[h];
    if (!c) return value_none();
    if (c->busy) return pg_fail("the connection is in use by another thread");
    pg_conns[h] = NULL;
    if (!c->broken) {
        HttpBuf req = { NULL, 0, 0 };
        size_t a = pg_begin(&req, 'X'); pg_end(&req, a);
        glow_gil_release();
        pg_send(c, req.p, req.n);
        if (c->ssl) SSL_shutdown(c->ssl);
        glow_gil_acquire();
        free(req.p);
    }
    pg_shutdown(c);
    free(c);
    return value_none();
}

/* ---- Signing for JWT / OAuth: crypto.base64, crypto.sign, crypto.verify ...
 *
 * GlowLang strings hold text, not arbitrary bytes, so everything binary
 * (digests, signatures, random bytes) stays in C and crosses into GlowLang
 * as base64url text.
 *
 *   crypto.base64(text, url)          text -> base64 (url=true: base64url, no padding)
 *   crypto.unbase64(text)             base64 or base64url -> text (must be UTF-8)
 *   crypto.random_token(n)            n random bytes as base64url
 *   crypto.sha256_b64url(text)        SHA-256 as base64url (PKCE S256)
 *   crypto.sign(alg, key, data)       JWS signature as base64url
 *   crypto.verify(alg, key, data, s)  true / false
 *   crypto.jwk_to_pem(jwk)            a public JWK (RSA or EC) as PEM text
 *
 * alg: HS256 HS384 HS512 (key: a secret), RS256 RS384 RS512 (RSA, at least
 * 2048 bits), ES256 ES384 (EC P-256 / P-384). Private keys and public keys
 * or certificates are PEM text. An HS algorithm refuses a PEM key, which
 * blocks the "algorithm confusion" attack on verifiers.
 */
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/ecdsa.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>

static Value jws_fail(const char *fn, const char *msg) {
    char buf[300];
    snprintf(buf, sizeof(buf), "crypto.%s: %s", fn, msg);
    pending_exception = value_string_copy(buf);
    has_exception = 1;
    return value_none();
}

static char *b64_encode(const unsigned char *in, size_t n, int url) {
    size_t cap = 4 * ((n + 2) / 3) + 1;
    char *out = malloc(cap);
    if (!out) return NULL;
    int len = EVP_EncodeBlock((unsigned char*)out, in, (int)n);
    if (url) {
        for (int i = 0; i < len; i++) { if (out[i] == '+') out[i] = '-'; else if (out[i] == '/') out[i] = '_'; }
        while (len > 0 && out[len - 1] == '=') len--;
        out[len] = 0;
    }
    return out;
}

/* Accepts standard and url alphabets, with or without padding.
   Returns malloc'd bytes and their count, or NULL on bad input. */
static unsigned char *b64_decode(const char *in, size_t n, size_t *outn) {
    char *t = malloc(n + 4);
    if (!t) return NULL;
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        char c = in[i];
        if (c == '-') c = '+'; else if (c == '_') c = '/';
        else if (c == '=') continue;
        else if (!(isalnum((unsigned char)c) || c == '+' || c == '/')) { free(t); return NULL; }
        t[k++] = c;
    }
    if (k % 4 == 1) { free(t); return NULL; }
    size_t pad = (4 - k % 4) % 4;
    for (size_t i = 0; i < pad; i++) t[k++] = '=';
    unsigned char *out = malloc(k / 4 * 3 + 1);
    int r = k ? EVP_DecodeBlock(out, (unsigned char*)t, (int)k) : 0;
    free(t);
    if (r < 0) { free(out); return NULL; }
    *outn = (size_t)r - pad;
    out[*outn] = 0;
    return out;
}

/* Valid UTF-8 with no NUL (what a GlowLang string can hold). */
static int utf8_ok(const unsigned char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        if (c == 0) return 0;
        if (c < 0x80) { i++; continue; }
        int extra;
        if (c >= 0xC2 && c <= 0xDF) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if (c >= 0xF0 && c <= 0xF4) extra = 3;
        else return 0;
        if (i + (size_t)extra >= n) return 0;
        for (int k = 1; k <= extra; k++) if ((s[i + k] & 0xC0) != 0x80) return 0;
        i += (size_t)extra + 1;
    }
    return 1;
}

static int str_arg(Value v, const char **s, size_t *n) {
    if (GET_TYPE(v) != VAL_STRING) return 0;
    *s = (v & MASK_PAYLOAD) ? AS_STRING(v) : "";
    *n = (v & MASK_PAYLOAD) ? rope_length(v) : 0;
    return 1;
}

static Value crypto_base64_native(int argc, Value *args) {
    const char *s; size_t n;
    if (argc < 1 || !str_arg(args[0], &s, &n)) return jws_fail("base64", "needs text");
    int url = argc > 1 && GET_TYPE(args[1]) == VAL_BOOL && AS_BOOL(args[1]);
    char *out = b64_encode((const unsigned char*)s, n, url);
    Value v = value_string_copy(out ? out : "");
    free(out);
    return v;
}

static Value crypto_unbase64_native(int argc, Value *args) {
    const char *s; size_t n, outn = 0;
    if (argc < 1 || !str_arg(args[0], &s, &n)) return jws_fail("unbase64", "needs text");
    unsigned char *out = b64_decode(s, n, &outn);
    if (!out) return jws_fail("unbase64", "the text is not valid base64");
    if (!utf8_ok(out, outn)) { free(out); return jws_fail("unbase64", "the decoded bytes are not text (binary data or invalid UTF-8)"); }
    Value v = value_string_copy_len((const char*)out, outn);
    free(out);
    return v;
}

static Value crypto_random_token_native(int argc, Value *args) {
    int64_t n;
    if (!random_count_arg(argc, args, "crypto.random_token", &n)) return value_none();
    unsigned char *buf = malloc((size_t)n);
    if (!buf || RAND_bytes(buf, (int)n) != 1) { free(buf); return jws_fail("random_token", "the secure random source failed"); }
    char *out = b64_encode(buf, (size_t)n, 1);
    OPENSSL_cleanse(buf, (size_t)n);
    free(buf);
    Value v = value_string_copy(out ? out : "");
    free(out);
    return v;
}

static Value crypto_sha256_b64url_native(int argc, Value *args) {
    const char *s; size_t n;
    if (argc < 1 || !str_arg(args[0], &s, &n)) return jws_fail("sha256_b64url", "needs text");
    unsigned char md[32];
    SHA256((const unsigned char*)s, n, md);
    char *out = b64_encode(md, 32, 1);
    Value v = value_string_copy(out);
    free(out);
    return v;
}

typedef struct { const char *name; int kind; const EVP_MD *(*md)(void); int ec_bytes; const char *curve; } JwsAlg;
/* kind: 1 HMAC, 2 RSA PKCS#1 v1.5, 3 ECDSA */
static const JwsAlg *jws_alg(const char *name) {
    static const JwsAlg algs[] = {
        { "HS256", 1, EVP_sha256, 0, NULL }, { "HS384", 1, EVP_sha384, 0, NULL }, { "HS512", 1, EVP_sha512, 0, NULL },
        { "RS256", 2, EVP_sha256, 0, NULL }, { "RS384", 2, EVP_sha384, 0, NULL }, { "RS512", 2, EVP_sha512, 0, NULL },
        { "ES256", 3, EVP_sha256, 32, "prime256v1" }, { "ES384", 3, EVP_sha384, 48, "secp384r1" },
    };
    for (size_t i = 0; i < sizeof(algs) / sizeof(algs[0]); i++) if (strcmp(algs[i].name, name) == 0) return &algs[i];
    return NULL;
}

/* Loads a PEM key. want_private: a private key; otherwise a public key,
   a certificate or a private key (its public half is used). */
static EVP_PKEY *jws_load_key(const char *pem, size_t n, int want_private) {
    EVP_PKEY *k = NULL;
    BIO *b = BIO_new_mem_buf(pem, (int)n);
    if (!b) return NULL;
    if (want_private) k = PEM_read_bio_PrivateKey(b, NULL, NULL, (void*)"");
    else {
        k = PEM_read_bio_PUBKEY(b, NULL, NULL, NULL);
        if (!k) {
            (void)BIO_reset(b);
            X509 *x = PEM_read_bio_X509(b, NULL, NULL, NULL);
            if (x) { k = X509_get_pubkey(x); X509_free(x); }
        }
        if (!k) { (void)BIO_reset(b); k = PEM_read_bio_PrivateKey(b, NULL, NULL, (void*)""); }
    }
    BIO_free(b);
    ERR_clear_error();
    return k;
}

/* NULL if the key suits the algorithm, else why not. */
static const char *jws_check_key(const JwsAlg *a, EVP_PKEY *k) {
    if (a->kind == 2) {
        if (EVP_PKEY_get_base_id(k) != EVP_PKEY_RSA) return "the key is not an RSA key";
        if (EVP_PKEY_get_bits(k) < 2048) return "RSA keys must be at least 2048 bits";
    } else if (a->kind == 3) {
        if (EVP_PKEY_get_base_id(k) != EVP_PKEY_EC) return "the key is not an EC key";
        char group[64] = "";
        size_t gl = 0;
        if (!EVP_PKEY_get_utf8_string_param(k, OSSL_PKEY_PARAM_GROUP_NAME, group, sizeof(group), &gl) || strcmp(group, a->curve) != 0)
            return a->ec_bytes == 32 ? "ES256 needs a P-256 key" : "ES384 needs a P-384 key";
    }
    return NULL;
}

static int looks_like_pem(const char *s, size_t n) {
    for (size_t i = 0; i + 10 <= n; i++) if (memcmp(s + i, "-----BEGIN", 10) == 0) return 1;
    return 0;
}

static Value crypto_sign_native(int argc, Value *args) {
    const char *alg, *key, *data; size_t an, kn, dn;
    if (argc < 3 || !str_arg(args[0], &alg, &an) || !str_arg(args[1], &key, &kn) || !str_arg(args[2], &data, &dn))
        return jws_fail("sign", "needs (alg, key, data) as text");
    const JwsAlg *a = jws_alg(alg);
    if (!a) return jws_fail("sign", "unsupported algorithm (use HS256/384/512, RS256/384/512, ES256 or ES384)");
    unsigned char sig[1024];
    size_t sl = sizeof(sig);
    if (a->kind == 1) {
        if (kn == 0) return jws_fail("sign", "the HMAC secret is empty");
        if (looks_like_pem(key, kn)) return jws_fail("sign", "an HS algorithm needs a shared secret, not a PEM key");
        unsigned int ul = 0;
        if (!HMAC(a->md(), key, (int)kn, (const unsigned char*)data, dn, sig, &ul)) return jws_fail("sign", "HMAC failed");
        sl = ul;
    } else {
        EVP_PKEY *k = jws_load_key(key, kn, 1);
        if (!k) return jws_fail("sign", "the key is not a PEM private key");
        const char *why = jws_check_key(a, k);
        if (why) { EVP_PKEY_free(k); return jws_fail("sign", why); }
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        unsigned char der[1024];
        size_t derl = sizeof(der);
        int ok = ctx && EVP_DigestSignInit(ctx, NULL, a->md(), NULL, k) == 1 &&
                 EVP_DigestSign(ctx, a->kind == 3 ? der : sig, a->kind == 3 ? &derl : &sl, (const unsigned char*)data, dn) == 1;
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(k);
        if (!ok) { ERR_clear_error(); return jws_fail("sign", "signing failed"); }
        if (a->kind == 3) {   /* DER (r, s) -> fixed-size r || s as JWS wants */
            const unsigned char *p = der;
            ECDSA_SIG *es = d2i_ECDSA_SIG(NULL, &p, (long)derl);
            if (!es) return jws_fail("sign", "signing failed");
            const BIGNUM *r, *s;
            ECDSA_SIG_get0(es, &r, &s);
            memset(sig, 0, (size_t)a->ec_bytes * 2);
            BN_bn2binpad(r, sig, a->ec_bytes);
            BN_bn2binpad(s, sig + a->ec_bytes, a->ec_bytes);
            ECDSA_SIG_free(es);
            sl = (size_t)a->ec_bytes * 2;
        }
    }
    char *out = b64_encode(sig, sl, 1);
    OPENSSL_cleanse(sig, sizeof(sig));
    Value v = value_string_copy(out);
    free(out);
    return v;
}

static Value crypto_verify_native(int argc, Value *args) {
    const char *alg, *key, *data, *sig64; size_t an, kn, dn, sn;
    if (argc < 4 || !str_arg(args[0], &alg, &an) || !str_arg(args[1], &key, &kn) || !str_arg(args[2], &data, &dn) || !str_arg(args[3], &sig64, &sn))
        return jws_fail("verify", "needs (alg, key, data, signature) as text");
    const JwsAlg *a = jws_alg(alg);
    if (!a) return jws_fail("verify", "unsupported algorithm (use HS256/384/512, RS256/384/512, ES256 or ES384)");
    size_t sigl = 0;
    unsigned char *sig = b64_decode(sig64, sn, &sigl);
    if (!sig) return value_bool(0);
    int good = 0;
    if (a->kind == 1) {
        if (kn == 0 || looks_like_pem(key, kn)) { free(sig); return jws_fail("verify", "an HS algorithm needs a shared secret, not a PEM key"); }
        unsigned char md[EVP_MAX_MD_SIZE]; unsigned int ml = 0;
        HMAC(a->md(), key, (int)kn, (const unsigned char*)data, dn, md, &ml);
        good = ml == sigl && CRYPTO_memcmp(md, sig, ml) == 0;
    } else {
        EVP_PKEY *k = jws_load_key(key, kn, 0);
        if (!k) { free(sig); return jws_fail("verify", "the key is not a PEM public key, certificate or private key"); }
        const char *why = jws_check_key(a, k);
        if (why) { EVP_PKEY_free(k); free(sig); return jws_fail("verify", why); }
        unsigned char der[256];
        const unsigned char *check = sig;
        size_t checkl = sigl;
        int shape_ok = 1;
        if (a->kind == 3) {   /* r || s -> DER for OpenSSL */
            shape_ok = sigl == (size_t)a->ec_bytes * 2;
            if (shape_ok) {
                ECDSA_SIG *es = ECDSA_SIG_new();
                BIGNUM *r = BN_bin2bn(sig, a->ec_bytes, NULL), *s = BN_bin2bn(sig + a->ec_bytes, a->ec_bytes, NULL);
                ECDSA_SIG_set0(es, r, s);
                unsigned char *p = der;
                int dl = i2d_ECDSA_SIG(es, &p);
                ECDSA_SIG_free(es);
                shape_ok = dl > 0;
                check = der; checkl = (size_t)dl;
            }
        }
        if (shape_ok) {
            EVP_MD_CTX *ctx = EVP_MD_CTX_new();
            good = ctx && EVP_DigestVerifyInit(ctx, NULL, a->md(), NULL, k) == 1 &&
                   EVP_DigestVerify(ctx, check, checkl, (const unsigned char*)data, dn) == 1;
            EVP_MD_CTX_free(ctx);
        }
        EVP_PKEY_free(k);
        ERR_clear_error();
    }
    free(sig);
    return value_bool(good);
}

/* A public JWK ({"kty": "RSA", "n", "e"} or {"kty": "EC", "crv", "x", "y"})
   as PEM, for verifying tokens from a provider's JWKS endpoint. */
static unsigned char *jwk_field(Dict *d, const char *name, size_t *n) {
    Value v = dict_get(d, name);
    if (GET_TYPE(v) != VAL_STRING || !(v & MASK_PAYLOAD)) return NULL;
    return b64_decode(AS_STRING(v), rope_length(v), n);
}

static Value crypto_jwk_to_pem_native(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_DICT) return jws_fail("jwk_to_pem", "needs a JWK dict");
    Dict *d = AS_DICT(args[0]);
    Value kty = dict_get(d, "kty");
    if (GET_TYPE(kty) != VAL_STRING) return jws_fail("jwk_to_pem", "the JWK has no kty");
    EVP_PKEY *k = NULL;
    EVP_PKEY_CTX *pc = NULL;
    OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
    OSSL_PARAM *params = NULL;
    BIGNUM *bn_n = NULL, *bn_e = NULL;
    unsigned char *a = NULL, *b = NULL, *pt = NULL;
    size_t an = 0, bnl = 0;
    const char *err = NULL;
    if (strcmp(AS_STRING(kty), "RSA") == 0) {
        a = jwk_field(d, "n", &an); b = jwk_field(d, "e", &bnl);
        if (!a || !b || an == 0 || bnl == 0) err = "an RSA JWK needs n and e";
        else {
            bn_n = BN_bin2bn(a, (int)an, NULL); bn_e = BN_bin2bn(b, (int)bnl, NULL);
            OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, bn_n);
            OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, bn_e);
            params = OSSL_PARAM_BLD_to_param(bld);
            pc = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL);
        }
    } else if (strcmp(AS_STRING(kty), "EC") == 0) {
        Value crv = dict_get(d, "crv");
        const char *group = NULL; size_t len = 0;
        if (GET_TYPE(crv) == VAL_STRING && strcmp(AS_STRING(crv), "P-256") == 0) { group = "prime256v1"; len = 32; }
        else if (GET_TYPE(crv) == VAL_STRING && strcmp(AS_STRING(crv), "P-384") == 0) { group = "secp384r1"; len = 48; }
        a = jwk_field(d, "x", &an); b = jwk_field(d, "y", &bnl);
        if (!group) err = "only the P-256 and P-384 curves are supported";
        else if (!a || !b || an != len || bnl != len) err = "an EC JWK needs x and y of the curve's size";
        else {
            pt = malloc(1 + 2 * len);
            pt[0] = 4; memcpy(pt + 1, a, len); memcpy(pt + 1 + len, b, len);
            OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, group, 0);
            OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, pt, 1 + 2 * len);
            params = OSSL_PARAM_BLD_to_param(bld);
            pc = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
        }
    } else err = "kty must be RSA or EC";
    if (!err && (!pc || !params || EVP_PKEY_fromdata_init(pc) != 1 || EVP_PKEY_fromdata(pc, &k, EVP_PKEY_PUBLIC_KEY, params) != 1))
        err = "the key data is invalid";
    Value result = value_none();
    if (!err) {
        BIO *out = BIO_new(BIO_s_mem());
        if (out && PEM_write_bio_PUBKEY(out, k)) {
            char *p; long n = BIO_get_mem_data(out, &p);
            result = value_string_copy_len(p, (size_t)n);
        } else err = "cannot write the key";
        BIO_free(out);
    }
    EVP_PKEY_free(k); EVP_PKEY_CTX_free(pc); OSSL_PARAM_free(params); OSSL_PARAM_BLD_free(bld);
    BN_free(bn_n); BN_free(bn_e); free(a); free(b); free(pt);
    ERR_clear_error();
    if (err) return jws_fail("jwk_to_pem", err);
    return result;
}

/* crypto.generate_keypair(alg) -> {"private": PEM, "public": PEM} for
   RS256/384/512 (RSA 2048, or "bits" 3072/4096 as a second argument),
   ES256 (P-256) or ES384 (P-384). */
static Value crypto_generate_keypair_native(int argc, Value *args) {
    const char *alg; size_t an;
    if (argc < 1 || !str_arg(args[0], &alg, &an)) return jws_fail("generate_keypair", "needs an algorithm such as RS256 or ES256");
    const JwsAlg *a = jws_alg(alg);
    if (!a || a->kind == 1) return jws_fail("generate_keypair", "use RS256, RS384, RS512, ES256 or ES384 (HS algorithms use crypto.random_token(32) as the secret)");
    EVP_PKEY *k = NULL;
    if (a->kind == 2) {
        int bits = argc > 1 && GET_TYPE(args[1]) == VAL_INT ? (int)AS_INT(args[1]) : 2048;
        if (bits != 2048 && bits != 3072 && bits != 4096) return jws_fail("generate_keypair", "RSA keys are 2048, 3072 or 4096 bits");
        glow_gil_release();
        k = EVP_RSA_gen((unsigned int)bits);
        glow_gil_acquire();
    } else {
        k = EVP_EC_gen(a->curve);
    }
    if (!k) { ERR_clear_error(); return jws_fail("generate_keypair", "key generation failed"); }
    BIO *priv = BIO_new(BIO_s_mem()), *pub = BIO_new(BIO_s_mem());
    Value result = value_none();
    if (priv && pub && PEM_write_bio_PrivateKey(priv, k, NULL, NULL, 0, NULL, NULL) && PEM_write_bio_PUBKEY(pub, k)) {
        char *p1, *p2;
        long n1 = BIO_get_mem_data(priv, &p1), n2 = BIO_get_mem_data(pub, &p2);
        Dict *d = dict_new(2);
        dict_set(d, "private", value_string_copy_len(p1, (size_t)n1));
        dict_set(d, "public", value_string_copy_len(p2, (size_t)n2));
        result = value_dict(d);
    }
    BIO_free(priv); BIO_free(pub); EVP_PKEY_free(k);
    if (GET_TYPE(result) == VAL_NONE) return jws_fail("generate_keypair", "cannot write the keys");
    return result;
}

/* crypto.pem_to_jwk(pem) -> the public JWK of a PEM key or certificate
   ({"kty": "RSA", "n", "e"} or {"kty": "EC", "crv", "x", "y"}), for
   publishing your own JWKS. */
static void jwk_put_bn(Dict *d, const char *name, const BIGNUM *bn, int pad) {
    int n = pad ? pad : BN_num_bytes(bn);
    unsigned char *buf = malloc((size_t)(n ? n : 1));
    BN_bn2binpad(bn, buf, n);
    char *t = b64_encode(buf, (size_t)n, 1);
    dict_set(d, name, value_string_copy(t));
    free(t); free(buf);
}

static Value crypto_pem_to_jwk_native(int argc, Value *args) {
    const char *pem; size_t n;
    if (argc < 1 || !str_arg(args[0], &pem, &n)) return jws_fail("pem_to_jwk", "needs PEM text");
    EVP_PKEY *k = jws_load_key(pem, n, 0);
    if (!k) return jws_fail("pem_to_jwk", "the text is not a PEM key or certificate");
    Dict *d = dict_new(4);
    Value result = value_dict(d);
    const char *err = NULL;
    if (EVP_PKEY_get_base_id(k) == EVP_PKEY_RSA) {
        BIGNUM *bn_n = NULL, *bn_e = NULL;
        if (EVP_PKEY_get_bn_param(k, OSSL_PKEY_PARAM_RSA_N, &bn_n) && EVP_PKEY_get_bn_param(k, OSSL_PKEY_PARAM_RSA_E, &bn_e)) {
            dict_set(d, "kty", value_string_copy("RSA"));
            jwk_put_bn(d, "n", bn_n, 0);
            jwk_put_bn(d, "e", bn_e, 0);
        } else err = "cannot read the RSA key";
        BN_free(bn_n); BN_free(bn_e);
    } else if (EVP_PKEY_get_base_id(k) == EVP_PKEY_EC) {
        char group[64] = ""; size_t gl = 0;
        EVP_PKEY_get_utf8_string_param(k, OSSL_PKEY_PARAM_GROUP_NAME, group, sizeof(group), &gl);
        int len = strcmp(group, "prime256v1") == 0 ? 32 : strcmp(group, "secp384r1") == 0 ? 48 : 0;
        BIGNUM *x = NULL, *y = NULL;
        if (!len) err = "only P-256 and P-384 keys are supported";
        else if (EVP_PKEY_get_bn_param(k, OSSL_PKEY_PARAM_EC_PUB_X, &x) && EVP_PKEY_get_bn_param(k, OSSL_PKEY_PARAM_EC_PUB_Y, &y)) {
            dict_set(d, "kty", value_string_copy("EC"));
            dict_set(d, "crv", value_string_copy(len == 32 ? "P-256" : "P-384"));
            jwk_put_bn(d, "x", x, len);
            jwk_put_bn(d, "y", y, len);
        } else err = "cannot read the EC key";
        BN_free(x); BN_free(y);
    } else err = "only RSA and EC keys are supported";
    EVP_PKEY_free(k);
    ERR_clear_error();
    if (err) return jws_fail("pem_to_jwk", err);
    return result;
}

static Value os_time(int argc, Value *args) { (void)argc; (void)args; struct timeval tv; gettimeofday(&tv, NULL); return value_float(tv.tv_sec + tv.tv_usec / 1000000.0); }

#include <time.h>
static Value os_format_time(int argc, Value *args) {
    if (argc < 2) return value_none();
    const char *format = AS_STRING(args[0]);
    double ts = val_to_double(args[1]);
    time_t rawtime = (time_t)ts;
    struct tm *timeinfo = localtime(&rawtime);
    char buffer[256];
    strftime(buffer, sizeof(buffer), format, timeinfo);
    return value_string_copy(buffer);
}

/* Regular expressions use GlowLang's own engine (regex.c) on every platform,
   so patterns behave identically on Windows, Linux and macOS. */
int glow_regex_search(const char *pattern, const char *text, int *match_start, int *match_len,
                      char *err, size_t errlen);
typedef struct GlowRegex GlowRegex;

/* A growable byte buffer for building re results. */
typedef struct { char *p; size_t n, cap; } ReBuf;
static void rb_add(ReBuf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
}

GlowRegex *glow_regex_compile(const char *pattern, char *err, size_t errlen);
void glow_regex_free(GlowRegex *re);
int glow_regex_groups(const GlowRegex *re);
const char *glow_regex_group_name(const GlowRegex *re, int i);
int glow_regex_each(const GlowRegex *re, const char *text, int len, int limit,
                    int (*on_match)(void *ctx, const int *caps), void *ctx);

/* Compiles args[0], raising the error if it is not a valid pattern. */
static GlowRegex *re_compile_arg(Value pattern, const char *fn) {
    char err[160];
    if (GET_TYPE(pattern) != VAL_STRING) {
        snprintf(err, sizeof(err), "re.%s: the pattern must be a string", fn);
        pending_exception = value_string_copy(err); has_exception = 1;
        return NULL;
    }
    GlowRegex *re = glow_regex_compile(AS_STRING(pattern), err, sizeof(err));
    if (!re) { pending_exception = value_string_copy(err); has_exception = 1; }
    return re;
}

static int re_text_arg(Value text, const char *fn) {
    if (GET_TYPE(text) == VAL_STRING) return 1;
    char err[120];
    snprintf(err, sizeof(err), "re.%s: the text must be a string", fn);
    pending_exception = value_string_copy(err); has_exception = 1;
    return 0;
}

/* A match as a dict: text, start, end, groups (a list, none for a group
   that did not take part) and named (name -> text). */
static Value re_match_dict(const GlowRegex *re, const char *text, const int *caps, int64_t start_char, int64_t end_char) {
    Dict *m = dict_new(8);
    dict_set(m, "text", value_string_copy_len(text + caps[0], (size_t)(caps[1] - caps[0])));
    dict_set(m, "start", value_int(start_char));
    dict_set(m, "end", value_int(end_char));
    int ng = glow_regex_groups(re);
    List *groups = list_new(ng);
    Dict *named = dict_new(4);
    for (int g = 1; g <= ng; g++) {
        Value v = caps[2 * g] >= 0 && caps[2 * g + 1] >= 0
            ? value_string_copy_len(text + caps[2 * g], (size_t)(caps[2 * g + 1] - caps[2 * g]))
            : value_none();
        list_push(groups, v);
        const char *name = glow_regex_group_name(re, g);
        if (name) dict_set(named, name, v);
    }
    dict_set(m, "groups", value_list(groups));
    dict_set(m, "named", value_dict(named));
    return value_dict(m);
}

/* text_v is the text as a value; (byte, ch) is a cursor for turning the
   byte offsets the matcher reports into character positions, moved forward
   as matches arrive in order. */
typedef struct { const GlowRegex *re; const char *text; List *out; int mode; Value text_v; size_t byte; int64_t ch; } ReCollect;

/* The character position of byte offset `at` (matches come in order). */
static int64_t re_char_pos(ReCollect *c, size_t at) {
    if (at < c->byte) { c->byte = 0; c->ch = 0; }
    size_t n = rope_length(c->text_v);
    while (c->byte < at && c->byte < n) { c->byte = glow_utf8_step(c->text, c->byte, n); c->ch++; }
    return c->ch;
}

/* mode 0: match dicts; 1: findall values (the match, its one group, or a
   list of its groups). */
static int re_collect(void *ctx, const int *caps) {
    ReCollect *c = ctx;
    if (c->mode == 0) {
        int64_t a = re_char_pos(c, (size_t)caps[0]), b = re_char_pos(c, (size_t)caps[1]);
        list_push(c->out, re_match_dict(c->re, c->text, caps, a, b));
        return 0;
    }
    int ng = glow_regex_groups(c->re);
    if (ng == 0) {
        list_push(c->out, value_string_copy_len(c->text + caps[0], (size_t)(caps[1] - caps[0])));
    } else {
        List *row = list_new(ng);
        for (int g = 1; g <= ng; g++) {
            int s = caps[2 * g], e = caps[2 * g + 1];
            list_push(row, s >= 0 && e >= 0 ? value_string_copy_len(c->text + s, (size_t)(e - s)) : value_string_copy(""));
        }
        list_push(c->out, ng == 1 ? row->items[0] : value_list(row));
    }
    return 0;
}

/* re_native.find(pattern, text): the first match or none. */
static Value native_re_find(int argc, Value *args) {
    if (argc < 2) return value_none();
    GlowRegex *re = re_compile_arg(args[0], "find");
    if (!re) return value_none();
    if (!re_text_arg(args[1], "find")) { glow_regex_free(re); return value_none(); }
    const char *text = AS_STRING(args[1]);
    ReCollect c = { re, text, list_new(1), 0, args[1], 0, 0 };
    glow_regex_each(re, text, (int)strlen(text), 1, re_collect, &c);
    Value r = c.out->size ? c.out->items[0] : value_none();
    glow_regex_free(re);
    return r;
}

/* re_native.all(pattern, text, findall_mode): every match, as match dicts
   or (findall_mode) as findall values. */
static Value native_re_all(int argc, Value *args) {
    if (argc < 2) return value_list(list_new(0));
    GlowRegex *re = re_compile_arg(args[0], "findall");
    if (!re) return value_none();
    if (!re_text_arg(args[1], "findall")) { glow_regex_free(re); return value_none(); }
    const char *text = AS_STRING(args[1]);
    ReCollect c = { re, text, list_new(4), argc > 2 && GET_TYPE(args[2]) == VAL_BOOL && AS_BOOL(args[2]), args[1], 0, 0 };
    glow_regex_each(re, text, (int)strlen(text), 0, re_collect, &c);
    glow_regex_free(re);
    return value_list(c.out);
}

typedef struct {
    const GlowRegex *re; const char *text; const char *repl;
    ReBuf sb; int last; const char *err;
} ReReplace;

/* Appends the replacement for one match: \1..\99 and \g<name> / \g<1>
   insert groups, \\ a backslash; other text is copied. */
static int re_replace_one(void *ctx, const int *caps) {
    ReReplace *r = ctx;
    rb_add(&r->sb, r->text + r->last, (size_t)(caps[0] - r->last));
    int ng = glow_regex_groups(r->re);
    for (const char *p = r->repl; *p; p++) {
        if (*p != '\\' || !p[1]) { rb_add(&r->sb, p, 1); continue; }
        int g = -1;
        if (p[1] == '\\') { rb_add(&r->sb, "\\", 1); p++; continue; }
        if (p[1] >= '0' && p[1] <= '9') {
            g = p[1] - '0'; p++;
            if (p[1] >= '0' && p[1] <= '9' && g * 10 + (p[1] - '0') <= ng) { g = g * 10 + (p[1] - '0'); p++; }
        } else if (p[1] == 'g' && p[2] == '<') {
            const char *name = p + 3, *end = strchr(name, '>');
            if (!end) { r->err = "re.replace: unterminated \\g<...> in the replacement"; return 1; }
            size_t nlen = (size_t)(end - name);
            char buf[64];
            if (nlen == 0 || nlen >= sizeof(buf)) { r->err = "re.replace: bad group name in the replacement"; return 1; }
            memcpy(buf, name, nlen); buf[nlen] = '\0';
            if (buf[0] >= '0' && buf[0] <= '9') g = atoi(buf);
            else for (int i = 1; i <= ng; i++) {
                const char *gn = glow_regex_group_name(r->re, i);
                if (gn && strcmp(gn, buf) == 0) { g = i; break; }
            }
            if (g < 0) { r->err = "re.replace: unknown group name in the replacement"; return 1; }
            p = end;
        } else { rb_add(&r->sb, p, 1); continue; }
        if (g > ng) { r->err = "re.replace: the replacement refers to a group the pattern does not have"; return 1; }
        int s = caps[2 * g], e = caps[2 * g + 1];
        if (s >= 0 && e >= 0) rb_add(&r->sb, r->text + s, (size_t)(e - s));
    }
    r->last = caps[1];
    return 0;
}

/* re_native.replace(pattern, text, replacement, count): count 0 replaces
   every match. */
static Value native_re_replace(int argc, Value *args) {
    if (argc < 3) return value_none();
    GlowRegex *re = re_compile_arg(args[0], "replace");
    if (!re) return value_none();
    if (!re_text_arg(args[1], "replace") || !re_text_arg(args[2], "replace")) { glow_regex_free(re); return value_none(); }
    const char *text = AS_STRING(args[1]);
    int count = argc > 3 && GET_TYPE(args[3]) == VAL_INT ? (int)AS_INT(args[3]) : 0;
    ReReplace r = { re, text, AS_STRING(args[2]), { NULL, 0, 0 }, 0, NULL };
    glow_regex_each(re, text, (int)strlen(text), count, re_replace_one, &r);
    glow_regex_free(re);
    if (r.err) { free(r.sb.p); pending_exception = value_string_copy(r.err); has_exception = 1; return value_none(); }
    rb_add(&r.sb, text + r.last, strlen(text + r.last));
    Value out = value_string_copy_len(r.sb.p ? r.sb.p : "", r.sb.n);
    free(r.sb.p);
    return out;
}

typedef struct { const GlowRegex *re; const char *text; List *out; int last; } ReSplit;

/* Text between matches, plus each match's groups (as Python does). An
   empty match at the very start splits nothing off. */
static int re_split_one(void *ctx, const int *caps) {
    ReSplit *s = ctx;
    if (caps[1] == 0 && caps[0] == 0) return 0;
    list_push(s->out, value_string_copy_len(s->text + s->last, (size_t)(caps[0] - s->last)));
    int ng = glow_regex_groups(s->re);
    for (int g = 1; g <= ng; g++) {
        int a = caps[2 * g], b = caps[2 * g + 1];
        list_push(s->out, a >= 0 && b >= 0 ? value_string_copy_len(s->text + a, (size_t)(b - a)) : value_none());
    }
    s->last = caps[1];
    return 0;
}

/* re_native.split(pattern, text, maxsplit): maxsplit 0 splits at every
   match. */
static Value native_re_split(int argc, Value *args) {
    if (argc < 2) return value_none();
    GlowRegex *re = re_compile_arg(args[0], "split");
    if (!re) return value_none();
    if (!re_text_arg(args[1], "split")) { glow_regex_free(re); return value_none(); }
    const char *text = AS_STRING(args[1]);
    int maxsplit = argc > 2 && GET_TYPE(args[2]) == VAL_INT ? (int)AS_INT(args[2]) : 0;
    ReSplit s = { re, text, list_new(4), 0 };
    glow_regex_each(re, text, (int)strlen(text), maxsplit, re_split_one, &s);
    list_push(s.out, value_string_copy(text + s.last));
    glow_regex_free(re);
    return value_list(s.out);
}

/* re_native.escape(text): text with every regex metacharacter escaped. */
static Value native_re_escape(int argc, Value *args) {
    if (argc < 1 || !re_text_arg(args[0], "escape")) return value_none();
    ReBuf sb = { NULL, 0, 0 };
    for (const char *p = AS_STRING(args[0]); *p; p++) {
        if (strchr("\\.^$|?*+()[]{}-/", *p)) rb_add(&sb, "\\", 1);
        rb_add(&sb, p, 1);
    }
    Value out = value_string_copy_len(sb.p ? sb.p : "", sb.n);
    free(sb.p);
    return out;
}

static Value native_re_match(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_STRING || GET_TYPE(args[1]) != VAL_STRING) return value_bool(0);
    int ms, ml;
    char err[128];
    int r = glow_regex_search(AS_STRING(args[0]), AS_STRING(args[1]), &ms, &ml, err, sizeof(err));
    if (r < 0) {
        pending_exception = value_string_copy(err);
        has_exception = 1;
        return value_bool(0);
    }
    return value_bool(r == 1);
}

static Value native_re_search(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_STRING || GET_TYPE(args[1]) != VAL_STRING) return value_none();
    const char *text = AS_STRING(args[1]);
    int ms, ml;
    char err[128];
    int r = glow_regex_search(AS_STRING(args[0]), text, &ms, &ml, err, sizeof(err));
    if (r < 0) {
        pending_exception = value_string_copy(err);
        has_exception = 1;
        return value_none();
    }
    if (r == 0) return value_none();
    return value_string_copy_len(text + ms, (size_t)ml);
}

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
    int opt = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return value_int(-4);
    listen(sockfd, 10); return value_int(sockfd);
}
static Value ssl_accept(int argc, Value *args) {
    if (argc < 1) return value_none();
    int server_fd = AS_INT(args[0]);
    glow_gil_release();
    int client_fd = accept(server_fd, NULL, NULL);
    glow_gil_acquire();
    if (client_fd < 0) return value_none();
    SSL *ssl = SSL_new(ssl_ctx); SSL_set_fd(ssl, client_fd);
    glow_gil_release();
    int accept_res = SSL_accept(ssl);
    glow_gil_acquire();
    if (accept_res <= 0) { SSL_free(ssl); close(client_fd); return value_none(); }
    return make_value(VAL_OBJECT, (uint64_t)(uintptr_t)ssl);
}
static Value net_listen(int argc, Value *args) {
    if (argc < 1) return value_int(-1);
    int port = AS_INT(args[0]), sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(port);
    int opt = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return value_int(-1);
    listen(sockfd, 10); return value_int(sockfd);
}
static Value net_close(int argc, Value *args) { if (argc < 1) return value_int(-1); close(AS_INT(args[0])); return value_none(); }
static Value http_listen(int argc, Value *args) {
    if (argc < 1) return value_int(-1);
    int port = AS_INT(args[0]), sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(port);
    int opt = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return value_int(-1);
    listen(sockfd, 1024); return value_int(sockfd);
}
static Value http_accept(int argc, Value *args) {
    if (argc < 1) return value_int(-1);
    int sockfd = AS_INT(args[0]);
    struct sockaddr_in client_addr; socklen_t client_len = sizeof(client_addr);
    glow_gil_release();
    int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
    glow_gil_acquire();
    return value_int(client_fd);
}
/* hdr starts with the lower-case name, in any case. */
static int http_name_is(const char *hdr, const char *name, size_t len) {
    for (size_t k = 0; k < len; k++) {
        char c = hdr[k];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != name[k]) return 0;
    }
    return 1;
}

/* The Content-Length in the header block hdr[0..n), or -1 when there is none. */
static long long http_content_length(const char *hdr, size_t n) {
    static const char name[] = "content-length:";
    size_t len = sizeof(name) - 1;
    for (size_t i = 0; i + len <= n; i++) {
        if ((i == 0 || hdr[i - 1] == '\n') && http_name_is(hdr + i, name, len)) {
            size_t j = i + len;
            while (j < n && (hdr[j] == ' ' || hdr[j] == '\t')) j++;
            long long v = 0; int digits = 0;
            while (j < n && hdr[j] >= '0' && hdr[j] <= '9' && digits < 15) { v = v * 10 + (hdr[j] - '0'); j++; digits++; }
            return digits ? v : -1;
        }
    }
    return -1;
}

static const char *http_find_crlf2(const char *p, size_t n) {
    for (size_t i = 0; i + 3 < n; i++)
        if (p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' && p[i + 3] == '\n') return p + i;
    return NULL;
}

/* http.read(client): one whole HTTP request, the headers and the
   Content-Length bytes of body. It used to return a single recv() of at
   most 4095 bytes, so anything larger arrived cut short. Data that does not
   start like an HTTP request is returned as soon as it arrives, as before.
   Returns none when the connection sends nothing. */
#define HTTP_READ_MAX_HEADERS (64 * 1024)
#define HTTP_READ_MAX_TOTAL (64 * 1024 * 1024)
static Value http_read(int argc, Value *args) {
    if (argc < 1) return value_none();
    int fd = AS_INT(args[0]);
    size_t cap = 16384, n = 0;
    char *buf = malloc(cap);
    if (!buf) return value_none();
    long long want = -1;          /* total bytes of the request, once known */
    glow_gil_release();
    for (;;) {
        if (n == cap) {
            size_t ncap = cap * 2;
            char *nb = realloc(buf, ncap);
            if (!nb) break;
            buf = nb; cap = ncap;
        }
        int got = read(fd, buf + n, (int)(cap - n));
        if (got <= 0) break;
        n += (size_t)got;
        if (want < 0) {
            const char *end = http_find_crlf2(buf, n);
            if (!end) {
                /* Not HTTP (no "HTTP/" in the first line yet): hand it over. */
                const char *nl = memchr(buf, '\n', n);
                size_t first = nl ? (size_t)(nl - buf) : n;
                int http_like = 0;
                for (size_t i = 0; i + 5 <= first; i++) if (memcmp(buf + i, "HTTP/", 5) == 0) { http_like = 1; break; }
                if ((nl && !http_like) || n >= HTTP_READ_MAX_HEADERS) break;
                continue;
            }
            size_t head = (size_t)(end - buf) + 4;
            long long body = http_content_length(buf, head);
            if (body < 0) body = 0;
            want = (long long)head + body;
            if (want > HTTP_READ_MAX_TOTAL) want = HTTP_READ_MAX_TOTAL;
        }
        if ((long long)n >= want) break;
    }
    glow_gil_acquire();
    if (n == 0) { free(buf); return value_none(); }
    Value r = value_string_copy_len(buf, n);
    free(buf);
    return r;
}
static Value http_write(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[1]) != VAL_STRING) return value_int(-1);
    int fd = AS_INT(args[0]);
    const char *resp = AS_STRING(args[1]);
    size_t total = rope_length(args[1]), sent = 0;
    glow_gil_release();
    /* A socket write may take only part of the data; keep going. */
    while (sent < total) {
        int res = write(fd, resp + sent, (int)(total - sent));
        if (res <= 0) break;
        sent += (size_t)res;
    }
    glow_gil_acquire();
    if (sent < total) return value_int(-1);
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
        if (pa->type == pb->type) {
            if (pa->size == pb->size) {
                PackedArray *res = packed_array_new_uninit(pa->type, pa->size);
                int sz = pa->size;
                if (pa->type == PACKED_FLOAT64) {
                    double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
                    int i = glow_cpu_has_avx2_fma() ? simd_add_f64(dr, da, db, sz) : 0;
                    for (; i < sz; i++) dr[i] = da[i] + db[i];
                } else {
                    int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
                    int i = glow_cpu_has_avx2_fma() ? simd_add_i64(ir, ia, ib, sz) : 0;
                    for (; i < sz; i++) ir[i] = ia[i] + ib[i];
                }
                // Copy shape from pa
                res->ndims = pa->ndims;
                for (int d=0; d<pa->ndims; d++) { res->shape[d] = pa->shape[d]; res->strides[d] = pa->strides[d]; }
                return value_packed(res);
            } else if (pa->size % pb->size == 0) { // Broadcast bias to tensor
                PackedArray *res = packed_array_new_uninit(pa->type, pa->size);
                int sz = pa->size;
                int bsz = pb->size;
                if (pa->type == PACKED_FLOAT64) {
                    double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
                    for (int i=0; i<sz; i++) dr[i] = da[i] + db[i % bsz];
                } else {
                    int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
                    for (int i=0; i<sz; i++) ir[i] = ia[i] + ib[i % bsz];
                }
                res->ndims = pa->ndims;
                for (int d=0; d<pa->ndims; d++) { res->shape[d] = pa->shape[d]; res->strides[d] = pa->strides[d]; }
                return value_packed(res);
            } else {
                printf("tensor.add fail: pa->size = %d, pb->size = %d\n", pa->size, pb->size);
            }
        } else {
            printf("tensor.add fail: pa->type = %d, pb->type = %d\n", pa->type, pb->type);
        }
    } else {
        printf("tensor.add fail: arg type mismatch. a=%d, b=%d\n", GET_TYPE(a), GET_TYPE(b));
    }
    return value_none();
}

static Value native_packed_sub(int argc, Value *args) {
    if (argc < 2) return value_none();
    Value a = args[0], b = args[1];
    if (GET_TYPE(a) == VAL_OBJECT && GET_OBJ_TYPE(a) == OBJ_PACKED_ARRAY &&
        GET_TYPE(b) == VAL_OBJECT && GET_OBJ_TYPE(b) == OBJ_PACKED_ARRAY) {
        PackedArray *pa = AS_PACKED_ARRAY(a), *pb = AS_PACKED_ARRAY(b);
        if (pa->type == pb->type) {
            if (pa->size == pb->size) {
                PackedArray *res = packed_array_new_uninit(pa->type, pa->size);
                int sz = pa->size;
                if (pa->type == PACKED_FLOAT64) {
                    double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
                    int i = glow_cpu_has_avx2_fma() ? simd_sub_f64(dr, da, db, sz) : 0;
                    for (; i < sz; i++) dr[i] = da[i] - db[i];
                } else {
                    int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
                    int i = glow_cpu_has_avx2_fma() ? simd_sub_i64(ir, ia, ib, sz) : 0;
                    for (; i < sz; i++) ir[i] = ia[i] - ib[i];
                }
                res->ndims = pa->ndims;
                for (int d=0; d<pa->ndims; d++) { res->shape[d] = pa->shape[d]; res->strides[d] = pa->strides[d]; }
                return value_packed(res);
            } else if (pa->size % pb->size == 0) {
                PackedArray *res = packed_array_new_uninit(pa->type, pa->size);
                int sz = pa->size;
                int bsz = pb->size;
                if (pa->type == PACKED_FLOAT64) {
                    double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
                    for (int i=0; i<sz; i++) dr[i] = da[i] - db[i % bsz];
                } else {
                    int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
                    for (int i=0; i<sz; i++) ir[i] = ia[i] - ib[i % bsz];
                }
                res->ndims = pa->ndims;
                for (int d=0; d<pa->ndims; d++) { res->shape[d] = pa->shape[d]; res->strides[d] = pa->strides[d]; }
                return value_packed(res);
            }
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
        if (pa->type == pb->type) {
            if (pa->size == pb->size) {
                PackedArray *res = packed_array_new_uninit(pa->type, pa->size);
                int sz = pa->size;
                if (pa->type == PACKED_FLOAT64) {
                    double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
                    int i = glow_cpu_has_avx2_fma() ? simd_mul_f64(dr, da, db, sz) : 0;
                    for (; i < sz; i++) dr[i] = da[i] * db[i];
                } else {
                    int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
                    int i = 0;
                    // No direct 64-bit integer multiplication in AVX2, simple loop
                    for (; i < sz; i++) ir[i] = ia[i] * ib[i];
                }
                res->ndims = pa->ndims;
                for (int d=0; d<pa->ndims; d++) { res->shape[d] = pa->shape[d]; res->strides[d] = pa->strides[d]; }
                return value_packed(res);
            } else if (pa->size % pb->size == 0) {
                PackedArray *res = packed_array_new_uninit(pa->type, pa->size);
                int sz = pa->size;
                int bsz = pb->size;
                if (pa->type == PACKED_FLOAT64) {
                    double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
                    for (int i=0; i<sz; i++) dr[i] = da[i] * db[i % bsz];
                } else {
                    int64_t *ia = (int64_t*)pa->data, *ib = (int64_t*)pb->data, *ir = (int64_t*)res->data;
                    for (int i=0; i<sz; i++) ir[i] = ia[i] * ib[i % bsz];
                }
                res->ndims = pa->ndims;
                for (int d=0; d<pa->ndims; d++) { res->shape[d] = pa->shape[d]; res->strides[d] = pa->strides[d]; }
                return value_packed(res);
            }
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
                int i = glow_cpu_has_avx2_fma() ? simd_dot_f64(da, db, sz, &sum) : 0;
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

/* ---- SQLite ----
   exec(db, sql, params) and query(db, sql, params) bind params to the
   statement's ? (a list) or :name (a dict) placeholders, so values are never
   parsed as SQL. Errors raise "sqlite: <message>" instead of being printed. */

static Value sqlite_error(const char *what, const char *msg) {
    char buf[600];
    snprintf(buf, sizeof(buf), "sqlite: %s%s%s", what, msg ? ": " : "", msg ? msg : "");
    pending_exception = value_string_copy(buf);
    has_exception = 1;
    return value_none();
}

/* The open connection behind args[0], or NULL with an error set. */
static sqlite3 *sqlite_db_arg(int argc, Value *args, const char *fn) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_OBJECT || !(args[0] & MASK_PAYLOAD) || GET_OBJ_TYPE(args[0]) != OBJ_SQLITE_HANDLE) {
        sqlite_error(fn, "expected a database from sqlite.open");
        return NULL;
    }
    SQLiteHandle *h = (SQLiteHandle*)(uintptr_t)(args[0] & MASK_PAYLOAD);
    if (!h->db) { sqlite_error(fn, "the database is closed"); return NULL; }
    return (sqlite3*)h->db;
}

static int sqlite_bind_value(sqlite3_stmt *st, int idx, Value v) {
    int64_t i;
    if (GET_TYPE(v) == VAL_NONE) return sqlite3_bind_null(st, idx);
    if (GET_TYPE(v) == VAL_BOOL) return sqlite3_bind_int64(st, idx, AS_BOOL(v) ? 1 : 0);
    if (GET_TYPE(v) == VAL_INT) return sqlite3_bind_int64(st, idx, AS_INT(v));
    if (is_bigint(v)) {
        if (bigint_fits_int64(AS_BIGINT(v), &i)) return sqlite3_bind_int64(st, idx, i);
        char *txt = bigint_to_new_string(AS_BIGINT(v));
        int rc = sqlite3_bind_text(st, idx, txt, -1, SQLITE_TRANSIENT);
        free(txt);
        return rc;
    }
    if (GET_TYPE(v) == VAL_FLOAT) {
        double d = AS_FLOAT(v);
        if (d == floor(d) && fabs(d) <= 9007199254740992.0) return sqlite3_bind_int64(st, idx, (int64_t)d);
        return sqlite3_bind_double(st, idx, d);
    }
    if (GET_TYPE(v) == VAL_STRING) return sqlite3_bind_text(st, idx, AS_STRING(v), (int)rope_length(v), SQLITE_TRANSIENT);
    return SQLITE_MISMATCH;
}

/* Bind a list (positional) or dict (named) of parameters. */
static int sqlite_bind_params(sqlite3_stmt *st, Value params, const char *fn) {
    int want = sqlite3_bind_parameter_count(st);
    if (GET_TYPE(params) == VAL_NONE) {
        if (want) { sqlite_error(fn, "the statement has placeholders but no values were given"); return 0; }
        return 1;
    }
    if (GET_TYPE(params) == VAL_LIST) {
        List *l = AS_LIST(params);
        if (l->size != want) {
            char msg[128];
            snprintf(msg, sizeof(msg), "the statement has %d placeholder%s but %d value%s given", want, want == 1 ? "" : "s", l->size, l->size == 1 ? " was" : "s were");
            sqlite_error(fn, msg);
            return 0;
        }
        for (int i = 0; i < l->size; i++)
            if (sqlite_bind_value(st, i + 1, l->items[i]) != SQLITE_OK) { sqlite_error(fn, "a value cannot be stored (use numbers, strings, bools or none)"); return 0; }
        return 1;
    }
    if (GET_TYPE(params) == VAL_DICT) {
        Dict *d = AS_DICT(params);
        for (int i = 1; i <= want; i++) {
            const char *name = sqlite3_bind_parameter_name(st, i);
            if (!name) { sqlite_error(fn, "use :name placeholders with a dict of values"); return 0; }
            int at = dict_get_idx(d, name + 1);
            if (at < 0) { char msg[200]; snprintf(msg, sizeof(msg), "no value for %s", name); sqlite_error(fn, msg); return 0; }
            if (sqlite_bind_value(st, i, d->values[at]) != SQLITE_OK) { sqlite_error(fn, "a value cannot be stored (use numbers, strings, bools or none)"); return 0; }
        }
        return 1;
    }
    sqlite_error(fn, "parameters must be a list (for ?) or a dict (for :name)");
    return 0;
}

static Value sqlite_int_value(int64_t v) {
    if (fits_int48(v)) return value_int(v);
    if (v >= -INT53_MAX && v <= INT53_MAX) return value_float((double)v);
    return value_bigint(bigint_from_int64(v));
}

/* The current row of a statement as a dict. 64-bit integers stay exact (they
   used to be cut to 48 bits) and BLOBs come back as bytes. */
Value glow_sqlite_row(void *stmt) {
    sqlite3_stmt *st = (sqlite3_stmt*)stmt;
    int cols = sqlite3_column_count(st);
    Dict *row = dict_new(cols > 0 ? cols : 1);
    Value rv = value_dict(row);
    for (int i = 0; i < cols; i++) {
        const char *interned = intern_string(sqlite3_column_name(st, i));
        Value val = value_none();
        switch (sqlite3_column_type(st, i)) {
            case SQLITE_INTEGER: val = sqlite_int_value(sqlite3_column_int64(st, i)); break;
            case SQLITE_FLOAT: val = value_float(sqlite3_column_double(st, i)); break;
            case SQLITE_TEXT: val = value_string_copy_len((const char*)sqlite3_column_text(st, i), (size_t)sqlite3_column_bytes(st, i)); break;
            case SQLITE_BLOB: val = value_string_copy_len((const char*)sqlite3_column_blob(st, i), (size_t)sqlite3_column_bytes(st, i)); break;
            default: break;
        }
        dict_set(row, interned, val);
    }
    return rv;
}

static Value native_sqlite_exec(int argc, Value *args) {
    if (argc >= 2 && GET_TYPE(args[0]) == VAL_DICT && GET_TYPE(args[1]) == VAL_OBJECT) { argc--; args++; }
    sqlite3 *db = sqlite_db_arg(argc, args, "exec");
    if (!db) return value_none();
    if (argc < 2 || GET_TYPE(args[1]) != VAL_STRING) return sqlite_error("exec", "expected SQL text");
    const char *sql = AS_STRING(args[1]);
    Value params = argc >= 3 ? args[2] : value_none();
    if (GET_TYPE(params) == VAL_NONE) {
        /* No values: may run several statements separated by ';'. */
        char *err_msg = NULL;
        if (sqlite3_exec(db, sql, 0, 0, &err_msg) != SQLITE_OK) {
            Value e = sqlite_error("exec", err_msg ? err_msg : sqlite3_errmsg(db));
            sqlite3_free(err_msg);
            return e;
        }
        return value_int(sqlite3_changes(db));
    }
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, sql, -1, &st, 0) != SQLITE_OK) return sqlite_error("exec", sqlite3_errmsg(db));
    if (!sqlite_bind_params(st, params, "exec")) { sqlite3_finalize(st); return value_none(); }
    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {}
    if (rc != SQLITE_DONE) { Value e = sqlite_error("exec", sqlite3_errmsg(db)); sqlite3_finalize(st); return e; }
    sqlite3_finalize(st);
    return value_int(sqlite3_changes(db));
}

/* step_query(db, sql, params): rows one at a time, for `for each row in ...`. */
static Value native_sqlite_step_query(int argc, Value *args) {
    if (argc >= 2 && GET_TYPE(args[0]) == VAL_DICT && GET_TYPE(args[1]) == VAL_OBJECT) { argc--; args++; }
    sqlite3 *db = sqlite_db_arg(argc, args, "step_query");
    if (!db) return value_none();
    if (argc < 2 || GET_TYPE(args[1]) != VAL_STRING) return sqlite_error("step_query", "expected SQL text");
    sqlite3_stmt *res;
    if (sqlite3_prepare_v2(db, AS_STRING(args[1]), -1, &res, 0) != SQLITE_OK) return sqlite_error("step_query", sqlite3_errmsg(db));
    if (!sqlite_bind_params(res, argc >= 3 ? args[2] : value_none(), "step_query")) { sqlite3_finalize(res); return value_none(); }
    SQLiteIter *it = gc_alloc(sizeof(SQLiteIter), OBJ_SQLITE_ITER);
    it->stmt = res;
    it->done = 0;
    return make_value(VAL_OBJECT, (uintptr_t)it);
}

/* query(db, sql, params): all rows as a list of dicts. */
static Value native_sqlite_query(int argc, Value *args) {
    if (argc >= 2 && GET_TYPE(args[0]) == VAL_DICT && GET_TYPE(args[1]) == VAL_OBJECT) { argc--; args++; }
    sqlite3 *db = sqlite_db_arg(argc, args, "query");
    if (!db) return value_none();
    if (argc < 2 || GET_TYPE(args[1]) != VAL_STRING) return sqlite_error("query", "expected SQL text");
    sqlite3_stmt *res;
    if (sqlite3_prepare_v2(db, AS_STRING(args[1]), -1, &res, 0) != SQLITE_OK) return sqlite_error("query", sqlite3_errmsg(db));
    if (!sqlite_bind_params(res, argc >= 3 ? args[2] : value_none(), "query")) { sqlite3_finalize(res); return value_none(); }
    List *rows = list_new(8);
    Value rows_v = value_list(rows);
    vm_stack[vm_stack_top++] = rows_v;   /* keep the list alive while rows are built */
    int rc;
    while ((rc = sqlite3_step(res)) == SQLITE_ROW) list_push(rows, glow_sqlite_row(res));
    vm_stack_top--;
    if (rc != SQLITE_DONE) { Value e = sqlite_error("query", sqlite3_errmsg(db)); sqlite3_finalize(res); return e; }
    sqlite3_finalize(res);
    return rows_v;
}

/* last_insert_id(db) and changes(db): from the most recent statement. */
static Value native_sqlite_last_insert_id(int argc, Value *args) {
    if (argc >= 2 && GET_TYPE(args[0]) == VAL_DICT) { argc--; args++; }
    sqlite3 *db = sqlite_db_arg(argc, args, "last_insert_id");
    return db ? sqlite_int_value(sqlite3_last_insert_rowid(db)) : value_none();
}

static Value native_sqlite_changes(int argc, Value *args) {
    if (argc >= 2 && GET_TYPE(args[0]) == VAL_DICT) { argc--; args++; }
    sqlite3 *db = sqlite_db_arg(argc, args, "changes");
    return db ? value_int(sqlite3_changes(db)) : value_none();
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
    if (!h->db) return value_int(SQLITE_OK);
    /* Clear the handle so neither a second close nor the collector's
       finalizer closes the same connection again (that was a use after free).
       close_v2 defers the real close until open query iterators finish. */
    int rc = sqlite3_close_v2(h->db);
    h->db = NULL;
    return value_int(rc);
}

static Value native_stacktrace(int argc, Value *args) {
    (void)argc; (void)args;
    print_stack_trace();
    return value_none();
}

/* Compiled code does not count instructions, so profiling runs everything
   in the interpreter (the counts are then exact and repeatable). */
static int profile_saved_nojit = 0;
static Value native_profile_start(int argc, Value *args) {
    (void)argc; (void)args;
    if (!profiling_active) profile_saved_nojit = glow_nojitexec;
    profiling_active = 1;
    glow_nojitexec = 1;
    return value_none();
}

static int profile_compare(const void *a, const void *b) {
    typedef struct { const char *file; int line; long count; } ProfileEntry;
    const ProfileEntry *x = a, *y = b;
    /* Most executions first; ties by file and line so the report is the
       same on every platform (qsort is not stable). */
    if (x->count != y->count) return x->count < y->count ? 1 : -1;
    int f = strcmp(x->file ? x->file : "", y->file ? y->file : "");
    if (f) return f;
    return (x->line > y->line) - (x->line < y->line);
}

static Value native_profile_stop(int argc, Value *args) {
    (void)argc; (void)args;
    if (profiling_active) glow_nojitexec = profile_saved_nojit;
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

/* glowlang profile program.glow: count each line's runs, report at the end. */
void glow_profile_begin(void) { native_profile_start(0, NULL); }
void glow_profile_report(void) { native_profile_stop(0, NULL); }

void print_error_trace(Value exception);

static Value os_sleep(int argc, Value *args) {
    if (argc > 0) {
        /* Milliseconds, int or float (`whenever` polls with 0.01). Reading a
           float with AS_INT used to turn 0.01 into an endless sleep. */
        double ms = val_to_double(args[0]);
        if (!(ms > 0)) ms = 0;
        if (ms > 86400000.0) ms = 86400000.0;
        // Drop GIL while sleeping so other threads can run
        glow_gil_release();
#ifdef _WIN32
        Sleep((DWORD)ms);
#else
        struct timespec ts;
        ts.tv_sec = (time_t)(ms / 1000.0);
        ts.tv_nsec = (long)((ms - (double)ts.tv_sec * 1000.0) * 1000000.0);
        while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
#endif
        glow_gil_acquire();
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

static Value native_tensor_ones(int argc, Value *args) {
    if (argc < 1 || (GET_TYPE(args[0]) != VAL_INT && GET_TYPE(args[0]) != VAL_FLOAT)) return value_none();
    int sz = (int)val_to_double(args[0]);
    if (sz < 0 || sz > 10000000) return value_none();
    PackedArray *pa = packed_array_new(PACKED_FLOAT64, sz);
    if (pa->data && sz > 0) {
        double *data = (double*)pa->data;
        for (int i = 0; i < sz; i++) data[i] = 1.0;
    }
    return value_packed(pa);
}

static Value native_tensor_get(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_OBJECT || GET_OBJ_TYPE(args[0]) != OBJ_PACKED_ARRAY) return value_none();
    PackedArray *pa = AS_PACKED_ARRAY(args[0]);
    int idx = 0;
    if (GET_TYPE(args[1]) == VAL_INT || GET_TYPE(args[1]) == VAL_FLOAT) {
        idx = (int)val_to_double(args[1]);
    } else if (GET_TYPE(args[1]) == VAL_OBJECT && GET_OBJ_TYPE(args[1]) == OBJ_LIST) {
        List *idx_list = (List*)(uintptr_t)(args[1] & MASK_PAYLOAD);
        if (idx_list->size != pa->ndims) return value_none();
        for (int i = 0; i < pa->ndims; i++) {
            int dim_idx = (int)val_to_double(idx_list->items[i]);
            if (dim_idx < 0 || dim_idx >= pa->shape[i]) return value_none(); // Out of bounds
            idx += dim_idx * pa->strides[i];
        }
    } else {
        return value_none();
    }

    idx += pa->offset;
    if (idx < 0 || idx >= pa->size) return value_none();
    return packed_array_get(pa, idx);
}

static Value native_tensor_set(int argc, Value *args) {
    if (argc < 3 || GET_TYPE(args[0]) != VAL_OBJECT || GET_OBJ_TYPE(args[0]) != OBJ_PACKED_ARRAY) return value_none();
    PackedArray *pa = AS_PACKED_ARRAY(args[0]);
    int idx = 0;
    if (GET_TYPE(args[1]) == VAL_INT || GET_TYPE(args[1]) == VAL_FLOAT) {
        idx = (int)val_to_double(args[1]);
    } else if (GET_TYPE(args[1]) == VAL_OBJECT && GET_OBJ_TYPE(args[1]) == OBJ_LIST) {
        List *idx_list = (List*)(uintptr_t)(args[1] & MASK_PAYLOAD);
        if (idx_list->size != pa->ndims) return value_none();
        for (int i = 0; i < pa->ndims; i++) {
            int dim_idx = (int)val_to_double(idx_list->items[i]);
            if (dim_idx < 0 || dim_idx >= pa->shape[i]) return value_none(); // Out of bounds
            idx += dim_idx * pa->strides[i];
        }
    } else {
        return value_none();
    }

    idx += pa->offset;
    if (idx >= 0 && idx < pa->size) {
        packed_array_set(pa, idx, args[2]);
    }
    return value_none();
}

Value native_tensor_shape(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_OBJECT || GET_OBJ_TYPE(args[0]) != OBJ_PACKED_ARRAY) return value_none();
    PackedArray *pa = AS_PACKED_ARRAY(args[0]);
    List *l = list_new(pa->ndims);
    for (int i = 0; i < pa->ndims; i++) {
        list_push(l, value_int(pa->shape[i]));
    }
    return value_list(l);
}

static Value native_tensor_reshape(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_OBJECT || GET_OBJ_TYPE(args[0]) != OBJ_PACKED_ARRAY) {
        printf("reshape fail: invalid arg0\n");
        return value_none();
    }
    PackedArray *orig = AS_PACKED_ARRAY(args[0]);
    if (GET_TYPE(args[1]) == VAL_OBJECT && GET_OBJ_TYPE(args[1]) == OBJ_PACKED_ARRAY) {
        PackedArray *pa = AS_PACKED_ARRAY(args[1]);
        int new_shape[8];
        int new_ndims = pa->size;
        if (new_ndims > 8) return value_none();
        int total_elements = 1;
        for(int i=0; i<new_ndims; i++) {
            new_shape[i] = (int)val_to_double(packed_array_get(pa, i));
            total_elements *= new_shape[i];
        }
        if (total_elements != orig->size) return value_none();

        PackedArray *view = gc_alloc(sizeof(PackedArray), OBJ_PACKED_ARRAY);
        view->type = orig->type;
        view->size = orig->size;
        view->capacity = orig->capacity;
        view->data = orig->data;
        view->is_view = true;
        view->base = orig->base ? orig->base : orig;

        view->ndims = new_ndims;
        int current_stride = 1;
        for (int i = new_ndims - 1; i >= 0; i--) {
            view->shape[i] = new_shape[i];
            view->strides[i] = current_stride;
            current_stride *= new_shape[i];
        }
        view->offset = orig->offset;
        return value_packed(view);
    }
    if (GET_TYPE(args[1]) != VAL_LIST) {
        printf("reshape fail: arg1 is not a list or packed array. type = %d\n", GET_TYPE(args[1]));
        return value_none();
    }
    List *shape_list = (List*)(uintptr_t)(args[1] & MASK_PAYLOAD);

    int new_shape[8];
    int new_ndims = shape_list->size;
    if (new_ndims > 8) {
        printf("reshape fail: ndims > 8\n");
        return value_none();
    }

    int total_elements = 1;
    for (int i = 0; i < new_ndims; i++) {
        new_shape[i] = (int)val_to_double(shape_list->items[i]);
        total_elements *= new_shape[i];
    }

    if (total_elements != orig->size) {
        printf("reshape fail: size mismatch. expected %d got %d\n", orig->size, total_elements);
        return value_none();
    }

    // Create view
    PackedArray *view = gc_alloc(sizeof(PackedArray), OBJ_PACKED_ARRAY);
    view->type = orig->type;
    view->size = orig->size;
    view->capacity = orig->capacity;
    view->data = orig->data;
    view->is_view = true;
    view->base = orig->base ? orig->base : orig;

    view->ndims = new_ndims;
    int current_stride = 1;
    for (int i = new_ndims - 1; i >= 0; i--) {
        view->shape[i] = new_shape[i];
        view->strides[i] = current_stride;
        current_stride *= new_shape[i];
    }
    view->offset = orig->offset;

    return value_packed(view);
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

    res->ndims = 2;
    res->shape[0] = cols;
    res->shape[1] = rows;
    res->strides[0] = rows;
    res->strides[1] = 1;

    return value_packed(res);
}

#if defined(__x86_64__) || defined(_M_X64)

typedef struct {
    int start_row;
    int end_row;
    double *da;
    double *db;
    double *dr;
    int cA;
    int cB;
} MatmulThreadData;

static void* matmul_worker(void* arg) {
    MatmulThreadData *data = (MatmulThreadData*)arg;
    int use_simd = glow_cpu_has_avx2_fma();
    for (int i = data->start_row; i < data->end_row; i++) {
        for (int j = 0; j < data->cB; j++) {
            double sum = 0;
            int k = use_simd ? simd_matmul_cell(&data->da[i * data->cA], data->db, data->cA, data->cB, j, &sum) : 0;
            for (; k < data->cA; k++) sum += data->da[i * data->cA + k] * data->db[k * data->cB + j];
            data->dr[i * data->cB + j] = sum;
        }
    }
    return NULL;
}

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

    PackedArray *res;
    if (strcmp(pa->device, "cuda") == 0) {
#ifdef GLOW_USE_CUDA
        res = packed_array_new(pa->type, 0);
        res->size = rA * cB;
        res->capacity = rA * cB;
        size_t elem_sz = (pa->type == PACKED_FLOAT64) ? sizeof(double) : sizeof(int64_t);
        res->data = cuda_tensor_malloc(res->size * elem_sz);
        cuda_tensor_matmul((const double*)pa->data, rA, cA, (const double*)pb->data, cB, (double*)res->data);
        strncpy(res->device, "cuda", 15);
        res->device[15] = '\0';
        res->ndims = 2;
        res->shape[0] = rA;
        res->shape[1] = cB;
        res->strides[0] = cB;
        res->strides[1] = 1;
        return value_packed(res);
#else
        runtime_error_thrown = 1;
        return value_none();
#endif
    } else {
        res = packed_array_new(pa->type, rA * cB);
        if (!res || !res->data) return value_none();
        if (pa->type == PACKED_FLOAT64) {
            double *da = (double*)pa->data, *db = (double*)pb->data, *dr = (double*)res->data;
            if (rA < 32 || cB < 32 || rA * cB < 4096) {
                MatmulThreadData tdata;
                tdata.start_row = 0;
                tdata.end_row = rA;
                tdata.da = da;
                tdata.db = db;
                tdata.dr = dr;
                tdata.cA = cA;
                tdata.cB = cB;
                matmul_worker(&tdata);
            } else {
                int num_threads = 4;
                if (rA < num_threads) num_threads = rA;
                if (num_threads < 1) num_threads = 1;

                pthread_t threads[8];
                MatmulThreadData tdata[8];
                int rows_per_thread = rA / num_threads;

                for (int t = 0; t < num_threads; t++) {
                    tdata[t].start_row = t * rows_per_thread;
                    tdata[t].end_row = (t == num_threads - 1) ? rA : (t + 1) * rows_per_thread;
                    tdata[t].da = da;
                    tdata[t].db = db;
                    tdata[t].dr = dr;
                    tdata[t].cA = cA;
                    tdata[t].cB = cB;
                    pthread_create(&threads[t], NULL, matmul_worker, &tdata[t]);
                }

                for (int t = 0; t < num_threads; t++) {
                    pthread_join(threads[t], NULL);
                }
            }
        } // end of if (pa->type == PACKED_FLOAT64)
    } // end of else

    res->ndims = 2;
    res->shape[0] = rA;
    res->shape[1] = cB;
    res->strides[0] = cB;
    res->strides[1] = 1;

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

    PackedArray *res;
    if (strcmp(pa->device, "cuda") == 0) {
#ifdef GLOW_USE_CUDA
        res = packed_array_new(pa->type, 0);
        res->size = rA * cB;
        res->capacity = rA * cB;
        size_t elem_sz = (pa->type == PACKED_FLOAT64) ? sizeof(double) : sizeof(int64_t);
        res->data = cuda_tensor_malloc(res->size * elem_sz);
        cuda_tensor_matmul((const double*)pa->data, rA, cA, (const double*)pb->data, cB, (double*)res->data);
        strncpy(res->device, "cuda", 15);
        res->device[15] = '\0';
        res->ndims = 2;
        res->shape[0] = rA;
        res->shape[1] = cB;
        res->strides[0] = cB;
        res->strides[1] = 1;
        return value_packed(res);
#else
        runtime_error_thrown = 1;
        return value_none();
#endif
    }

    res = packed_array_new(pa->type, rA * cB);
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
    /* The result is an rA x cB matrix, as on x86. */
    res->ndims = 2;
    res->shape[0] = rA;
    res->shape[1] = cB;
    res->strides[0] = cB;
    res->strides[1] = 1;
    return value_packed(res);
}
#endif

static Value native_tensor_to(int argc, Value *args) {
    if (argc < 2) return value_none();
    if (GET_TYPE(args[0]) != VAL_OBJECT || !(args[0] & MASK_PAYLOAD) || GET_OBJ_TYPE(args[0]) != OBJ_PACKED_ARRAY) return value_none();
    if (GET_TYPE(args[1]) != VAL_STRING) return value_none();

    PackedArray *pa = AS_PACKED_ARRAY(args[0]);
    const char *target_device = AS_STRING(args[1]);

    if (strcmp(target_device, pa->device) == 0) {
        return args[0]; // Already on target device
    }

    if (strcmp(target_device, "cuda") == 0) {
#ifdef GLOW_USE_CUDA
        PackedArray *new_pa = packed_array_new(pa->type, 0); // No CPU mem
        new_pa->size = pa->size;
        new_pa->capacity = pa->size;
        size_t elem_sz = (pa->type == PACKED_FLOAT64) ? sizeof(double) : sizeof(int64_t);
        new_pa->data = cuda_tensor_malloc(pa->size * elem_sz);
        if (pa->size > 0 && new_pa->data && pa->data) {
            cuda_tensor_to_device(new_pa->data, pa->data, pa->size * elem_sz);
        }
        new_pa->ndims = pa->ndims;
        for (int i = 0; i < pa->ndims; i++) {
            new_pa->shape[i] = pa->shape[i];
            new_pa->strides[i] = pa->strides[i];
        }
        new_pa->offset = pa->offset;
        strncpy(new_pa->device, "cuda", 15);
        new_pa->device[15] = '\0';
        return value_packed(new_pa);
#else
        runtime_error_thrown = 1;
        return value_none();
#endif
    } else if (strcmp(target_device, "cpu") == 0) {
        PackedArray *new_pa = packed_array_new(pa->type, pa->size); // Allocates CPU mem
        size_t elem_sz = (pa->type == PACKED_FLOAT64) ? sizeof(double) : sizeof(int64_t);
#ifdef GLOW_USE_CUDA
        if (pa->size > 0 && new_pa->data && pa->data) {
            if (strcmp(pa->device, "cuda") == 0) {
                cuda_tensor_to_host(new_pa->data, pa->data, pa->size * elem_sz);
            } else {
                memcpy(new_pa->data, pa->data, pa->size * elem_sz);
            }
        }
#else
        if (pa->size > 0 && new_pa->data && pa->data) {
            memcpy(new_pa->data, pa->data, pa->size * elem_sz);
        }
#endif
        new_pa->ndims = pa->ndims;
        for (int i = 0; i < pa->ndims; i++) {
            new_pa->shape[i] = pa->shape[i];
            new_pa->strides[i] = pa->strides[i];
        }
        new_pa->offset = pa->offset;
        strncpy(new_pa->device, "cpu", 15);
        new_pa->device[15] = '\0';
        return value_packed(new_pa);
    }
    return value_none();
}

#ifdef __linux__
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
#else
static Value native_gui_open(int argc, Value *args) { (void)argc; (void)args; return value_bool(0); }
static Value native_gui_clear(int argc, Value *args) { (void)argc; (void)args; return value_none(); }
static Value native_gui_rect(int argc, Value *args) { (void)argc; (void)args; return value_none(); }
static Value native_gui_poll(int argc, Value *args) { (void)argc; (void)args; return value_none(); }
#endif

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
    if (GET_TYPE(args[0]) != VAL_OBJECT) return args[0];
    GCObject *obj = (GCObject*)(uintptr_t)(args[0] & MASK_PAYLOAD);
    if (obj->type != OBJ_STRING_BUILDER) return args[0];
    StringBuilder *sb = (StringBuilder*)obj;
    const char *s = AS_STRING(args[1]);
    int len = strlen(s);
    if ((long long)sb->size + len > 64 * 1024 * 1024LL) {
        pending_exception = value_string_copy("StringBuilder size limit exceeded");
        has_exception = 1;
        return args[0];
    }
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
    if (GET_TYPE(args[0]) != VAL_OBJECT) return value_none();
    GCObject *obj = (GCObject*)(uintptr_t)(args[0] & MASK_PAYLOAD);
    if (obj->type != OBJ_STRING_BUILDER) return value_none();
    StringBuilder *sb = (StringBuilder*)obj;
    return value_string_copy(sb->buffer);
}

/* The name type() gives v. */
const char *glow_type_name(Value v) {
    uint64_t t = GET_TYPE(v);
    if (t == VAL_INT) return "int";
    if (t == VAL_FLOAT) {
        /* Integers past the 48-bit inline range are stored as exact doubles
           (up to 2^53); they are still integers. */
        double d = AS_FLOAT(v);
        if (d == floor(d) && fabs(d) > 140737488355327.0 && fabs(d) <= 9007199254740992.0)
            return "int";
        return "float";
    }
    if (t == VAL_BOOL) return "bool";
    if (t == VAL_STRING) return "string";
    if (t == VAL_LIST) return "list";
    if (t == VAL_DICT) return "dict";
    if (t == VAL_NONE) return "none";
    if (t == VAL_OBJECT) {
        if (!(v & MASK_PAYLOAD)) return "object";
        ObjType ot = GET_OBJ_TYPE(v);
        if (ot == OBJ_CLASS) return "class";
        if (ot == OBJ_INSTANCE) return "instance";
        if (ot == OBJ_BOUND_METHOD) return "method";
        if (ot == OBJ_NATIVE_FUNC) return "native_func";
        if (ot == OBJ_PACKED_ARRAY) return "tensor";
        if (ot == OBJ_BIGINT) return "int";
        if (ot == OBJ_TRAIT) return "trait";
        return "object";
    }
    return "none";
}

/* ---- Classes: super, class_name, is_a ---- */

static int is_obj_type(Value v, ObjType t) {
    return GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD) && GET_OBJ_TYPE(v) == t;
}

static Value class_error(const char *msg) {
    pending_exception = value_string_copy(msg);
    has_exception = 1;
    return value_none();
}

/* The compiler turns super.m(args) inside a method of a class C that extends
   P into __glow_super(P, "m", self, args...). It runs P's m, looked up from P
   upward, on the same self. */
static Value native_glow_super(int argc, Value *args) {
    if (argc < 3 || GET_TYPE(args[1]) != VAL_STRING) return class_error("super: bad call");
    const char *name = AS_STRING(args[1]);
    if (!is_obj_type(args[0], OBJ_CLASS)) {
        const char *why = GET_TYPE(args[0]) == VAL_STRING && strcmp(AS_STRING(args[0]), "no-parent") == 0
            ? "super can only be used in a class that extends another class"
            : "super can only be used inside a method of a class";
        char msg[200];
        snprintf(msg, sizeof msg, "%s (super.%.80s)", why, name);
        return class_error(msg);
    }
    for (Class *k = AS_CLASS(args[0]); k; k = k->parent) {
        Value m = dict_get(k->methods, name);
        if (is_obj_type(m, OBJ_BOUND_METHOD) && AS_BOUND_METHOD(m)->method && AS_BOUND_METHOD(m)->method->body) {
            /* A method's frame: register 0 is self, then its parameters, then
               its other locals (none). Missing arguments are none and extra
               ones are ignored, as in an ordinary method call. */
            FuncEntry *fe = AS_BOUND_METHOD(m)->method;
            int nargs = argc - 3;
            int new_base = vm_stack_top;
            int needed = new_base + fe->body->local_count + 16;
            if (needed + 512 >= vm_stack_size || glow_stack_low()) return class_error("Maximum recursion depth exceeded");
            vm_stack[new_base] = args[2];
            for (int i = 0; i < fe->param_count; i++) vm_stack[new_base + 1 + i] = i < nargs ? args[3 + i] : value_none();
            for (int i = 1 + fe->param_count; i < fe->body->local_count; i++) vm_stack[new_base + i] = value_none();
            int saved_top = vm_stack_top;
            vm_stack_top = needed;
            Value r = run_bytecode(fe->body, NULL, NULL, new_base);
            vm_stack_top = saved_top;
            return r;
        }
    }
    char msg[240];
    snprintf(msg, sizeof msg, "super: %.80s and the classes it extends have no method '%.80s'", AS_CLASS(args[0])->name, name);
    return class_error(msg);
}

/* class_name(x): the class of an object ("Dog"), a class's own name, or
   type(x) for anything else. */
static Value native_class_name(int argc, Value *args) {
    if (argc < 1) return class_error("class_name(x) needs a value");
    Value v = args[0];
    if (is_obj_type(v, OBJ_INSTANCE) && AS_INSTANCE(v)->klass && AS_INSTANCE(v)->klass->name)
        return value_string_copy(AS_INSTANCE(v)->klass->name);
    if (is_obj_type(v, OBJ_CLASS) && AS_CLASS(v)->name) return value_string_copy(AS_CLASS(v)->name);
    return value_string_copy(glow_type_name(v));
}

/* is_a(x, C): x was made from C or from a class that extends it (or x is C
   or a subclass of it). C may be the class or its name. A type name such
   as "int" or "list" checks type(x). */
static Value native_is_a(int argc, Value *args) {
    if (argc < 2) return class_error("is_a(x, class) needs a value and a class");
    Value v = args[0], c = args[1];
    Class *k = is_obj_type(v, OBJ_INSTANCE) ? AS_INSTANCE(v)->klass : is_obj_type(v, OBJ_CLASS) ? AS_CLASS(v) : NULL;
    if (is_obj_type(c, OBJ_CLASS)) {
        for (; k; k = k->parent) if (k == AS_CLASS(c)) return value_bool(1);
        return value_bool(0);
    }
    if (GET_TYPE(c) == VAL_STRING && (c & MASK_PAYLOAD)) {
        const char *want = AS_STRING(c);
        for (; k; k = k->parent) if (k->name && strcmp(k->name, want) == 0) return value_bool(1);
        return value_bool(strcmp(glow_type_name(v), want) == 0);
    }
    return class_error("is_a(x, class): the second argument must be a class or a name");
}

Value native_type(int argc, Value *args) {
    if (argc < 1) return value_none();
    return value_string_copy(glow_type_name(args[0]));
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
        if (IS_FLOAT_BITS(v)) printf("float(%g)\n", AS_FLOAT(v));
        else {
            uint64_t t = GET_TYPE(v);
            if (t == VAL_INT) printf("int(%lld)\n", (long long)AS_INT(v));
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

static Value native_dict(int argc, Value *args) {
    Dict *d = dict_new(argc > 0 ? (argc / 2 + 1) : 4);
    for (int i = 0; i + 1 < argc; i += 2) {
        if (GET_TYPE(args[i]) == VAL_STRING) {
            dict_set(d, AS_STRING(args[i]), args[i + 1]);
        }
    }
    return value_dict(d);
}

static Value native_assert(int argc, Value *args) {
    if (argc < 1 || !value_truthy(args[0])) {
        const char *msg = "Assertion failed";
        if (argc > 1 && GET_TYPE(args[1]) == VAL_STRING) {
            msg = AS_STRING(args[1]);
        }
        fprintf(stderr, "[ASSERTION FAILED] %s\n", msg);
        exit(1);
    }
    return value_bool(1);
}

static Value native_threads_join_all(int argc, Value *args) {
    (void)argc; (void)args;
    glow_threads_join_all();
    return value_none();
}

static Value native_threads_active(int argc, Value *args) {
    (void)argc; (void)args;
    return value_int(glow_threads_alive());
}

void std_init(void) {
#ifdef _WIN32
    /* Sockets (http, net, ssl, redis) need Winsock started once per process;
       without it every socket() call failed and http.listen returned -1. */
    static int winsock_started = 0;
    if (!winsock_started) { WSADATA wsa; winsock_started = WSAStartup(MAKEWORD(2, 2), &wsa) == 0; }
#endif
    var_set("assert", native_func_new("assert", native_assert));
    var_set("dict", native_func_new("dict", native_dict));
    var_set("len", native_func_new("len", native_len));
    var_set("type", native_func_new("type", native_type));
    var_set("class_name", native_func_new("class_name", native_class_name));
    var_set("is_a", native_func_new("is_a", native_is_a));
    var_set("__glow_super", native_func_new("__glow_super", native_glow_super));
    var_set("range", native_func_new("range", native_range));
    var_set("floor", native_func_new("floor", math_floor));
    var_set("ceil", native_func_new("ceil", math_ceil));
    var_set("round", native_func_new("round", math_round));
    var_set("int", native_func_new("int", native_int));
    var_set("str", native_func_new("str", native_str));
    var_set("string", native_func_new("string", native_str));
    /* `none` is a predefined name rather than a keyword, and `pass` (the
       Python-style do-nothing statement) reads one; declaring them keeps
       them working now that undeclared names raise. */
    var_set("none", value_none());
    var_set("pass", value_none());
    var_set("pairs", native_func_new("pairs", native_pairs));
    var_set("__pairs_flat", native_func_new("__pairs_flat", native_pairs_flat));
    var_set("input", native_func_new("input", native_input));
    var_set("ask", native_func_new("ask", native_input));

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
    dict_set(math, "floor", native_func_new("floor", math_floor));
    dict_set(math, "ceil", native_func_new("ceil", math_ceil));
    dict_set(math, "round", native_func_new("round", math_round));
    dict_set(math, "vec2_add", native_func_new("vec2_add", math_vec2_add));
    dict_set(math, "vec2_dist", native_func_new("vec2_dist", math_vec2_dist));
    dict_set(math, "log2", native_func_new("log2", math_log2));
    dict_set(math, "sinh", native_func_new("sinh", math_sinh));
    dict_set(math, "cosh", native_func_new("cosh", math_cosh));
    dict_set(math, "tanh", native_func_new("tanh", math_tanh));
    dict_set(math, "trunc", native_func_new("trunc", math_trunc));
    dict_set(math, "isnan", native_func_new("isnan", math_isnan));
    dict_set(math, "isinf", native_func_new("isinf", math_isinf));
    dict_set(math, "tau", value_float(6.28318530717958647692));
    dict_set(math, "nan", value_float(NAN));
    var_set("math", value_dict(math));

    Dict *collision = dict_new(4);
    dict_set(collision, "aabb", native_func_new("aabb", native_collision_aabb));
    var_set("collision", value_dict(collision));

    Dict *crypto = dict_new(4);
    dict_set(crypto, "sha256", native_func_new("sha256", crypto_sha256));
    dict_set(crypto, "hmac", native_func_new("hmac", crypto_hmac_sha256));
    dict_set(crypto, "hmac_sha256", native_func_new("hmac_sha256", crypto_hmac_sha256));
    dict_set(crypto, "blake3", native_func_new("blake3", native_crypto_blake_fast));
    dict_set(crypto, "random_bytes", native_func_new("random_bytes", crypto_random_bytes));
    dict_set(crypto, "random_hex", native_func_new("random_hex", crypto_random_hex));
    dict_set(crypto, "equal", native_func_new("equal", crypto_equal));
    dict_set(crypto, "uuid4", native_func_new("uuid4", crypto_uuid4));
    dict_set(crypto, "base64", native_func_new("base64", crypto_base64_native));
    dict_set(crypto, "unbase64", native_func_new("unbase64", crypto_unbase64_native));
    dict_set(crypto, "random_token", native_func_new("random_token", crypto_random_token_native));
    dict_set(crypto, "sha256_b64url", native_func_new("sha256_b64url", crypto_sha256_b64url_native));
    dict_set(crypto, "sign", native_func_new("sign", crypto_sign_native));
    dict_set(crypto, "verify", native_func_new("verify", crypto_verify_native));
    dict_set(crypto, "jwk_to_pem", native_func_new("jwk_to_pem", crypto_jwk_to_pem_native));
    dict_set(crypto, "pem_to_jwk", native_func_new("pem_to_jwk", crypto_pem_to_jwk_native));
    dict_set(crypto, "generate_keypair", native_func_new("generate_keypair", crypto_generate_keypair_native));
    var_set("crypto", value_dict(crypto));

    Dict *tensor = dict_new(4);
    dict_set(tensor, "create", native_func_new("create", native_tensor_create));
    dict_set(tensor, "zeros", native_func_new("zeros", native_tensor_zeros));
    dict_set(tensor, "ones", native_func_new("ones", native_tensor_ones));
    dict_set(tensor, "shape", native_func_new("shape", native_tensor_shape));
    dict_set(tensor, "reshape", native_func_new("reshape", native_tensor_reshape));
    dict_set(tensor, "get", native_func_new("get", native_tensor_get));
    dict_set(tensor, "set", native_func_new("set", native_tensor_set));
    dict_set(tensor, "add", native_func_new("add", native_packed_add));
    dict_set(tensor, "sub", native_func_new("sub", native_packed_sub));
    dict_set(tensor, "mul", native_func_new("mul", native_packed_mul));
    dict_set(tensor, "dot", native_func_new("dot", native_packed_dot));
    dict_set(tensor, "transpose", native_func_new("transpose", native_tensor_transpose));
    dict_set(tensor, "matmul", native_func_new("matmul", native_tensor_matmul));
    dict_set(tensor, "to_device", native_func_new("to_device", native_tensor_to));
    dict_set(tensor, "dequantize_int4", native_func_new("dequantize_int4", native_tensor_dequant_int4));
    var_set("tensor", value_dict(tensor));

#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    Dict *sqlite = dict_new(4);
    dict_set(sqlite, "open", native_func_new("open", native_sqlite_open));
    dict_set(sqlite, "exec", native_func_new("exec", native_sqlite_exec));
    dict_set(sqlite, "query", native_func_new("query", native_sqlite_query));
    dict_set(sqlite, "step_query", native_func_new("step_query", native_sqlite_step_query));
    dict_set(sqlite, "close", native_func_new("close", native_sqlite_close));
    dict_set(sqlite, "last_insert_id", native_func_new("last_insert_id", native_sqlite_last_insert_id));
    dict_set(sqlite, "changes", native_func_new("changes", native_sqlite_changes));
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
    dict_set(os, "format_time", native_func_new("format_time", os_format_time));
    dict_set(os, "sleep", native_func_new("sleep", os_sleep));
    dict_set(os, "getenv", native_func_new("getenv", native_os_getenv));
    dict_set(os, "gc_info", native_func_new("gc_info", native_os_gc_info));
    dict_set(os, "verify_gc", native_func_new("verify_gc", native_verify_gc));
    dict_set(os, "system", native_func_new("system", os_system));
    var_set("os", value_dict(os));
    glow_process_init();
    glow_ffi_init();
#endif

    Dict *threads = dict_new(4);
    dict_set(threads, "join_all", native_func_new("join_all", native_threads_join_all));
    dict_set(threads, "active", native_func_new("active", native_threads_active));
    var_set("threads", value_dict(threads));

    Dict *re_native = dict_new(8);
    dict_set(re_native, "match", native_func_new("match", native_re_match));
    dict_set(re_native, "search", native_func_new("search", native_re_search));
    dict_set(re_native, "find", native_func_new("find", native_re_find));
    dict_set(re_native, "all", native_func_new("all", native_re_all));
    dict_set(re_native, "replace", native_func_new("replace", native_re_replace));
    dict_set(re_native, "split", native_func_new("split", native_re_split));
    dict_set(re_native, "escape", native_func_new("escape", native_re_escape));
    var_set("re_native", value_dict(re_native));

#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
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
    glow_scripting_init();   /* os.args, os.env, fs.glob, ... (process.c) */

    Dict *pg = dict_new(8);
    dict_set(pg, "connect", native_func_new("connect", pg_connect_native));
    dict_set(pg, "query", native_func_new("query", pg_query_native));
    dict_set(pg, "script", native_func_new("script", pg_script_native));
    dict_set(pg, "status", native_func_new("status", pg_status_native));
    dict_set(pg, "server_version", native_func_new("server_version", pg_server_version_native));
    dict_set(pg, "close", native_func_new("close", pg_close_native));
    var_set("pg", value_dict(pg));
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
#endif
    Dict *json = dict_new(4);
    dict_set(json, "stringify", native_func_new("stringify", native_json_stringify));
    dict_set(json, "parse", native_func_new("parse", native_json_parse));
    dict_set(json, "scan_fast", native_func_new("scan_fast", native_fast_json_scan));
    var_set("json", value_dict(json));

    Dict *net_ext = dict_new(2);
    dict_set(net_ext, "poll_fast", native_func_new("poll_fast", native_net_poll_fast));
    var_set("net_fast", value_dict(net_ext));

    Dict *media = dict_new(2);
    dict_set(media, "png_dims", native_func_new("png_dims", native_media_png_dims));
    var_set("media", value_dict(media));

    glow_corelib_init();
    glow_datetime_init();
}

Value glow_native_pairs(int argc, Value *args) { return native_pairs(argc, args); }

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

    // 3.5 Clear GC roots
    vm_last_class = NULL;
    vm_last_trait = NULL;

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
    if (global_table.index) {
        free(global_table.index);
        global_table.index = NULL;
    }
    global_table.count = 0;
    global_table.capacity = 0;
    global_table.index_cap = 0;

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
