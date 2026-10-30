/* ffi: call functions in C libraries (.dll, .so, .dylib) from GlowLang.
 *
 *   let libc = ffi.open(ffi.libc())
 *   let c_abs = libc.function("abs", ["int"], "int")
 *   show c_abs(-5)
 *
 * ffi.open returns a library dict: function(name, arg_types, return_type,
 * opts) makes a GlowLang function for a C function; symbol(name) gives a
 * symbol's address. Memory helpers (alloc, free, load, store, string,
 * from_string, sizeof) handle pointers and out-parameters. ("func" and
 * "write" are keywords, hence function and store.) See docs/ffi_guide.md.
 *
 * Calls are made without libffi: arguments are split into integer-class and
 * float-class and passed through a fixed function-pointer prototype that puts
 * each class in the registers the platform ABI expects (see call_c). Shapes
 * the prototypes cannot express (structs by value, variadic functions,
 * callbacks, too many float arguments) are refused when the function is
 * declared, never guessed.
 */
#include "glowlang.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if UINTPTR_MAX != 0xFFFFFFFFFFFFFFFFu
#error "the GlowLang FFI supports 64-bit targets only"
#endif

/* ---- errors and values ---- */

static Value ffi_error(const char *fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    pending_exception = value_string_copy(msg);
    has_exception = 1;
    return value_none();
}

static int is_str(Value v) { return GET_TYPE(v) == VAL_STRING && (v & MASK_PAYLOAD); }

static int is_big(Value v) { return GET_TYPE(v) == VAL_OBJECT && (v & MASK_PAYLOAD) && is_bigint(v); }

/* Integer value of v as a sign and magnitude, exactly; 0 if v is not a
   whole number or does not fit in 64 bits. */
static int value_to_integer(Value v, int *neg, uint64_t *mag) {
    if (GET_TYPE(v) == VAL_INT) {
        int64_t i = AS_INT(v);
        *neg = i < 0;
        *mag = i < 0 ? (uint64_t)0 - (uint64_t)i : (uint64_t)i;
        return 1;
    }
    if (GET_TYPE(v) == VAL_FLOAT) {
        double d = AS_FLOAT(v);
        if (d != floor(d) || fabs(d) >= 18446744073709551616.0) return 0;
        *neg = d < 0;
        *mag = (uint64_t)fabs(d);
        return 1;
    }
    if (is_big(v)) {
        BigInt *b = AS_BIGINT(v);
        if (b->nlimbs > 2) return 0;
        uint64_t u = b->limbs[0];
        if (b->nlimbs == 2) u |= (uint64_t)b->limbs[1] << 32;
        *neg = b->neg && u != 0;
        *mag = u;
        return 1;
    }
    if (GET_TYPE(v) == VAL_BOOL) { *neg = 0; *mag = (v & 1) ? 1 : 0; return 1; }
    return 0;
}

/* A GlowLang integer for i, through the numeric tower (inline, exact double,
   big integer). */
static Value int_value(int64_t i) {
    if (fits_int48(i)) return value_int(i);
    if (i >= -INT53_MAX && i <= INT53_MAX) return value_float((double)i);
    return value_bigint(bigint_from_int64(i));
}

static Value uint_value(uint64_t u) {
    if (u <= (uint64_t)INT64_MAX) return int_value((int64_t)u);
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)u);
    return value_bigint(bigint_from_string(buf));
}

static int is_number(Value v) { return GET_TYPE(v) == VAL_INT || GET_TYPE(v) == VAL_FLOAT || is_big(v); }

static double number_value(Value v) {
    if (is_big(v)) return bigint_to_double(AS_BIGINT(v));
    return val_to_double(v);
}

/* ---- types ---- */

typedef enum {
    T_VOID, T_BOOL, T_I8, T_U8, T_I16, T_U16, T_I32, T_U32, T_I64, T_U64,
    T_FLOAT, T_DOUBLE, T_POINTER, T_STRING
} FfiType;

typedef struct { const char *name; FfiType type; } TypeName;

static const TypeName type_names[] = {
    {"void", T_VOID}, {"bool", T_BOOL},
    {"int8", T_I8}, {"uint8", T_U8}, {"char", T_I8}, {"uchar", T_U8},
    {"int16", T_I16}, {"uint16", T_U16}, {"short", T_I16}, {"ushort", T_U16},
    {"int32", T_I32}, {"uint32", T_U32}, {"int", T_I32}, {"uint", T_U32},
    {"int64", T_I64}, {"uint64", T_U64}, {"longlong", T_I64}, {"ulonglong", T_U64},
#ifdef _WIN32
    {"long", T_I32}, {"ulong", T_U32},
#else
    {"long", T_I64}, {"ulong", T_U64},
#endif
    {"size_t", T_U64}, {"ssize_t", T_I64}, {"intptr", T_I64}, {"uintptr", T_U64},
    {"float", T_FLOAT}, {"double", T_DOUBLE},
    {"pointer", T_POINTER}, {"string", T_STRING},
    {NULL, T_VOID}
};

static const char *type_label(FfiType t) {
    static const char *labels[] = { "void", "bool", "int8", "uint8", "int16", "uint16", "int32", "uint32",
                                    "int64", "uint64", "float", "double", "pointer", "string" };
    return labels[t];
}

static int type_size(FfiType t) {
    switch (t) {
        case T_VOID: return 0;
        case T_BOOL: case T_I8: case T_U8: return 1;
        case T_I16: case T_U16: return 2;
        case T_I32: case T_U32: case T_FLOAT: return 4;
        default: return 8;
    }
}

static int is_float_class(FfiType t) { return t == T_FLOAT || t == T_DOUBLE; }

/* Look up a type name; 0 (with an error set) if it is unknown. */
static int parse_type(Value v, FfiType *out, const char *fn, const char *what) {
    if (is_str(v)) {
        const char *s = AS_STRING(v);
        for (const TypeName *t = type_names; t->name; t++)
            if (!strcmp(t->name, s)) { *out = t->type; return 1; }
        ffi_error("%s: unknown type \"%s\" for %s (structs by value and callbacks are not supported; use \"pointer\")", fn, s, what);
        return 0;
    }
    ffi_error("%s: the type for %s must be a string such as \"int\" or \"double\"", fn, what);
    return 0;
}

/* Check v against an integer type's range and give its bits as a 64-bit
   register value (sign- or zero-extended). */
static int integer_arg(Value v, FfiType t, uint64_t *bits, const char *fn, int i) {
    int neg;
    uint64_t mag;
    if (!value_to_integer(v, &neg, &mag)) { ffi_error("%s: argument %d must be a whole number for %s", fn, i + 1, type_label(t)); return 0; }
    int is_signed = t == T_I8 || t == T_I16 || t == T_I32 || t == T_I64;
    int bitsz = type_size(t) * 8;
    if (is_signed) {
        uint64_t max_pos = ((uint64_t)1 << (bitsz - 1)) - 1;
        if ((!neg && mag > max_pos) || (neg && mag > max_pos + 1)) goto range;
        int64_t s = neg ? (int64_t)((uint64_t)0 - mag) : (int64_t)mag;
        *bits = (uint64_t)s;
    } else {
        if (neg) goto range;
        if (bitsz < 64 && mag > (((uint64_t)1 << bitsz) - 1)) goto range;
        *bits = mag;
    }
    return 1;
range:
    ffi_error("%s: argument %d is out of range for %s", fn, i + 1, type_label(t));
    return 0;
}

/* The GlowLang value for an integer-class result, from the low bytes of a
   register (the upper bits of a narrow C return value are unspecified). */
static Value integer_result(uint64_t r, FfiType t) {
    switch (t) {
        case T_BOOL: return value_bool((r & 0xFF) != 0);
        case T_I8: return value_int((int8_t)r);
        case T_U8: return value_int((uint8_t)r);
        case T_I16: return value_int((int16_t)r);
        case T_U16: return value_int((uint16_t)r);
        case T_I32: return value_int((int32_t)r);
        case T_U32: return value_int((uint32_t)r);
        case T_I64: return int_value((int64_t)r);
        case T_U64: return uint_value(r);
        case T_POINTER: return r ? uint_value(r) : value_none();
        case T_STRING: return r ? value_string_copy((const char *)(uintptr_t)r) : value_none();
        default: return value_none();
    }
}

/* ---- libraries ---- */

typedef struct {
    int refs;
    void *handle;
    char *path;
} Lib;

typedef struct {
    Lib *lib;
    void *fn;
    char *name;
    int nargs;
    FfiType args[16];
    FfiType ret;
    int release_gil;
} FfiFn;

static void lib_release(void *data) {
    Lib *lib = data;
    if (!lib || --lib->refs > 0) return;
#ifdef _WIN32
    if (lib->handle) FreeLibrary((HMODULE)lib->handle);
#else
    if (lib->handle) dlclose(lib->handle);
#endif
    free(lib->path);
    free(lib);
}

static void fn_release(void *data) {
    FfiFn *f = data;
    if (!f) return;
    lib_release(f->lib);
    free(f->name);
    free(f);
}

static void *lib_symbol(Lib *lib, const char *name) {
#ifdef _WIN32
    return (void *)GetProcAddress((HMODULE)lib->handle, name);
#else
    dlerror();
    return dlsym(lib->handle, name);
#endif
}

#ifdef _WIN32
static wchar_t *to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = malloc((size_t)n * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static void win_message(DWORD code, char *out, size_t outsz) {
    out[0] = '\0';
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, out, (DWORD)outsz, NULL);
    size_t n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == '.')) out[--n] = '\0';
}

/* Load a DLL without ever searching the current folder. A path is made
   absolute and its own folder is searched for its dependencies; a bare name
   is looked for in the application and system folders, then on PATH. */
static HMODULE load_library(const char *path, char *err, size_t errsz) {
    wchar_t *w = to_wide(path);
    if (!w) { snprintf(err, errsz, "bad library name"); return NULL; }
    HMODULE h = NULL;
    DWORD code = 0;
    if (wcspbrk(w, L"\\/:")) {
        wchar_t full[4096];
        DWORD n = GetFullPathNameW(w, 4096, full, NULL);
        if (n && n < 4096) h = LoadLibraryExW(full, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!h) code = GetLastError();
    } else {
        h = LoadLibraryExW(w, NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!h) {
            code = GetLastError();
            DWORD n = GetEnvironmentVariableW(L"PATH", NULL, 0);
            wchar_t *pathv = n ? malloc(n * sizeof(wchar_t)) : NULL;
            if (pathv && GetEnvironmentVariableW(L"PATH", pathv, n)) {
                for (wchar_t *d = pathv; d && *d && !h;) {
                    wchar_t *end = wcschr(d, L';');
                    size_t len = end ? (size_t)(end - d) : wcslen(d);
                    if (len >= 2 && d[0] == L'"' && d[len - 1] == L'"') { d++; len -= 2; }
                    if (len > 0 && len < 3800) {
                        wchar_t cand[4096];
                        memcpy(cand, d, len * sizeof(wchar_t));
                        size_t at = len;
                        if (cand[at - 1] != L'\\' && cand[at - 1] != L'/') cand[at++] = L'\\';
                        wcsncpy(cand + at, w, 4096 - at - 1);
                        cand[4095] = 0;
                        DWORD a = GetFileAttributesW(cand);
                        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY))
                            h = LoadLibraryExW(cand, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                    }
                    if (!end) break;
                    d = end + 1;
                }
            }
            free(pathv);
        }
    }
    free(w);
    if (!h) {
        char msg[512];
        win_message(code, msg, sizeof(msg));
        snprintf(err, errsz, "%s", msg[0] ? msg : "could not load it");
    }
    return h;
}
#endif

static Value lib_func(int argc, Value *args);
static Value lib_symbol_native(int argc, Value *args);

/* ffi.open(path): load a library. */
static Value ffi_open(int argc, Value *args) {
    if (argc < 1 || !is_str(args[0]) || !AS_STRING(args[0])[0]) return ffi_error("ffi.open: expected a library name or path");
    const char *path = AS_STRING(args[0]);
    void *h;
#ifdef _WIN32
    char err[600];
    h = load_library(path, err, sizeof(err));
    if (!h) return ffi_error("ffi.open: cannot load %s: %s", path, err);
#else
    h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { const char *e = dlerror(); return ffi_error("ffi.open: cannot load %s: %s", path, e ? e : "unknown error"); }
#endif
    Lib *lib = calloc(1, sizeof(Lib));
    lib->handle = h;
    lib->path = strdup(path);
    lib->refs = 2;   /* held by the func and symbol natives below */
    Dict *d = dict_new(4);
    Value dv = value_dict(d);
    dict_set(d, "path", value_string_copy(path));
    dict_set(d, "function", native_func_new_data("function", lib_func, lib, lib_release));
    dict_set(d, "symbol", native_func_new_data("symbol", lib_symbol_native, lib, lib_release));
    return dv;
}

static Lib *current_lib(void) { return glow_current_native ? (Lib *)glow_current_native->data : NULL; }

/* lib.symbol(name): the address of a symbol. */
static Value lib_symbol_native(int argc, Value *args) {
    Lib *lib = current_lib();
    if (!lib) return ffi_error("symbol: call it on a library from ffi.open");
    if (argc < 1 || !is_str(args[0])) return ffi_error("symbol: expected a symbol name");
    void *p = lib_symbol(lib, AS_STRING(args[0]));
    if (!p) return ffi_error("symbol: %s not found in %s", AS_STRING(args[0]), lib->path);
    return uint_value((uint64_t)(uintptr_t)p);
}

static Value ffi_call_native(int argc, Value *args);

/* lib.function(name, arg_types, return_type, opts): declare a C function. */
static Value lib_func(int argc, Value *args) {
    Lib *lib = current_lib();
    if (!lib) return ffi_error("function: call it on a library from ffi.open");
    if (argc < 3 || !is_str(args[0]) || GET_TYPE(args[1]) != VAL_LIST)
        return ffi_error("function: expected (name, [argument types], return type), e.g. lib.function(\"abs\", [\"int\"], \"int\")");
    const char *name = AS_STRING(args[0]);
    List *types = AS_LIST(args[1]);
    FfiFn f;
    memset(&f, 0, sizeof(f));
    if (types->size > 16) return ffi_error("%s: at most 16 arguments are supported", name);
    int nint = 0, nfloat = 0;
    for (int i = 0; i < types->size; i++) {
        char what[32];
        snprintf(what, sizeof(what), "argument %d", i + 1);
        if (!parse_type(types->items[i], &f.args[i], name, what)) return value_none();
        if (f.args[i] == T_VOID) return ffi_error("%s: argument %d cannot be void", name, i + 1);
        if (is_float_class(f.args[i])) nfloat++; else nint++;
    }
    f.nargs = types->size;
    if (!parse_type(args[2], &f.ret, name, "the return value")) return value_none();
#ifndef _WIN32
    if (nfloat > 8) return ffi_error("%s: at most 8 float or double arguments are supported", name);
#endif
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    if (nint > 8) return ffi_error("%s: at most 8 integer or pointer arguments are supported on this Mac", name);
#endif
    (void)nint;
    if (argc >= 4 && GET_TYPE(args[3]) == VAL_DICT) {
        Value rg = dict_get(AS_DICT(args[3]), "release_gil");
        f.release_gil = GET_TYPE(rg) == VAL_BOOL && (rg & 1);
    } else if (argc >= 4 && GET_TYPE(args[3]) != VAL_NONE) {
        return ffi_error("%s: options must be a dict", name);
    }
    f.fn = lib_symbol(lib, name);
    if (!f.fn) return ffi_error("function: %s not found in %s", name, lib->path);
    FfiFn *heap = malloc(sizeof(FfiFn));
    *heap = f;
    heap->name = strdup(name);
    heap->lib = lib;
    lib->refs++;
    return native_func_new_data(heap->name, ffi_call_native, heap, fn_release);
}

/* ---- the call ----
 *
 * call_c receives every argument as a 64-bit register value. A float is
 * the low 32 bits of its slot; a double is the whole slot. */

typedef uint64_t I;
typedef double D;

/* errno and GetLastError right after the last call on this thread. */
static THREAD_LOCAL int last_errno;
#ifdef _WIN32
static THREAD_LOCAL DWORD last_win_error;
#endif

static D bits_to_double(uint64_t b) { D d; memcpy(&d, &b, sizeof(d)); return d; }

#ifdef _WIN32
/* Windows x64: positional. Each of the first four arguments goes in RCX/RDX/
   R8/R9 or XMM0-3 by position, so the prototype depends on which of them are
   float-class (16 shapes). Arguments 5+ are 8-byte stack slots, where a
   double's bits passed as an integer are identical; extra slots are ignored
   by the callee and removed by the caller. */
#define S4 s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13], s[14], s[15]
#define TAIL I, I, I, I, I, I, I, I, I, I, I, I
#define CALL4(R, T0, A0, T1, A1, T2, A2, T3, A3) ((R (*)(T0, T1, T2, T3, TAIL))fn)(A0, A1, A2, A3, S4)
#define DISPATCH4(R) switch (mask) { \
    case 0:  r = CALL4(R, I, s[0], I, s[1], I, s[2], I, s[3]); break; \
    case 1:  r = CALL4(R, D, d[0], I, s[1], I, s[2], I, s[3]); break; \
    case 2:  r = CALL4(R, I, s[0], D, d[1], I, s[2], I, s[3]); break; \
    case 3:  r = CALL4(R, D, d[0], D, d[1], I, s[2], I, s[3]); break; \
    case 4:  r = CALL4(R, I, s[0], I, s[1], D, d[2], I, s[3]); break; \
    case 5:  r = CALL4(R, D, d[0], I, s[1], D, d[2], I, s[3]); break; \
    case 6:  r = CALL4(R, I, s[0], D, d[1], D, d[2], I, s[3]); break; \
    case 7:  r = CALL4(R, D, d[0], D, d[1], D, d[2], I, s[3]); break; \
    case 8:  r = CALL4(R, I, s[0], I, s[1], I, s[2], D, d[3]); break; \
    case 9:  r = CALL4(R, D, d[0], I, s[1], I, s[2], D, d[3]); break; \
    case 10: r = CALL4(R, I, s[0], D, d[1], I, s[2], D, d[3]); break; \
    case 11: r = CALL4(R, D, d[0], D, d[1], I, s[2], D, d[3]); break; \
    case 12: r = CALL4(R, I, s[0], I, s[1], D, d[2], D, d[3]); break; \
    case 13: r = CALL4(R, D, d[0], I, s[1], D, d[2], D, d[3]); break; \
    case 14: r = CALL4(R, I, s[0], D, d[1], D, d[2], D, d[3]); break; \
    default: r = CALL4(R, D, d[0], D, d[1], D, d[2], D, d[3]); break; \
}

static I call_int(void *fn, const I *s, const D *d, int mask) { I r; DISPATCH4(I); return r; }
static D call_double(void *fn, const I *s, const D *d, int mask) { D r; DISPATCH4(D); return r; }
static float call_float(void *fn, const I *s, const D *d, int mask) { float r; DISPATCH4(float); return r; }

/* GlowLang links the C runtime statically, so its errno is private; DLLs use
   the shared runtime (ucrtbase.dll), whose errno is read through _errno. */
static int *shared_errno(void) {
    static int *(*fn)(void);
    if (!fn) {
        HMODULE h = GetModuleHandleW(L"ucrtbase.dll");
        if (h) fn = (int *(*)(void))GetProcAddress(h, "_errno");
    }
    return fn ? fn() : NULL;
}

/* Returns the result in *ri (integer class) or *rd (float class). */
static void call_c(const FfiFn *f, const I *slots, I *ri, D *rd) {
    I s[16];
    D d[4];
    int mask = 0;
    memset(s, 0, sizeof(s));
    for (int i = 0; i < f->nargs; i++) s[i] = slots[i];
    for (int i = 0; i < 4; i++) {
        d[i] = bits_to_double(s[i]);
        if (i < f->nargs && is_float_class(f->args[i])) mask |= 1 << i;
    }
    void *fn = f->fn;
    int *shared = shared_errno();
    if (shared) *shared = 0;
    errno = 0;
    SetLastError(0);
    if (f->ret == T_DOUBLE) *rd = call_double(fn, s, d, mask);
    else if (f->ret == T_FLOAT) *rd = call_float(fn, s, d, mask);
    else *ri = call_int(fn, s, d, mask);
    last_win_error = GetLastError();
    if (!shared) shared = shared_errno();   /* the call may have loaded it */
    last_errno = (shared && *shared) ? *shared : errno;
}

#else
/* System V x86-64 and AArch64: integer-class and float-class arguments are
   assigned to separate register files, each in order. So every call uses one
   prototype with 16 integer slots then 8 double slots: integer arguments in
   order in the first, float arguments in order in the second. Integer
   arguments past the registers (6 on x86-64, 8 on AArch64) go on the stack in
   order, as the callee expects while its float arguments fit in the 8 float
   registers (enforced when the function is declared). */
#define IARGS I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I
#define DARGS D, D, D, D, D, D, D, D
#define IVALS ii[0], ii[1], ii[2], ii[3], ii[4], ii[5], ii[6], ii[7], ii[8], ii[9], ii[10], ii[11], ii[12], ii[13], ii[14], ii[15]
#define DVALS dd[0], dd[1], dd[2], dd[3], dd[4], dd[5], dd[6], dd[7]

static void call_c(const FfiFn *f, const I *slots, I *ri, D *rd) {
    I ii[16];
    D dd[8];
    int ni = 0, nd = 0;
    memset(ii, 0, sizeof(ii));
    memset(dd, 0, sizeof(dd));
    for (int i = 0; i < f->nargs; i++) {
        if (is_float_class(f->args[i])) dd[nd++] = bits_to_double(slots[i]);
        else ii[ni++] = slots[i];
    }
    void *fn = f->fn;
    errno = 0;
    if (f->ret == T_DOUBLE) *rd = ((D (*)(IARGS, DARGS))fn)(IVALS, DVALS);
    else if (f->ret == T_FLOAT) *rd = ((float (*)(IARGS, DARGS))fn)(IVALS, DVALS);
    else *ri = ((I (*)(IARGS, DARGS))fn)(IVALS, DVALS);
    last_errno = errno;
}
#endif

/* Calling a declared C function. */
static Value ffi_call_native(int argc, Value *args) {
    FfiFn *f = glow_current_native ? (FfiFn *)glow_current_native->data : NULL;
    if (!f) return ffi_error("ffi: not a C function");
    if (argc != f->nargs)
        return ffi_error("%s: expected %d argument%s, got %d", f->name, f->nargs, f->nargs == 1 ? "" : "s", argc);
    I slots[16];
    char *strings[16];
    int nstrings = 0;
    memset(slots, 0, sizeof(slots));
    for (int i = 0; i < f->nargs; i++) {
        Value v = args[i];
        FfiType t = f->args[i];
        if (t == T_FLOAT || t == T_DOUBLE) {
            if (!is_number(v)) { ffi_error("%s: argument %d must be a number", f->name, i + 1); goto fail; }
            double x = number_value(v);
            if (t == T_DOUBLE) memcpy(&slots[i], &x, sizeof(x));
            else { float fl = (float)x; uint32_t b; memcpy(&b, &fl, sizeof(b)); slots[i] = b; }
        } else if (t == T_STRING) {
            if (GET_TYPE(v) == VAL_NONE) slots[i] = 0;
            else if (is_str(v)) {
                /* A copy: the GC may run while the call releases the GIL. */
                size_t n = rope_length(v);
                char *c = malloc(n + 1);
                memcpy(c, AS_STRING(v), n);
                c[n] = '\0';
                strings[nstrings++] = c;
                slots[i] = (I)(uintptr_t)c;
            } else {
                /* Not an integer address: that would turn a wrong argument
                   (strlen(5)) into a crash. C memory holding text is passed
                   with a "pointer" parameter. */
                ffi_error("%s: argument %d must be a string or none (declare it \"pointer\" to pass C memory)", f->name, i + 1);
                goto fail;
            }
        } else if (t == T_POINTER) {
            if (GET_TYPE(v) == VAL_NONE) slots[i] = 0;
            else if (!integer_arg(v, T_U64, &slots[i], f->name, i)) goto fail;
        } else if (t == T_BOOL) {
            if (GET_TYPE(v) == VAL_BOOL) slots[i] = (v & 1) ? 1 : 0;
            else { ffi_error("%s: argument %d must be true or false", f->name, i + 1); goto fail; }
        } else {
            if (!integer_arg(v, t, &slots[i], f->name, i)) goto fail;
        }
    }
    I ri = 0;
    D rd = 0;
    if (f->release_gil) glow_gil_release();
    call_c(f, slots, &ri, &rd);
    if (f->release_gil) glow_gil_acquire();
    for (int i = 0; i < nstrings; i++) free(strings[i]);
    if (f->ret == T_VOID) return value_none();
    if (f->ret == T_DOUBLE) return value_from_double(rd);
    if (f->ret == T_FLOAT) return value_from_double((double)(float)rd);
    return integer_result(ri, f->ret);
fail:
    for (int i = 0; i < nstrings; i++) free(strings[i]);
    return value_none();
}

/* ---- memory ---- */

static int pointer_arg(Value v, uint64_t *p, const char *fn) {
    if (GET_TYPE(v) == VAL_NONE) { ffi_error("%s: the pointer is null", fn); return 0; }
    int neg;
    uint64_t mag;
    if (!value_to_integer(v, &neg, &mag) || neg) { ffi_error("%s: expected a pointer (an address from ffi.alloc or a C function)", fn); return 0; }
    if (mag == 0) { ffi_error("%s: the pointer is null", fn); return 0; }
    *p = mag;
    return 1;
}

static int offset_arg(int argc, Value *args, int i, int64_t *off, const char *fn) {
    *off = 0;
    if (argc <= i || GET_TYPE(args[i]) == VAL_NONE) return 1;
    if (GET_TYPE(args[i]) != VAL_INT) { ffi_error("%s: the offset must be a whole number", fn); return 0; }
    *off = AS_INT(args[i]);
    return 1;
}

/* ffi.alloc(size): zeroed memory, freed with ffi.free. */
static Value ffi_alloc(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT || AS_INT(args[0]) <= 0 || AS_INT(args[0]) > ((int64_t)1 << 40))
        return ffi_error("ffi.alloc: expected a size in bytes");
    void *p = calloc(1, (size_t)AS_INT(args[0]));
    if (!p) return ffi_error("ffi.alloc: out of memory");
    return uint_value((uint64_t)(uintptr_t)p);
}

static Value ffi_free(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) == VAL_NONE) return value_none();
    uint64_t p;
    if (!pointer_arg(args[0], &p, "ffi.free")) return value_none();
    free((void *)(uintptr_t)p);
    return value_none();
}

/* ffi.load(ptr, type, offset): the value of that type stored at ptr+offset. */
static Value ffi_load(int argc, Value *args) {
    uint64_t p;
    FfiType t;
    int64_t off;
    if (argc < 2) return ffi_error("ffi.load: expected (pointer, type, offset)");
    if (!pointer_arg(args[0], &p, "ffi.load") || !parse_type(args[1], &t, "ffi.load", "the value") ||
        !offset_arg(argc, args, 2, &off, "ffi.load")) return value_none();
    if (t == T_VOID) return ffi_error("ffi.load: cannot read void");
    const unsigned char *at = (const unsigned char *)(uintptr_t)(p + (uint64_t)off);
    uint64_t raw = 0;
    memcpy(&raw, at, (size_t)type_size(t));
    if (t == T_DOUBLE) { double d; memcpy(&d, at, sizeof(d)); return value_from_double(d); }
    if (t == T_FLOAT) { float fl; memcpy(&fl, at, sizeof(fl)); return value_from_double((double)fl); }
    return integer_result(raw, t);
}

/* ffi.store(ptr, type, value, offset). */
static Value ffi_store(int argc, Value *args) {
    uint64_t p;
    FfiType t;
    int64_t off;
    if (argc < 3) return ffi_error("ffi.store: expected (pointer, type, value, offset)");
    if (!pointer_arg(args[0], &p, "ffi.store") || !parse_type(args[1], &t, "ffi.store", "the value") ||
        !offset_arg(argc, args, 3, &off, "ffi.store")) return value_none();
    unsigned char *at = (unsigned char *)(uintptr_t)(p + (uint64_t)off);
    Value v = args[2];
    if (t == T_VOID || t == T_STRING) return ffi_error("ffi.store: cannot write %s (for text, use ffi.from_string and store the pointer)", type_label(t));
    if (t == T_DOUBLE || t == T_FLOAT) {
        if (!is_number(v)) return ffi_error("ffi.store: expected a number");
        double d = number_value(v);
        if (t == T_DOUBLE) memcpy(at, &d, sizeof(d));
        else { float fl = (float)d; memcpy(at, &fl, sizeof(fl)); }
        return value_none();
    }
    uint64_t bits;
    if (t == T_BOOL) {
        if (GET_TYPE(v) != VAL_BOOL) return ffi_error("ffi.store: expected true or false");
        bits = (v & 1) ? 1 : 0;
    } else if (t == T_POINTER && GET_TYPE(v) == VAL_NONE) bits = 0;
    else if (!integer_arg(v, t == T_POINTER ? T_U64 : t, &bits, "ffi.store", 2)) return value_none();
    memcpy(at, &bits, (size_t)type_size(t));   /* little-endian targets only */
    return value_none();
}

/* ffi.string(ptr, length): the bytes at ptr as a string; without a length,
   up to the first NUL. */
static Value ffi_string(int argc, Value *args) {
    uint64_t p;
    if (argc < 1 || !pointer_arg(args[0], &p, "ffi.string")) return value_none();
    const char *s = (const char *)(uintptr_t)p;
    if (argc < 2 || GET_TYPE(args[1]) == VAL_NONE) return value_string_copy(s);
    if (GET_TYPE(args[1]) != VAL_INT || AS_INT(args[1]) < 0) return ffi_error("ffi.string: the length must be a whole number");
    size_t n = (size_t)AS_INT(args[1]);
    char *tmp = malloc(n + 1);
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    /* value_string_copy stops at a NUL; build the string with its length. */
    GCString *gs = gc_alloc(sizeof(GCString) + n + 1, OBJ_STRING);
    gs->size = (int)n;
    memcpy(gs->chars, tmp, n + 1);
    free(tmp);
    return make_value(VAL_STRING, (uint64_t)(uintptr_t)gs);
}

/* ffi.from_string(text): a NUL-terminated copy in C memory (free it with
   ffi.free). */
static Value ffi_from_string(int argc, Value *args) {
    if (argc < 1 || !is_str(args[0])) return ffi_error("ffi.from_string: expected a string");
    size_t n = rope_length(args[0]);
    char *c = malloc(n + 1);
    if (!c) return ffi_error("ffi.from_string: out of memory");
    memcpy(c, AS_STRING(args[0]), n);
    c[n] = '\0';
    return uint_value((uint64_t)(uintptr_t)c);
}

static Value ffi_sizeof(int argc, Value *args) {
    FfiType t;
    if (argc < 1 || !parse_type(args[0], &t, "ffi.sizeof", "the value")) return value_none();
    return value_int(type_size(t));
}

static Value ffi_errno(int argc, Value *args) { (void)argc; (void)args; return value_int(last_errno); }

static Value ffi_last_error(int argc, Value *args) {
    (void)argc; (void)args;
#ifdef _WIN32
    return value_int((int64_t)last_win_error);
#else
    return value_int(last_errno);
#endif
}

/* The C runtime and math library, by their name on this system. */
static Value ffi_libc(int argc, Value *args) {
    (void)argc; (void)args;
#if defined(_WIN32)
    return value_string_copy("ucrtbase.dll");
#elif defined(__APPLE__)
    return value_string_copy("/usr/lib/libSystem.B.dylib");
#else
    return value_string_copy("libc.so.6");
#endif
}

static Value ffi_libm(int argc, Value *args) {
    (void)argc; (void)args;
#if defined(_WIN32)
    return value_string_copy("ucrtbase.dll");
#elif defined(__APPLE__)
    return value_string_copy("/usr/lib/libSystem.B.dylib");
#else
    return value_string_copy("libm.so.6");
#endif
}

/* ffi.lib_name("sqlite3"): sqlite3.dll, libsqlite3.so or libsqlite3.dylib. */
static Value ffi_lib_name(int argc, Value *args) {
    if (argc < 1 || !is_str(args[0])) return ffi_error("ffi.lib_name: expected a library name");
    char buf[512];
#if defined(_WIN32)
    snprintf(buf, sizeof(buf), "%s.dll", AS_STRING(args[0]));
#elif defined(__APPLE__)
    snprintf(buf, sizeof(buf), "lib%s.dylib", AS_STRING(args[0]));
#else
    snprintf(buf, sizeof(buf), "lib%s.so", AS_STRING(args[0]));
#endif
    return value_string_copy(buf);
}

void glow_ffi_init(void) {
    Dict *d = dict_new(16);
    dict_set(d, "open", native_func_new("open", ffi_open));
    dict_set(d, "alloc", native_func_new("alloc", ffi_alloc));
    dict_set(d, "free", native_func_new("free", ffi_free));
    dict_set(d, "load", native_func_new("load", ffi_load));
    dict_set(d, "store", native_func_new("store", ffi_store));
    dict_set(d, "string", native_func_new("string", ffi_string));
    dict_set(d, "from_string", native_func_new("from_string", ffi_from_string));
    dict_set(d, "sizeof", native_func_new("sizeof", ffi_sizeof));
    dict_set(d, "errno", native_func_new("errno", ffi_errno));
    dict_set(d, "last_error", native_func_new("last_error", ffi_last_error));
    dict_set(d, "libc", native_func_new("libc", ffi_libc));
    dict_set(d, "libm", native_func_new("libm", ffi_libm));
    dict_set(d, "lib_name", native_func_new("lib_name", ffi_lib_name));
    dict_set(d, "null", value_int(0));
    var_set("ffi", value_dict(d));
}
