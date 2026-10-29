/* process: run other programs and talk to them.
 *
 *   process.run(cmd, opts)    run to completion; returns
 *                             {code, stdout, stderr, ok, timed_out}
 *   process.start(cmd, opts)  start a long-running program with piped
 *                             stdin/stdout; returns a handle for
 *                             send / read_line / read_all / close_input /
 *                             wait / kill / alive / pid / close
 *   process.find(name)        full path of a program on PATH, or none
 *
 * cmd is a list ["python", "-c", "print(1)"], run directly with no shell, so
 * arguments are passed exactly as given; or a string, run by the system
 * shell (/bin/sh -c, or cmd.exe /c on Windows).
 *
 * opts (all optional): input (text for stdin), cwd, env (dict merged over
 * the current environment; a none value removes a variable), timeout
 * (seconds), stdout ("pipe" | "inherit" | "null"), stderr ("pipe" |
 * "stdout" | "inherit" | "null").
 *
 * Blocking waits release the GIL so other GlowLang threads keep running.
 * Started programs are killed when GlowLang exits; on timeout the whole
 * process tree is killed (a job object on Windows, a process group
 * elsewhere).
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE   /* pipe2 */
#endif
#include "glowlang.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#include <wctype.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
#endif

/* ---- values and errors ---- */

static Value proc_error(const char *fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    pending_exception = value_string_copy(msg);
    has_exception = 1;
    return value_none();
}

static Value string_n(const char *s, size_t n) {
    GCString *gs = gc_alloc(sizeof(GCString) + n + 1, OBJ_STRING);
    gs->size = (int)n;
    if (n) memcpy(gs->chars, s, n);
    gs->chars[n] = '\0';
    return make_value(VAL_STRING, (uint64_t)(uintptr_t)gs);
}

static int is_str(Value v) { return GET_TYPE(v) == VAL_STRING && (v & MASK_PAYLOAD); }

static int is_number(Value v) { return GET_TYPE(v) == VAL_INT || GET_TYPE(v) == VAL_FLOAT; }

/* A malloc'd copy of a GlowLang string; *len gets its byte length. */
static char *dup_str(Value v, size_t *len) {
    const char *s = rope_flatten(v);
    size_t n = rope_length(v);
    char *out = malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    if (len) *len = n;
    return out;
}

/* ---- byte buffer ---- */

typedef struct { char *p; size_t n, cap; } Buf;

static int buf_add(Buf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->n + n + 1) cap *= 2;
        char *p = realloc(b->p, cap);
        if (!p) return 0;
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
    return 1;
}

static void buf_free(Buf *b) { free(b->p); b->p = NULL; b->n = b->cap = 0; }

/* ---- monotonic clock ---- */

static double now_seconds(void) {
#ifdef _WIN32
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

/* ---- options ---- */

enum { IO_PIPE, IO_INHERIT, IO_NULL, IO_STDOUT };

typedef struct {
    char **argv;          /* NULL-terminated, when cmd is a list */
    int argc;
    char *shell;          /* the command string, when cmd is a string */
    char *cwd;
    int env_set;          /* opts has env */
    int env_n;
    char **env_keys;
    char **env_vals;      /* NULL entry: remove the variable */
    char *input;
    size_t input_len;
    int has_input;
    double timeout;       /* < 0: none */
    int out_mode, err_mode;
} Opts;

static void opts_free(Opts *o) {
    for (int i = 0; i < o->argc; i++) free(o->argv[i]);
    free(o->argv);
    free(o->shell);
    free(o->cwd);
    for (int i = 0; i < o->env_n; i++) { free(o->env_keys[i]); free(o->env_vals[i]); }
    free(o->env_keys);
    free(o->env_vals);
    free(o->input);
    memset(o, 0, sizeof(*o));
}

static int parse_mode(Value v, int allow_stdout, int *mode, const char *fn, const char *what) {
    if (GET_TYPE(v) == VAL_NONE) return 1;
    const char *s = is_str(v) ? AS_STRING(v) : "";
    if (!strcmp(s, "pipe")) *mode = IO_PIPE;
    else if (!strcmp(s, "inherit")) *mode = IO_INHERIT;
    else if (!strcmp(s, "null")) *mode = IO_NULL;
    else if (allow_stdout && !strcmp(s, "stdout")) *mode = IO_STDOUT;
    else {
        proc_error("%s: %s must be \"pipe\", %s\"inherit\" or \"null\"", fn, what, allow_stdout ? "\"stdout\", " : "");
        return 0;
    }
    return 1;
}

/* Read cmd and opts into o. Returns 0 (with an exception set) on bad input. */
static int parse_args(const char *fn, int argc, Value *args, int started, Opts *o) {
    memset(o, 0, sizeof(*o));
    o->timeout = -1;
    o->out_mode = IO_PIPE;
    o->err_mode = started ? IO_INHERIT : IO_PIPE;
    if (argc < 1) { proc_error("%s: expected a command (a list of strings or a string)", fn); return 0; }

    Value cmd = args[0];
    if (is_str(cmd)) {
        if (rope_length(cmd) == 0) { proc_error("%s: the command is empty", fn); return 0; }
        o->shell = dup_str(cmd, NULL);
    } else if (GET_TYPE(cmd) == VAL_LIST) {
        List *l = AS_LIST(cmd);
        if (l->size == 0) { proc_error("%s: the command list is empty", fn); return 0; }
        o->argv = calloc((size_t)l->size + 1, sizeof(char *));
        for (int i = 0; i < l->size; i++) {
            if (!is_str(l->items[i])) { proc_error("%s: command item %d is not a string", fn, i); opts_free(o); return 0; }
            size_t n;
            o->argv[i] = dup_str(l->items[i], &n);
            o->argc = i + 1;
            if (strlen(o->argv[i]) != n) { proc_error("%s: command item %d contains a NUL character", fn, i); opts_free(o); return 0; }
        }
        if (o->argv[0][0] == '\0') { proc_error("%s: the program name is empty", fn); opts_free(o); return 0; }
    } else {
        proc_error("%s: expected a command (a list of strings or a string)", fn);
        return 0;
    }

    if (argc < 2 || GET_TYPE(args[1]) == VAL_NONE) return 1;
    if (GET_TYPE(args[1]) != VAL_DICT) { proc_error("%s: options must be a dict", fn); opts_free(o); return 0; }
    Dict *d = AS_DICT(args[1]);

    Value v = dict_get(d, "cwd");
    if (is_str(v)) o->cwd = dup_str(v, NULL);
    else if (GET_TYPE(v) != VAL_NONE) { proc_error("%s: cwd must be a string", fn); opts_free(o); return 0; }

    v = dict_get(d, "input");
    if (is_str(v)) {
        if (started) { proc_error("%s: input is for process.run; use process.send", fn); opts_free(o); return 0; }
        o->input = dup_str(v, &o->input_len);
        o->has_input = 1;
    } else if (GET_TYPE(v) != VAL_NONE) { proc_error("%s: input must be a string", fn); opts_free(o); return 0; }

    v = dict_get(d, "timeout");
    if (is_number(v)) {
        if (started) { proc_error("%s: timeout is for process.run; pass it to read_line or wait", fn); opts_free(o); return 0; }
        o->timeout = val_to_double(v);
        if (o->timeout < 0) o->timeout = 0;
    } else if (GET_TYPE(v) != VAL_NONE) { proc_error("%s: timeout must be a number of seconds", fn); opts_free(o); return 0; }

    if (!started && !parse_mode(dict_get(d, "stdout"), 0, &o->out_mode, fn, "stdout")) { opts_free(o); return 0; }
    if (!parse_mode(dict_get(d, "stderr"), !started, &o->err_mode, fn, "stderr")) { opts_free(o); return 0; }
    if (started && o->err_mode == IO_PIPE) {
        /* An unread stderr pipe fills up and blocks the program. */
        proc_error("%s: stderr must be \"inherit\" or \"null\" for started programs", fn);
        opts_free(o); return 0;
    }

    v = dict_get(d, "env");
    if (GET_TYPE(v) == VAL_DICT) {
        Dict *e = AS_DICT(v);
        o->env_set = 1;
        o->env_keys = calloc((size_t)e->size + 1, sizeof(char *));
        o->env_vals = calloc((size_t)e->size + 1, sizeof(char *));
        for (int i = 0; i < e->capacity; i++) {
            if (GET_TYPE(e->keys[i]) == VAL_NONE) continue;
            const char *k = (const char *)(uintptr_t)(e->keys[i] & MASK_PAYLOAD);
            Value ev = e->values[i];
            if (!k[0] || strchr(k, '=')) { proc_error("%s: bad environment variable name \"%s\"", fn, k); opts_free(o); return 0; }
            char *val = NULL;
            if (is_str(ev)) val = dup_str(ev, NULL);
            else if (is_number(ev) || GET_TYPE(ev) == VAL_BOOL) {
                char tmp[64];
                if (GET_TYPE(ev) == VAL_BOOL) snprintf(tmp, sizeof(tmp), "%s", (ev & 1) ? "true" : "false");
                else if (GET_TYPE(ev) == VAL_INT) snprintf(tmp, sizeof(tmp), "%lld", (long long)AS_INT(ev));
                else snprintf(tmp, sizeof(tmp), "%.17g", val_to_double(ev));
                val = strdup(tmp);
            } else if (GET_TYPE(ev) != VAL_NONE) { proc_error("%s: env value for \"%s\" must be a string", fn, k); opts_free(o); return 0; }
            o->env_keys[o->env_n] = strdup(k);
            o->env_vals[o->env_n] = val;
            o->env_n++;
        }
    } else if (GET_TYPE(v) != VAL_NONE) { proc_error("%s: env must be a dict", fn); opts_free(o); return 0; }
    return 1;
}

/* The value PATH will have in the child. */
static const char *env_override(const Opts *o, const char *name, int *found) {
    for (int i = 0; i < o->env_n; i++) {
#ifdef _WIN32
        if (!_stricmp(o->env_keys[i], name)) { *found = 1; return o->env_vals[i]; }
#else
        if (!strcmp(o->env_keys[i], name)) { *found = 1; return o->env_vals[i]; }
#endif
    }
    *found = 0;
    return NULL;
}

/* ================================================================
 *  Windows
 * ================================================================ */
#ifdef _WIN32

static wchar_t *to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = malloc((size_t)n * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static char *to_utf8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = malloc((size_t)n);
    if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

static void win_error(char *err, size_t errsz, const char *what, DWORD code) {
    char sys[512] = "";
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, sys, sizeof(sys), NULL);
    size_t n = strlen(sys);
    while (n && (sys[n - 1] == '\n' || sys[n - 1] == '\r' || sys[n - 1] == '.')) sys[--n] = '\0';
    snprintf(err, errsz, "%s: %s", what, sys[0] ? sys : "unknown error");
}

static int is_file_w(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int has_ext_w(const wchar_t *name) {
    const wchar_t *base = name;
    for (const wchar_t *p = name; *p; p++) if (*p == L'\\' || *p == L'/' || *p == L':') base = p + 1;
    return wcschr(base, L'.') != NULL;
}

/* Try dir\name, and dir\name + each PATHEXT extension when name has none. */
static wchar_t *try_candidates(const wchar_t *dir, size_t dirlen, const wchar_t *name, const wchar_t *pathext) {
    size_t nlen = wcslen(name);
    wchar_t *buf = malloc((dirlen + nlen + 64 + 2) * sizeof(wchar_t));
    if (!buf) return NULL;
    size_t at = 0;
    if (dirlen) {
        memcpy(buf, dir, dirlen * sizeof(wchar_t));
        at = dirlen;
        if (buf[at - 1] != L'\\' && buf[at - 1] != L'/') buf[at++] = L'\\';
    }
    memcpy(buf + at, name, (nlen + 1) * sizeof(wchar_t));
    if (has_ext_w(name)) {
        if (is_file_w(buf)) return buf;
        free(buf);
        return NULL;
    }
    const wchar_t *e = pathext;
    while (*e) {
        const wchar_t *end = wcschr(e, L';');
        size_t elen = end ? (size_t)(end - e) : wcslen(e);
        if (elen > 0 && elen < 60) {
            memcpy(buf + at + nlen, e, elen * sizeof(wchar_t));
            buf[at + nlen + elen] = 0;
            if (is_file_w(buf)) return buf;
        }
        if (!end) break;
        e = end + 1;
    }
    free(buf);
    return NULL;
}

/* Full path of a program: as given when it contains a path, else from PATH
   (the current folder is not searched). */
static wchar_t *resolve_program(const char *name, const Opts *o) {
    wchar_t *wname = to_wide(name);
    if (!wname) return NULL;
    wchar_t pathext[512];
    if (!GetEnvironmentVariableW(L"PATHEXT", pathext, 512)) wcscpy(pathext, L".COM;.EXE;.BAT;.CMD");
    wchar_t *found = NULL;
    if (wcspbrk(wname, L"\\/:")) {
        found = try_candidates(L"", 0, wname, pathext);
    } else {
        int over = 0;
        const char *opath = o ? env_override(o, "PATH", &over) : NULL;
        wchar_t *path = NULL;
        if (over) path = opath ? to_wide(opath) : NULL;
        else {
            DWORD n = GetEnvironmentVariableW(L"PATH", NULL, 0);
            if (n) { path = malloc(n * sizeof(wchar_t)); if (path) GetEnvironmentVariableW(L"PATH", path, n); }
        }
        for (wchar_t *d = path; d && *d && !found;) {
            wchar_t *end = wcschr(d, L';');
            size_t len = end ? (size_t)(end - d) : wcslen(d);
            wchar_t *dir = d;
            if (len >= 2 && dir[0] == L'"' && dir[len - 1] == L'"') { dir++; len -= 2; }
            if (len) found = try_candidates(dir, len, wname, pathext);
            if (!end) break;
            d = end + 1;
        }
        free(path);
    }
    free(wname);
    return found;
}

static int is_batch_w(const wchar_t *p) {
    size_t n = wcslen(p);
    return n > 4 && (!_wcsicmp(p + n - 4, L".bat") || !_wcsicmp(p + n - 4, L".cmd"));
}

/* Append one argument quoted the way CommandLineToArgvW and the C runtime
   read it back. */
static void append_arg(Buf *b, const char *a) {
    if (a[0] && !strpbrk(a, " \t\n\v\"")) { buf_add(b, a, strlen(a)); return; }
    buf_add(b, "\"", 1);
    for (const char *p = a;; p++) {
        size_t slashes = 0;
        while (*p == '\\') { slashes++; p++; }
        if (!*p) { for (size_t i = 0; i < slashes * 2; i++) buf_add(b, "\\", 1); break; }
        if (*p == '"') { for (size_t i = 0; i < slashes * 2 + 1; i++) buf_add(b, "\\", 1); }
        else { for (size_t i = 0; i < slashes; i++) buf_add(b, "\\", 1); }
        buf_add(b, p, 1);
    }
    buf_add(b, "\"", 1);
}

/* cmd.exe re-reads a batch file's command line with its own rules, so only
   arguments it cannot misread are allowed (see CVE-2024-24576). */
static int append_batch_arg(Buf *b, const char *a) {
    if (strpbrk(a, "\"%\r\n")) return 0;
    if (!a[0] || strpbrk(a, " \t&|<>()^,;=!")) { buf_add(b, "\"", 1); buf_add(b, a, strlen(a)); buf_add(b, "\"", 1); }
    else buf_add(b, a, strlen(a));
    return 1;
}

static wchar_t *system_cmd_exe(void) {
    wchar_t dir[MAX_PATH];
    UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    if (!n || n > MAX_PATH - 10) return _wcsdup(L"C:\\Windows\\System32\\cmd.exe");
    wchar_t *p = malloc((n + 10) * sizeof(wchar_t));
    swprintf(p, n + 10, L"%ls\\cmd.exe", dir);
    return p;
}

static int name_cmp(const void *a, const void *b) {
    const wchar_t *x = *(const wchar_t *const *)a, *y = *(const wchar_t *const *)b;
    /* Compare the names only (up to '='; a leading '=' is part of the name). */
    for (;; x++, y++) {
        wchar_t cx = (*x == L'=' && x != *(const wchar_t *const *)a) ? 0 : towupper(*x);
        wchar_t cy = (*y == L'=' && y != *(const wchar_t *const *)b) ? 0 : towupper(*y);
        if (cx != cy || !cx) return (int)cx - (int)cy;
    }
}

static int same_name(const wchar_t *entry, const wchar_t *name) {
    size_t n = wcslen(name);
    return !_wcsnicmp(entry, name, n) && entry[n] == L'=';
}

/* A sorted environment block: the current environment with the overrides. */
static wchar_t *build_env_block(const Opts *o) {
    wchar_t *cur = GetEnvironmentStringsW();
    size_t count = 0;
    for (wchar_t *p = cur; *p; p += wcslen(p) + 1) count++;
    wchar_t **items = calloc(count + (size_t)o->env_n + 1, sizeof(wchar_t *));
    wchar_t **wkeys = calloc((size_t)o->env_n + 1, sizeof(wchar_t *));
    size_t n = 0;
    for (int i = 0; i < o->env_n; i++) wkeys[i] = to_wide(o->env_keys[i]);
    for (wchar_t *p = cur; *p; p += wcslen(p) + 1) {
        int replaced = 0;
        for (int i = 0; i < o->env_n && !replaced; i++) if (wkeys[i] && same_name(p, wkeys[i])) replaced = 1;
        if (!replaced) items[n++] = _wcsdup(p);
    }
    for (int i = 0; i < o->env_n; i++) {
        if (!o->env_vals[i] || !wkeys[i]) continue;
        wchar_t *wv = to_wide(o->env_vals[i]);
        size_t len = wcslen(wkeys[i]) + wcslen(wv) + 2;
        items[n] = malloc(len * sizeof(wchar_t));
        swprintf(items[n], len, L"%ls=%ls", wkeys[i], wv);
        n++;
        free(wv);
    }
    FreeEnvironmentStringsW(cur);
    qsort(items, n, sizeof(wchar_t *), name_cmp);
    size_t total = 1;
    for (size_t i = 0; i < n; i++) total += wcslen(items[i]) + 1;
    wchar_t *block = malloc((total + 1) * sizeof(wchar_t)), *w = block;
    for (size_t i = 0; i < n; i++) {
        size_t len = wcslen(items[i]) + 1;
        memcpy(w, items[i], len * sizeof(wchar_t));
        w += len;
        free(items[i]);
    }
    *w++ = 0;
    if (n == 0) *w = 0;
    for (int i = 0; i < o->env_n; i++) free(wkeys[i]);
    free(wkeys);
    free(items);
    return block;
}

typedef struct {
    HANDLE proc, job;
    DWORD pid;
    HANDLE in_w, out_r, err_r;   /* parent's ends; NULL when not piped */
} Child;

static HANDLE open_null(int for_write) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    return CreateFileW(L"NUL", for_write ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
}

static HANDLE dup_inheritable(DWORD std, int for_write) {
    HANDLE h = GetStdHandle(std), out = NULL;
    if (h && h != INVALID_HANDLE_VALUE &&
        DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &out, 0, TRUE, DUPLICATE_SAME_ACCESS))
        return out;
    return open_null(for_write);
}

/* Make a pipe whose child end is inheritable. */
static int make_pipe(HANDLE *child_end, HANDLE *parent_end, int child_reads) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE r, w;
    if (!CreatePipe(&r, &w, &sa, 0)) return 0;
    *child_end = child_reads ? r : w;
    *parent_end = child_reads ? w : r;
    SetHandleInformation(*parent_end, HANDLE_FLAG_INHERIT, 0);
    return 1;
}

/* Start the program. stdin is piped when pipe_in, else inherited. */
static int start_child(const Opts *o, int pipe_in, int kill_on_close, Child *c, char *err, size_t errsz) {
    memset(c, 0, sizeof(*c));
    wchar_t *app = NULL, *wcmd = NULL, *wcwd = NULL, *envb = NULL;
    Buf cmd = { 0 };
    HANDLE cin = NULL, cout = NULL, cerr = NULL;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    int ok = 0;

    if (o->shell) {
        app = system_cmd_exe();
        buf_add(&cmd, "cmd.exe /d /s /c \"", 18);
        buf_add(&cmd, o->shell, strlen(o->shell));
        buf_add(&cmd, "\"", 1);
    } else {
        app = resolve_program(o->argv[0], o);
        if (!app) { snprintf(err, errsz, "program not found: %s", o->argv[0]); goto done; }
        if (is_batch_w(app)) {
            char *app8 = to_utf8(app);
            wchar_t *shell = system_cmd_exe();
            buf_add(&cmd, "cmd.exe /d /e:ON /v:OFF /s /c \"", 31);
            int safe = append_batch_arg(&cmd, app8);
            for (int i = 1; i < o->argc && safe; i++) { buf_add(&cmd, " ", 1); safe = append_batch_arg(&cmd, o->argv[i]); }
            buf_add(&cmd, "\"", 1);
            free(app8);
            free(app);
            app = shell;
            if (!safe) { snprintf(err, errsz, "an argument contains \" %% or a line break, which cannot be passed safely to the batch file %s", o->argv[0]); goto done; }
        } else {
            for (int i = 0; i < o->argc; i++) { if (i) buf_add(&cmd, " ", 1); append_arg(&cmd, o->argv[i]); }
        }
    }
    wcmd = to_wide(cmd.p ? cmd.p : "");
    if (o->cwd) {
        wcwd = to_wide(o->cwd);
        DWORD a = GetFileAttributesW(wcwd);
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) { snprintf(err, errsz, "cwd is not a folder: %s", o->cwd); goto done; }
    }
    if (o->env_set) envb = build_env_block(o);

    if (pipe_in) { if (!make_pipe(&cin, &c->in_w, 1)) { win_error(err, errsz, "pipe", GetLastError()); goto done; } }
    else cin = dup_inheritable(STD_INPUT_HANDLE, 0);
    if (o->out_mode == IO_PIPE) { if (!make_pipe(&cout, &c->out_r, 0)) { win_error(err, errsz, "pipe", GetLastError()); goto done; } }
    else if (o->out_mode == IO_INHERIT) cout = dup_inheritable(STD_OUTPUT_HANDLE, 1);
    else cout = open_null(1);
    if (o->err_mode == IO_PIPE) { if (!make_pipe(&cerr, &c->err_r, 0)) { win_error(err, errsz, "pipe", GetLastError()); goto done; } }
    else if (o->err_mode == IO_STDOUT) {
        if (!DuplicateHandle(GetCurrentProcess(), cout, GetCurrentProcess(), &cerr, 0, TRUE, DUPLICATE_SAME_ACCESS)) cerr = open_null(1);
    }
    else if (o->err_mode == IO_INHERIT) cerr = dup_inheritable(STD_ERROR_HANDLE, 1);
    else cerr = open_null(1);

    /* Inherit only these three handles, so programs started at the same
       time from other threads do not keep each other's pipes open. */
    HANDLE list[3] = { cin, cout, cerr };
    SIZE_T asz = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &asz);
    attrs = malloc(asz);
    if (!attrs || !InitializeProcThreadAttributeList(attrs, 1, 0, &asz) ||
        !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, list, sizeof(list), NULL, NULL)) {
        win_error(err, errsz, "process attributes", GetLastError());
        free(attrs); attrs = NULL;
        goto done;
    }
    STARTUPINFOEXW si;
    memset(&si, 0, sizeof(si));
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = cin;
    si.StartupInfo.hStdOutput = cout;
    si.StartupInfo.hStdError = cerr;
    si.lpAttributeList = attrs;

    c->job = CreateJobObjectW(NULL, NULL);
    if (c->job && kill_on_close) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
        memset(&li, 0, sizeof(li));
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(c->job, JobObjectExtendedLimitInformation, &li, sizeof(li));
    }

    PROCESS_INFORMATION pi;
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED;
    if (!CreateProcessW(app, wcmd, NULL, NULL, TRUE, flags, envb, wcwd, &si.StartupInfo, &pi)) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) snprintf(err, errsz, "program not found: %s", o->argv ? o->argv[0] : "cmd.exe");
        else win_error(err, errsz, o->argv ? o->argv[0] : "cmd.exe", e);
        goto done;
    }
    if (c->job) AssignProcessToJobObject(c->job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    c->proc = pi.hProcess;
    c->pid = pi.dwProcessId;
    ok = 1;

done:
    if (attrs) { DeleteProcThreadAttributeList(attrs); free(attrs); }
    if (cin && cin != INVALID_HANDLE_VALUE) CloseHandle(cin);
    if (cout && cout != INVALID_HANDLE_VALUE) CloseHandle(cout);
    if (cerr && cerr != INVALID_HANDLE_VALUE) CloseHandle(cerr);
    if (!ok) {
        if (c->in_w) CloseHandle(c->in_w);
        if (c->out_r) CloseHandle(c->out_r);
        if (c->err_r) CloseHandle(c->err_r);
        if (c->job) CloseHandle(c->job);
        memset(c, 0, sizeof(*c));
    }
    free(app); free(wcmd); free(wcwd); free(envb); buf_free(&cmd);
    return ok;
}

typedef struct { HANDLE h; Buf buf; } ReadJob;
typedef struct { HANDLE h; const char *p; size_t n; } WriteJob;

static DWORD WINAPI reader_main(LPVOID arg) {
    ReadJob *j = arg;
    char tmp[65536];
    DWORD got;
    while (ReadFile(j->h, tmp, sizeof(tmp), &got, NULL) && got > 0) buf_add(&j->buf, tmp, got);
    return 0;
}

static DWORD WINAPI writer_main(LPVOID arg) {
    WriteJob *j = arg;
    size_t at = 0;
    DWORD put;
    while (at < j->n) {
        DWORD chunk = (DWORD)((j->n - at) > 65536 ? 65536 : (j->n - at));
        if (!WriteFile(j->h, j->p + at, chunk, &put, NULL)) break;
        at += put;
    }
    CloseHandle(j->h);
    j->h = NULL;
    return 0;
}

static Value process_run(int argc, Value *args) {
    Opts o;
    if (!parse_args("process.run", argc, args, 0, &o)) return value_none();
    char err[1200];
    Child c;
    if (!start_child(&o, o.has_input, 0, &c, err, sizeof(err))) { opts_free(&o); return proc_error("process.run: %s", err); }

    fflush(stdout); fflush(stderr);
    glow_gil_release();
    ReadJob out = { c.out_r, { 0 } }, er = { c.err_r, { 0 } };
    WriteJob in = { c.in_w, o.input, o.input_len };
    HANDLE threads[3];
    int nt = 0;
    if (c.out_r) threads[nt++] = CreateThread(NULL, 0, reader_main, &out, 0, NULL);
    if (c.err_r) threads[nt++] = CreateThread(NULL, 0, reader_main, &er, 0, NULL);
    if (c.in_w) threads[nt++] = CreateThread(NULL, 0, writer_main, &in, 0, NULL);

    int timed_out = 0;
    double deadline = o.timeout >= 0 ? now_seconds() + o.timeout : 0;
    DWORD wait_ms = o.timeout >= 0 ? (DWORD)(o.timeout * 1000.0 + 0.5) : INFINITE;
    if (WaitForSingleObject(c.proc, wait_ms) == WAIT_TIMEOUT) {
        timed_out = 1;
        if (c.job) TerminateJobObject(c.job, 1); else TerminateProcess(c.proc, 1);
        WaitForSingleObject(c.proc, INFINITE);
    }
    /* The pipes close once every program holding them has exited. */
    DWORD rest = INFINITE;
    if (o.timeout >= 0) { double left = deadline - now_seconds(); rest = left > 0 ? (DWORD)(left * 1000.0) : 0; }
    if (nt && WaitForMultipleObjects((DWORD)nt, threads, TRUE, timed_out ? 2000 : rest) == WAIT_TIMEOUT) {
        timed_out = 1;
        if (c.job) TerminateJobObject(c.job, 1);
        for (int i = 0; i < nt; i++) CancelSynchronousIo(threads[i]);
        WaitForMultipleObjects((DWORD)nt, threads, TRUE, 5000);
    }
    for (int i = 0; i < nt; i++) CloseHandle(threads[i]);
    DWORD code = 0;
    GetExitCodeProcess(c.proc, &code);
    if (in.h) CloseHandle(in.h);
    if (c.out_r) CloseHandle(c.out_r);
    if (c.err_r) CloseHandle(c.err_r);
    CloseHandle(c.proc);
    if (c.job) CloseHandle(c.job);
    glow_gil_acquire();

    Dict *d = dict_new(8);
    dict_set(d, "code", value_int((int64_t)(int32_t)code));
    dict_set(d, "stdout", string_n(out.buf.p, out.buf.n));
    dict_set(d, "stderr", string_n(er.buf.p, er.buf.n));
    dict_set(d, "ok", value_bool(code == 0 && !timed_out));
    dict_set(d, "timed_out", value_bool(timed_out));
    buf_free(&out.buf); buf_free(&er.buf);
    opts_free(&o);
    return value_dict(d);
}

/* ---- started programs ---- */

typedef struct {
    int used, gen;
    Child c;
    HANDLE reader;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    Buf buf;
    size_t pos;
    int eof;
} Proc;

static DWORD WINAPI start_reader(LPVOID arg) {
    Proc *p = arg;
    char tmp[65536];
    DWORD got;
    for (;;) {
        BOOL ok = ReadFile(p->c.out_r, tmp, sizeof(tmp), &got, NULL);
        EnterCriticalSection(&p->cs);
        if (!ok || got == 0) p->eof = 1;
        else buf_add(&p->buf, tmp, got);
        WakeAllConditionVariable(&p->cv);
        LeaveCriticalSection(&p->cs);
        if (!ok || got == 0) return 0;
    }
}

#else
/* ================================================================
 *  POSIX
 * ================================================================ */

static int is_exec_file(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode) && access(p, X_OK) == 0;
}

/* Full path of a program: as given when it contains '/', else from PATH. */
static char *resolve_program(const char *name, const Opts *o) {
    if (strchr(name, '/')) return strdup(name);
    int over = 0;
    const char *path = o ? env_override(o, "PATH", &over) : NULL;
    if (!over) path = getenv("PATH");
    if (!path) path = "/usr/local/bin:/usr/bin:/bin";
    size_t nlen = strlen(name);
    for (const char *d = path;;) {
        const char *end = strchr(d, ':');
        size_t len = end ? (size_t)(end - d) : strlen(d);
        char *cand = malloc(len + nlen + 3);
        if (len) { memcpy(cand, d, len); cand[len] = '/'; memcpy(cand + len + 1, name, nlen + 1); }
        else { cand[0] = '.'; cand[1] = '/'; memcpy(cand + 2, name, nlen + 1); }
        if (is_exec_file(cand)) return cand;
        free(cand);
        if (!end) break;
        d = end + 1;
    }
    return NULL;
}

/* The environment for the child: environ with the overrides applied. */
static char **build_envp(const Opts *o) {
    size_t count = 0;
    for (char **e = environ; e && *e; e++) count++;
    char **out = calloc(count + (size_t)o->env_n + 1, sizeof(char *));
    size_t n = 0;
    for (char **e = environ; e && *e; e++) {
        int replaced = 0;
        for (int i = 0; i < o->env_n && !replaced; i++) {
            size_t kl = strlen(o->env_keys[i]);
            if (!strncmp(*e, o->env_keys[i], kl) && (*e)[kl] == '=') replaced = 1;
        }
        if (!replaced) out[n++] = strdup(*e);
    }
    for (int i = 0; i < o->env_n; i++) {
        if (!o->env_vals[i]) continue;
        size_t len = strlen(o->env_keys[i]) + strlen(o->env_vals[i]) + 2;
        out[n] = malloc(len);
        snprintf(out[n], len, "%s=%s", o->env_keys[i], o->env_vals[i]);
        n++;
    }
    out[n] = NULL;
    return out;
}

static void free_strv(char **v) {
    if (!v) return;
    for (char **p = v; *p; p++) free(*p);
    free(v);
}

typedef struct {
    pid_t pid;
    int in_w, out_r, err_r;   /* parent's ends; -1 when not piped */
} Child;

static void set_cloexec(int fd) { fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC); }

static int make_pipe(int fds[2]) {
#ifdef __linux__
    return pipe2(fds, O_CLOEXEC) == 0;
#else
    if (pipe(fds) != 0) return 0;
    set_cloexec(fds[0]);
    set_cloexec(fds[1]);
    return 1;
#endif
}

static void close_fd(int *fd) { if (*fd >= 0) { close(*fd); *fd = -1; } }

static int start_child(const Opts *o, int pipe_in, int kill_on_close, Child *c, char *err, size_t errsz) {
    (void)kill_on_close;
    c->pid = -1; c->in_w = c->out_r = c->err_r = -1;
    char *path = NULL, **argv = NULL, **envp = NULL;
    char *shell_argv[4] = { "sh", "-c", NULL, NULL };
    int in_p[2] = { -1, -1 }, out_p[2] = { -1, -1 }, err_p[2] = { -1, -1 }, exec_p[2] = { -1, -1 };
    int ok = 0;

    if (o->shell) { path = strdup("/bin/sh"); shell_argv[2] = o->shell; argv = shell_argv; }
    else {
        path = resolve_program(o->argv[0], o);
        if (!path) { snprintf(err, errsz, "program not found: %s", o->argv[0]); goto done; }
        argv = o->argv;
    }
    if (o->cwd) {
        struct stat st;
        if (stat(o->cwd, &st) != 0 || !S_ISDIR(st.st_mode)) { snprintf(err, errsz, "cwd is not a folder: %s", o->cwd); goto done; }
    }
    if (o->env_set) envp = build_envp(o);
    if ((pipe_in && !make_pipe(in_p)) || (o->out_mode == IO_PIPE && !make_pipe(out_p)) ||
        (o->err_mode == IO_PIPE && !make_pipe(err_p)) || !make_pipe(exec_p)) {
        snprintf(err, errsz, "pipe: %s", strerror(errno));
        goto done;
    }

    pid_t pid = fork();
    if (pid < 0) { snprintf(err, errsz, "fork: %s", strerror(errno)); goto done; }
    if (pid == 0) {
        /* Child: only async-signal-safe calls until exec. Its own process
           group lets a timeout kill everything it starts. */
        int stage = 0, e;
        setpgid(0, 0);
        signal(SIGPIPE, SIG_DFL);
        if (in_p[0] >= 0) dup2(in_p[0], 0);
        if (out_p[1] >= 0) dup2(out_p[1], 1);
        else if (o->out_mode == IO_NULL) { int fd = open("/dev/null", O_WRONLY); if (fd >= 0) dup2(fd, 1); }
        if (err_p[1] >= 0) dup2(err_p[1], 2);
        else if (o->err_mode == IO_STDOUT) dup2(1, 2);
        else if (o->err_mode == IO_NULL) { int fd = open("/dev/null", O_WRONLY); if (fd >= 0) dup2(fd, 2); }
        if (o->cwd && chdir(o->cwd) != 0) { stage = 1; goto fail; }
        execve(path, argv, envp ? envp : environ);
    fail:
        e = errno;
        { int msg[2] = { stage, e }; ssize_t w = write(exec_p[1], msg, sizeof(msg)); (void)w; }
        _exit(127);
    }

    close_fd(&in_p[0]); close_fd(&out_p[1]); close_fd(&err_p[1]); close_fd(&exec_p[1]);
    int msg[2];
    ssize_t got;
    do got = read(exec_p[0], msg, sizeof(msg)); while (got < 0 && errno == EINTR);
    if (got == (ssize_t)sizeof(msg)) {
        int st;
        waitpid(pid, &st, 0);
        const char *what = msg[0] == 1 ? o->cwd : (o->argv ? o->argv[0] : "/bin/sh");
        if (msg[0] == 0 && (msg[1] == ENOENT || msg[1] == ENOTDIR)) snprintf(err, errsz, "program not found: %s", what);
        else snprintf(err, errsz, "%s: %s", what, strerror(msg[1]));
        goto done;
    }
    c->pid = pid;
    c->in_w = in_p[1]; in_p[1] = -1;
    c->out_r = out_p[0]; out_p[0] = -1;
    c->err_r = err_p[0]; err_p[0] = -1;
    ok = 1;

done:
    close_fd(&in_p[0]); close_fd(&in_p[1]); close_fd(&out_p[0]); close_fd(&out_p[1]);
    close_fd(&err_p[0]); close_fd(&err_p[1]); close_fd(&exec_p[0]); close_fd(&exec_p[1]);
    free(path);
    free_strv(envp);
    return ok;
}

static void kill_tree(pid_t pid) {
    if (pid <= 0) return;
    if (kill(-pid, SIGKILL) != 0) kill(pid, SIGKILL);
}

static int exit_code(int st) {
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return -WTERMSIG(st);
    return -1;
}

static Value process_run(int argc, Value *args) {
    Opts o;
    if (!parse_args("process.run", argc, args, 0, &o)) return value_none();
    char err[1200];
    Child c;
    if (!start_child(&o, o.has_input, 0, &c, err, sizeof(err))) { opts_free(&o); return proc_error("process.run: %s", err); }

    fflush(stdout); fflush(stderr);
    glow_gil_release();
    Buf out = { 0 }, er = { 0 };
    size_t in_at = 0;
    if (c.in_w >= 0) fcntl(c.in_w, F_SETFL, fcntl(c.in_w, F_GETFL) | O_NONBLOCK);
    int timed_out = 0;
    double deadline = o.timeout >= 0 ? now_seconds() + o.timeout : 0, grace_end = 0;
    char tmp[65536];
    while (c.in_w >= 0 || c.out_r >= 0 || c.err_r >= 0) {
        if (c.in_w >= 0 && in_at >= o.input_len) { close_fd(&c.in_w); continue; }
        struct pollfd fds[3];
        int nf = 0, iw = -1, io = -1, ie = -1;
        if (c.in_w >= 0) { iw = nf; fds[nf].fd = c.in_w; fds[nf].events = POLLOUT; nf++; }
        if (c.out_r >= 0) { io = nf; fds[nf].fd = c.out_r; fds[nf].events = POLLIN; nf++; }
        if (c.err_r >= 0) { ie = nf; fds[nf].fd = c.err_r; fds[nf].events = POLLIN; nf++; }
        int wait_ms = -1;
        if (timed_out) {
            double left = grace_end - now_seconds();
            if (left <= 0) break;
            wait_ms = (int)(left * 1000.0) + 1;
        } else if (o.timeout >= 0) {
            double left = deadline - now_seconds();
            if (left <= 0) {
                timed_out = 1;
                kill_tree(c.pid);
                close_fd(&c.in_w);
                grace_end = now_seconds() + 2.0;
                continue;
            }
            wait_ms = (int)(left * 1000.0) + 1;
        }
        int r = poll(fds, (nfds_t)nf, wait_ms);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (iw >= 0 && fds[iw].revents) {
            if (fds[iw].revents & (POLLERR | POLLHUP | POLLNVAL)) close_fd(&c.in_w);
            else {
                ssize_t w = write(c.in_w, o.input + in_at, o.input_len - in_at);
                if (w > 0) in_at += (size_t)w;
                else if (w < 0 && errno != EAGAIN && errno != EINTR) close_fd(&c.in_w);
            }
        }
        if (io >= 0 && fds[io].revents) {
            ssize_t n = read(c.out_r, tmp, sizeof(tmp));
            if (n > 0) buf_add(&out, tmp, (size_t)n);
            else if (n == 0 || (errno != EAGAIN && errno != EINTR)) close_fd(&c.out_r);
        }
        if (ie >= 0 && fds[ie].revents) {
            ssize_t n = read(c.err_r, tmp, sizeof(tmp));
            if (n > 0) buf_add(&er, tmp, (size_t)n);
            else if (n == 0 || (errno != EAGAIN && errno != EINTR)) close_fd(&c.err_r);
        }
    }
    close_fd(&c.in_w); close_fd(&c.out_r); close_fd(&c.err_r);
    int st = 0;
    /* The program may still be running after closing its output (or have
       none piped): the timeout still applies while waiting for it. */
    if (!timed_out && o.timeout >= 0) {
        int sleep_us = 500;
        for (;;) {
            pid_t r = waitpid(c.pid, &st, WNOHANG);
            if (r == c.pid || (r < 0 && errno != EINTR)) goto reaped;
            if (now_seconds() >= deadline) { timed_out = 1; break; }
            struct timespec ts = { 0, (long)sleep_us * 1000 };
            nanosleep(&ts, NULL);
            if (sleep_us < 20000) sleep_us *= 2;
        }
    }
    if (timed_out) kill_tree(c.pid);
    while (waitpid(c.pid, &st, 0) < 0 && errno == EINTR) {}
reaped:
    glow_gil_acquire();

    int code = exit_code(st);
    Dict *d = dict_new(8);
    dict_set(d, "code", value_int(code));
    dict_set(d, "stdout", string_n(out.p, out.n));
    dict_set(d, "stderr", string_n(er.p, er.n));
    dict_set(d, "ok", value_bool(code == 0 && !timed_out));
    dict_set(d, "timed_out", value_bool(timed_out));
    buf_free(&out); buf_free(&er);
    opts_free(&o);
    return value_dict(d);
}

typedef struct {
    int used, gen;
    Child c;
    Buf buf;
    size_t pos;
    int eof;
    int exited, code;
} Proc;

#endif

/* ================================================================
 *  Spawned-program handles (shared)
 * ================================================================ */

#define MAX_PROCS 1024
static Proc procs[MAX_PROCS];
static int procs_ready;

static void procs_cleanup(void);

static Proc *get_proc(int argc, Value *args, const char *fn) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT) { proc_error("%s: expected a process handle", fn); return NULL; }
    int64_t h = AS_INT(args[0]);
    int idx = (int)(h & 0xFFFF) - 1, gen = (int)(h >> 16);
    if (idx < 0 || idx >= MAX_PROCS || !procs[idx].used || procs[idx].gen != gen) {
        proc_error("%s: the process handle is closed or invalid", fn);
        return NULL;
    }
    return &procs[idx];
}

static double timeout_arg(int argc, Value *args, int i) {
    if (argc > i && is_number(args[i])) { double t = val_to_double(args[i]); return t < 0 ? 0 : t; }
    return -1;
}

static Value process_start(int argc, Value *args) {
    Opts o;
    if (!parse_args("process.start", argc, args, 1, &o)) return value_none();
    int idx = -1;
    for (int i = 0; i < MAX_PROCS; i++) if (!procs[i].used) { idx = i; break; }
    if (idx < 0) { opts_free(&o); return proc_error("process.start: too many open processes (close some with process.close)"); }
    if (!procs_ready) { procs_ready = 1; atexit(procs_cleanup); }
    Proc *p = &procs[idx];
    int gen = p->gen + 1;
    memset(p, 0, sizeof(*p));
    p->gen = gen & 0x7FFFFFF;
    char err[1200];
    if (!start_child(&o, 1, 1, &p->c, err, sizeof(err))) { opts_free(&o); return proc_error("process.start: %s", err); }
    opts_free(&o);
#ifdef _WIN32
    InitializeCriticalSection(&p->cs);
    InitializeConditionVariable(&p->cv);
    p->reader = CreateThread(NULL, 0, start_reader, p, 0, NULL);
#endif
    p->used = 1;
    return value_int(((int64_t)p->gen << 16) | (idx + 1));
}

/* Writes the whole text; false once the program has closed its input. */
static Value process_send(int argc, Value *args) {
    Proc *p = get_proc(argc, args, "process.send");
    if (!p) return value_none();
    if (argc < 2 || !is_str(args[1])) return proc_error("process.send: expected text to write");
    size_t n;
    char *data = dup_str(args[1], &n);
    int ok = 1;
    glow_gil_release();
#ifdef _WIN32
    if (!p->c.in_w) ok = 0;
    for (size_t at = 0; ok && at < n;) {
        DWORD put;
        DWORD chunk = (DWORD)((n - at) > 65536 ? 65536 : (n - at));
        if (!WriteFile(p->c.in_w, data + at, chunk, &put, NULL)) ok = 0;
        else at += put;
    }
#else
    if (p->c.in_w < 0) ok = 0;
    for (size_t at = 0; ok && at < n;) {
        ssize_t w = write(p->c.in_w, data + at, n - at);
        if (w > 0) at += (size_t)w;
        else if (w < 0 && errno == EINTR) continue;
        else ok = 0;
    }
#endif
    glow_gil_acquire();
    free(data);
    return value_bool(ok);
}

/* Take bytes [pos, pos+n) out of the buffer as a string. */
static char *take(Proc *p, size_t n, size_t skip, size_t *outn) {
    char *s = malloc(n + 1);
    memcpy(s, p->buf.p + p->pos, n);
    s[n] = '\0';
    *outn = n;
    p->pos += n + skip;
    if (p->pos == p->buf.n) p->pos = p->buf.n = 0;
    else if (p->pos > 65536 && p->pos * 2 > p->buf.n) {
        memmove(p->buf.p, p->buf.p + p->pos, p->buf.n - p->pos);
        p->buf.n -= p->pos;
        p->pos = 0;
    }
    return s;
}

/* Wait (with the GIL released) for a full line, the end of output, or the
   timeout. Returns the line without its line ending; NULL at the end of
   output (*timed is set when the timeout passed instead). */
static char *read_line_c(Proc *p, double timeout, int all, size_t *outn, int *timed) {
    double deadline = timeout >= 0 ? now_seconds() + timeout : 0;
    char *res = NULL;
    *timed = 0;
#ifdef _WIN32
    EnterCriticalSection(&p->cs);
#endif
    for (;;) {
        size_t avail = p->buf.n - p->pos;
        if (!all) {
            char *nl = avail ? memchr(p->buf.p + p->pos, '\n', avail) : NULL;
            if (nl) {
                size_t n = (size_t)(nl - (p->buf.p + p->pos));
                size_t keep = (n && nl[-1] == '\r') ? n - 1 : n;
                res = take(p, keep, n - keep + 1, outn);
                break;
            }
        }
        if (p->eof) {
            if (avail) res = take(p, avail, 0, outn);
            break;
        }
        double left = timeout >= 0 ? deadline - now_seconds() : -1;
        if (timeout >= 0 && left <= 0) { *timed = 1; break; }
#ifdef _WIN32
        SleepConditionVariableCS(&p->cv, &p->cs, timeout >= 0 ? (DWORD)(left * 1000.0) + 1 : INFINITE);
#else
        struct pollfd pf = { p->c.out_r, POLLIN, 0 };
        int r = poll(&pf, 1, timeout >= 0 ? (int)(left * 1000.0) + 1 : -1);
        if (r < 0 && errno != EINTR) { p->eof = 1; continue; }
        if (r > 0) {
            char tmp[65536];
            ssize_t n = read(p->c.out_r, tmp, sizeof(tmp));
            if (n > 0) buf_add(&p->buf, tmp, (size_t)n);
            else if (n == 0 || errno != EINTR) p->eof = 1;
        }
#endif
    }
#ifdef _WIN32
    LeaveCriticalSection(&p->cs);
#endif
    return res;
}

static Value read_common(int argc, Value *args, const char *fn, int all) {
    Proc *p = get_proc(argc, args, fn);
    if (!p) return value_none();
    double timeout = timeout_arg(argc, args, 1);
    size_t n = 0;
    int timed;
    glow_gil_release();
    char *s = read_line_c(p, timeout, all, &n, &timed);
    glow_gil_acquire();
    if (timed) return proc_error("%s: timed out after %g seconds", fn, timeout);
    if (!s) return all ? string_n("", 0) : value_none();
    Value v = string_n(s, n);
    free(s);
    return v;
}

/* The next line of output, or none when the program closed its output. */
static Value process_read_line(int argc, Value *args) { return read_common(argc, args, "process.read_line", 0); }

/* All remaining output (waits for the program to close its output). */
static Value process_read_all(int argc, Value *args) { return read_common(argc, args, "process.read_all", 1); }

static Value process_close_input(int argc, Value *args) {
    Proc *p = get_proc(argc, args, "process.close_input");
    if (!p) return value_none();
#ifdef _WIN32
    if (p->c.in_w) { CloseHandle(p->c.in_w); p->c.in_w = NULL; }
#else
    close_fd(&p->c.in_w);
#endif
    return value_none();
}

/* 1: exited (code in *code), 0: still running after the timeout. */
static int wait_c(Proc *p, double timeout, int *code) {
#ifdef _WIN32
    DWORD r = WaitForSingleObject(p->c.proc, timeout >= 0 ? (DWORD)(timeout * 1000.0 + 0.5) : INFINITE);
    if (r == WAIT_TIMEOUT) return 0;
    DWORD c = 0;
    GetExitCodeProcess(p->c.proc, &c);
    *code = (int)(int32_t)c;
    return 1;
#else
    if (p->exited) { *code = p->code; return 1; }
    double deadline = timeout >= 0 ? now_seconds() + timeout : 0;
    int sleep_us = 500;
    for (;;) {
        int st;
        pid_t r = waitpid(p->c.pid, &st, timeout >= 0 ? WNOHANG : 0);
        if (r == p->c.pid) { p->exited = 1; p->code = exit_code(st); *code = p->code; return 1; }
        if (r < 0 && errno != EINTR) { p->exited = 1; p->code = -1; *code = -1; return 1; }
        if (r == 0) {
            if (now_seconds() >= deadline) return 0;
            struct timespec ts = { 0, (long)sleep_us * 1000 };
            nanosleep(&ts, NULL);
            if (sleep_us < 20000) sleep_us *= 2;
        }
    }
#endif
}

/* The exit code, or none if the program is still running after timeout. */
static Value process_wait(int argc, Value *args) {
    Proc *p = get_proc(argc, args, "process.wait");
    if (!p) return value_none();
    double timeout = timeout_arg(argc, args, 1);
    int code = 0, done;
    glow_gil_release();
    done = wait_c(p, timeout, &code);
    glow_gil_acquire();
    return done ? value_int(code) : value_none();
}

static Value process_alive(int argc, Value *args) {
    Proc *p = get_proc(argc, args, "process.alive");
    if (!p) return value_none();
    int code;
    return value_bool(!wait_c(p, 0, &code));
}

static Value process_pid(int argc, Value *args) {
    Proc *p = get_proc(argc, args, "process.pid");
    if (!p) return value_none();
    return value_int((int64_t)p->c.pid);
}

static void kill_proc(Proc *p) {
#ifdef _WIN32
    if (p->c.job) TerminateJobObject(p->c.job, 1);
    else if (p->c.proc) TerminateProcess(p->c.proc, 1);
#else
    int code;
    if (!wait_c(p, 0, &code)) kill_tree(p->c.pid);
#endif
}

/* Kill the program and everything it started. */
static Value process_kill(int argc, Value *args) {
    Proc *p = get_proc(argc, args, "process.kill");
    if (!p) return value_none();
    kill_proc(p);
    return value_none();
}

static void close_proc(Proc *p) {
    kill_proc(p);
#ifdef _WIN32
    if (p->c.in_w) CloseHandle(p->c.in_w);
    WaitForSingleObject(p->c.proc, 5000);
    if (p->reader) { WaitForSingleObject(p->reader, 5000); CloseHandle(p->reader); }
    if (p->c.out_r) CloseHandle(p->c.out_r);
    CloseHandle(p->c.proc);
    if (p->c.job) CloseHandle(p->c.job);
    DeleteCriticalSection(&p->cs);
#else
    int code;
    close_fd(&p->c.in_w);
    close_fd(&p->c.out_r);
    wait_c(p, -1, &code);
#endif
    buf_free(&p->buf);
    int gen = p->gen;
    memset(p, 0, sizeof(*p));
    p->gen = gen;
}

/* Kill the program if it is still running and free the handle. */
static Value process_close(int argc, Value *args) {
    Proc *p = get_proc(argc, args, "process.close");
    if (!p) return value_none();
    glow_gil_release();
    close_proc(p);
    glow_gil_acquire();
    return value_none();
}

static void procs_cleanup(void) {
    for (int i = 0; i < MAX_PROCS; i++) if (procs[i].used) kill_proc(&procs[i]);
}

/* The current folder, as an absolute path. */
static Value process_cwd(int argc, Value *args) {
    (void)argc; (void)args;
#ifdef _WIN32
    DWORD n = GetCurrentDirectoryW(0, NULL);
    wchar_t *w = n ? malloc(n * sizeof(wchar_t)) : NULL;
    if (!w || !GetCurrentDirectoryW(n, w)) { free(w); return value_none(); }
    char *s = to_utf8(w);
    free(w);
#else
    char *s = getcwd(NULL, 0);
#endif
    if (!s) return value_none();
    Value v = value_string_copy(s);
    free(s);
    return v;
}

static Value process_find(int argc, Value *args) {
    if (argc < 1 || !is_str(args[0])) return proc_error("process.find: expected a program name");
    const char *name = AS_STRING(args[0]);
    if (!name[0]) return value_none();
#ifdef _WIN32
    wchar_t *w = resolve_program(name, NULL);
    if (!w) return value_none();
    char *s = to_utf8(w);
    free(w);
#else
    char *s = resolve_program(name, NULL);
    if (!s) return value_none();
#endif
    Value v = value_string_copy(s ? s : "");
    free(s);
    return v;
}

void glow_process_init(void) {
#ifndef _WIN32
    /* A program that exits while we write to it must not kill GlowLang. */
    struct sigaction sa;
    if (sigaction(SIGPIPE, NULL, &sa) == 0 && sa.sa_handler == SIG_DFL) signal(SIGPIPE, SIG_IGN);
#endif
    Dict *p = dict_new(16);
    dict_set(p, "run", native_func_new("run", process_run));
    dict_set(p, "start", native_func_new("start", process_start));
    dict_set(p, "send", native_func_new("send", process_send));
    dict_set(p, "read_line", native_func_new("read_line", process_read_line));
    dict_set(p, "read_all", native_func_new("read_all", process_read_all));
    dict_set(p, "close_input", native_func_new("close_input", process_close_input));
    dict_set(p, "wait", native_func_new("wait", process_wait));
    dict_set(p, "alive", native_func_new("alive", process_alive));
    dict_set(p, "pid", native_func_new("pid", process_pid));
    dict_set(p, "kill", native_func_new("kill", process_kill));
    dict_set(p, "close", native_func_new("close", process_close));
    dict_set(p, "find", native_func_new("find", process_find));
    dict_set(p, "cwd", native_func_new("cwd", process_cwd));
    /* The running glowlang, for starting GlowLang programs as workers. */
    char self[4096];
    dict_set(p, "executable", glow_self_exe_path(self, sizeof(self)) > 0 ? value_string_copy(self) : value_none());
    var_set("process", value_dict(p));
}
