/* Test library for tests/ffi_abi_test.glow: argument and return shapes the C
   runtime does not cover, to check the FFI's calling conventions on every
   platform. Built next to glowlang by the Makefile, CMake and
   scripts/build-windows.ps1 (as ffi_testlib.dll / libffi_testlib.so /
   libffi_testlib.dylib). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

/* Every integer width: narrow results must come back sign- or zero-extended. */
EXPORT int8_t t_i8(int8_t x) { return (int8_t)(x - 1); }
EXPORT uint8_t t_u8(uint8_t x) { return (uint8_t)(x + 1); }
EXPORT int16_t t_i16(int16_t x) { return (int16_t)(x - 1); }
EXPORT uint16_t t_u16(uint16_t x) { return (uint16_t)(x + 1); }
EXPORT int32_t t_i32(int32_t x) { return x - 1; }
EXPORT uint32_t t_u32(uint32_t x) { return x + 1; }
EXPORT int64_t t_i64(int64_t x) { return x - 1; }
EXPORT uint64_t t_u64(uint64_t x) { return x + 1; }
EXPORT int64_t t_i64_min(void) { return INT64_MIN; }
EXPORT uint64_t t_u64_max(void) { return UINT64_MAX; }
EXPORT bool t_not(bool b) { return !b; }

/* Floats in and out, alone and mixed with integers in every position. */
EXPORT float t_addf(float a, float b) { return a + b; }
EXPORT double t_f2d(float a, double b) { return (double)a * b; }
EXPORT double t_id(int a, double b) { return a + b; }
EXPORT double t_di(double a, int b) { return a - b; }
EXPORT double t_idid(int a, double b, int c, double d) { return a * 1000.0 + b * 100.0 + c * 10.0 + d; }
EXPORT double t_didi(double a, int b, double c, int d) { return a * 1000.0 + b * 100.0 + c * 10.0 + d; }
EXPORT float t_fifi(float a, int b, float c, int d) { return a + (float)b * 10.0f + c * 100.0f + (float)d * 1000.0f; }

/* More arguments than registers (stack), integers only and mixed. */
EXPORT int64_t t_sum10(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e,
                       int64_t f, int64_t g, int64_t h, int64_t i, int64_t j) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j;
}
EXPORT double t_mix12(int a, double b, int c, double d, int e, double f,
                      int g, double h, int i, double j, int k, float l) {
    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 + j * 10 + k * 11 + (double)l * 12;
}
EXPORT double t_sum8d(double a, double b, double c, double d, double e, double f, double g, double h) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h;
}
/* Narrow integers past the registers. */
EXPORT int32_t t_narrow9(int8_t a, uint8_t b, int16_t c, uint16_t d, int32_t e, uint32_t f, int8_t g, int16_t h, int32_t i) {
    return a + b + c + d + e + (int32_t)f + g + h + i;
}

/* Pointers and strings. */
EXPORT void *t_ptr(void *p) { return p; }
EXPORT bool t_is_null(const void *p) { return p == NULL; }
EXPORT void t_fill(int32_t *out, int32_t n) { for (int32_t i = 0; i < n; i++) out[i] = i * i; }
EXPORT const char *t_hello(void) { return "hello from C"; }
EXPORT const char *t_null_string(void) { return NULL; }
EXPORT size_t t_len(const char *s) { return s ? strlen(s) : 0; }
EXPORT void t_upper(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32); }

/* A global for lib.symbol. */
EXPORT int32_t t_counter = 41;
EXPORT void t_bump(void) { t_counter++; }
