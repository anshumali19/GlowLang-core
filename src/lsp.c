/*
 * GlowLang Language Server Protocol (LSP) Implementation
 * ======================================================
 * Full JSON-RPC 2.0 LSP server with:
 *   - initialize / shutdown / exit
 *   - textDocument/didOpen, didChange, didSave
 *   - textDocument/completion
 *   - textDocument/hover
 *   - textDocument/definition
 *   - textDocument/publishDiagnostics
 *   - textDocument/semanticTokens/full
 *
 * Uses only standard C library + glowlang.h APIs.
 * Runs over stdin/stdout when invoked via `glowlang --lsp`.
 */

#include "lsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <process.h>
#define access _access
#define read _read
#define write _write
#define close _close
#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED) && !defined(__MINGW32__) && !defined(__MINGW64__)
typedef int ssize_t;
#endif
#else
#include <unistd.h>
#endif
#include <ctype.h>

/* ================================================================
 *  EXTERN DECLARATIONS (from glowlang.h / parser)
 * ================================================================ */
extern void glow_clear_errors(void);
extern int  glow_error_count;
extern GlowError *glow_errors;
extern int  yyparse(void);
extern int  line_num;

/* ================================================================
 *  LOGGING  (all debug output goes to stderr, never stdout)
 * ================================================================ */
#define LSP_LOG(fmt, ...) fprintf(stderr, "[glowlang-lsp] " fmt "\n", ##__VA_ARGS__)

/* ================================================================
 *  SIMPLE JSON HELPERS  (no external library)
 * ================================================================ */

/* Find the value of a string key in a JSON object.
 * Returns pointer to first char after the colon (trimmed of whitespace/quote).
 * Writes the extracted string value into `out` (max `out_sz` bytes).
 * Returns 0 on success, -1 on not found. */
static int json_get_string(const char *json, const char *key, char *out, int out_sz) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    if (*p != '"') return -1;
    p++; /* skip opening quote */
    int i = 0;
    while (*p && *p != '"' && i < out_sz - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                case '"': out[i++] = '"'; break;
                case '\\': out[i++] = '\\'; break;
                case '/': out[i++] = '/'; break;
                default: out[i++] = *p; break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return 0;
}

/* Extract an integer value for a given key. Returns -1 if not found. */
static int json_get_int(const char *json, const char *key) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    if (!isdigit((unsigned char)*p) && *p != '-') return -1;
    return atoi(p);
}

/* Extract a nested JSON object by key.
 * Copies the substring { ... } into `out`. Returns 0 on success. */
static int json_get_object(const char *json, const char *key, char *out, int out_sz) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p && *p != '{') p++;
    if (*p != '{') return -1;
    int depth = 0;
    int i = 0;
    while (*p && i < out_sz - 1) {
        if (*p == '{') depth++;
        else if (*p == '}') depth--;
        out[i++] = *p;
        p++;
        if (depth == 0) break;
    }
    out[i] = '\0';
    return 0;
}

/* Get the full text content from a didOpen/didChange textDocument field.
 * Specifically handles the large "text" field which may contain escape sequences. */
static int json_get_text_content(const char *json, char *out, int out_sz) {
    /* Look for "text" key - it's usually the last big field */
    const char *marker = "\"text\"";
    const char *p = strstr(json, marker);
    if (!p) return -1;
    p += strlen(marker);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (*p != '"') return -1;
    p++; /* skip opening quote */
    int i = 0;
    while (*p && i < out_sz - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case 'n': out[i++] = '\n'; break;
                case 'r': out[i++] = '\r'; break;
                case 't': out[i++] = '\t'; break;
                case '"': out[i++] = '"'; break;
                case '\\': out[i++] = '\\'; break;
                case '/': out[i++] = '/'; break;
                case 'u':
                    /* basic unicode escape - just skip for now */
                    out[i++] = '?';
                    if (*(p+1)) p++;
                    if (*(p+1)) p++;
                    if (*(p+1)) p++;
                    if (*(p+1)) p++;
                    break;
                default: out[i++] = *p; break;
            }
        } else if (*p == '"') {
            break; /* unescaped quote = end of string */
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return 0;
}

/* Escape a string for JSON output. Writes into `out` (max out_sz). */
static void json_escape(const char *in, char *out, int out_sz) {
    int i = 0;
    while (*in && i < out_sz - 2) {
        switch (*in) {
            case '"':  if (i + 2 < out_sz) { out[i++] = '\\'; out[i++] = '"'; } break;
            case '\\': if (i + 2 < out_sz) { out[i++] = '\\'; out[i++] = '\\'; } break;
            case '\n': if (i + 2 < out_sz) { out[i++] = '\\'; out[i++] = 'n'; } break;
            case '\r': if (i + 2 < out_sz) { out[i++] = '\\'; out[i++] = 'r'; } break;
            case '\t': if (i + 2 < out_sz) { out[i++] = '\\'; out[i++] = 't'; } break;
            default:
                if ((unsigned char)*in < 0x20) {
                    /* skip control characters */
                } else {
                    out[i++] = *in;
                }
                break;
        }
        in++;
    }
    out[i] = '\0';
}

/* ================================================================
 *  DOCUMENT STORE
 * ================================================================ */
static LspDocStore doc_store = { .count = 0 };

static LspDocument *doc_find(const char *uri) {
    for (int i = 0; i < doc_store.count; i++) {
        if (strcmp(doc_store.docs[i].uri, uri) == 0) {
            return &doc_store.docs[i];
        }
    }
    return NULL;
}

static LspDocument *doc_open(const char *uri, const char *text) {
    LspDocument *doc = doc_find(uri);
    if (!doc) {
        if (doc_store.count >= LSP_MAX_DOCS) {
            LSP_LOG("Document store full, cannot open: %s", uri);
            return NULL;
        }
        doc = &doc_store.docs[doc_store.count++];
        strncpy(doc->uri, uri, LSP_MAX_URI - 1);
        doc->uri[LSP_MAX_URI - 1] = '\0';
        doc->text = NULL;
    }
    /* Update text */
    int len = (int)strlen(text);
    if (len >= LSP_MAX_DOC_SIZE) len = LSP_MAX_DOC_SIZE - 1;
    doc->text = (char *)realloc(doc->text, len + 1);
    memcpy(doc->text, text, len);
    doc->text[len] = '\0';
    doc->text_len = len;
    return doc;
}

/* ================================================================
 *  LSP SEND (JSON-RPC response over stdout)
 * ================================================================ */
static void lsp_send(const char *json) {
    int len = (int)strlen(json);
    fprintf(stdout, "Content-Length: %d\r\n\r\n%s", len, json);
    fflush(stdout);
}

/* Send a response for a given request id */
static void lsp_send_response(int id, const char *result_json) {
    char buf[1024 * 128];
    snprintf(buf, sizeof(buf),
        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":%s}", id, result_json);
    lsp_send(buf);
}

/* Send a notification (no id) */
static void lsp_send_notification(const char *method, const char *params_json) {
    char buf[1024 * 256];
    snprintf(buf, sizeof(buf),
        "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s}", method, params_json);
    lsp_send(buf);
}

/* ================================================================
 *  KEYWORD / BUILTIN TABLES
 * ================================================================ */
typedef struct {
    const char *name;
    const char *detail;
    const char *documentation;
    int kind; /* LSP CompletionItemKind: 14=Keyword, 3=Function, 9=Module */
} CompletionEntry;

static const CompletionEntry keywords[] = {
    {"show",       "keyword", "Print values to stdout",                                          14},
    {"write",      "keyword", "Print values to stdout without trailing newline",                 14},
    {"when",       "keyword", "Conditional statement (if)",                                      14},
    {"whenever",   "keyword", "Event-driven conditional (triggers when condition becomes true)", 14},
    {"eif",        "keyword", "Else-if branch",                                                  14},
    {"otherwise",  "keyword", "Else branch (default case)",                                      14},
    {"while",      "keyword", "Loop while condition is true",                                    14},
    {"until",      "keyword", "Loop until condition becomes true",                               14},
    {"repeat",     "keyword", "Repeat N times",                                                  14},
    {"for",        "keyword", "For loop",                                                        14},
    {"each",       "keyword", "Iterate over elements",                                           14},
    {"in",         "keyword", "Membership or iteration operator",                                14},
    {"func",       "keyword", "Define a named function",                                         14},
    {"lambda",     "keyword", "Create an anonymous function (closure)",                          14},
    {"return",     "keyword", "Return a value from a function",                                  14},
    {"stop",       "keyword", "Break out of a loop",                                             14},
    {"skip",       "keyword", "Skip to the next loop iteration (continue)",                      14},
    {"end",        "keyword", "End a block (if/while/for/func/class)",                           14},
    {"let",        "keyword", "Declare a block-scoped variable",                                 14},
    {"try",        "keyword", "Begin a try-catch error handling block",                          14},
    {"catch",      "keyword", "Handle exceptions from a try block",                              14},
    {"throw",      "keyword", "Throw an exception",                                              14},
    {"class",      "keyword", "Define a class",                                                  14},
    {"trait",      "keyword", "Define a trait (interface with default implementations)",          14},
    {"extends",    "keyword", "Inherit from a parent class",                                     14},
    {"use",        "keyword", "Import a module or apply a trait to a class",                     14},
    {"spawn",      "keyword", "Launch a concurrent task (lightweight thread)",                   14},
    {"true",       "keyword", "Boolean true literal",                                            14},
    {"false",      "keyword", "Boolean false literal",                                           14},
    {"times",      "keyword", "Repeat multiplier (e.g., repeat 5 times)",                        14},
    {"input",      "keyword", "Read user input from stdin",                                      14},
    {"is",         "keyword", "Equality comparison operator",                                    14},
    {"is_not",     "keyword", "Inequality comparison operator",                                  14},
    {"from",       "keyword", "Range start or import source",                                    14},
    {"to",         "keyword", "Range end",                                                       14},
    {"every",      "keyword", "Define class members and properties (natural syntax)",            14},
    {"blueprint",  "keyword", "Define a class (GlowLang natural syntax for class)",              14},
    {"action",     "keyword", "Define a method or action in a class",                            14},
    {"can",        "keyword", "Capability declaration in a class/trait",                          14},
    {NULL, NULL, NULL, 0}
};

static const CompletionEntry builtins[] = {
    {"len",        "function", "Return the length of a string, list, or dict",                    3},
    {"type",       "function", "Return the type name of a value as a string",                     3},
    {"range",      "function", "Generate a list of integers in a range",                          3},
    {"append",     "function", "Append an element to a list",                                     3},
    {"push",       "function", "Push an element to the end of a list",                            3},
    {"pop",        "function", "Remove and return the last element of a list",                    3},
    {"sort",       "function", "Sort a list in-place",                                            3},
    {"reverse",    "function", "Reverse a list in-place",                                         3},
    {"keys",       "function", "Return all keys of a dictionary",                                 3},
    {"values",     "function", "Return all values of a dictionary",                               3},
    {"has",        "function", "Check if a dict contains a key",                                  3},
    {"remove",     "function", "Remove an element by key or index",                               3},
    {"merge",      "function", "Merge two dictionaries",                                          3},
    {"upper",      "function", "Convert string to uppercase",                                     3},
    {"lower",      "function", "Convert string to lowercase",                                     3},
    {"trim",       "function", "Remove leading/trailing whitespace from a string",                3},
    {"find",       "function", "Find the index of a substring in a string",                       3},
    {"replace",    "function", "Replace occurrences of a substring",                              3},
    {"starts_with","function", "Check if a string starts with a prefix",                          3},
    {"ends_with",  "function", "Check if a string ends with a suffix",                            3},
    {"split",      "function", "Split a string by a delimiter into a list",                       3},
    {NULL, NULL, NULL, 0}
};

typedef struct {
    const char *prefix;     /* module name, e.g. "os" */
    const char *member;     /* member name, e.g. "time" */
    const char *label;      /* full label, e.g. "os.time" */
    const char *doc;        /* documentation */
} ModuleMember;

static const ModuleMember modules[] = {
    {"os",     "time",      "os.time",        "Get the current Unix timestamp"},
    {"os",     "exit",      "os.exit",        "Exit the program with a status code"},
    {"os",     "sleep",     "os.sleep",       "Sleep for a number of seconds"},
    {"os",     "system",    "os.system",      "Execute a system shell command"},
    {"math",   "abs",       "math.abs",       "Return the absolute value of a number"},
    {"math",   "min",       "math.min",       "Return the minimum of two values"},
    {"math",   "max",       "math.max",       "Return the maximum of two values"},
    {"math",   "round",     "math.round",     "Round a number to the nearest integer"},
    {"math",   "floor",     "math.floor",     "Round a number down to the nearest integer"},
    {"math",   "ceil",      "math.ceil",      "Round a number up to the nearest integer"},
    {"math",   "sqrt",      "math.sqrt",      "Return the square root of a number"},
    {"fs",     "read",      "fs.read",        "Read the contents of a file as a string"},
    {"fs",     "write",     "fs.write",       "Write a string to a file"},
    {"fs",     "exists",    "fs.exists",      "Check if a file or directory exists"},
    {"fs",     "size",      "fs.size",        "Get the size of a file in bytes"},
    {"fs",     "mkdir",     "fs.mkdir",       "Create a directory"},
    {"fs",     "remove",    "fs.remove",      "Remove a file or directory"},
    {"fs",     "list",      "fs.list",        "List files in a directory"},
    {"json",   "parse",     "json.parse",     "Parse a JSON string into a GlowLang value"},
    {"json",   "stringify", "json.stringify",  "Convert a GlowLang value to a JSON string"},
    {"http",   "get",       "http.get",       "Perform an HTTP GET request"},
    {"http",   "post",      "http.post",      "Perform an HTTP POST request"},
    {"tensor", "create",    "tensor.create",  "Create a tensor from a list of values"},
    {"tensor", "zeros",     "tensor.zeros",   "Create a tensor filled with zeros"},
    {"tensor", "ones",      "tensor.ones",    "Create a tensor filled with ones"},
    {"tensor", "dot",       "tensor.dot",     "Compute the dot product of two tensors"},
    {NULL, NULL, NULL, NULL}
};

/* Keyword names for fast lookup / semantic tokens */
static const char *keyword_names[] = {
    "show", "write", "when", "whenever", "eif", "otherwise",
    "while", "until", "repeat", "for", "each", "in",
    "func", "lambda", "return", "stop", "skip", "end",
    "let", "try", "catch", "throw", "class", "trait",
    "extends", "use", "spawn", "true", "false", "times",
    "input", "is", "is_not", "from", "to",
    "every", "blueprint", "action", "can",
    "and", "or", "not",
    NULL
};

static const char *builtin_names[] = {
    "len", "type", "range", "append", "push", "pop",
    "sort", "reverse", "keys", "values", "has", "remove",
    "merge", "upper", "lower", "trim", "find", "replace",
    "starts_with", "ends_with", "split",
    NULL
};

static int is_keyword(const char *word) {
    for (int i = 0; keyword_names[i]; i++)
        if (strcmp(word, keyword_names[i]) == 0) return 1;
    return 0;
}

static int is_builtin(const char *word) {
    for (int i = 0; builtin_names[i]; i++)
        if (strcmp(word, builtin_names[i]) == 0) return 1;
    return 0;
}

/* ================================================================
 *  HANDLER: initialize
 * ================================================================ */
static void handle_initialize(int id) {
    const char *result =
        "{"
            "\"capabilities\":{"
                "\"textDocumentSync\":1,"
                "\"completionProvider\":{\"triggerCharacters\":[\".\"],\"resolveProvider\":false},"
                "\"hoverProvider\":true,"
                "\"definitionProvider\":true,"
                "\"semanticTokensProvider\":{"
                    "\"legend\":{"
                        "\"tokenTypes\":[\"keyword\",\"function\",\"variable\",\"string\",\"number\",\"comment\",\"operator\",\"class\",\"type\"],"
                        "\"tokenModifiers\":[]"
                    "},"
                    "\"full\":true"
                "}"
            "},"
            "\"serverInfo\":{\"name\":\"glowlang-lsp\",\"version\":\"1.0.0\"}"
        "}";
    lsp_send_response(id, result);
    LSP_LOG("Initialized with full capabilities");
}

/* ================================================================
 *  HANDLER: shutdown
 * ================================================================ */
static void handle_shutdown(int id) {
    lsp_send_response(id, "null");
    LSP_LOG("Shutdown acknowledged");
}

/* ================================================================
 *  HANDLER: textDocument/didOpen & textDocument/didChange
 * ================================================================ */
static void publish_diagnostics(const char *uri, const char *text);

static void handle_did_open(const char *json) {
    char uri[LSP_MAX_URI];
    if (json_get_string(json, "uri", uri, sizeof(uri)) != 0) {
        LSP_LOG("didOpen: could not extract URI");
        return;
    }
    char *text = (char *)malloc(LSP_MAX_DOC_SIZE);
    if (!text) return;
    if (json_get_text_content(json, text, LSP_MAX_DOC_SIZE) != 0) {
        LSP_LOG("didOpen: could not extract text");
        free(text);
        return;
    }
    doc_open(uri, text);
    publish_diagnostics(uri, text);
    free(text);
    LSP_LOG("Opened document: %s", uri);
}

static void handle_did_change(const char *json) {
    char uri[LSP_MAX_URI];
    if (json_get_string(json, "uri", uri, sizeof(uri)) != 0) {
        LSP_LOG("didChange: could not extract URI");
        return;
    }
    /* For textDocumentSync=1 (full), the entire text is sent */
    char *text = (char *)malloc(LSP_MAX_DOC_SIZE);
    if (!text) return;

    /* The text is inside contentChanges[0].text */
    const char *changes = strstr(json, "\"contentChanges\"");
    if (changes) {
        if (json_get_text_content(changes, text, LSP_MAX_DOC_SIZE) != 0) {
            /* Fallback: try from top level */
            json_get_text_content(json, text, LSP_MAX_DOC_SIZE);
        }
    } else {
        json_get_text_content(json, text, LSP_MAX_DOC_SIZE);
    }

    doc_open(uri, text);
    publish_diagnostics(uri, text);
    free(text);
}

/* ================================================================
 *  HANDLER: textDocument/publishDiagnostics
 * ================================================================ */
static void publish_diagnostics(const char *uri, const char *text) {
    /* Run the GlowLang parser on the text to detect syntax errors */
    glow_clear_errors();
    line_num = 1;

    YY_BUFFER_STATE buf = yy_scan_string(text);
    yyparse();
    yy_delete_buffer(buf);

    /* Build the diagnostics array */
    /* Start with parser errors */
    char diag_buf[1024 * 64];
    int offset = 0;
    offset += snprintf(diag_buf + offset, sizeof(diag_buf) - offset, "[");

    int diag_count = 0;

    /* Parser errors */
    for (int i = 0; i < glow_error_count && i < 100; i++) {
        int line = glow_errors[i].line > 0 ? glow_errors[i].line - 1 : 0; /* LSP lines are 0-based */
        char escaped_msg[1024];
        json_escape(glow_errors[i].message ? glow_errors[i].message : "syntax error", escaped_msg, sizeof(escaped_msg));

        if (diag_count > 0) {
            offset += snprintf(diag_buf + offset, sizeof(diag_buf) - offset, ",");
        }
        offset += snprintf(diag_buf + offset, sizeof(diag_buf) - offset,
            "{\"range\":{\"start\":{\"line\":%d,\"character\":0},\"end\":{\"line\":%d,\"character\":1000}},"
            "\"severity\":1,\"source\":\"glowlang\",\"message\":\"%s\"}",
            line, line, escaped_msg);
        diag_count++;
    }

    /* 'require' is not GlowLang: point to 'use'. ('import' is valid.) */
    const char *scan = text;
    int scan_line = 0;
    while (*scan) {
        /* Track line number */
        if (*scan == '\n') { scan_line++; scan++; continue; }

        /* Check for 'require' at a word boundary */
        if (strncmp(scan, "require", 7) == 0 && (scan == text || !isalnum((unsigned char)*(scan-1))) && !isalnum((unsigned char)scan[7]) && scan[7] != '_') {
            if (diag_count > 0 && offset < (int)sizeof(diag_buf) - 256) {
                offset += snprintf(diag_buf + offset, sizeof(diag_buf) - offset, ",");
            }
            if (offset < (int)sizeof(diag_buf) - 256) {
                offset += snprintf(diag_buf + offset, sizeof(diag_buf) - offset,
                    "{\"range\":{\"start\":{\"line\":%d,\"character\":0},\"end\":{\"line\":%d,\"character\":7}},"
                    "\"severity\":2,\"source\":\"glowlang\",\"message\":\"Use 'use' instead of 'require' in GlowLang\"}",
                    scan_line, scan_line);
                diag_count++;
            }
        }
        scan++;
    }

    offset += snprintf(diag_buf + offset, sizeof(diag_buf) - offset, "]");

    /* Send notification */
    char params[1024 * 132];
    char escaped_uri[LSP_MAX_URI * 2];
    json_escape(uri, escaped_uri, sizeof(escaped_uri));
    snprintf(params, sizeof(params),
        "{\"uri\":\"%s\",\"diagnostics\":%s}", escaped_uri, diag_buf);
    lsp_send_notification("textDocument/publishDiagnostics", params);
}

/* ================================================================
 *  HANDLER: textDocument/completion
 * ================================================================ */
static void handle_completion(int id, const char *json) {
    /* Extract position to determine context */
    char uri[LSP_MAX_URI];
    json_get_string(json, "uri", uri, sizeof(uri));
    int line = json_get_int(json, "line");
    int character = json_get_int(json, "character");
    (void)line;

    /* Check if we're completing after a dot (module member) */
    LspDocument *doc = doc_find(uri);
    int after_dot = 0;
    char module_prefix[64] = {0};

    if (doc && doc->text && character > 0) {
        /* Find the current line */
        const char *p = doc->text;
        int cur_line = 0;
        while (*p && cur_line < line) {
            if (*p == '\n') cur_line++;
            p++;
        }
        /* Now p points to the start of the current line, find the char before cursor */
        if (character > 0 && p[character - 1] == '.') {
            after_dot = 1;
            /* Extract the word before the dot */
            int start = character - 2;
            while (start >= 0 && (isalnum((unsigned char)p[start]) || p[start] == '_')) start--;
            start++;
            int len = character - 1 - start;
            if (len > 0 && len < 63) {
                strncpy(module_prefix, p + start, len);
                module_prefix[len] = '\0';
            }
        }
    }

    char result[1024 * 64];
    int off = 0;
    off += snprintf(result + off, sizeof(result) - off, "{\"isIncomplete\":false,\"items\":[");

    int first = 1;

    if (after_dot && module_prefix[0]) {
        /* Complete module members */
        for (int i = 0; modules[i].prefix; i++) {
            if (strcmp(modules[i].prefix, module_prefix) == 0) {
                if (!first) off += snprintf(result + off, sizeof(result) - off, ",");
                off += snprintf(result + off, sizeof(result) - off,
                    "{\"label\":\"%s\",\"kind\":3,\"detail\":\"module function\","
                    "\"documentation\":{\"kind\":\"markdown\",\"value\":\"%s\"},"
                    "\"insertText\":\"%s\"}",
                    modules[i].member, modules[i].doc, modules[i].member);
                first = 0;
            }
        }
    } else {
        /* Complete keywords */
        for (int i = 0; keywords[i].name; i++) {
            if (!first) off += snprintf(result + off, sizeof(result) - off, ",");
            off += snprintf(result + off, sizeof(result) - off,
                "{\"label\":\"%s\",\"kind\":%d,\"detail\":\"%s\","
                "\"documentation\":{\"kind\":\"markdown\",\"value\":\"%s\"}}",
                keywords[i].name, keywords[i].kind, keywords[i].detail, keywords[i].documentation);
            first = 0;
        }
        /* Complete built-in functions */
        for (int i = 0; builtins[i].name; i++) {
            if (!first) off += snprintf(result + off, sizeof(result) - off, ",");
            off += snprintf(result + off, sizeof(result) - off,
                "{\"label\":\"%s\",\"kind\":%d,\"detail\":\"%s\","
                "\"documentation\":{\"kind\":\"markdown\",\"value\":\"%s\"}}",
                builtins[i].name, builtins[i].kind, builtins[i].detail, builtins[i].documentation);
            first = 0;
        }
        /* Complete module names */
        const char *mod_names[] = {"os", "math", "fs", "json", "http", "tensor", NULL};
        const char *mod_docs[] = {
            "Operating system utilities (time, exit, sleep, system)",
            "Math functions (abs, min, max, round, floor, ceil, sqrt)",
            "File system operations (read, write, exists, size, mkdir, remove, list)",
            "JSON parsing and serialization (parse, stringify)",
            "HTTP client functions (get, post)",
            "Tensor operations (create, zeros, ones, dot)"
        };
        for (int i = 0; mod_names[i]; i++) {
            if (!first) off += snprintf(result + off, sizeof(result) - off, ",");
            off += snprintf(result + off, sizeof(result) - off,
                "{\"label\":\"%s\",\"kind\":9,\"detail\":\"module\","
                "\"documentation\":{\"kind\":\"markdown\",\"value\":\"%s\"}}",
                mod_names[i], mod_docs[i]);
            first = 0;
        }
    }

    off += snprintf(result + off, sizeof(result) - off, "]}");
    lsp_send_response(id, result);
}

/* ================================================================
 *  HANDLER: textDocument/hover
 * ================================================================ */
static void handle_hover(int id, const char *json) {
    char uri[LSP_MAX_URI];
    json_get_string(json, "uri", uri, sizeof(uri));
    int line = json_get_int(json, "line");
    int character = json_get_int(json, "character");

    LspDocument *doc = doc_find(uri);
    if (!doc || !doc->text) {
        lsp_send_response(id, "null");
        return;
    }

    /* Find the word under cursor */
    const char *p = doc->text;
    int cur_line = 0;
    while (*p && cur_line < line) {
        if (*p == '\n') cur_line++;
        p++;
    }

    /* Find word boundaries around `character` */
    int start = character, end = character;
    int line_len = 0;
    const char *line_start = p;
    while (line_start[line_len] && line_start[line_len] != '\n') line_len++;

    if (character >= line_len) {
        lsp_send_response(id, "null");
        return;
    }

    while (start > 0 && (isalnum((unsigned char)line_start[start - 1]) || line_start[start - 1] == '_'))
        start--;
    while (end < line_len && (isalnum((unsigned char)line_start[end]) || line_start[end] == '_'))
        end++;

    int word_len = end - start;
    if (word_len <= 0 || word_len > 63) {
        lsp_send_response(id, "null");
        return;
    }

    char word[64];
    strncpy(word, line_start + start, word_len);
    word[word_len] = '\0';

    /* Check for module.member (e.g. os.time) */
    char full_word[128] = {0};
    if (end < line_len && line_start[end] == '.') {
        int mem_start = end + 1;
        int mem_end = mem_start;
        while (mem_end < line_len && (isalnum((unsigned char)line_start[mem_end]) || line_start[mem_end] == '_'))
            mem_end++;
        if (mem_end > mem_start) {
            char member[64];
            int mlen = mem_end - mem_start;
            if (mlen > 63) mlen = 63;
            strncpy(member, line_start + mem_start, mlen);
            member[mlen] = '\0';
            snprintf(full_word, sizeof(full_word), "%s.%s", word, member);
        }
    }

    /* Search for documentation */
    const char *doc_text = NULL;
    const char *detail = NULL;

    /* Check module members first */
    if (full_word[0]) {
        for (int i = 0; modules[i].label; i++) {
            if (strcmp(modules[i].label, full_word) == 0) {
                doc_text = modules[i].doc;
                detail = "module function";
                break;
            }
        }
    }

    /* Check keywords */
    if (!doc_text) {
        for (int i = 0; keywords[i].name; i++) {
            if (strcmp(keywords[i].name, word) == 0) {
                doc_text = keywords[i].documentation;
                detail = keywords[i].detail;
                break;
            }
        }
    }

    /* Check builtins */
    if (!doc_text) {
        for (int i = 0; builtins[i].name; i++) {
            if (strcmp(builtins[i].name, word) == 0) {
                doc_text = builtins[i].documentation;
                detail = builtins[i].detail;
                break;
            }
        }
    }

    if (!doc_text) {
        lsp_send_response(id, "null");
        return;
    }

    char result[4096];
    snprintf(result, sizeof(result),
        "{\"contents\":{\"kind\":\"markdown\","
        "\"value\":\"**%s** (%s)\\n\\n%s\"},"
        "\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
        "\"end\":{\"line\":%d,\"character\":%d}}}",
        word, detail, doc_text, line, start, line, end);
    lsp_send_response(id, result);
}

/* ================================================================
 *  HANDLER: textDocument/definition
 * ================================================================ */
static void handle_definition(int id, const char *json) {
    char uri[LSP_MAX_URI];
    json_get_string(json, "uri", uri, sizeof(uri));
    int line = json_get_int(json, "line");
    int character = json_get_int(json, "character");

    LspDocument *doc = doc_find(uri);
    if (!doc || !doc->text) {
        lsp_send_response(id, "null");
        return;
    }

    /* Find the word under cursor */
    const char *p = doc->text;
    int cur_line = 0;
    while (*p && cur_line < line) {
        if (*p == '\n') cur_line++;
        p++;
    }
    int start = character, end = character;
    int line_len = 0;
    const char *line_start = p;
    while (line_start[line_len] && line_start[line_len] != '\n') line_len++;

    if (character >= line_len) {
        lsp_send_response(id, "null");
        return;
    }

    while (start > 0 && (isalnum((unsigned char)line_start[start - 1]) || line_start[start - 1] == '_'))
        start--;
    while (end < line_len && (isalnum((unsigned char)line_start[end]) || line_start[end] == '_'))
        end++;

    int word_len = end - start;
    if (word_len <= 0 || word_len > 127) {
        lsp_send_response(id, "null");
        return;
    }

    char word[128];
    strncpy(word, line_start + start, word_len);
    word[word_len] = '\0';

    /* Scan the document for definition patterns:
     *   func NAME
     *   let NAME
     *   class NAME
     *   blueprint NAME
     */
    const char *patterns[] = {"func ", "let ", "class ", "blueprint ", NULL};
    const char *scan = doc->text;
    int scan_line = 0;
    int found = 0;
    int def_line = 0, def_char = 0, def_end = 0;

    while (*scan) {
        if (*scan == '\n') {
            scan_line++;
            scan++;
            continue;
        }

        /* Check each pattern */
        for (int pi = 0; patterns[pi]; pi++) {
            int plen = (int)strlen(patterns[pi]);
            if (strncmp(scan, patterns[pi], plen) == 0) {
                const char *name_start = scan + plen;
                /* Skip whitespace */
                while (*name_start == ' ' || *name_start == '\t') name_start++;
                /* Extract name */
                const char *name_end = name_start;
                while (*name_end && (isalnum((unsigned char)*name_end) || *name_end == '_'))
                    name_end++;
                int nlen = (int)(name_end - name_start);
                if (nlen > 0 && nlen == word_len && strncmp(name_start, word, word_len) == 0) {
                    /* Find character position within line */
                    const char *lp = scan;
                    while (lp > doc->text && *(lp - 1) != '\n') lp--;
                    def_line = scan_line;
                    def_char = (int)(name_start - lp);
                    def_end = def_char + nlen;
                    found = 1;
                    break;
                }
            }
        }
        if (found) break;
        scan++;
    }

    if (!found) {
        lsp_send_response(id, "null");
        return;
    }

    char escaped_uri[LSP_MAX_URI * 2];
    json_escape(uri, escaped_uri, sizeof(escaped_uri));

    char result[4096];
    snprintf(result, sizeof(result),
        "{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
        "\"end\":{\"line\":%d,\"character\":%d}}}",
        escaped_uri, def_line, def_char, def_line, def_end);
    lsp_send_response(id, result);
}

/* ================================================================
 *  HANDLER: textDocument/semanticTokens/full
 * ================================================================ */

/* Semantic token encoding: array of 5-tuples
 * [deltaLine, deltaStartChar, length, tokenType, tokenModifiers] */
#define MAX_SEM_TOKENS 8192

static void handle_semantic_tokens(int id, const char *json) {
    char uri[LSP_MAX_URI];
    json_get_string(json, "uri", uri, sizeof(uri));

    LspDocument *doc = doc_find(uri);
    if (!doc || !doc->text) {
        lsp_send_response(id, "{\"data\":[]}");
        return;
    }

    /* We'll accumulate absolute tokens, then convert to deltas */
    typedef struct {
        int line;
        int col;
        int length;
        int type;
    } SemToken;

    SemToken *tokens = (SemToken *)calloc(MAX_SEM_TOKENS, sizeof(SemToken));
    int token_count = 0;

    const char *p = doc->text;
    int cur_line = 0, cur_col = 0;

    while (*p && token_count < MAX_SEM_TOKENS - 1) {
        /* Track position */
        if (*p == '\n') {
            cur_line++;
            cur_col = 0;
            p++;
            continue;
        }

        /* Skip whitespace */
        if (*p == ' ' || *p == '\t' || *p == '\r') {
            cur_col++;
            p++;
            continue;
        }

        /* Single-line comment */
        if (p[0] == '/' && p[1] == '/') {
            int start_col = cur_col;
            int len = 0;
            while (*p && *p != '\n') { p++; len++; cur_col++; }
            tokens[token_count++] = (SemToken){cur_line, start_col, len, TOK_COMMENT};
            continue;
        }

        /* Multi-line comment */
        if (p[0] == '/' && p[1] == '*') {
            int start_line = cur_line, start_col = cur_col;
            int len = 0;
            /* For simplicity, treat the whole block comment as one token on its start line */
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\n') { cur_line++; cur_col = 0; }
                else cur_col++;
                p++; len++;
            }
            if (*p) { p += 2; len += 2; cur_col += 2; } /* skip */ 
            tokens[token_count++] = (SemToken){start_line, start_col, len, TOK_COMMENT};
            continue;
        }

        /* String literals */
        if (*p == '"' || *p == '\'') {
            char quote = *p;
            int start_col = cur_col;
            int len = 1;
            p++; cur_col++;
            while (*p && *p != quote) {
                if (*p == '\\' && *(p + 1)) { p++; len++; cur_col++; }
                if (*p == '\n') { cur_line++; cur_col = 0; }
                else cur_col++;
                p++; len++;
            }
            if (*p == quote) { p++; len++; cur_col++; }
            tokens[token_count++] = (SemToken){cur_line, start_col, len, TOK_STRING};
            continue;
        }

        /* f-string */
        if (*p == 'f' && (p[1] == '"' || p[1] == '\'')) {
            char quote = p[1];
            int start_col = cur_col;
            int len = 2;
            p += 2; cur_col += 2;
            while (*p && *p != quote) {
                if (*p == '\\' && *(p + 1)) { p++; len++; cur_col++; }
                if (*p == '\n') { cur_line++; cur_col = 0; }
                else cur_col++;
                p++; len++;
            }
            if (*p == quote) { p++; len++; cur_col++; }
            tokens[token_count++] = (SemToken){cur_line, start_col, len, TOK_STRING};
            continue;
        }

        /* Numbers */
        if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
            int start_col = cur_col;
            int len = 0;
            while (*p && (isdigit((unsigned char)*p) || *p == '.' || *p == 'e' || *p == 'E' ||
                   ((*p == '+' || *p == '-') && (p[-1] == 'e' || p[-1] == 'E')))) {
                p++; len++; cur_col++;
            }
            tokens[token_count++] = (SemToken){cur_line, start_col, len, TOK_NUMBER};
            continue;
        }

        /* Identifiers and keywords */
        if (isalpha((unsigned char)*p) || *p == '_') {
            int start_col = cur_col;
            const char *word_start = p;
            int len = 0;
            while (*p && (isalnum((unsigned char)*p) || *p == '_')) {
                p++; len++; cur_col++;
            }
            char word[128];
            int wlen = len < 127 ? len : 127;
            strncpy(word, word_start, wlen);
            word[wlen] = '\0';

            int tok_type = TOK_VARIABLE;
            if (is_keyword(word)) tok_type = TOK_KEYWORD;
            else if (is_builtin(word)) tok_type = TOK_FUNCTION;
            else if (strcmp(word, "class") == 0 || strcmp(word, "blueprint") == 0 || strcmp(word, "trait") == 0)
                tok_type = TOK_CLASS;

            /* If previous token was 'func', 'class', 'blueprint', 'trait', or 'action', mark as function/class */
            if (token_count > 0) {
                /* Heuristic: look back in source for context */
                const char *prev = word_start - 1;
                while (prev > doc->text && (*prev == ' ' || *prev == '\t')) prev--;
                /* Check if the preceding word is a definition keyword */
                const char *def_kws[] = {"func", "class", "blueprint", "trait", "action", NULL};
                for (int k = 0; def_kws[k]; k++) {
                    int dlen = (int)strlen(def_kws[k]);
                    if (prev - dlen + 1 >= doc->text) {
                        const char *check = prev - dlen + 1;
                        if (strncmp(check, def_kws[k], dlen) == 0 &&
                            (check == doc->text || !isalnum((unsigned char)*(check - 1)))) {
                            if (strcmp(def_kws[k], "func") == 0 || strcmp(def_kws[k], "action") == 0)
                                tok_type = TOK_FUNCTION;
                            else
                                tok_type = TOK_CLASS;
                            break;
                        }
                    }
                }
            }

            tokens[token_count++] = (SemToken){cur_line, start_col, len, tok_type};
            continue;
        }

        /* Operators */
        if (strchr("+-*/%=<>!&|^~?:", *p)) {
            int start_col = cur_col;
            int len = 1;
            /* Check for two-char operators */
            if ((p[0] == '=' && p[1] == '=') || (p[0] == '!' && p[1] == '=') ||
                (p[0] == '<' && p[1] == '=') || (p[0] == '>' && p[1] == '=') ||
                (p[0] == '+' && p[1] == '=') || (p[0] == '-' && p[1] == '=') ||
                (p[0] == '*' && p[1] == '=') || (p[0] == '/' && p[1] == '=') ||
                (p[0] == '%' && p[1] == '=') || (p[0] == '*' && p[1] == '*')) {
                len = 2;
            }
            p += len; cur_col += len;
            tokens[token_count++] = (SemToken){cur_line, start_col, len, TOK_OPERATOR};
            continue;
        }

        /* Skip other characters (parens, braces, brackets, commas, etc.) */
        p++;
        cur_col++;
    }

    /* Encode as delta tokens */
    char *result = (char *)malloc(MAX_SEM_TOKENS * 32 + 64);
    int roff = 0;
    roff += sprintf(result + roff, "{\"data\":[");

    int prev_line = 0, prev_col = 0;
    int first = 1;
    for (int i = 0; i < token_count; i++) {
        int dl = tokens[i].line - prev_line;
        int dc = (dl == 0) ? tokens[i].col - prev_col : tokens[i].col;
        if (!first) roff += sprintf(result + roff, ",");
        roff += sprintf(result + roff, "%d,%d,%d,%d,0", dl, dc, tokens[i].length, tokens[i].type);
        prev_line = tokens[i].line;
        prev_col = tokens[i].col;
        first = 0;
    }

    roff += sprintf(result + roff, "]}");
    lsp_send_response(id, result);

    free(tokens);
    free(result);
}

/* ================================================================
 *  MAIN LSP LOOP (stdin/stdout JSON-RPC 2.0)
 * ================================================================ */
void lsp_run_stdio(void) {
    LSP_LOG("GlowLang LSP server starting on stdio...");

    /* Read header + body loop */
    char *msg_buf = (char *)malloc(LSP_MAX_DOC_SIZE * 2);
    if (!msg_buf) {
        LSP_LOG("Failed to allocate message buffer");
        return;
    }

    int running = 1;
    while (running) {
        /* Read Content-Length header */
        int content_length = -1;
        char header_line[512];

        while (1) {
            if (!fgets(header_line, sizeof(header_line), stdin)) {
                LSP_LOG("EOF on stdin, shutting down");
                running = 0;
                break;
            }
            /* Blank line (just \r\n) signals end of headers */
            if (strcmp(header_line, "\r\n") == 0 || strcmp(header_line, "\n") == 0) {
                break;
            }
            if (strncmp(header_line, "Content-Length:", 15) == 0) {
                content_length = atoi(header_line + 15);
            }
            /* Ignore other headers (Content-Type, etc.) */
        }

        if (!running) break;

        if (content_length <= 0) {
            LSP_LOG("Invalid Content-Length: %d, skipping", content_length);
            continue;
        }

        if (content_length >= LSP_MAX_DOC_SIZE * 2 - 1) {
            LSP_LOG("Message too large: %d bytes", content_length);
            /* Drain the message */
            int remaining = content_length;
            while (remaining > 0) {
                int to_read = remaining < 4096 ? remaining : 4096;
                char drain[4096];
                int rd = (int)fread(drain, 1, to_read, stdin);
                if (rd <= 0) break;
                remaining -= rd;
            }
            continue;
        }

        /* Read exactly content_length bytes */
        int total_read = 0;
        while (total_read < content_length) {
            int rd = (int)fread(msg_buf + total_read, 1, content_length - total_read, stdin);
            if (rd <= 0) {
                LSP_LOG("EOF while reading message body");
                running = 0;
                break;
            }
            total_read += rd;
        }
        if (!running) break;
        msg_buf[content_length] = '\0';

        LSP_LOG("Received %d bytes", content_length);

        /* Parse JSON-RPC request */
        char method[256] = {0};
        int id = json_get_int(msg_buf, "id");
        json_get_string(msg_buf, "method", method, sizeof(method));

        LSP_LOG("Method: '%s', id: %d", method, id);

        /* Route to handler */
        if (strcmp(method, "initialize") == 0) {
            handle_initialize(id);
        }
        else if (strcmp(method, "initialized") == 0) {
            /* Notification, no response needed */
            LSP_LOG("Client initialized");
        }
        else if (strcmp(method, "shutdown") == 0) {
            handle_shutdown(id);
        }
        else if (strcmp(method, "exit") == 0) {
            LSP_LOG("Exit requested");
            running = 0;
        }
        else if (strcmp(method, "textDocument/didOpen") == 0) {
            handle_did_open(msg_buf);
        }
        else if (strcmp(method, "textDocument/didChange") == 0) {
            handle_did_change(msg_buf);
        }
        else if (strcmp(method, "textDocument/didSave") == 0) {
            /* No-op: diagnostics already sent on change */
            LSP_LOG("Document saved (no-op)");
        }
        else if (strcmp(method, "textDocument/didClose") == 0) {
            /* Could remove from doc store, but not critical */
            LSP_LOG("Document closed");
        }
        else if (strcmp(method, "textDocument/completion") == 0) {
            handle_completion(id, msg_buf);
        }
        else if (strcmp(method, "textDocument/hover") == 0) {
            handle_hover(id, msg_buf);
        }
        else if (strcmp(method, "textDocument/definition") == 0) {
            handle_definition(id, msg_buf);
        }
        else if (strcmp(method, "textDocument/semanticTokens/full") == 0) {
            handle_semantic_tokens(id, msg_buf);
        }
        else {
            /* Unknown method: if it has an id, respond with method not found */
            if (id >= 0) {
                char err_buf[512];
                snprintf(err_buf, sizeof(err_buf),
                    "{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":{\"code\":-32601,\"message\":\"Method not found: %s\"}}",
                    id, method);
                lsp_send(err_buf);
            }
            LSP_LOG("Unknown method: %s", method);
        }
    }

    /* Cleanup */
    for (int i = 0; i < doc_store.count; i++) {
        free(doc_store.docs[i].text);
        doc_store.docs[i].text = NULL;
    }
    doc_store.count = 0;
    free(msg_buf);
    LSP_LOG("LSP server shut down cleanly");
}

/* ================================================================
 *  LEGACY: handle_client over file descriptor
 * ================================================================ */
static void send_response_fd(int fd, const char *json) {
    size_t len = strlen(json);
    char header[128];
    int hlen = sprintf(header, "Content-Length: %zu\r\n\r\n", len);
    if (write(fd, header, hlen) < 0) { /* ignore */ }
    if (write(fd, json, len) < 0) { /* ignore */ }
}

void handle_client(int fd) {
    /* When invoked with fd=0 (stdin), run the full LSP protocol server.
     * This is the path taken by `glowlang --lsp`. */
    if (fd == 0) {
        lsp_run_stdio();
        return;
    }

    /* Legacy: simple error-count mode over a socket fd */
    char buffer[4096];
    while (1) {
        ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        glow_clear_errors();
        yyparse();
        char resp[8192];
        sprintf(resp, "{\"id\": 1, \"result\": {\"errors\": %d}}", glow_error_count);
        send_response_fd(fd, resp);
    }
    close(fd);
}
