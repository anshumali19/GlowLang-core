/*
 * GlowLang Profiler Report Tool
 * Reads trace output from `glowlang --trace` and generates
 * flame graph data, hotspot analysis, and HTML reports.
 *
 * Usage:
 *   glowlang --trace program.glow 2>trace.log
 *   glow_profiler trace.log
 *   glow_profiler --html trace.log > report.html
 *   glow_profiler --flamegraph trace.log > flamegraph.folded
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_ENTRIES 100000
#define MAX_FUNCS   4096
#define MAX_NAME    256

typedef struct {
    char name[MAX_NAME];
    long total_time_us;
    long self_time_us;
    long call_count;
    long alloc_bytes;
} FuncProfile;

typedef struct {
    char stack[1024];
    long duration_us;
} TraceEntry;

static FuncProfile funcs[MAX_FUNCS];
static int func_count = 0;

static FuncProfile *find_or_create(const char *name) {
    for (int i = 0; i < func_count; i++) {
        if (strcmp(funcs[i].name, name) == 0) return &funcs[i];
    }
    if (func_count >= MAX_FUNCS) return &funcs[0];
    FuncProfile *f = &funcs[func_count++];
    memset(f, 0, sizeof(*f));
    strncpy(f->name, name, MAX_NAME - 1);
    return f;
}

static int cmp_total(const void *a, const void *b) {
    const FuncProfile *fa = (const FuncProfile *)a;
    const FuncProfile *fb = (const FuncProfile *)b;
    return (fb->total_time_us > fa->total_time_us) - (fb->total_time_us < fa->total_time_us);
}

static void parse_trace(FILE *f) {
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n\r")] = '\0';

        /* Trace format: [TRACE] CALL func_name (line N) */
        if (strstr(line, "[TRACE] CALL ")) {
            char *name_start = strstr(line, "CALL ") + 5;
            char *paren = strchr(name_start, '(');
            char name[MAX_NAME];
            if (paren) {
                int len = (int)(paren - name_start);
                while (len > 0 && name_start[len-1] == ' ') len--;
                if (len > 0 && len < MAX_NAME) {
                    strncpy(name, name_start, len);
                    name[len] = '\0';
                    FuncProfile *fp = find_or_create(name);
                    fp->call_count++;
                    fp->total_time_us += 10; /* Estimate */
                }
            }
        }
        /* Trace format: [TRACE] ALLOC bytes=N */
        else if (strstr(line, "[TRACE] ALLOC")) {
            char *eq = strstr(line, "bytes=");
            if (eq) {
                long bytes = atol(eq + 6);
                if (func_count > 0) funcs[func_count - 1].alloc_bytes += bytes;
            }
        }
        /* Trace format: [TRACE] EXEC op=NAME count=N */
        else if (strstr(line, "[TRACE] EXEC")) {
            /* Opcode execution stats */
            char *op_start = strstr(line, "op=");
            if (op_start) {
                char name[MAX_NAME];
                sscanf(op_start + 3, "%255s", name);
                FuncProfile *fp = find_or_create(name);
                fp->call_count++;
                fp->self_time_us += 1;
            }
        }
    }
}

static void print_report(void) {
    qsort(funcs, func_count, sizeof(FuncProfile), cmp_total);

    printf("GlowLang Profiler Report\n");
    printf("========================\n\n");
    printf("%-30s %10s %10s %10s %10s\n", "Function", "Calls", "Total(us)", "Self(us)", "Alloc(B)");
    printf("%-30s %10s %10s %10s %10s\n", "--------", "-----", "---------", "--------", "--------");

    for (int i = 0; i < func_count && i < 50; i++) {
        printf("%-30s %10ld %10ld %10ld %10ld\n",
            funcs[i].name, funcs[i].call_count,
            funcs[i].total_time_us, funcs[i].self_time_us,
            funcs[i].alloc_bytes);
    }

    printf("\nTotal functions profiled: %d\n", func_count);
}

static void print_flamegraph(void) {
    /* Output folded stacks for flamegraph.pl */
    for (int i = 0; i < func_count; i++) {
        if (funcs[i].call_count > 0) {
            printf("main;%s %ld\n", funcs[i].name, funcs[i].total_time_us);
        }
    }
}

static void print_html(void) {
    qsort(funcs, func_count, sizeof(FuncProfile), cmp_total);

    printf("<!DOCTYPE html>\n<html><head><title>GlowLang Profile</title>\n");
    printf("<style>\n");
    printf("body{font-family:system-ui;background:#0d1117;color:#e6edf3;padding:2rem}\n");
    printf("h1{color:#58a6ff}table{width:100%%;border-collapse:collapse;margin-top:1rem}\n");
    printf("th,td{padding:8px 12px;text-align:left;border-bottom:1px solid #30363d}\n");
    printf("th{background:#161b22;color:#8b949e;font-size:.85rem;text-transform:uppercase}\n");
    printf("tr:hover{background:#161b22}.bar{height:8px;background:#238636;border-radius:4px}\n");
    printf("</style></head><body>\n");
    printf("<h1>GlowLang Profiler Report</h1>\n");
    printf("<table><tr><th>Function</th><th>Calls</th><th>Total(μs)</th><th>Self(μs)</th><th>Alloc</th><th>Hotness</th></tr>\n");

    long max_time = func_count > 0 ? funcs[0].total_time_us : 1;
    if (max_time == 0) max_time = 1;

    for (int i = 0; i < func_count && i < 100; i++) {
        int pct = (int)(funcs[i].total_time_us * 100 / max_time);
        printf("<tr><td>%s</td><td>%ld</td><td>%ld</td><td>%ld</td><td>%ldB</td>",
            funcs[i].name, funcs[i].call_count,
            funcs[i].total_time_us, funcs[i].self_time_us,
            funcs[i].alloc_bytes);
        printf("<td><div class='bar' style='width:%d%%'></div></td></tr>\n", pct);
    }
    printf("</table></body></html>\n");
}

int main(int argc, char **argv) {
    int html_mode = 0, flamegraph_mode = 0;
    const char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--html") == 0) html_mode = 1;
        else if (strcmp(argv[i], "--flamegraph") == 0) flamegraph_mode = 1;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: glow_profiler [--html|--flamegraph] <trace.log>\n");
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

    if (func_count == 0) {
        fprintf(stderr, "No trace data found. Run with: glowlang --trace program.glow 2>trace.log\n");
        return 1;
    }

    if (html_mode) print_html();
    else if (flamegraph_mode) print_flamegraph();
    else print_report();

    return 0;
}
