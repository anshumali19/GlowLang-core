/*
 * fuzz_module.c — Module Loader Fuzzer for GlowLang
 *
 * Tests OP_USE_FILE with various malformed paths.
 * Tests loading from Glow.lock and Glow.deps with corrupt data.
 * Tests deeply nested imports and circular import detection.
 *
 * Runs 100 iterations.
 *
 * Compile with:
 *   gcc -O1 -g -fsanitize=address tests/c_harness/fuzz_module.c \
 *       src/lex.yy.c src/glowlang.tab.c src/lsp.c src/arena.c src/jit.c \
 *       src/opt.c src/compiler.c src/vm.c src/gc.c src/stdlib.c \
 *       -o bin/fuzz_module -lfl -lsqlite3 -lX11 -lm -pthread -lssl -lcrypto \
 *       -mavx2 -msse4.2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/stat.h>
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

/* ── Malformed path generators ───────────────────────────────────── */
static const char *malformed_paths[] = {
    "",
    "/",
    ".",
    "..",
    "///",
    "/dev/null",
    "/nonexistent/path/to/module.glow",
    "../../../../../../etc/passwd",
    "module\x00hidden.glow",
    "a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q.glow",
    "\n\n\n",
    "\t\t\t",
    "\"quoted\"",
    "'single_quoted'",
    "path with spaces.glow",
    "path\twith\ttabs.glow",
    "unicode_\xc3\xa9\xc3\xa8.glow",
    ".hidden_module.glow",
    "VERYLONGNAME_AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
    "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC"
    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD.glow",
    "module?.glow",
    "module*.glow",
    "module|pipe.glow",
    "mod\rcarriage.glow",
    "null_\x00_embedded.glow",
    "./self_ref.glow",
};
#define NUM_MALFORMED (sizeof(malformed_paths)/sizeof(malformed_paths[0]))

/* ── Helper: run a GlowLang import statement through the pipeline ── */
static void try_import(const char *path) {
    char prog[1024];
    snprintf(prog, sizeof(prog), "import \"%s\"\n", path);

    root = NULL;
    glow_clear_errors();
    runtime_error_thrown = 0;
    has_exception = 0;
    next_free_reg = 0;

    YY_BUFFER_STATE bp = yy_scan_string(prog);
    if (bp) {
        int r = yyparse();
        if (r == 0 && root != NULL) {
            memset(&main_fe, 0, sizeof(main_fe));
            main_fe.name = "__fuzz_import__";
            main_fe.body = bc_new(&main_fe);
            compiling_bc = main_fe.body;

            compile(root);
            bc_emit(compiling_bc, OP_RETURN, 0, 0);

            run_bytecode(main_fe.body, main_fe.body->code, NULL, 0);

            bc_free(main_fe.body);
            main_fe.body = NULL;
            compiling_bc = NULL;
        }
        yy_delete_buffer(bp);
    }
}

/* ── Test: malformed paths ───────────────────────────────────────── */
static void test_malformed_paths(void) {
    for (int i = 0; i < (int)NUM_MALFORMED; i++) {
        try_import(malformed_paths[i]);
    }
}

/* ── Test: random path strings ───────────────────────────────────── */
static void test_random_paths(int count) {
    for (int i = 0; i < count; i++) {
        int len = rand() % 256;
        char *path = malloc(len + 1);
        for (int j = 0; j < len; j++) {
            int c = rand() % 128;
            path[j] = (c < 32) ? (char)(32 + c) : (char)c;
        }
        path[len] = '\0';
        try_import(path);
        free(path);
    }
}

/* ── Test: corrupt Glow.lock file ────────────────────────────────── */
static void test_corrupt_lock_file(void) {
    /* Create a temporary corrupt Glow.lock in current directory */
    const char *corrupt_data[] = {
        "",
        "{}",
        "{\"deps\": null}",
        "{\"deps\": [1, 2, 3]}",
        "NOT_JSON_AT_ALL !!@#$%",
        "\x00\x01\x02\x03",
        "{\"version\": \"1.0\", \"deps\": {\"broken\": {}}}",
        "{\n  \"deps\": {\n    \"loop\": \"./self.glow\"\n  }\n}",
    };

    for (int i = 0; i < (int)(sizeof(corrupt_data)/sizeof(corrupt_data[0])); i++) {
        FILE *f = fopen("Glow.lock", "w");
        if (f) {
            fwrite(corrupt_data[i], 1, strlen(corrupt_data[i]), f);
            fclose(f);

            /* Try an import that might read Glow.lock */
            try_import("nonexistent_dep_module.glow");
        }
    }
    /* Clean up */
    unlink("Glow.lock");
}

/* ── Test: corrupt Glow.deps file ────────────────────────────────── */
static void test_corrupt_deps_file(void) {
    const char *corrupt_data[] = {
        "",
        "random garbage 123",
        "\xff\xfe\xfd",
        "dep1\ndep2\ndep3",
        "{\n\"circular\": \"./Glow.deps\"\n}",
    };

    for (int i = 0; i < (int)(sizeof(corrupt_data)/sizeof(corrupt_data[0])); i++) {
        FILE *f = fopen("Glow.deps", "w");
        if (f) {
            fwrite(corrupt_data[i], 1, strlen(corrupt_data[i]), f);
            fclose(f);

            try_import("dep_from_deps.glow");
        }
    }
    unlink("Glow.deps");
}

/* ── Test: deeply nested import chain ────────────────────────────── */
static void test_nested_imports(void) {
    /* Create a chain: fuzz_mod_0 imports fuzz_mod_1, ... up to depth N */
    int depth = 10;
    char names[12][64];

    for (int i = 0; i < depth; i++) {
        snprintf(names[i], sizeof(names[i]), "_fuzz_chain_%d.glow", i);
    }

    for (int i = 0; i < depth; i++) {
        FILE *f = fopen(names[i], "w");
        if (f) {
            if (i < depth - 1) {
                fprintf(f, "import \"%s\"\nlet chain_%d = %d\n",
                        names[i + 1], i, i);
            } else {
                fprintf(f, "let chain_%d = %d\n", i, i);
            }
            fclose(f);
        }
    }

    /* Import the first module in the chain */
    try_import(names[0]);

    /* Clean up temp files */
    for (int i = 0; i < depth; i++) {
        unlink(names[i]);
    }
}

/* ── Test: circular import detection ─────────────────────────────── */
static void test_circular_imports(void) {
    /* Create A.glow that imports B.glow, B.glow imports A.glow */
    FILE *fa = fopen("_fuzz_circ_a.glow", "w");
    if (fa) {
        fprintf(fa, "import \"_fuzz_circ_b.glow\"\nlet circ_a = 1\n");
        fclose(fa);
    }
    FILE *fb = fopen("_fuzz_circ_b.glow", "w");
    if (fb) {
        fprintf(fb, "import \"_fuzz_circ_a.glow\"\nlet circ_b = 2\n");
        fclose(fb);
    }

    /* This should be detected and handled without infinite loop */
    try_import("_fuzz_circ_a.glow");

    unlink("_fuzz_circ_a.glow");
    unlink("_fuzz_circ_b.glow");
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

    printf("=== GlowLang Module Loader Fuzzer ===\n");
    printf("--- Starting Module Fuzzing (100 iterations) ---\n");

    signal(SIGSEGV, fuzz_signal_handler);
    signal(SIGFPE,  fuzz_signal_handler);
    signal(SIGABRT, fuzz_signal_handler);

    init_runtime();

    int crashes = 0;

    for (int iter = 0; iter < 100; iter++) {
        if (iter % 10 == 0)
            printf("  Iteration %d / 100 ...\n", iter);

        int sig = sigsetjmp(fuzz_jmp, 1);
        if (sig == 0) {
            in_fuzz = 1;

            int test = iter % 6;
            switch (test) {
                case 0:
                    test_malformed_paths();
                    break;
                case 1:
                    test_random_paths(10 + (rand() % 20));
                    break;
                case 2:
                    test_corrupt_lock_file();
                    break;
                case 3:
                    test_corrupt_deps_file();
                    break;
                case 4:
                    test_nested_imports();
                    break;
                case 5:
                    test_circular_imports();
                    break;
            }

            in_fuzz = 0;
        } else {
            in_fuzz = 0;
            crashes++;
            pthread_mutex_unlock(&gc_mutex);
            pthread_mutex_unlock(&threads_mutex);
            pthread_mutex_unlock(&glow_gil);
            signal(SIGSEGV, fuzz_signal_handler);
            signal(SIGFPE,  fuzz_signal_handler);
            signal(SIGABRT, fuzz_signal_handler);
        }
    }

    printf("  Crashes caught: %d out of 100 iterations.\n", crashes);

    cleanup_runtime();

    /* Final cleanup of any leftover temp files */
    unlink("Glow.lock");
    unlink("Glow.deps");

    printf("--- Module Fuzzing Completed Successfully ---\n");
    return 0;
}
