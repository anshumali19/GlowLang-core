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

  Value jit_call_helper(FuncEntry *fe, int argc, Value *args, Instruction *pc) {
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
          for (int _i = fe->param_count; _i < fe->body->local_count; _i++)
              vm_stack[new_base + _i] = value_none();
         int saved_top = vm_stack_top;
         vm_stack_top = needed;

         TypeSignature sig = compute_signature(&vm_stack[new_base], fe->param_count);
         void *jit = func_get_specialization(fe, sig);
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
             if (pc) { pc->cache[0].key = (void*)sig; pc->cache[0].val = (intptr_t)fe->body->jit_code; }
             int deopt_pc = 0;
             call_stack_push(fe, NULL);
             res = ((JITFunc)fe->body->jit_code)(&vm_stack[new_base], &deopt_pc);
             call_stack_pop();
             if (IS_DEOPT(res)) res = run_bytecode(fe->body, &fe->body->code[deopt_pc], NULL, new_base);
         } else if (fe->body->baseline_jit_code) {
             if (pc) { pc->cache[0].key = (void*)sig; pc->cache[0].val = (intptr_t)fe->body->baseline_jit_code; }
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
    if (!code && fe->spec_count > 0) {
        void *spec = func_get_specialization(fe, compute_signature(&vm_stack[new_base], fe->param_count));
        if (spec != (void*)1) code = spec;
    }
    Value res;
    if (code && !glow_nojitexec) {
    if (glow_jit_debug) {
        fprintf(stderr, "[DBG] jit_self_call_direct: fe=%p, &call_depth=%p, call_depth=%d, fe->body->baseline_jit_code=%p\n", (void*)fe, (void*)&call_depth, call_depth, (void*)(fe ? fe->body->baseline_jit_code : NULL));
        fflush(stderr);
    }
    if (!fe || !fe->body) {
        return VAL_DEOPT_SENTINEL; // deopt
    }
    void *jit_code = fe->body->jit_code ? fe->body->jit_code : fe->body->baseline_jit_code;
    if (!jit_code && fe->spec_count > 0) {
        TypeSignature sig = compute_signature(args, fe->param_count);
        void *spec = func_get_specialization(fe, sig);
        if (spec && spec != (void*)1) {
            jit_code = spec;
        }
    }
    if (!jit_code) {
        return VAL_DEOPT_SENTINEL; // deopt
    }
    if (call_depth >= 2000) {
        if (glow_jit_debug) {
            fprintf(stderr, "[DBG] jit_self_call_with_depth: depth=%d >= 2000, deopt\n", call_depth);
            fflush(stderr);
        }
        if (glow_jit_debug) {
            fprintf(stderr, "[DBG] jit_self_call_direct: depth=%d >= 2000, deopt\n", call_depth);
            fflush(stderr);
        }
        return VAL_DEOPT_SENTINEL;
    }

    int new_base = vm_stack_top;
    int needed = new_base + fe->body->local_count + 16;
    Value temp_args[16];
    int copy_count = fe->param_count < 16 ? fe->param_count : 16;
    for (int _i = 0; _i < copy_count; _i++)
        temp_args[_i] = args[_i];
    if (needed >= vm_stack_size) {
        int old_size = vm_stack_size;
        while (needed >= vm_stack_size) vm_stack_size *= 2;
        vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
        for (int _i = old_size; _i < vm_stack_size; _i++) vm_stack[_i] = value_none();
    }
    if (needed > vm_stack_top) vm_stack_top = needed;
    for (int _i = 0; _i < copy_count; _i++)
        vm_stack[new_base + _i] = temp_args[_i];
    for (int _i = fe->param_count; _i < fe->body->local_count; _i++)
        vm_stack[new_base + _i] = value_none();

    typedef Value (*JITFunc)(Value *R, int *pc_idx);
    int deopt_pc = 0;
    call_depth++;
    /* Use resolved JIT code */
    if (glow_jit_debug) {
        fprintf(stderr, "[DBG] jit_self_call_direct: depth=%d, calling JIT at %p\n", call_depth, jit_code);
        fflush(stderr);
    }
    Value res = ((JITFunc)jit_code)(&vm_stack[new_base], &deopt_pc);
    call_depth--;
    vm_stack_top = new_base; // restore frame
    return res;
}

/* Proper self-recursion helper: manages call stack, checks depth, handles deopt.
 * Mirrors jit_call_helper but skips specialization lookup since callee is always `fe`.
 * Returns DEOPT sentinel for deep recursion so caller (JIT or C) can fall back. */
Value jit_self_call_proper(FuncEntry *fe, Value *args, Instruction *pc) {
    if (pc && call_stack_ptr > 0) call_stack[call_stack_ptr - 1].pc = pc;
    if (fe && fe->body) {
        /* Deep recursion: return DEOPT sentinel so caller (JIT or C) falls back to interpreter. */
        if (call_depth >= 2000) {
        if (glow_jit_debug) {
            fprintf(stderr, "[DBG] jit_self_call_with_depth: depth=%d >= 2000, deopt\n", call_depth);
            fflush(stderr);
        }
            if (glow_jit_debug) {
                fprintf(stderr, "[DBG] jit_self_call_proper: depth=%d >= 2000, deopt\n", call_depth);
                fflush(stderr);
            }
            return VAL_DEOPT_SENTINEL;
        }

        int new_base = vm_stack_top;
        int needed = new_base + fe->body->local_count + 3;
        Value temp_args[16];
        int copy_count = fe->param_count < 16 ? fe->param_count : 16;
        for (int _i = 0; _i < copy_count; _i++)
            temp_args[_i] = args[_i];
        if (needed >= vm_stack_size) {
            int old_size = vm_stack_size;
            while (needed >= vm_stack_size) vm_stack_size *= 2;
            vm_stack = realloc(vm_stack, sizeof(Value) * vm_stack_size);
            for (int _i = old_size; _i < vm_stack_size; _i++) vm_stack[_i] = value_none();
        }
        if (needed > vm_stack_top) vm_stack_top = needed;
        for (int _i = 0; _i < copy_count; _i++)
            vm_stack[new_base + _i] = temp_args[_i];
        for (int _i = fe->param_count; _i < fe->body->local_count; _i++)
            vm_stack[new_base + _i] = value_none();

        typedef Value (*JITFunc)(Value *R, int *pc_idx);
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

 Value jit_call_helper_name(const char *name, int argc, Value *args, Instruction *pc) {
    if (pc && call_stack_ptr > 0) call_stack[call_stack_ptr - 1].pc = pc;
    FuncEntry *fe = func_get(name);
    if (fe) return jit_call_helper(fe, argc, args, pc);
    Value v = var_get(name);
    return jit_call_helper_val(v, argc, args, pc);
 }

Value jit_call_helper_val(Value callee, int argc, Value *args, Instruction *pc) {
    if (pc && call_stack_ptr > 0) call_stack[call_stack_ptr - 1].pc = pc;
    /* Anything this helper cannot complete exactly like the interpreter
       (constructing an instance runs `init`; calling a non-callable raises)
       returns the deopt sentinel before doing any work, and the compiled
       code then lets the interpreter execute the call instruction. */
    if (!(GET_TYPE(callee) == VAL_OBJECT && (callee & MASK_PAYLOAD))) return VAL_DEOPT_SENTINEL;
    if (GET_TYPE(callee) == VAL_OBJECT && (callee & MASK_PAYLOAD)) {
        ObjType ot = GET_OBJ_TYPE(callee);
        if (ot == OBJ_CLASS) return VAL_DEOPT_SENTINEL;
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
            return AS_NATIVE_FUNC(callee)->func(argc, args);
        }
    }
    return VAL_DEOPT_SENTINEL;
}

Value jit_get_property_helper(Value o, const char *n, Instruction *pc) {
    if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD)) {
        ObjType ot = GET_OBJ_TYPE(o);
        if(ot==OBJ_INSTANCE) {
            Instance *inst = AS_INSTANCE(o);
            Dict *d = inst->properties;
            intptr_t idx = (pc && pc->cache[0].val) ? pc->cache[0].val - 1 : -1;
            Value v;
            if (idx >= 0 && idx < d->capacity && GET_TYPE(d->keys[idx]) == VAL_STRING && pc && pc->cache[0].key == (void*)(uintptr_t)(d->keys[idx] & MASK_PAYLOAD)) {
                v = d->values[idx];
            } else {
                int new_idx = dict_get_idx(d, n);
                if (new_idx != -1) {
                    if (pc) {
                        pc->cache[0].val = new_idx + 1;
                        pc->cache[0].key = (void*)(uintptr_t)(d->keys[new_idx] & MASK_PAYLOAD);
                    }
                    v = d->values[new_idx];
                } else {
                    v = class_get_method(inst->klass, n);
                }
            }
            if (GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD) && GET_OBJ_TYPE(v) == OBJ_BOUND_METHOD && GET_TYPE(AS_BOUND_METHOD(v)->instance) == VAL_NONE) {
                v = bound_method_new(o, AS_BOUND_METHOD(v)->method);
            }
            return v;
        } return value_none();
    } else if(GET_TYPE(o)==VAL_DICT) {
        Value v = dict_get(AS_DICT(o), n);
        if (GET_TYPE(v) == VAL_NONE) {
            if (strcmp(n, "has") == 0) v = bound_native_method_new(o, native_dict_has);
            else if (strcmp(n, "keys") == 0) v = bound_native_method_new(o, native_dict_keys);
            else if (strcmp(n, "values") == 0) v = bound_native_method_new(o, native_dict_values);
            else if (strcmp(n, "remove") == 0) v = bound_native_method_new(o, native_dict_remove);
            else if (strcmp(n, "merge") == 0) v = bound_native_method_new(o, native_dict_merge);
        }
        return v;
    } else if(GET_TYPE(o)==VAL_STRING) {
        Value v = value_none();
        if (strcmp(n, "contains") == 0) v = bound_native_method_new(o, native_str_contains);
        else if (strcmp(n, "find") == 0) v = bound_native_method_new(o, native_str_find);
        else if (strcmp(n, "slice") == 0) v = bound_native_method_new(o, native_str_slice);
        else if (strcmp(n, "split") == 0) v = bound_native_method_new(o, native_str_split);
        return v;
    }
    return value_none();
}

Value jit_set_property_helper(Value o, const char *n, Value v, Instruction *pc) {
    if(GET_TYPE(o)==VAL_OBJECT && (o & MASK_PAYLOAD) && GET_OBJ_TYPE(o)==OBJ_INSTANCE) {
        Instance *inst = AS_INSTANCE(o);
        Dict *d = inst->properties;
        intptr_t idx = (pc && pc->cache[0].val) ? pc->cache[0].val - 1 : -1;
        if (idx >= 0 && idx < d->capacity && GET_TYPE(d->keys[idx]) == VAL_STRING && pc && pc->cache[0].key == (void*)(uintptr_t)(d->keys[idx] & MASK_PAYLOAD)) {
            d->values[idx] = v;
            gc_write_barrier((GCObject*)inst, v);
        } else {
            int new_idx = dict_get_idx(d, n);
            if (new_idx != -1) {
                if (pc) {
                    pc->cache[0].val = new_idx + 1;
                    pc->cache[0].key = (void*)(uintptr_t)(d->keys[new_idx] & MASK_PAYLOAD);
                }
                d->values[new_idx] = v;
                gc_write_barrier((GCObject*)inst, v);
            } else {
                instance_set(inst, n, v);
            }
        }
    } else if(GET_TYPE(o)==VAL_DICT) dict_set(AS_DICT(o), n, v);
    return v;
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

Value jit_call_method_helper(Value obj, const char *n, int argc, Value *args, Instruction *pc) {
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
                typedef Value (*JITFunc)(Value *R, int *pc_idx);
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
    } else if(GET_TYPE(obj)==VAL_LIST) {
        if (strcmp(n, "append") == 0 || strcmp(n, "push") == 0) { native_fe = native_list_append; is_native_dict_str_method = 1; }
        else if (strcmp(n, "pop") == 0) { native_fe = native_list_pop; is_native_dict_str_method = 1; }
        else if (strcmp(n, "len") == 0) { native_fe = native_list_len; is_native_dict_str_method = 1; }
    } else if(GET_TYPE(obj)==VAL_DICT) {
        Value dict_member;
        if (dict_member_is_callable(AS_DICT(obj), n, &dict_member)) fallback_callee = dict_member;
        else if (strcmp(n, "has") == 0) { native_fe = native_dict_has; is_native_dict_str_method = 1; }
        else if (strcmp(n, "keys") == 0) { native_fe = native_dict_keys; is_native_dict_str_method = 1; }
        else if (strcmp(n, "values") == 0) { native_fe = native_dict_values; is_native_dict_str_method = 1; }
        else if (strcmp(n, "remove") == 0) { native_fe = native_dict_remove; is_native_dict_str_method = 1; }
        else if (strcmp(n, "merge") == 0) { native_fe = native_dict_merge; is_native_dict_str_method = 1; }
        else if (strcmp(n, "len") == 0) { native_fe = native_len; is_native_dict_str_method = 1; }
        else fallback_callee = dict_get(AS_DICT(obj), n);
    } else if(GET_TYPE(obj)==VAL_STRING) {
        if (strcmp(n, "contains") == 0) { native_fe = native_str_contains; is_native_dict_str_method = 1; }
        else if (strcmp(n, "find") == 0) { native_fe = native_str_find; is_native_dict_str_method = 1; }
        else if (strcmp(n, "slice") == 0) { native_fe = native_str_slice; is_native_dict_str_method = 1; }
        else if (strcmp(n, "split") == 0) { native_fe = native_str_split; is_native_dict_str_method = 1; }
        else if (strcmp(n, "len") == 0) { native_fe = native_len; is_native_dict_str_method = 1; }
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
        TypeSignature sig = compute_signature(args, fe->param_count);
        void *jit = func_get_specialization(fe, sig);
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
        final_res = nf->func(argc, fargs);
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
        Value v = dict_get(AS_DICT(coll), AS_STRING(v_idx));
        /* Missing keys that name dict methods produce bound methods in the
           interpreter; let it handle those. */
        if (GET_TYPE(v) == VAL_NONE && GET_TYPE(v_idx) == VAL_STRING) return VAL_DEOPT_SENTINEL;
        return v;
    } else if (GET_TYPE(coll) == VAL_STRING) {
        if (GET_TYPE(v_idx) != VAL_INT && GET_TYPE(v_idx) != VAL_FLOAT) return VAL_DEOPT_SENTINEL;
        const char *s = AS_STRING(coll);
        int len = (int)strlen(s);
        int i = (GET_TYPE(v_idx) == VAL_INT) ? (int)AS_INT(v_idx) : (int)AS_FLOAT(v_idx);
        if (i < 0) i += len;
        if ((size_t)i < (size_t)len) {
            char ch[2] = { s[i], '\0' };
            return value_string_copy(ch);
        }
        return value_none();
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
     if (is_bigint(v)) return bigint_to_double(AS_BIGINT(v));
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
    /* memcpy is already vectorised by the C runtime for whatever the CPU
       supports; hand-written AVX2 here made every build require AVX2. */
    memcpy(gs->chars, s, len + 1);
    return make_value(VAL_STRING, (uint64_t)(uintptr_t)gs);
}

/* Build a GCString directly from a length-delimited span (which need not be
   NUL-terminated), allocating the string once. Lets callers that already know
   the length (slice, split, JSON keys) avoid a temp malloc + second copy. */
static Value value_string_copy_len(const char *s, size_t len) {
    if (!s) { s = ""; len = 0; }
    GCString *gs = gc_alloc(sizeof(GCString) + len + 1, OBJ_STRING);
    gs->size = (int)len;
    memcpy(gs->chars, s, len);
    gs->chars[len] = '\0';
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
    global_table.names[slot] = name;
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
    uint32_t h;
    const char *interned = intern_string_hash(key, &h);
    gc_write_barrier((GCObject*)d, val);
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
        /* Re-insert with known-interned keys: hash once, skip intern_string. */
        for (int i=0; i<old_cap; i++) {
            if (GET_TYPE(old_keys[i]) != VAL_NONE) {
                const char *k = (const char*)(uintptr_t)(old_keys[i] & MASK_PAYLOAD);
                uint32_t kh = hash_string(k);
                int ki = kh & (d->capacity - 1);
                while (GET_TYPE(d->keys[ki]) != VAL_NONE)
                    ki = (ki + 1) & (d->capacity - 1);
                d->keys[ki] = make_value(VAL_STRING, (uintptr_t)k);
                d->values[ki] = old_values[i];
                d->size++;
            }
        }
        free(old_keys); free(old_values);
        /* Re-insert the new key (already interned, hash known). */
        idx = h & (d->capacity - 1);
        while (GET_TYPE(d->keys[idx]) != VAL_NONE)
            idx = (idx + 1) & (d->capacity - 1);
        d->keys[idx] = make_value(VAL_STRING, (uintptr_t)interned);
        d->values[idx] = val;
        d->size++;
        return;
    }

    d->keys[idx] = make_value(VAL_STRING, (uintptr_t)interned);
    d->values[idx] = val;
    d->size++;
}

Value dict_get(Dict *d, const char *key) {
    if (!d || d->capacity == 0 || !key) return value_none();
    uint32_t h;
    const char *interned = intern_string_find(key, &h);
    if (!interned) return value_none();
    int idx = h & (d->capacity - 1);
    while (GET_TYPE(d->keys[idx]) != VAL_NONE) {
        if ((const char*)(uintptr_t)(d->keys[idx] & MASK_PAYLOAD) == interned) return d->values[idx];
        idx = (idx + 1) & (d->capacity - 1);
    }
    return value_none();
}

int dict_get_idx(Dict *d, const char *key) {
    if (!d || d->capacity == 0 || !key) return -1;
    uint32_t h;
    const char *interned = intern_string_find(key, &h);
    if (!interned) return -1;
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
    if ((v & MASK_NAN) != MASK_NAN) { format_double(nb, sizeof(nb), AS_FLOAT(v)); ds_puts(d, nb); return; }
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
    char *s = value_to_new_cstring(v);
    Value r = value_string_copy(s);
    free(s);
    return r;
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
    if (GET_TYPE(v) == VAL_STRING) return value_int((int64_t)rope_length(v));
    if (GET_TYPE(v) == VAL_LIST) return value_int(AS_LIST(v)->size);
Value native_len(int argc, Value *args) { 
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

static Value math_sin(int argc, Value *args) { return argc > 0 ? value_from_double(sin(val_to_double(args[0]))) : value_none(); }
static Value math_cos(int argc, Value *args) { return argc > 0 ? value_from_double(cos(val_to_double(args[0]))) : value_none(); }
static Value math_sqrt(int argc, Value *args) { return argc > 0 ? value_from_double(sqrt(val_to_double(args[0]))) : value_none(); }
static Value math_pow(int argc, Value *args) { return argc > 1 ? value_from_double(pow(val_to_double(args[0]), val_to_double(args[1]))) : value_none(); }
static Value math_floor(int argc, Value *args) { return argc > 0 ? value_int((int64_t)floor(val_to_double(args[0]))) : value_none(); }
static Value math_ceil(int argc, Value *args)  { return argc > 0 ? value_int((int64_t)ceil(val_to_double(args[0])))  : value_none(); }
static Value math_round(int argc, Value *args) { return argc > 0 ? value_int((int64_t)llround(val_to_double(args[0]))) : value_none(); }
static Value native_int(int argc, Value *args) {
    if (argc < 1) return value_none();
    Value v = args[0];
    if (GET_TYPE(v) == VAL_INT) return v;
    return value_int((int64_t)val_to_double(v));
}

static Value crypto_sha256(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_none();
    const char *str = AS_STRING(args[0]);
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)str, strlen(str), hash);
    static const char hex_chars[] = "0123456789abcdef";
    char out[SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        out[i * 2]     = hex_chars[hash[i] >> 4];
        out[i * 2 + 1] = hex_chars[hash[i] & 0x0F];
    }
    out[SHA256_DIGEST_LENGTH * 2] = '\0';
    return value_string_copy(out);
}

static Value crypto_hmac_sha256(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[0]) != VAL_STRING || GET_TYPE(args[1]) != VAL_STRING) return value_none();
    const char *key = AS_STRING(args[0]);
    const char *data = AS_STRING(args[1]);
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;

    if (!HMAC(EVP_sha256(), key, (int)strlen(key), (const unsigned char*)data, strlen(data), md, &md_len)) {
        return value_none();
    }

    static const char hex_chars[] = "0123456789abcdef";
    char out[EVP_MAX_MD_SIZE * 2 + 1];
    for (unsigned int i = 0; i < md_len; i++) {
        out[i * 2]     = hex_chars[md[i] >> 4];
        out[i * 2 + 1] = hex_chars[md[i] & 0x0F];
    }
    out[md_len * 2] = '\0';
    return value_string_copy(out);
}

static Value native_crypto_blake_fast(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return value_none();
    const char *str = AS_STRING(args[0]);
    size_t len = strlen(str);
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)str[i];
        h *= 0x100000001B3ULL;
        h = (h << 13) | (h >> 51);
    }
    char out[32];
    sprintf(out, "%016llx", (unsigned long long)h);
    return value_string_copy(out);
}

static Value native_tensor_dequant_int4(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_OBJECT || GET_OBJ_TYPE(args[0]) != OBJ_PACKED_ARRAY) return value_none();
    PackedArray *src = AS_PACKED_ARRAY(args[0]);
    int sz = src->size * 2;
    PackedArray *pa = packed_array_new(PACKED_FLOAT64, sz);
    if (pa->data && sz > 0 && src->data) {
        double *out = (double*)pa->data;
        uint8_t *in = (uint8_t*)src->data;
        for (int i = 0; i < src->size; i++) {
            uint8_t b = in[i];
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
    while (*v_start == ' ') v_start++;
    int v_len = strlen(v_start);
    if (v_len > 0 && v_start[v_len - 1] == '}') v_len--;
    char val[128];
    if (v_len >= 128) v_len = 127;
    memcpy(val, v_start, v_len);
    val[v_len] = '\0';
    Dict *d = dict_new(2);
    dict_set(d, "key", value_string_copy(key));
    dict_set(d, "val", value_string_copy(val));
    return value_dict(d);
}

static Value native_net_poll_fast(int argc, Value *args) {
    return value_bool(1);
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
    if ((v & MASK_NAN) != MASK_NAN) {
        char tmp[64]; format_double(tmp, sizeof(tmp), AS_FLOAT(v)); json_append_str(tmp, sb); return;
    }
    if (is_bigint(v)) { char *_s = bigint_to_new_string(AS_BIGINT(v)); json_append_str(_s, sb); free(_s); return; }
    uint64_t type = (v & MASK_TYPE) >> 48;
    if (type == VAL_INT) {
        char tmp[64]; snprintf(tmp, sizeof(tmp), "%lld", (long long)AS_INT(v)); json_append_str(tmp, sb);
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

    // Invalid JSON, prevent infinite loops by advancing src
    if (**src) (*src)++;
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
    uint32_t h;
    const char *interned = intern_string_find(key, &h);
    if (!interned || d->capacity == 0) return value_none();
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
    return value_string_copy_len(s + start, (size_t)slice_len);
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

    // Pause GC during split to protect the unrooted list 'l' and its items
    size_t old_gc_thresh = gc_threshold;
    gc_threshold = (size_t)-1;

    size_t delim_len = strlen(delim);
    if (delim_len == 0) {
        int len = strlen(s);
        for (int i = 0; i < len; i++) {
            char buf[2] = { s[i], '\0' };
            list_push(l, value_string_copy(buf));
        }
        gc_threshold = old_gc_thresh;
        return value_list(l);
    }

    const char *current = s;
    const char *next = strstr(current, delim);
    while (next != NULL) {
        size_t part_len = next - current;
        list_push(l, value_string_copy_len(current, part_len));
        current = next + delim_len;
        next = strstr(current, delim);
    }
    list_push(l, value_string_copy(current));

    gc_threshold = old_gc_thresh;
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
            for (int i = 0; i < hdrs->capacity; i++) {
                if (GET_TYPE(hdrs->keys[i]) == VAL_STRING && GET_TYPE(hdrs->values[i]) == VAL_STRING) {
                    /* Dict keys are interned C strings, not GC strings. */
                    req_len += snprintf(req + req_len, sizeof(req) - req_len, "%s: %s\r\n", (const char*)(uintptr_t)(hdrs->keys[i] & MASK_PAYLOAD), AS_STRING(hdrs->values[i]));
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

    glow_gil_release();

    struct hostent *he = gethostbyname(hostname);
    if (!he) {
        glow_gil_acquire();
        return value_none();
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        glow_gil_acquire();
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
        glow_gil_acquire();
        return value_none();
    }

    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    if (is_ssl) {
        SSL_library_init();
        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            close(sockfd);
            glow_gil_acquire();
            return value_none();
        }
        ssl = SSL_new(ctx);
        if (!ssl) {
            SSL_CTX_free(ctx);
            close(sockfd);
            glow_gil_acquire();
            return value_none();
        }
        SSL_set_fd(ssl, sockfd);
        if (SSL_connect(ssl) <= 0) {
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            close(sockfd);
            glow_gil_acquire();
            return value_none();
        }
    }

    if (is_ssl) {
        SSL_write(ssl, req, req_len);
        if (body_len > 0) SSL_write(ssl, body_str, body_len);
    } else {
        ssize_t _wr = send(sockfd, req, req_len, 0);
        if (body_len > 0) _wr = send(sockfd, body_str, body_len, 0);
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

    glow_gil_acquire();

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
static Value http_read(int argc, Value *args) {
    if (argc < 1) return value_none();
    int fd = AS_INT(args[0]);
    char buf[4096];
    glow_gil_release();
    int n = read(fd, buf, 4095);
    glow_gil_acquire();
    if (n <= 0) return value_none();
    buf[n] = '\0';
    return value_string_copy(buf);
}
static Value http_write(int argc, Value *args) {
    if (argc < 2 || GET_TYPE(args[1]) != VAL_STRING) return value_int(-1);
    int fd = AS_INT(args[0]);
    const char *resp = AS_STRING(args[1]);
    glow_gil_release();
    int res = write(fd, resp, strlen(resp));
    glow_gil_acquire();
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
    if (!db) { printf("SQLite Error: database is closed\n"); return value_int(SQLITE_MISUSE); }
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
    if (!db) return value_none();
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
    if (!db) return value_none();
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
    return (int)(((ProfileEntry*)b)->count - ((ProfileEntry*)a)->count);
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

static Value native_tensor_shape(int argc, Value *args) {
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
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

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

Value native_type(int argc, Value *args) {
    if (argc < 1) return value_none();
    uint64_t t = GET_TYPE(args[0]);
    if (t == VAL_INT) return value_string_copy("int");
    if (t == VAL_FLOAT) return value_string_copy("float");
    if (t == VAL_BOOL) return value_string_copy("bool");
    if (t == VAL_STRING) return value_string_copy("string");
    if (t == VAL_LIST) return value_string_copy("list");
    if (t == VAL_DICT) return value_string_copy("dict");
    if (t == VAL_NONE) return value_string_copy("none");
    if (t == VAL_OBJECT) {
        if (!(args[0] & MASK_PAYLOAD)) return value_string_copy("object");
        ObjType ot = GET_OBJ_TYPE(args[0]);
        if (ot == OBJ_CLASS) return value_string_copy("class");
        if (ot == OBJ_INSTANCE) return value_string_copy("instance");
        if (ot == OBJ_BOUND_METHOD) return value_string_copy("method");
        if (ot == OBJ_NATIVE_FUNC) return value_string_copy("native_func");
        if (ot == OBJ_PACKED_ARRAY) return value_string_copy("tensor");
        if (ot == OBJ_BIGINT) return value_string_copy("bigint");
        if (ot == OBJ_TRAIT) return value_string_copy("trait");
        return value_string_copy("object");
    }
    return value_string_copy("none");
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
        if (((v) & MASK_NAN) != MASK_NAN) printf("float(%g)\n", AS_FLOAT(v));
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
    var_set("assert", native_func_new("assert", native_assert));
    var_set("dict", native_func_new("dict", native_dict));
    var_set("len", native_func_new("len", native_len));
    var_set("type", native_func_new("type", native_type));
    var_set("range", native_func_new("range", native_range));
    var_set("floor", native_func_new("floor", math_floor));
    var_set("ceil", native_func_new("ceil", math_ceil));
    var_set("round", native_func_new("round", math_round));
    var_set("int", native_func_new("int", native_int));
    var_set("str", native_func_new("str", native_str));

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
    var_set("math", value_dict(math));

    Dict *collision = dict_new(4);
    dict_set(collision, "aabb", native_func_new("aabb", native_collision_aabb));
    var_set("collision", value_dict(collision));

    Dict *crypto = dict_new(4);
    dict_set(crypto, "sha256", native_func_new("sha256", crypto_sha256));
    dict_set(crypto, "hmac", native_func_new("hmac", crypto_hmac_sha256));
    dict_set(crypto, "hmac_sha256", native_func_new("hmac_sha256", crypto_hmac_sha256));
    dict_set(crypto, "blake3", native_func_new("blake3", native_crypto_blake_fast));
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
#endif

    Dict *threads = dict_new(4);
    dict_set(threads, "join_all", native_func_new("join_all", native_threads_join_all));
    dict_set(threads, "active", native_func_new("active", native_threads_active));
    var_set("threads", value_dict(threads));

    Dict *re_native = dict_new(4);
    dict_set(re_native, "match", native_func_new("match", native_re_match));
    dict_set(re_native, "search", native_func_new("search", native_re_search));
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
