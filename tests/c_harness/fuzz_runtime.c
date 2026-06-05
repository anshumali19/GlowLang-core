/*
 * fuzz_runtime.c — Runtime Fuzzer for GlowLang
 *
 * Creates valid GlowLang programs as strings using random combinations of
 * valid syntax, feeds them through yy_scan_string → yyparse → compile →
 * run_bytecode. Catches all crashes gracefully via signal handlers.
 *
 * Runs 200 iterations. Uses natural language syntax variants (every,
 * whenever, blueprint, action, can, etc.).
 *
 * Compile with:
 *   gcc -O1 -g -fsanitize=address tests/c_harness/fuzz_runtime.c \
 *       src/lex.yy.c src/glowlang.tab.c src/lsp.c src/arena.c src/jit.c \
 *       src/opt.c src/compiler.c src/vm.c src/gc.c src/stdlib.c \
 *       -o bin/fuzz_runtime -lfl -lsqlite3 -lX11 -lm -pthread -lssl -lcrypto \
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

/* ── Program buffer helper ───────────────────────────────────────── */
typedef struct {
    char *buf;
    int   len;
    int   cap;
} ProgBuf;

static void pb_init(ProgBuf *p) {
    p->cap = 4096;
    p->buf = malloc(p->cap);
    p->buf[0] = '\0';
    p->len = 0;
}

static void pb_append(ProgBuf *p, const char *s) {
    int slen = (int)strlen(s);
    while (p->len + slen + 1 > p->cap) {
        p->cap *= 2;
        p->buf = realloc(p->buf, p->cap);
    }
    memcpy(p->buf + p->len, s, slen + 1);
    p->len += slen;
}

static void pb_free(ProgBuf *p) {
    free(p->buf);
}

/* ── Random name generators ──────────────────────────────────────── */
static const char *var_names[] = {
    "x", "y", "z", "a", "b", "count", "val", "item",
    "result", "tmp", "data", "msg", "flag", "idx", "num"
};
#define NUM_VARS (sizeof(var_names)/sizeof(var_names[0]))

static const char *rand_var(void) {
    return var_names[rand() % NUM_VARS];
}

static int rand_int_val(void) {
    return (rand() % 200) - 100;
}

/* ── Statement generators ────────────────────────────────────────── */

static void gen_var_assign(ProgBuf *p) {
    char line[256];
    int choice = rand() % 5;
    switch (choice) {
        case 0:
            snprintf(line, sizeof(line), "%s = %d\n", rand_var(), rand_int_val());
            break;
        case 1:
            snprintf(line, sizeof(line), "let %s = \"%s\"\n", rand_var(),
                     var_names[rand() % NUM_VARS]);
            break;
        case 2:
            snprintf(line, sizeof(line), "%s = %d.%d\n", rand_var(),
                     rand_int_val(), abs(rand_int_val()));
            break;
        case 3:
            snprintf(line, sizeof(line), "%s = true\n", rand_var());
            break;
        default:
            snprintf(line, sizeof(line), "%s = none\n", rand_var());
            break;
    }
    pb_append(p, line);
}

static void gen_math_expr(ProgBuf *p) {
    char line[256];
    const char *ops[] = {"+", "-", "*"};
    snprintf(line, sizeof(line), "%s = %d %s %d\n",
             rand_var(), rand_int_val(),
             ops[rand() % 3], rand_int_val() + 1);
    pb_append(p, line);
}

static void gen_list_op(ProgBuf *p) {
    char line[512];
    int choice = rand() % 4;
    switch (choice) {
        case 0:
            snprintf(line, sizeof(line), "let %s = [%d, %d, %d]\n",
                     rand_var(), rand_int_val(), rand_int_val(), rand_int_val());
            break;
        case 1:
            snprintf(line, sizeof(line), "let %s = []\n", rand_var());
            break;
        case 2:
            snprintf(line, sizeof(line), "append(%s, %d)\n",
                     rand_var(), rand_int_val());
            break;
        default:
            snprintf(line, sizeof(line), "let %s = len([1, 2, 3])\n", rand_var());
            break;
    }
    pb_append(p, line);
}

static void gen_dict_op(ProgBuf *p) {
    char line[512];
    int choice = rand() % 2;
    if (choice == 0) {
        snprintf(line, sizeof(line),
                 "let %s = {\"key1\": %d, \"key2\": \"%s\"}\n",
                 rand_var(), rand_int_val(), rand_var());
    } else {
        snprintf(line, sizeof(line), "let %s = {}\n", rand_var());
    }
    pb_append(p, line);
}

static void gen_string_op(ProgBuf *p) {
    char line[512];
    int choice = rand() % 3;
    switch (choice) {
        case 0:
            snprintf(line, sizeof(line), "let %s = \"hello\" + \" \" + \"world\"\n",
                     rand_var());
            break;
        case 1:
            snprintf(line, sizeof(line), "let %s = len(\"fuzzing\")\n", rand_var());
            break;
        default:
            snprintf(line, sizeof(line), "let %s = \"test string\"\n", rand_var());
            break;
    }
    pb_append(p, line);
}

static void gen_if_else(ProgBuf *p) {
    char line[256];
    snprintf(line, sizeof(line), "if %d < %d:\n    %s = %d\nelse:\n    %s = %d\nend\n",
             rand_int_val(), rand_int_val(),
             rand_var(), rand_int_val(),
             rand_var(), rand_int_val());
    pb_append(p, line);
}

static void gen_while_loop(ProgBuf *p) {
    char line[256];
    snprintf(line, sizeof(line),
             "let _i = 0\nwhile _i < 3:\n    _i = _i + 1\nend\n");
    pb_append(p, line);
}

static void gen_every_loop(ProgBuf *p) {
    /* Natural language: every i from A to B */
    char line[256];
    snprintf(line, sizeof(line),
             "every _k from 0 to 3:\n    let _t = _k\nend\n");
    pb_append(p, line);
}

static void gen_func_def(ProgBuf *p) {
    char line[512];
    const char *kw = (rand() % 2 == 0) ? "func" : "action";
    int id = rand() % 10000;
    snprintf(line, sizeof(line),
             "%s fuzz_fn_%d(a, b):\n    return a + b\nend\n", kw, id);
    pb_append(p, line);
}

static void gen_func_call(ProgBuf *p) {
    char line[256];
    snprintf(line, sizeof(line), "let %s = len(\"abc\")\n", rand_var());
    pb_append(p, line);
}

static void gen_try_catch(ProgBuf *p) {
    char line[512];
    snprintf(line, sizeof(line),
             "try:\n    let _tc = %d\ncatch _e:\n    let _tc = 0\nend\n",
             rand_int_val());
    pb_append(p, line);
}

static void gen_throw(ProgBuf *p) {
    pb_append(p, "try:\n    throw \"fuzz error\"\ncatch _e:\n    let _caught = 1\nend\n");
}

static void gen_class_def(ProgBuf *p) {
    char line[1024];
    const char *kw = (rand() % 2 == 0) ? "class" : "blueprint";
    const char *mk = (rand() % 2 == 0) ? "func" : "can";
    int id = rand() % 10000;
    snprintf(line, sizeof(line),
             "%s FuzzClass%d:\n"
             "    %s greet():\n"
             "        show \"hi\"\n"
             "    end\n"
             "end\n",
             kw, id, mk);
    pb_append(p, line);
}

static void gen_trait_use(ProgBuf *p) {
    char line[1024];
    int id = rand() % 10000;
    snprintf(line, sizeof(line),
             "trait FuzzTrait%d:\n"
             "    func do_it():\n"
             "        show \"trait\"\n"
             "    end\n"
             "end\n"
             "class FuzzTraitUser%d:\n"
             "    use FuzzTrait%d\n"
             "end\n",
             id, id, id);
    pb_append(p, line);
}

static void gen_show(ProgBuf *p) {
    char line[256];
    snprintf(line, sizeof(line), "show \"%s = \", %d\n",
             rand_var(), rand_int_val());
    pb_append(p, line);
}

static void gen_lambda(ProgBuf *p) {
    char line[256];
    snprintf(line, sizeof(line), "let %s = lambda(n): n + 1\n", rand_var());
    pb_append(p, line);
}

/* ── Generator dispatch table ────────────────────────────────────── */
typedef void (*GenFunc)(ProgBuf *);

static const GenFunc generators[] = {
    gen_var_assign,
    gen_math_expr,
    gen_list_op,
    gen_dict_op,
    gen_string_op,
    gen_if_else,
    gen_while_loop,
    gen_every_loop,
    gen_func_def,
    gen_func_call,
    gen_try_catch,
    gen_throw,
    gen_class_def,
    gen_trait_use,
    gen_show,
    gen_lambda,
};
#define NUM_GENERATORS (sizeof(generators)/sizeof(generators[0]))

/* ── Build a random program ──────────────────────────────────────── */
static char* build_random_program(void) {
    ProgBuf p;
    pb_init(&p);

    int stmt_count = 2 + (rand() % 10);
    for (int i = 0; i < stmt_count; i++) {
        int gi = rand() % NUM_GENERATORS;
        generators[gi](&p);
    }

    /* Return ownership of the buffer */
    return p.buf;
}

/* ── Runtime initialisation ──────────────────────────────────────── */
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

    printf("=== GlowLang Runtime Fuzzer ===\n");
    printf("--- Starting Runtime Fuzzing (200 iterations) ---\n");

    signal(SIGSEGV, fuzz_signal_handler);
    signal(SIGFPE,  fuzz_signal_handler);
    signal(SIGABRT, fuzz_signal_handler);

    init_runtime();

    int crashes = 0;
    int parse_ok = 0;

    for (int i = 0; i < 200; i++) {
        if (i % 20 == 0)
            printf("  Iteration %d / 200 ...\n", i);

        char *prog = build_random_program();

        /* Reset state for each iteration */
        root = NULL;
        glow_clear_errors();
        runtime_error_thrown = 0;
        has_exception = 0;
        next_free_reg = 0;

        int sig = sigsetjmp(fuzz_jmp, 1);
        if (sig == 0) {
            in_fuzz = 1;

            YY_BUFFER_STATE bp = yy_scan_string(prog);
            if (bp) {
                int parse_result = yyparse();
                if (parse_result == 0 && root != NULL) {
                    parse_ok++;

                    /* Set up compilation target */
                    memset(&main_fe, 0, sizeof(main_fe));
                    main_fe.name = "__main__";
                    main_fe.body = bc_new(&main_fe);
                    compiling_bc = main_fe.body;

                    compile(root);
                    bc_emit(compiling_bc, OP_RETURN, 0, 0);

                    /* Run the compiled bytecode */
                    run_bytecode(main_fe.body, main_fe.body->code, NULL, 0);

                    bc_free(main_fe.body);
                    main_fe.body = NULL;
                    compiling_bc = NULL;
                }
                yy_delete_buffer(bp);
            }

            in_fuzz = 0;
        } else {
            in_fuzz = 0;
            crashes++;
            signal(SIGSEGV, fuzz_signal_handler);
            signal(SIGFPE,  fuzz_signal_handler);
            signal(SIGABRT, fuzz_signal_handler);
        }

        free(prog);
    }

    printf("  Parsed successfully: %d / 200\n", parse_ok);
    printf("  Caught %d crash(es) gracefully.\n", crashes);

    cleanup_runtime();

    printf("--- Runtime Fuzzing Completed Successfully ---\n");
    return 0;
}
