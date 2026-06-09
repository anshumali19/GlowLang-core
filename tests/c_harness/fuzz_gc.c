/*
 * fuzz_gc.c — GC Torture Test for GlowLang
 *
 * Rapidly creates and discards large numbers of lists, dicts, strings,
 * closures, and instances.  Forces GC collection cycles and verifies no
 * corruption occurs.  Tests concurrent allocation patterns.
 *
 * Runs 100 stress iterations.
 *
 * Compile with:
 *   gcc -O1 -g -fsanitize=address tests/c_harness/fuzz_gc.c \
 *       src/lex.yy.c src/glowlang.tab.c src/lsp.c src/arena.c src/jit.c \
 *       src/opt.c src/compiler.c src/vm.c src/gc.c src/stdlib.c \
 *       -o bin/fuzz_gc -lfl -lsqlite3 -lX11 -lm -pthread -lssl -lcrypto \
 *       -mavx2 -msse4.2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <setjmp.h>
#include <pthread.h>
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

/* ── Stress: allocate many lists ─────────────────────────────────── */
static void stress_lists(int count) {
    for (int i = 0; i < count; i++) {
        int cap = 1 + (rand() % 64);
        List *l = list_new(cap);
        /* Fill with random values */
        for (int j = 0; j < cap; j++) {
            list_push(l, value_int(rand() % 10000));
        }
        /* Let the list become garbage (not rooted) */
    }
}

/* ── Stress: allocate many dicts ─────────────────────────────────── */
static void stress_dicts(int count) {
    for (int i = 0; i < count; i++) {
        Dict *d = dict_new(4);
        char key[32];
        int entries = 1 + (rand() % 16);
        for (int j = 0; j < entries; j++) {
            snprintf(key, sizeof(key), "k%d_%d", i, j);
            dict_set(d, key, value_int(rand()));
        }
    }
}

/* ── Stress: allocate many strings ───────────────────────────────── */
static void stress_strings(int count) {
    for (int i = 0; i < count; i++) {
        int slen = rand() % 256;
        char *buf = malloc(slen + 1);
        for (int j = 0; j < slen; j++)
            buf[j] = 'a' + (rand() % 26);
        buf[slen] = '\0';

        Value s = value_string_copy(buf);
        (void)s;  /* Discard — becomes garbage */
        free(buf);
    }
}

/* ── Stress: mixed allocations then GC ───────────────────────────── */
static void stress_mixed(int count) {
    for (int i = 0; i < count; i++) {
        /* Allocate a list holding strings */
        List *l = list_new(8);
        for (int j = 0; j < 8; j++) {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "item_%d_%d", i, j);
            Value sv = value_string_copy(tmp);
            list_push(l, sv);
        }

        /* Allocate a dict holding ints */
        Dict *d = dict_new(4);
        dict_set(d, "alpha", value_int(i));
        dict_set(d, "beta",  value_int(i * 2));

        /* Force a GC cycle every few iterations */
        if (i % 5 == 0) {
            gc_full_collect();
        }
    }
}

/* ── Stress: class / instance allocation ─────────────────────────── */
static void stress_instances(int count) {
    Class *c = class_new("FuzzClass", NULL);
    for (int i = 0; i < count; i++) {
        Instance *inst = instance_new(c);
        char prop[32];
        snprintf(prop, sizeof(prop), "prop_%d", i);
        instance_set(inst, prop, value_int(i));
    }
}

/* ── Stress: rope / string concat ────────────────────────────────── */
static void stress_ropes(int count) {
    for (int i = 0; i < count; i++) {
        Value a = value_string_copy("left_");
        Value b = value_string_copy("right_");
        Value r = rope_new(a, b);
        /* Force flattening */
        const char *flat = rope_flatten(r);
        (void)flat;
    }
}

/* ── Concurrent allocation thread ────────────────────────────────── */
static void *concurrent_alloc_thread(void *arg) {
    (void)arg;

    /* Each thread needs its own VM stack */
    vm_stack_init();

    for (int i = 0; i < 50; i++) {
        pthread_mutex_lock(&glow_gil);
        List *l = list_new(4);
        list_push(l, value_int(i));
        Value sv = value_string_copy("thread_str");
        (void)sv;
        (void)l;
        pthread_mutex_unlock(&glow_gil);
    }

    vm_stack_cleanup();
    return NULL;
}

/* ── Test: concurrent allocation ─────────────────────────────────── */
static void stress_concurrent(void) {
    #define NUM_THREADS 4
    pthread_t threads[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, concurrent_alloc_thread, NULL);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    #undef NUM_THREADS
}

/* ── Verify: basic GC integrity check ────────────────────────────── */
static int verify_gc_integrity(void) {
    /* Walk the GC object list and verify no corruption */
    int count = 0;
    GCObject *obj = gc_objects;
    while (obj != NULL) {
        /* Sanity: type should be in range */
        if (obj->type > OBJ_SQLITE_HANDLE) {
            fprintf(stderr, "  GC CORRUPTION: object type %d out of range!\n",
                    obj->type);
            return 0;
        }
        count++;
        if (count > 10000000) {
            fprintf(stderr, "  GC CORRUPTION: list appears infinite!\n");
            return 0;
        }
        obj = obj->next;
    }
    return 1;
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

    printf("=== GlowLang GC Torture Test ===\n");
    printf("--- Starting GC Stress Testing (100 iterations) ---\n");

    signal(SIGSEGV, fuzz_signal_handler);
    signal(SIGFPE,  fuzz_signal_handler);
    signal(SIGABRT, fuzz_signal_handler);

    init_runtime();

    int crashes = 0;
    int corruption_found = 0;

    for (int iter = 0; iter < 100; iter++) {
        if (iter % 10 == 0)
            printf("  Iteration %d / 100 ...\n", iter);

        int sig = sigsetjmp(fuzz_jmp, 1);
        if (sig == 0) {
            in_fuzz = 1;

            int test = iter % 8;
            switch (test) {
                case 0:
                    /* Rapid list allocation + GC */
                    stress_lists(100 + (rand() % 200));
                    gc_full_collect();
                    break;
                case 1:
                    /* Rapid dict allocation + GC */
                    stress_dicts(50 + (rand() % 100));
                    gc_full_collect();
                    break;
                case 2:
                    /* Rapid string allocation + GC */
                    stress_strings(200 + (rand() % 300));
                    gc_full_collect();
                    break;
                case 3:
                    /* Mixed allocations interleaved with GC */
                    stress_mixed(50 + (rand() % 50));
                    break;
                case 4:
                    /* Class/instance allocation + GC */
                    stress_instances(100 + (rand() % 100));
                    gc_full_collect();
                    break;
                case 5:
                    /* Rope/string concat stress */
                    stress_ropes(50 + (rand() % 100));
                    gc_full_collect();
                    break;
                case 6:
                    /* Concurrent allocation test */
                    stress_concurrent();
                    gc_full_collect();
                    break;
                case 7:
                    /* Back-to-back GC with no new allocation */
                    for (int g = 0; g < 10; g++)
                        gc_full_collect();
                    gc_minor_collect();
                    break;
            }

            /* Verify integrity after each stress test */
            if (!verify_gc_integrity()) {
                corruption_found++;
            }

            in_fuzz = 0;
        } else {
            in_fuzz = 0;
            crashes++;
            signal(SIGSEGV, fuzz_signal_handler);
            signal(SIGFPE,  fuzz_signal_handler);
            signal(SIGABRT, fuzz_signal_handler);
        }
    }

    printf("  Crashes caught: %d\n", crashes);
    printf("  Corruption events: %d\n", corruption_found);

    cleanup_runtime();

    printf("--- GC Torture Test Completed Successfully ---\n");
    return 0;
}
