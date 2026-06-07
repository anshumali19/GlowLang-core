/*
 * GlowLang Standalone Debugger
 * ============================
 * A standalone debugging tool that parses GlowLang trace output.
 * Does NOT link against the GlowLang runtime.
 *
 * Build:  gcc -o glow_debugger glow_debugger.c
 * Usage:  ./glow_debugger --trace trace.log [--interactive]
 *         ./glow_debugger --attach <pid>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#ifdef _WIN32
#include <io.h>
#define access _access
#else
#include <unistd.h>
#endif

/* ================================================================
 *  ANSI Color Codes
 * ================================================================ */
#define CLR_RESET   "\033[0m"
#define CLR_RED     "\033[31m"
#define CLR_GREEN   "\033[32m"
#define CLR_YELLOW  "\033[33m"
#define CLR_BLUE    "\033[34m"
#define CLR_MAGENTA "\033[35m"
#define CLR_CYAN    "\033[36m"
#define CLR_BOLD    "\033[1m"
#define CLR_DIM     "\033[2m"

/* ================================================================
 *  Data Structures
 * ================================================================ */
#define MAX_TRACE_LINES  500000
#define MAX_LINE_LEN     4096
#define MAX_BREAKPOINTS  256
#define MAX_WATCHPOINTS  64
#define MAX_SYMBOLS      4096
#define MAX_CALL_STACK   1024

typedef struct {
    int  pc;
    char opcode[64];
    int  r0, r1, r2, arg;
    char stack_state[MAX_LINE_LEN];
    int  source_line;      /* extracted from trace context */
    char raw_line[MAX_LINE_LEN];
} TraceEntry;

typedef struct {
    int  is_line;          /* 1 = line breakpoint, 0 = function name */
    int  line;
    char func_name[128];
    int  enabled;
} Breakpoint;

typedef struct {
    char var_name[128];
    char last_value[512];
    int  enabled;
} Watchpoint;

typedef struct {
    char name[128];
    int  first_seen_pc;
    int  call_count;
} Symbol;

typedef struct {
    char func_name[128];
    int  entry_pc;
    int  depth;
} CallFrame;

#ifdef _WIN32
typedef int pid_t;
#endif

static TraceEntry  *trace = NULL;
static int         trace_count = 0;
static Breakpoint  breakpoints[MAX_BREAKPOINTS];
static int         bp_count = 0;
static Watchpoint  watchpoints[MAX_WATCHPOINTS];
static int         wp_count = 0;
static Symbol      symbols[MAX_SYMBOLS];
static int         sym_count = 0;
static CallFrame   call_stack[MAX_CALL_STACK];
static int         call_depth = 0;
static int         current_pc = 0;

/* ================================================================
 *  Trace Parsing
 * ================================================================ */

/* Parse a single DEBUG line:
 *   DEBUG NNNN: OPCODE_NAME  r0:V r1:V r2:V arg:V
 */
static int parse_debug_line(const char *line, TraceEntry *e) {
    memset(e, 0, sizeof(*e));
    strncpy(e->raw_line, line, MAX_LINE_LEN - 1);

    if (strncmp(line, "DEBUG ", 6) != 0) return 0;

    const char *p = line + 6;
    /* Parse PC */
    e->pc = (int)strtol(p, NULL, 10);

    /* Find colon after PC */
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p == ' ') p++;

    /* Parse opcode */
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < 63) {
        e->opcode[i++] = *p++;
    }
    e->opcode[i] = '\0';

    /* Parse registers: r0:V r1:V r2:V arg:V */
    const char *r;
    r = strstr(p, "r0:");
    if (r) e->r0 = (int)strtol(r + 3, NULL, 10);
    r = strstr(p, "r1:");
    if (r) e->r1 = (int)strtol(r + 3, NULL, 10);
    r = strstr(p, "r2:");
    if (r) e->r2 = (int)strtol(r + 3, NULL, 10);
    r = strstr(p, "arg:");
    if (r) e->arg = (int)strtol(r + 4, NULL, 10);

    return 1;
}

/* Load and parse a trace log file */
static int load_trace(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, CLR_RED "Error: Cannot open trace file '%s'\n" CLR_RESET, filename);
        return -1;
    }

    char line[MAX_LINE_LEN];
    TraceEntry *current = NULL;

    while (fgets(line, sizeof(line), f) && trace_count < MAX_TRACE_LINES) {
        /* Strip newline */
        int len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len-1] == '\r') line[--len] = '\0';

        if (strncmp(line, "DEBUG ", 6) == 0 && strchr(line + 6, ':')) {
            /* Check if it's a DEBUG instruction line (has PC number after DEBUG) */
            const char *after_debug = line + 6;
            if (isdigit((unsigned char)*after_debug)) {
                current = &trace[trace_count];
                if (parse_debug_line(line, current)) {
                    /* Extract symbols from certain opcodes */
                    if (strcmp(current->opcode, "CALL_VALUE") == 0 ||
                        strcmp(current->opcode, "FUNC_DEF") == 0 ||
                        strcmp(current->opcode, "METHOD_DEF") == 0 ||
                        strcmp(current->opcode, "CLASS_DEF") == 0) {
                        /* We'll infer function names from context */
                    }
                    trace_count++;
                }
            }
        } else if (strncmp(line, "   Stack: ", 10) == 0 && current) {
            /* Stack state line - attach to previous entry */
            strncpy(current->stack_state, line + 10, MAX_LINE_LEN - 1);
        }
    }

    fclose(f);

    /* Build symbol table from CALL_VALUE patterns */
    for (int i = 0; i < trace_count; i++) {
        if (strcmp(trace[i].opcode, "CALL_VALUE") == 0) {
            /* Look for preceding LOAD_VAR or CONST_STRING for function name */
            if (i > 0 && strcmp(trace[i-1].opcode, "LOAD_VAR") == 0) {
                char sym_name[128];
                snprintf(sym_name, sizeof(sym_name), "func_%d", trace[i-1].arg);
                int found = 0;
                for (int s = 0; s < sym_count; s++) {
                    if (strcmp(symbols[s].name, sym_name) == 0) {
                        symbols[s].call_count++;
                        found = 1;
                        break;
                    }
                }
                if (!found && sym_count < MAX_SYMBOLS) {
                    strncpy(symbols[sym_count].name, sym_name, 127);
                    symbols[sym_count].first_seen_pc = trace[i].pc;
                    symbols[sym_count].call_count = 1;
                    sym_count++;
                }
            }
        }
    }

    printf(CLR_GREEN "Loaded %d trace entries, %d symbols\n" CLR_RESET, trace_count, sym_count);
    return 0;
}

/* ================================================================
 *  Debugger Commands
 * ================================================================ */

static void print_banner(void) {
    printf(CLR_BOLD CLR_CYAN
        "\n"
        "  ╔═══════════════════════════════════════╗\n"
        "  ║     GlowLang Debugger v1.0            ║\n"
        "  ║     Type 'help' or '?' for commands   ║\n"
        "  ╚═══════════════════════════════════════╝\n"
        CLR_RESET "\n");
}

static void cmd_help(void) {
    printf(CLR_BOLD "\n  GlowLang Debugger Commands:\n" CLR_RESET);
    printf("  %-20s %s\n", CLR_YELLOW "break <line>" CLR_RESET,    "Set breakpoint at line number");
    printf("  %-20s %s\n", CLR_YELLOW "break <name>" CLR_RESET,    "Set breakpoint at function name");
    printf("  %-20s %s\n", CLR_YELLOW "continue (c)" CLR_RESET,    "Continue to next breakpoint");
    printf("  %-20s %s\n", CLR_YELLOW "step (s)" CLR_RESET,        "Step one instruction forward");
    printf("  %-20s %s\n", CLR_YELLOW "print <expr> (p)" CLR_RESET,"Print register or stack value");
    printf("  %-20s %s\n", CLR_YELLOW "backtrace (bt)" CLR_RESET,  "Show call stack");
    printf("  %-20s %s\n", CLR_YELLOW "locals (l)" CLR_RESET,      "Show local variables / stack state");
    printf("  %-20s %s\n", CLR_YELLOW "heap (h)" CLR_RESET,        "Show heap / GC statistics");
    printf("  %-20s %s\n", CLR_YELLOW "watch <var> (w)" CLR_RESET, "Watch a variable for changes");
    printf("  %-20s %s\n", CLR_YELLOW "info (i)" CLR_RESET,        "Show breakpoints and watchpoints");
    printf("  %-20s %s\n", CLR_YELLOW "list" CLR_RESET,            "Show current trace context");
    printf("  %-20s %s\n", CLR_YELLOW "symbols" CLR_RESET,         "Show symbol table");
    printf("  %-20s %s\n", CLR_YELLOW "help (?)" CLR_RESET,        "Show this help");
    printf("  %-20s %s\n", CLR_YELLOW "quit (q)" CLR_RESET,        "Exit debugger");
    printf("\n");
}

static void cmd_break(const char *arg) {
    if (!arg || !*arg) {
        printf(CLR_RED "Usage: break <line_number> or break <func_name>\n" CLR_RESET);
        return;
    }
    if (bp_count >= MAX_BREAKPOINTS) {
        printf(CLR_RED "Maximum breakpoints reached (%d)\n" CLR_RESET, MAX_BREAKPOINTS);
        return;
    }

    Breakpoint *bp = &breakpoints[bp_count];
    bp->enabled = 1;

    if (isdigit((unsigned char)arg[0])) {
        bp->is_line = 1;
        bp->line = atoi(arg);
        printf(CLR_GREEN "Breakpoint %d set at line %d\n" CLR_RESET, bp_count, bp->line);
    } else {
        bp->is_line = 0;
        strncpy(bp->func_name, arg, 127);
        printf(CLR_GREEN "Breakpoint %d set at function '%s'\n" CLR_RESET, bp_count, bp->func_name);
    }
    bp_count++;
}

static int check_breakpoint(int idx) {
    TraceEntry *e = &trace[idx];
    for (int i = 0; i < bp_count; i++) {
        if (!breakpoints[i].enabled) continue;
        if (breakpoints[i].is_line && e->pc == breakpoints[i].line) return i;
        if (!breakpoints[i].is_line) {
            /* Check if opcode matches function name pattern */
            if (strstr(e->opcode, "CALL") && e->arg == breakpoints[i].line) return i;
            /* Also check by name in symbol table */
            for (int s = 0; s < sym_count; s++) {
                if (strcmp(symbols[s].name, breakpoints[i].func_name) == 0 &&
                    e->pc == symbols[s].first_seen_pc) return i;
            }
        }
    }
    return -1;
}

static void show_trace_entry(int idx) {
    if (idx < 0 || idx >= trace_count) {
        printf(CLR_DIM "(no trace entry at index %d)\n" CLR_RESET, idx);
        return;
    }
    TraceEntry *e = &trace[idx];
    printf(CLR_BOLD CLR_CYAN "  [%04d] " CLR_RESET, idx);
    printf(CLR_YELLOW "%-16s" CLR_RESET, e->opcode);
    printf(" r0:" CLR_GREEN "%d" CLR_RESET, e->r0);
    printf(" r1:" CLR_GREEN "%d" CLR_RESET, e->r1);
    printf(" r2:" CLR_GREEN "%d" CLR_RESET, e->r2);
    printf(" arg:" CLR_GREEN "%d" CLR_RESET, e->arg);
    printf("  (pc=%d)\n", e->pc);
    if (e->stack_state[0]) {
        printf(CLR_DIM "         Stack: %s\n" CLR_RESET, e->stack_state);
    }
}

static void cmd_step(void) {
    if (current_pc >= trace_count) {
        printf(CLR_RED "End of trace reached.\n" CLR_RESET);
        return;
    }

    /* Update call stack */
    TraceEntry *e = &trace[current_pc];
    if (strcmp(e->opcode, "CALL_VALUE") == 0 && call_depth < MAX_CALL_STACK) {
        snprintf(call_stack[call_depth].func_name, 128, "func_%d", e->r1);
        call_stack[call_depth].entry_pc = current_pc;
        call_stack[call_depth].depth = call_depth;
        call_depth++;
    } else if (strcmp(e->opcode, "RETURN") == 0 && call_depth > 0) {
        call_depth--;
    }

    show_trace_entry(current_pc);
    current_pc++;
}

static void cmd_continue(void) {
    if (current_pc >= trace_count) {
        printf(CLR_RED "End of trace reached.\n" CLR_RESET);
        return;
    }

    current_pc++; /* skip current to avoid re-hitting same bp */
    while (current_pc < trace_count) {
        /* Update call stack */
        TraceEntry *e = &trace[current_pc];
        if (strcmp(e->opcode, "CALL_VALUE") == 0 && call_depth < MAX_CALL_STACK) {
            snprintf(call_stack[call_depth].func_name, 128, "func_%d", e->r1);
            call_stack[call_depth].entry_pc = current_pc;
            call_stack[call_depth].depth = call_depth;
            call_depth++;
        } else if (strcmp(e->opcode, "RETURN") == 0 && call_depth > 0) {
            call_depth--;
        }

        int bp = check_breakpoint(current_pc);
        if (bp >= 0) {
            printf(CLR_RED CLR_BOLD "Breakpoint %d hit" CLR_RESET " at trace index %d\n", bp, current_pc);
            show_trace_entry(current_pc);
            return;
        }
        current_pc++;
    }
    printf(CLR_YELLOW "Reached end of trace without hitting breakpoint.\n" CLR_RESET);
}

static void cmd_backtrace(void) {
    printf(CLR_BOLD "\n  Call Stack (%d frames):\n" CLR_RESET, call_depth);
    if (call_depth == 0) {
        printf("  " CLR_DIM "(empty - at top level)\n" CLR_RESET);
    }
    for (int i = call_depth - 1; i >= 0; i--) {
        printf("  " CLR_CYAN "#%d" CLR_RESET "  %s%s%s",
            call_depth - 1 - i,
            CLR_YELLOW, call_stack[i].func_name, CLR_RESET);
        printf("  (entered at trace index %d)\n", call_stack[i].entry_pc);
    }
    /* Show current position */
    printf("  " CLR_CYAN "#%d" CLR_RESET "  " CLR_YELLOW "<current>" CLR_RESET
           "  (trace index %d)\n", call_depth, current_pc);
    printf("\n");
}

static void cmd_locals(void) {
    if (current_pc <= 0 || current_pc > trace_count) {
        printf(CLR_DIM "(no local state available)\n" CLR_RESET);
        return;
    }
    TraceEntry *e = &trace[current_pc > 0 ? current_pc - 1 : 0];
    printf(CLR_BOLD "\n  Local State at trace index %d:\n" CLR_RESET, current_pc - 1);
    printf("  Opcode:  " CLR_YELLOW "%s\n" CLR_RESET, e->opcode);
    printf("  PC:      %d\n", e->pc);
    printf("  r0: " CLR_GREEN "%d" CLR_RESET
           "  r1: " CLR_GREEN "%d" CLR_RESET
           "  r2: " CLR_GREEN "%d" CLR_RESET
           "  arg: " CLR_GREEN "%d\n" CLR_RESET,
           e->r0, e->r1, e->r2, e->arg);
    if (e->stack_state[0]) {
        printf("  Stack:   %s\n", e->stack_state);
    }
    printf("\n");
}

static void cmd_heap(void) {
    /* Estimate heap stats from trace - count object types */
    int str_count = 0, obj_count = 0, list_count = 0, dict_count = 0;
    int max_stack_depth = 0;

    for (int i = 0; i < trace_count; i++) {
        const char *s = trace[i].stack_state;
        if (!s[0]) continue;
        int depth = 0;
        for (const char *p = s; *p; p++) {
            if (*p == ',') depth++;
            if (*p == '"') str_count++;
            if (strncmp(p, "<obj>", 5) == 0) obj_count++;
        }
        if (depth > 0) depth++;  /* N commas = N+1 items */
        if (depth > max_stack_depth) max_stack_depth = depth;
    }

    printf(CLR_BOLD "\n  Heap / Memory Statistics (estimated from trace):\n" CLR_RESET);
    printf("  %-25s " CLR_GREEN "%d\n" CLR_RESET, "String literals seen:", str_count);
    printf("  %-25s " CLR_GREEN "%d\n" CLR_RESET, "Object references:", obj_count);
    printf("  %-25s " CLR_GREEN "%d\n" CLR_RESET, "Max stack depth:", max_stack_depth);
    printf("  %-25s " CLR_GREEN "%d\n" CLR_RESET, "Total trace entries:", trace_count);
    printf("\n");
}

static void cmd_watch(const char *arg) {
    if (!arg || !*arg) {
        printf(CLR_RED "Usage: watch <register_or_expr>\n" CLR_RESET);
        return;
    }
    if (wp_count >= MAX_WATCHPOINTS) {
        printf(CLR_RED "Maximum watchpoints reached (%d)\n" CLR_RESET, MAX_WATCHPOINTS);
        return;
    }
    Watchpoint *wp = &watchpoints[wp_count];
    strncpy(wp->var_name, arg, 127);
    wp->last_value[0] = '\0';
    wp->enabled = 1;
    printf(CLR_GREEN "Watchpoint %d set on '%s'\n" CLR_RESET, wp_count, arg);
    wp_count++;
}

static void cmd_print(const char *arg) {
    if (!arg || !*arg) {
        printf(CLR_RED "Usage: print <register> (r0, r1, r2, arg, pc, stack)\n" CLR_RESET);
        return;
    }
    if (current_pc <= 0 || current_pc > trace_count) {
        printf(CLR_DIM "(no state - step first)\n" CLR_RESET);
        return;
    }
    TraceEntry *e = &trace[current_pc > 0 ? current_pc - 1 : 0];
    if (strcmp(arg, "r0") == 0) printf("r0 = " CLR_GREEN "%d\n" CLR_RESET, e->r0);
    else if (strcmp(arg, "r1") == 0) printf("r1 = " CLR_GREEN "%d\n" CLR_RESET, e->r1);
    else if (strcmp(arg, "r2") == 0) printf("r2 = " CLR_GREEN "%d\n" CLR_RESET, e->r2);
    else if (strcmp(arg, "arg") == 0) printf("arg = " CLR_GREEN "%d\n" CLR_RESET, e->arg);
    else if (strcmp(arg, "pc") == 0) printf("pc = " CLR_GREEN "%d\n" CLR_RESET, e->pc);
    else if (strcmp(arg, "stack") == 0) {
        if (e->stack_state[0]) printf("stack = %s\n", e->stack_state);
        else printf(CLR_DIM "(no stack state)\n" CLR_RESET);
    }
    else if (strcmp(arg, "opcode") == 0) printf("opcode = " CLR_YELLOW "%s\n" CLR_RESET, e->opcode);
    else printf(CLR_DIM "Unknown: %s (try: r0, r1, r2, arg, pc, stack, opcode)\n" CLR_RESET, arg);
}

static void cmd_info(void) {
    printf(CLR_BOLD "\n  Breakpoints (%d):\n" CLR_RESET, bp_count);
    for (int i = 0; i < bp_count; i++) {
        printf("  %d: %s", i, breakpoints[i].enabled ? CLR_GREEN "enabled" CLR_RESET : CLR_RED "disabled" CLR_RESET);
        if (breakpoints[i].is_line) printf(" at line %d", breakpoints[i].line);
        else printf(" at func '%s'", breakpoints[i].func_name);
        printf("\n");
    }
    printf(CLR_BOLD "\n  Watchpoints (%d):\n" CLR_RESET, wp_count);
    for (int i = 0; i < wp_count; i++) {
        printf("  %d: '%s' %s\n", i, watchpoints[i].var_name,
            watchpoints[i].enabled ? CLR_GREEN "enabled" CLR_RESET : CLR_RED "disabled" CLR_RESET);
    }
    printf("\n");
}

static void cmd_list(void) {
    int start = current_pc > 5 ? current_pc - 5 : 0;
    int end = current_pc + 5;
    if (end > trace_count) end = trace_count;
    printf(CLR_BOLD "\n  Trace context (current = %d):\n" CLR_RESET, current_pc);
    for (int i = start; i < end; i++) {
        if (i == current_pc) printf(CLR_BOLD CLR_RED " >> " CLR_RESET);
        else printf("    ");
        show_trace_entry(i);
    }
    printf("\n");
}

static void cmd_symbols(void) {
    printf(CLR_BOLD "\n  Symbol Table (%d entries):\n" CLR_RESET, sym_count);
    printf("  %-20s %-10s %s\n", "Name", "First PC", "Calls");
    printf("  %-20s %-10s %s\n", "----", "--------", "-----");
    for (int i = 0; i < sym_count; i++) {
        printf("  " CLR_YELLOW "%-20s" CLR_RESET " %-10d " CLR_GREEN "%d\n" CLR_RESET,
            symbols[i].name, symbols[i].first_seen_pc, symbols[i].call_count);
    }
    printf("\n");
}

/* ================================================================
 *  REPL
 * ================================================================ */

static void run_repl(void) {
    print_banner();
    char line[1024];

    while (1) {
        printf(CLR_BOLD CLR_BLUE "(glow-dbg) " CLR_RESET);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        /* Strip newline */
        int len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
        if (len == 0) continue;

        /* Parse command and argument */
        char cmd[64] = {0}, arg[256] = {0};
        char *space = strchr(line, ' ');
        if (space) {
            int clen = (int)(space - line);
            if (clen > 63) clen = 63;
            strncpy(cmd, line, clen);
            /* Skip spaces after command */
            const char *a = space + 1;
            while (*a == ' ') a++;
            strncpy(arg, a, 255);
        } else {
            strncpy(cmd, line, 63);
        }

        /* Dispatch commands */
        if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
            cmd_help();
        } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
            printf(CLR_YELLOW "Goodbye.\n" CLR_RESET);
            break;
        } else if (strcmp(cmd, "break") == 0 || strcmp(cmd, "b") == 0) {
            cmd_break(arg);
        } else if (strcmp(cmd, "continue") == 0 || strcmp(cmd, "c") == 0) {
            cmd_continue();
        } else if (strcmp(cmd, "step") == 0 || strcmp(cmd, "s") == 0) {
            cmd_step();
        } else if (strcmp(cmd, "print") == 0 || strcmp(cmd, "p") == 0) {
            cmd_print(arg);
        } else if (strcmp(cmd, "backtrace") == 0 || strcmp(cmd, "bt") == 0) {
            cmd_backtrace();
        } else if (strcmp(cmd, "locals") == 0 || strcmp(cmd, "l") == 0) {
            cmd_locals();
        } else if (strcmp(cmd, "heap") == 0 || strcmp(cmd, "h") == 0) {
            cmd_heap();
        } else if (strcmp(cmd, "watch") == 0 || strcmp(cmd, "w") == 0) {
            cmd_watch(arg);
        } else if (strcmp(cmd, "info") == 0 || strcmp(cmd, "i") == 0) {
            cmd_info();
        } else if (strcmp(cmd, "list") == 0) {
            cmd_list();
        } else if (strcmp(cmd, "symbols") == 0 || strcmp(cmd, "sym") == 0) {
            cmd_symbols();
        } else {
            printf(CLR_RED "Unknown command: '%s'. Type 'help' for commands.\n" CLR_RESET, cmd);
        }
    }
}

/* ================================================================
 *  Attach Mode (via signals)
 * ================================================================ */

static void cmd_attach(pid_t pid) {
    printf(CLR_CYAN "Sending SIGUSR1 to GlowLang process %d...\n" CLR_RESET, pid);
    if (kill(pid, SIGUSR1) != 0) {
        perror(CLR_RED "Failed to send signal" CLR_RESET);
        return;
    }
    printf(CLR_GREEN "Signal sent. Check process output for debug dump.\n" CLR_RESET);
    printf(CLR_DIM "Note: The GlowLang runtime must have a SIGUSR1 handler installed.\n" CLR_RESET);
    printf(CLR_DIM "Use --trace mode for full interactive debugging.\n" CLR_RESET);
}

/* ================================================================
 *  Usage & Main
 * ================================================================ */

static void print_usage(const char *prog) {
    printf(CLR_BOLD "GlowLang Debugger v1.0\n" CLR_RESET);
    printf("\nUsage:\n");
    printf("  %s --trace <file>    Load trace log for interactive debugging\n", prog);
    printf("  %s --input <file>    Same as --trace\n", prog);
    printf("  %s --attach <pid>    Send debug signal to running process\n", prog);
    printf("\nOptions:\n");
    printf("  --interactive        Start interactive REPL (default with --trace)\n");
    printf("  --help, -h           Show this help\n");
    printf("\nExamples:\n");
    printf("  %s --trace trace.log\n", prog);
    printf("  %s --attach 12345\n", prog);
    printf("\n");
}

int main(int argc, char *argv[]) {
    trace = (TraceEntry *)malloc(MAX_TRACE_LINES * sizeof(TraceEntry));
    if (!trace) {
        fprintf(stderr, "Error: out of memory for trace buffer\n");
        return 1;
    }
    const char *trace_file = NULL;
    pid_t attach_pid = 0;
    int interactive = 0;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--trace") == 0 || strcmp(argv[i], "--input") == 0) && i + 1 < argc) {
            trace_file = argv[++i];
            interactive = 1;
        } else if (strcmp(argv[i], "--attach") == 0 && i + 1 < argc) {
            attach_pid = (pid_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--interactive") == 0) {
            interactive = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, CLR_RED "Unknown option: %s\n" CLR_RESET, argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!trace_file && !attach_pid) {
        print_usage(argv[0]);
        return 1;
    }

    if (attach_pid) {
        cmd_attach(attach_pid);
        if (!trace_file) return 0;
    }

    if (trace_file) {
        if (load_trace(trace_file) != 0) return 1;
        if (interactive) {
            run_repl();
        } else {
            /* Default: show summary */
            printf(CLR_BOLD "\nTrace Summary:\n" CLR_RESET);
            printf("  Total entries: %d\n", trace_count);
            printf("  Symbols found: %d\n", sym_count);
            cmd_heap();
        }
    }

    return 0;
}
