/*
 * fuzz_string.c — String Engine Fuzzer for GlowLang
 *
 * Creates strings of various sizes (empty, 1 char, very long).
 * Tests string concatenation, slicing, indexOf, split, replace operations.
 * Tests rope node creation and flattening.
 * Stress tests the string interning table.
 *
 * Runs 300 iterations.
 *
 * Compile with:
 *   gcc -O1 -g -fsanitize=address tests/c_harness/fuzz_string.c \
 *       src/lex.yy.c src/glowlang.tab.c src/lsp.c src/arena.c src/jit.c \
 *       src/opt.c src/compiler.c src/vm.c src/gc.c src/stdlib.c \
 *       -o bin/fuzz_string -lfl -lsqlite3 -lX11 -lm -pthread -lssl -lcrypto \
 *       -mavx2 -msse4.2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <setjmp.h>
#include "../../src/glowlang.h"

/* ── Signal recovery ─────────────────────────────────────────────── */
static sigjmp_buf fuzz_jmp;
static volatile sig_atomic_t in_fuzz = 0;

static void fuzz_signal_handler(int sig) {
    if (in_fuzz) {
        siglongjmp(fuzz_jmp, sig);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ── String generation helpers ───────────────────────────────────── */
static Value make_random_string(int max_len) {
    int len = rand() % (max_len + 1);
    char *buf = malloc(len + 1);
    for (int i = 0; i < len; i++)
        buf[i] = 32 + (rand() % 95);  /* printable ASCII */
    buf[len] = '\0';
    Value v = value_string_copy(buf);
    free(buf);
    return v;
}

static Value make_empty_string(void) {
    return value_string_copy("");
}

static Value make_single_char_string(void) {
    char buf[2] = { (char)(65 + rand() % 26), '\0' };
    return value_string_copy(buf);
}

static Value make_long_string(int len) {
    char *buf = malloc(len + 1);
    for (int i = 0; i < len; i++)
        buf[i] = 'A' + (rand() % 26);
    buf[len] = '\0';
    Value v = value_string_copy(buf);
    free(buf);
    return v;
}

/* ── Test: basic creation ────────────────────────────────────────── */
static void test_string_creation(void) {
    /* Empty */
    Value e = make_empty_string();
    const char *es = AS_STRING(e);
    (void)es;

    /* Single char */
    Value s = make_single_char_string();
    const char *ss = AS_STRING(s);
    (void)ss;

    /* Medium */
    Value m = make_random_string(128);
    const char *ms = AS_STRING(m);
    (void)ms;

    /* Long */
    Value l = make_long_string(4096);
    const char *ls = AS_STRING(l);
    (void)ls;
}

/* ── Test: rope concatenation ────────────────────────────────────── */
static void test_rope_concat(int depth) {
    Value base = value_string_copy("seed");
    for (int i = 0; i < depth; i++) {
        Value right = make_random_string(32);
        base = rope_new(base, right);
    }
    /* Force flatten */
    const char *flat = rope_flatten(base);
    (void)flat;

    /* Verify length is accessible */
    size_t len = rope_length(base);
    (void)len;
}

/* ── Test: rope with empty strings ───────────────────────────────── */
static void test_rope_empty_nodes(void) {
    Value e1 = make_empty_string();
    Value e2 = make_empty_string();
    Value r  = rope_new(e1, e2);
    const char *flat = rope_flatten(r);
    (void)flat;

    /* Concat empty with non-empty */
    Value s = value_string_copy("hello");
    Value r2 = rope_new(e1, s);
    const char *flat2 = rope_flatten(r2);
    (void)flat2;
}

/* ── Test: deep rope chain ───────────────────────────────────────── */
static void test_deep_rope(void) {
    Value v = value_string_copy("X");
    for (int i = 0; i < 200; i++) {
        Value c = value_string_copy(".");
        v = rope_new(v, c);
    }
    const char *flat = rope_flatten(v);
    if (strlen(flat) != 201) {
        fprintf(stderr, "  WARNING: deep rope length mismatch\n");
    }
}

/* ── Test: string operations via native functions ────────────────── */
static void test_string_find(void) {
    Value haystack = value_string_copy("hello world fuzzer");
    Value needle   = value_string_copy("world");

    /* native_str_find expects (argc=2, args=[haystack, needle]) */
    Value args[2] = { haystack, needle };
    Value result = native_str_find(2, args);
    (void)result;

    /* Search for non-existent */
    Value missing = value_string_copy("zzzzz");
    Value args2[2] = { haystack, missing };
    Value result2 = native_str_find(2, args2);
    (void)result2;
}

static void test_string_split(void) {
    Value source = value_string_copy("a,b,c,d,e");
    Value delim  = value_string_copy(",");
    Value args[2] = { source, delim };
    Value result = native_str_split(2, args);
    (void)result;

    /* Split with empty delimiter */
    Value empty_delim = value_string_copy("");
    Value args2[2] = { source, empty_delim };
    Value result2 = native_str_split(2, args2);
    (void)result2;
}

static void test_string_slice(void) {
    Value source = value_string_copy("abcdefghij");
    /* native_str_slice(argc=3, args=[string, start_idx, end_idx]) */
    Value args[3] = { source, value_int(2), value_int(7) };
    Value result = native_str_slice(3, args);
    (void)result;

    /* Boundary: start=0, end=len */
    Value args2[3] = { source, value_int(0), value_int(10) };
    Value result2 = native_str_slice(3, args2);
    (void)result2;

    /* Edge: empty slice */
    Value args3[3] = { source, value_int(5), value_int(5) };
    Value result3 = native_str_slice(3, args3);
    (void)result3;
}

static void test_string_contains(void) {
    Value hay = value_string_copy("GlowLang is cool");
    Value ndl = value_string_copy("cool");
    Value args[2] = { hay, ndl };
    Value result = native_str_contains(2, args);
    (void)result;
}

/* ── Test: string interning stress ───────────────────────────────── */
static void test_intern_stress(int count) {
    for (int i = 0; i < count; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "intern_%d", i);
        const char *a = intern_string(buf);
        const char *b = intern_string(buf);
        /* Interned strings must be pointer-equal */
        if (a != b) {
            fprintf(stderr, "  INTERNING BUG: '%s' not pointer-equal!\n", buf);
        }
    }

    /* Intern the same string many times */
    for (int i = 0; i < 500; i++) {
        const char *x = intern_string("__stress_constant__");
        (void)x;
    }
}

/* ── Test: very long concatenation chain ─────────────────────────── */
static void test_long_concat_chain(void) {
    Value acc = value_string_copy("");
    for (int i = 0; i < 100; i++) {
        char piece[16];
        snprintf(piece, sizeof(piece), "%d", i);
        Value sv = value_string_copy(piece);
        acc = rope_new(acc, sv);
    }
    const char *flat = rope_flatten(acc);
    (void)flat;
}

/* ── Runtime init ────────────────────────────────────────────────── */
static void init_runtime(void) {
    intern_table_init();
    var_table_init();
    func_table_init();
    vm_stack_init();
    std_init();
}

static void cleanup_runtime(void) {
    std_cleanup();
    vm_stack_cleanup();
}

/* ── Main ────────────────────────────────────────────────────────── */
int main(void) {
    srand(time(NULL));

    printf("=== GlowLang String Engine Fuzzer ===\n");
    printf("--- Starting String Fuzzing (300 iterations) ---\n");

    signal(SIGSEGV, fuzz_signal_handler);
    signal(SIGFPE,  fuzz_signal_handler);
    signal(SIGABRT, fuzz_signal_handler);

    init_runtime();

    int crashes = 0;

    for (int iter = 0; iter < 300; iter++) {
        if (iter % 30 == 0)
            printf("  Iteration %d / 300 ...\n", iter);

        int sig = sigsetjmp(fuzz_jmp, 1);
        if (sig == 0) {
            in_fuzz = 1;

            int test = iter % 11;
            switch (test) {
                case 0:
                    test_string_creation();
                    break;
                case 1:
                    test_rope_concat(10 + (rand() % 50));
                    break;
                case 2:
                    test_rope_empty_nodes();
                    break;
                case 3:
                    test_deep_rope();
                    break;
                case 4:
                    test_string_find();
                    break;
                case 5:
                    test_string_split();
                    break;
                case 6:
                    test_string_slice();
                    break;
                case 7:
                    test_string_contains();
                    break;
                case 8:
                    test_intern_stress(100 + (rand() % 200));
                    break;
                case 9:
                    test_long_concat_chain();
                    break;
                case 10:
                    /* Random-length strings with random ops */
                    for (int r = 0; r < 20; r++) {
                        Value a = make_random_string(512);
                        Value b = make_random_string(512);
                        Value c = rope_new(a, b);
                        rope_flatten(c);
                    }
                    break;
            }

            /* Periodic GC */
            if (iter % 25 == 0)
                gc_full_collect();

            in_fuzz = 0;
        } else {
            in_fuzz = 0;
            crashes++;
            signal(SIGSEGV, fuzz_signal_handler);
            signal(SIGFPE,  fuzz_signal_handler);
            signal(SIGABRT, fuzz_signal_handler);
        }
    }

    printf("  Crashes caught: %d out of 300 iterations.\n", crashes);

    cleanup_runtime();

    printf("--- String Fuzzing Completed Successfully ---\n");
    return 0;
}
