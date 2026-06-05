/*
 * GlowLang Inspector Tool
 * Visualizes VM stack, heap, GC stats, and runtime state from trace output.
 *
 * Usage:
 *   glowlang --trace program.glow 2>trace.log
 *   glow_inspector trace.log
 *   glow_inspector --gc trace.log
 *   glow_inspector --stack trace.log
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STACK 1024
#define MAX_FRAMES 256

typedef struct {
    long total_allocated;
    long total_freed;
    long live_objects;
    long gc_cycles;
    long gen0_collections;
    long gen1_collections;
    long peak_heap;
} GCStats;

typedef struct {
    char func_name[256];
    int line;
    int depth;
} StackFrame;

typedef struct {
    long string_count;
    long list_count;
    long dict_count;
    long closure_count;
    long instance_count;
    long tensor_count;
} ObjectCounts;

static GCStats gc = {0};
static StackFrame stack[MAX_FRAMES];
static int stack_depth = 0;
static ObjectCounts objects = {0};

static void parse_trace(FILE *f) {
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n\r")] = '\0';

        if (strstr(line, "[GC] collect")) {
            gc.gc_cycles++;
            char *freed = strstr(line, "freed=");
            if (freed) gc.total_freed += atol(freed + 6);
        }
        else if (strstr(line, "[GC] alloc")) {
            char *bytes = strstr(line, "bytes=");
            if (bytes) {
                long b = atol(bytes + 6);
                gc.total_allocated += b;
                long live = gc.total_allocated - gc.total_freed;
                if (live > gc.peak_heap) gc.peak_heap = live;
            }
        }
        else if (strstr(line, "[TRACE] CALL")) {
            if (stack_depth < MAX_FRAMES) {
                char *name = strstr(line, "CALL ") + 5;
                char *paren = strchr(name, '(');
                if (paren) {
                    int len = (int)(paren - name);
                    while (len > 0 && name[len-1] == ' ') len--;
                    if (len > 0 && len < 255) {
                        strncpy(stack[stack_depth].func_name, name, len);
                        stack[stack_depth].func_name[len] = '\0';
                        stack[stack_depth].depth = stack_depth;
                        char *ln = strstr(line, "line ");
                        if (ln) stack[stack_depth].line = atoi(ln + 5);
                        stack_depth++;
                    }
                }
            }
        }
        else if (strstr(line, "[TRACE] RETURN")) {
            if (stack_depth > 0) stack_depth--;
        }
        else if (strstr(line, "OBJ_STRING")) objects.string_count++;
        else if (strstr(line, "OBJ_LIST")) objects.list_count++;
        else if (strstr(line, "OBJ_DICT")) objects.dict_count++;
        else if (strstr(line, "OBJ_CLOSURE")) objects.closure_count++;
        else if (strstr(line, "OBJ_INSTANCE")) objects.instance_count++;
        else if (strstr(line, "OBJ_PACKED_ARRAY")) objects.tensor_count++;
    }
}

static void print_gc_stats(void) {
    printf("GC Statistics\n");
    printf("=============\n\n");
    printf("  Total allocated:    %ld bytes\n", gc.total_allocated);
    printf("  Total freed:        %ld bytes\n", gc.total_freed);
    printf("  Live memory:        %ld bytes\n", gc.total_allocated - gc.total_freed);
    printf("  Peak heap:          %ld bytes\n", gc.peak_heap);
    printf("  GC cycles:          %ld\n", gc.gc_cycles);
    printf("  Gen0 collections:   %ld\n", gc.gen0_collections);
    printf("  Gen1 collections:   %ld\n", gc.gen1_collections);
}

static void print_object_counts(void) {
    printf("\nObject Counts\n");
    printf("=============\n\n");
    printf("  Strings:    %ld\n", objects.string_count);
    printf("  Lists:      %ld\n", objects.list_count);
    printf("  Dicts:      %ld\n", objects.dict_count);
    printf("  Closures:   %ld\n", objects.closure_count);
    printf("  Instances:  %ld\n", objects.instance_count);
    printf("  Tensors:    %ld\n", objects.tensor_count);
}

static void print_stack(void) {
    printf("\nCall Stack (last %d frames)\n", stack_depth);
    printf("===========================\n\n");
    for (int i = stack_depth - 1; i >= 0; i--) {
        printf("  #%d  %s", stack_depth - 1 - i, stack[i].func_name);
        if (stack[i].line > 0) printf(" (line %d)", stack[i].line);
        printf("\n");
    }
    if (stack_depth == 0) printf("  (empty)\n");
}

static void print_full_report(void) {
    printf("GlowLang Runtime Inspector Report\n");
    printf("==================================\n\n");
    print_gc_stats();
    print_object_counts();
    print_stack();
}

int main(int argc, char **argv) {
    int gc_only = 0, stack_only = 0;
    const char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gc") == 0) gc_only = 1;
        else if (strcmp(argv[i], "--stack") == 0) stack_only = 1;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: glow_inspector [--gc|--stack] <trace.log>\n");
            return 0;
        }
        else filename = argv[i];
    }

    FILE *f = filename ? fopen(filename, "r") : stdin;
    if (!f) {
        fprintf(stderr, "Error: Could not open '%s'\n", filename);
        return 1;
    }

    parse_trace(f);
    if (f != stdin) fclose(f);

    if (gc_only) print_gc_stats();
    else if (stack_only) print_stack();
    else print_full_report();

    return 0;
}
