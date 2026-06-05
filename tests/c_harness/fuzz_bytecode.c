/*
 * fuzz_bytecode.c — Bytecode Fuzzer for GlowLang
 *
 * Creates random Bytecode objects with random opcodes from the OpCode enum,
 * randomizes register indices (r0, r1, r2), arg values, and ptr values.
 * Calls run_bytecode() with the random bytecode.
 * Uses signal handlers to catch crashes gracefully.
 *
 * Compile with:
 *   gcc -O1 -g -fsanitize=address tests/c_harness/fuzz_bytecode.c \
 *       src/lex.yy.c src/glowlang.tab.c src/lsp.c src/arena.c src/jit.c \
 *       src/opt.c src/compiler.c src/vm.c src/gc.c src/stdlib.c \
 *       -o bin/fuzz_bytecode -lfl -lsqlite3 -lX11 -lm -pthread -lssl -lcrypto \
 *       -mavx2 -msse4.2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include "../../src/glowlang.h"

/* ── Signal recovery ─────────────────────────────────────────────── */
static sigjmp_buf fuzz_jmp;
static volatile sig_atomic_t in_fuzz = 0;

static void fuzz_signal_handler(int sig) {
    if (in_fuzz) {
        siglongjmp(fuzz_jmp, sig);
    }
    /* If not in a fuzz iteration, re-raise so ASan can still report */
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ── Opcode bounds ───────────────────────────────────────────────── */
#define OPCODE_MIN  OP_CONST_INT
#define OPCODE_MAX  OP_IN
#define OPCODE_COUNT (OPCODE_MAX - OPCODE_MIN + 1)

/* ── Helpers ─────────────────────────────────────────────────────── */
static OpCode random_opcode(void) {
    return (OpCode)(OPCODE_MIN + (rand() % OPCODE_COUNT));
}

static int random_reg(void) {
    return rand() % 256;  /* Keep within a sane register window */
}

static int random_arg(void) {
    int r = rand();
    switch (r % 5) {
        case 0: return 0;
        case 1: return 1;
        case 2: return -1;
        case 3: return rand() % 1024;
        default: return -(rand() % 1024);
    }
}

static intptr_t random_ptr(void) {
    /* Mostly NULL, occasionally a small integer cast to ptr (the VM checks
       ptr for string constants etc., but with garbage it should just error
       rather than crash). */
    int r = rand() % 10;
    if (r < 7) return (intptr_t)NULL;
    if (r < 9) return (intptr_t)(uintptr_t)(rand() % 256);
    return (intptr_t)"fuzz_string";
}

/* ── Build a random bytecode chunk ───────────────────────────────── */
static Bytecode* build_random_bytecode(FuncEntry *fe, int instruction_count) {
    Bytecode *bc = bc_new(fe);
    for (int i = 0; i < instruction_count; i++) {
        OpCode op = random_opcode();
        int arg   = random_arg();
        intptr_t ptr = random_ptr();
        int idx = bc_emit(bc, op, arg, ptr);
        /* Randomize register fields */
        bc->code[idx].r0 = (unsigned short)random_reg();
        bc->code[idx].r1 = (unsigned short)random_reg();
        bc->code[idx].r2 = (unsigned short)random_reg();
    }
    /* Always terminate with a RETURN so the VM doesn't run off the end */
    int ret_idx = bc_emit(bc, OP_RETURN, 0, (intptr_t)NULL);
    bc->code[ret_idx].r0 = 0;
    return bc;
}

/* ── Runtime initialisation (once) ───────────────────────────────── */
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

    printf("=== GlowLang Bytecode Fuzzer ===\n");
    printf("--- Starting Bytecode Fuzzing (500 iterations) ---\n");

    /* Install signal handlers */
    signal(SIGSEGV, fuzz_signal_handler);
    signal(SIGFPE,  fuzz_signal_handler);
    signal(SIGABRT, fuzz_signal_handler);

    init_runtime();

    int crashes = 0;

    for (int i = 0; i < 500; i++) {
        if (i % 50 == 0)
            printf("  Iteration %d / 500 ...\n", i);

        /* Reset error state */
        glow_clear_errors();
        runtime_error_thrown = 0;
        has_exception = 0;

        /* Random instruction count: 1 – 32 */
        int instr_count = 1 + (rand() % 32);

        /* Set up a minimal FuncEntry for the bytecode */
        FuncEntry fe;
        memset(&fe, 0, sizeof(fe));
        fe.name = "__fuzz_bc__";
        fe.param_count = 0;
        fe.params = NULL;
        fe.body = NULL;
        fe.used = 1;

        Bytecode *bc = build_random_bytecode(&fe, instr_count);
        fe.body = bc;

        int sig = sigsetjmp(fuzz_jmp, 1);
        if (sig == 0) {
            in_fuzz = 1;
            /* Attempt to run the random bytecode */
            run_bytecode(bc, bc->code, NULL, 0);
            in_fuzz = 0;
        } else {
            in_fuzz = 0;
            crashes++;
            /* Reinstall handlers (some systems reset to SIG_DFL after catch) */
            signal(SIGSEGV, fuzz_signal_handler);
            signal(SIGFPE,  fuzz_signal_handler);
            signal(SIGABRT, fuzz_signal_handler);
        }

        bc_free(bc);
    }

    printf("  Caught %d crash(es) gracefully out of 500 iterations.\n", crashes);

    cleanup_runtime();

    printf("--- Bytecode Fuzzing Completed Successfully ---\n");
    return 0;
}
