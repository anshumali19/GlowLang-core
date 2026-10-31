/* Portable regular-expression engine for re_native.match / re_native.search.
 *
 * Supports the POSIX extended syntax GlowLang programs use plus the common
 * Perl shorthands: literals, `.`, bracket sets with ranges, negation and
 * [:class:] names, `^`, `$`, capturing groups `(...)`, named groups
 * `(?<name>...)` / `(?P<name>...)`, non-capturing `(?:...)`, alternation
 * `|`, greedy and lazy quantifiers `* + ? {m} {m,} {m,n}`, escapes
 * `\d \w \s` (and their negations), `\b \B`, `\n \t \r \f \v`, escaped
 * metacharacters, and a leading `(?i)` for case-insensitive matching.
 *
 * The pattern is compiled to a small instruction program and run by a
 * backtracking matcher that remembers which (split instruction, position)
 * states already failed, so matching is O(pattern * text) in the worst case
 * rather than exponential. Semantics are leftmost-first (like Perl/PCRE).
 * Group positions are recorded by SAVE instructions and undone on
 * backtracking; they never decide whether a state fails, so the memo of
 * failed states stays valid.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

enum { RX_CHAR, RX_ANY, RX_SET, RX_BOL, RX_EOL, RX_WORDB, RX_NWORDB, RX_SPLIT, RX_JMP, RX_MATCH, RX_SAVE };

typedef struct { unsigned char op; unsigned char c; int x, y; } RxInst;
typedef struct { uint32_t bits[8]; } RxSet;

enum { N_EMPTY, N_CHAR, N_ANY, N_SET, N_BOL, N_EOL, N_WORDB, N_NWORDB, N_CAT, N_ALT, N_REP, N_GROUP };
typedef struct RxNode {
    int type;
    int c;
    int set;
    int min, max, greedy;
    struct RxNode *a, *b;
} RxNode;

#define RX_MAX_REPEAT 1000
#define RX_MAX_PROGRAM 200000

typedef struct {
    const char *p;
    const char *err;
    RxNode **nodes; int nnodes, cap_nodes;
    RxSet *sets; int nsets, cap_sets;
    RxInst *prog; int nprog, cap_prog;
    int depth;
    int ngroups;            /* capturing groups so far */
    char **names;           /* names[i]: group i+1's name, or NULL */
    int icase;              /* (?i): letters match either case */
} RxCompiler;

#define RX_MAX_GROUPS 99

static RxNode *rx_node(RxCompiler *rc, int type) {
    if (rc->nnodes == rc->cap_nodes) {
        rc->cap_nodes = rc->cap_nodes ? rc->cap_nodes * 2 : 32;
        rc->nodes = realloc(rc->nodes, sizeof(RxNode*) * (size_t)rc->cap_nodes);
    }
    RxNode *n = calloc(1, sizeof(RxNode));
    n->type = type;
    rc->nodes[rc->nnodes++] = n;
    return n;
}

static int rx_new_set(RxCompiler *rc) {
    if (rc->nsets == rc->cap_sets) {
        rc->cap_sets = rc->cap_sets ? rc->cap_sets * 2 : 8;
        rc->sets = realloc(rc->sets, sizeof(RxSet) * (size_t)rc->cap_sets);
    }
    memset(&rc->sets[rc->nsets], 0, sizeof(RxSet));
    return rc->nsets++;
}

static void set_add(RxSet *s, int c) { s->bits[(c & 255) >> 5] |= 1u << (c & 31); }
static int set_has(const RxSet *s, int c) { return (s->bits[(c & 255) >> 5] >> (c & 31)) & 1; }
static void set_add_range(RxSet *s, int lo, int hi) { for (int c = lo; c <= hi; c++) set_add(s, c); }
static void set_invert(RxSet *s) { for (int i = 0; i < 8; i++) s->bits[i] = ~s->bits[i]; }

/* (?i): give every letter in the set its other case too. */
static void set_fold_case(RxSet *s) {
    for (int c = 'a'; c <= 'z'; c++) {
        int u = c - 32;
        if (set_has(s, c) || set_has(s, u)) { set_add(s, c); set_add(s, u); }
    }
}

static int is_word_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static void set_add_class(RxSet *s, char cls) {
    switch (cls) {
        case 'd': set_add_range(s, '0', '9'); break;
        case 'w': set_add_range(s, 'a', 'z'); set_add_range(s, 'A', 'Z'); set_add_range(s, '0', '9'); set_add(s, '_'); break;
        case 's': set_add(s, ' '); set_add(s, '\t'); set_add(s, '\n'); set_add(s, '\r'); set_add(s, '\f'); set_add(s, '\v'); break;
    }
}

/* Adds a [:name:] class; returns 0 if the name is unknown. */
static int set_add_posix(RxSet *s, const char *name, size_t len) {
#define IS(n) (len == sizeof(n) - 1 && strncmp(name, n, len) == 0)
    if (IS("alpha")) { set_add_range(s, 'a', 'z'); set_add_range(s, 'A', 'Z'); }
    else if (IS("digit")) set_add_range(s, '0', '9');
    else if (IS("alnum")) { set_add_range(s, 'a', 'z'); set_add_range(s, 'A', 'Z'); set_add_range(s, '0', '9'); }
    else if (IS("upper")) set_add_range(s, 'A', 'Z');
    else if (IS("lower")) set_add_range(s, 'a', 'z');
    else if (IS("space")) set_add_class(s, 's');
    else if (IS("blank")) { set_add(s, ' '); set_add(s, '\t'); }
    else if (IS("xdigit")) { set_add_range(s, '0', '9'); set_add_range(s, 'a', 'f'); set_add_range(s, 'A', 'F'); }
    else if (IS("word")) set_add_class(s, 'w');
    else if (IS("punct")) {
        for (int c = 33; c < 127; c++)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) set_add(s, c);
    }
    else if (IS("print")) set_add_range(s, 32, 126);
    else if (IS("graph")) set_add_range(s, 33, 126);
    else if (IS("cntrl")) { set_add_range(s, 0, 31); set_add(s, 127); }
    else return 0;
    return 1;
#undef IS
}

static int rx_escape_char(char c) {
    switch (c) {
        case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r';
        case 'f': return '\f'; case 'v': return '\v'; case '0': return 0;
        default: return (unsigned char)c;
    }
}

static RxNode *rx_parse_alt(RxCompiler *rc);

static RxNode *rx_parse_set(RxCompiler *rc) {
    /* rc->p points just past '[' */
    int si = rx_new_set(rc);
    RxSet *s = &rc->sets[si];
    int negate = 0;
    if (*rc->p == '^') { negate = 1; rc->p++; }
    int first = 1;
    while (*rc->p && (*rc->p != ']' || first)) {
        first = 0;
        int lo;
        if (rc->p[0] == '[' && rc->p[1] == ':') {
            const char *end = strstr(rc->p + 2, ":]");
            if (!end) { rc->err = "unterminated [: :] class"; return NULL; }
            s = &rc->sets[si];
            if (!set_add_posix(s, rc->p + 2, (size_t)(end - (rc->p + 2)))) { rc->err = "unknown [: :] class"; return NULL; }
            rc->p = end + 2;
            continue;
        }
        if (*rc->p == '\\' && rc->p[1]) {
            char e = rc->p[1];
            rc->p += 2;
            s = &rc->sets[si];
            if (e == 'd' || e == 'w' || e == 's') { set_add_class(s, e); continue; }
            if (e == 'D' || e == 'W' || e == 'S') {
                RxSet tmp; memset(&tmp, 0, sizeof(tmp)); set_add_class(&tmp, (char)(e + 32)); set_invert(&tmp);
                for (int i = 0; i < 8; i++) s->bits[i] |= tmp.bits[i];
                continue;
            }
            lo = rx_escape_char(e);
        } else {
            lo = (unsigned char)*rc->p++;
        }
        if (rc->p[0] == '-' && rc->p[1] && rc->p[1] != ']') {
            int hi;
            rc->p++;
            if (*rc->p == '\\' && rc->p[1]) { hi = rx_escape_char(rc->p[1]); rc->p += 2; }
            else hi = (unsigned char)*rc->p++;
            if (hi < lo) { rc->err = "invalid range in []"; return NULL; }
            set_add_range(&rc->sets[si], lo, hi);
        } else {
            set_add(&rc->sets[si], lo);
        }
    }
    if (*rc->p != ']') { rc->err = "unterminated []"; return NULL; }
    rc->p++;
    if (rc->icase) set_fold_case(&rc->sets[si]);
    if (negate) set_invert(&rc->sets[si]);
    RxNode *n = rx_node(rc, N_SET);
    n->set = si;
    return n;
}

/* A literal character; under (?i) a letter matches either case. */
static RxNode *rx_char_node(RxCompiler *rc, int c) {
    if (rc->icase && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
        int si = rx_new_set(rc);
        set_add(&rc->sets[si], c);
        set_fold_case(&rc->sets[si]);
        RxNode *n = rx_node(rc, N_SET);
        n->set = si;
        return n;
    }
    RxNode *n = rx_node(rc, N_CHAR);
    n->c = c;
    return n;
}

static RxNode *rx_parse_atom(RxCompiler *rc) {
    char c = *rc->p;
    if (c == '(') {
        rc->p++;
        int group = 0;            /* capturing group number, 0 for (?:...) */
        if (rc->p[0] == '?' && rc->p[1] == ':') {
            rc->p += 2;
        } else if (rc->p[0] == '?' && (rc->p[1] == '<' || (rc->p[1] == 'P' && rc->p[2] == '<'))) {
            rc->p += rc->p[1] == 'P' ? 3 : 2;
            const char *name = rc->p;
            while ((*rc->p >= 'a' && *rc->p <= 'z') || (*rc->p >= 'A' && *rc->p <= 'Z') ||
                   (*rc->p >= '0' && *rc->p <= '9') || *rc->p == '_') rc->p++;
            size_t nlen = (size_t)(rc->p - name);
            if (nlen == 0 || *rc->p != '>') { rc->err = "invalid group name"; return NULL; }
            rc->p++;
            if (rc->ngroups >= RX_MAX_GROUPS) { rc->err = "too many groups"; return NULL; }
            group = ++rc->ngroups;
            rc->names = realloc(rc->names, sizeof(char*) * (size_t)rc->ngroups);
            rc->names[group - 1] = malloc(nlen + 1);
            memcpy(rc->names[group - 1], name, nlen);
            rc->names[group - 1][nlen] = '\0';
            for (int i = 0; i < group - 1; i++)
                if (rc->names[i] && strcmp(rc->names[i], rc->names[group - 1]) == 0) { rc->err = "duplicate group name"; return NULL; }
        } else if (rc->p[0] == '?') {
            rc->err = "unsupported (? group ((?i) is only allowed at the start)";
            return NULL;
        } else {
            if (rc->ngroups >= RX_MAX_GROUPS) { rc->err = "too many groups"; return NULL; }
            group = ++rc->ngroups;
            rc->names = realloc(rc->names, sizeof(char*) * (size_t)rc->ngroups);
            rc->names[group - 1] = NULL;
        }
        if (++rc->depth > 200) { rc->err = "pattern nested too deeply"; return NULL; }
        RxNode *inner = rx_parse_alt(rc);
        rc->depth--;
        if (!inner) return NULL;
        if (*rc->p != ')') { rc->err = "missing )"; return NULL; }
        rc->p++;
        if (!group) return inner;
        RxNode *g = rx_node(rc, N_GROUP);
        g->a = inner;
        g->c = group;
        return g;
    }
    if (c == '[') { rc->p++; return rx_parse_set(rc); }
    if (c == '.') { rc->p++; return rx_node(rc, N_ANY); }
    if (c == '^') { rc->p++; return rx_node(rc, N_BOL); }
    if (c == '$') { rc->p++; return rx_node(rc, N_EOL); }
    if (c == '\\') {
        char e = rc->p[1];
        if (!e) { rc->err = "trailing backslash"; return NULL; }
        rc->p += 2;
        if (e == 'b') return rx_node(rc, N_WORDB);
        if (e == 'B') return rx_node(rc, N_NWORDB);
        if (e == 'd' || e == 'w' || e == 's' || e == 'D' || e == 'W' || e == 'S') {
            int si = rx_new_set(rc);
            set_add_class(&rc->sets[si], (char)(e | 32));
            if (e < 'a') set_invert(&rc->sets[si]);
            RxNode *n = rx_node(rc, N_SET);
            n->set = si;
            return n;
        }
        return rx_char_node(rc, rx_escape_char(e));
    }
    if (c == '*' || c == '+' || c == '?' || c == '{') { rc->err = "quantifier with nothing to repeat"; return NULL; }
    rc->p++;
    return rx_char_node(rc, (unsigned char)c);
}

static int rx_parse_int(RxCompiler *rc, int *out) {
    if (*rc->p < '0' || *rc->p > '9') return 0;
    long v = 0;
    while (*rc->p >= '0' && *rc->p <= '9') { v = v * 10 + (*rc->p - '0'); if (v > 100000) v = 100000; rc->p++; }
    *out = (int)v;
    return 1;
}

static RxNode *rx_parse_rep(RxCompiler *rc) {
    RxNode *atom = rx_parse_atom(rc);
    if (!atom) return NULL;
    for (;;) {
        int min, max;
        char c = *rc->p;
        if (c == '*') { min = 0; max = -1; rc->p++; }
        else if (c == '+') { min = 1; max = -1; rc->p++; }
        else if (c == '?') { min = 0; max = 1; rc->p++; }
        else if (c == '{') {
            const char *save = rc->p;
            rc->p++;
            if (!rx_parse_int(rc, &min)) { rc->p = save; break; }
            max = min;
            if (*rc->p == ',') {
                rc->p++;
                if (!rx_parse_int(rc, &max)) max = -1;
            }
            if (*rc->p != '}') { rc->p = save; break; }
            rc->p++;
            if (min > RX_MAX_REPEAT || max > RX_MAX_REPEAT) { rc->err = "repetition count too large"; return NULL; }
            if (max != -1 && max < min) { rc->err = "invalid {m,n} range"; return NULL; }
        } else break;
        int greedy = 1;
        if (*rc->p == '?') { greedy = 0; rc->p++; }
        RxNode *r = rx_node(rc, N_REP);
        r->a = atom; r->min = min; r->max = max; r->greedy = greedy;
        atom = r;
    }
    return atom;
}

static RxNode *rx_parse_cat(RxCompiler *rc) {
    RxNode *left = NULL;
    while (*rc->p && *rc->p != '|' && *rc->p != ')') {
        RxNode *r = rx_parse_rep(rc);
        if (!r) return NULL;
        if (!left) left = r;
        else { RxNode *c = rx_node(rc, N_CAT); c->a = left; c->b = r; left = c; }
    }
    return left ? left : rx_node(rc, N_EMPTY);
}

static RxNode *rx_parse_alt(RxCompiler *rc) {
    RxNode *left = rx_parse_cat(rc);
    if (!left) return NULL;
    while (*rc->p == '|') {
        rc->p++;
        RxNode *right = rx_parse_cat(rc);
        if (!right) return NULL;
        RxNode *a = rx_node(rc, N_ALT); a->a = left; a->b = right; left = a;
    }
    return left;
}

static int rx_emit(RxCompiler *rc, int op, int c, int x, int y) {
    if (rc->nprog >= RX_MAX_PROGRAM) { rc->err = "pattern too large"; return -1; }
    if (rc->nprog == rc->cap_prog) {
        rc->cap_prog = rc->cap_prog ? rc->cap_prog * 2 : 64;
        rc->prog = realloc(rc->prog, sizeof(RxInst) * (size_t)rc->cap_prog);
    }
    RxInst *in = &rc->prog[rc->nprog];
    in->op = (unsigned char)op; in->c = (unsigned char)c; in->x = x; in->y = y;
    return rc->nprog++;
}

static int rx_gen(RxCompiler *rc, RxNode *n) {
    if (rc->err) return 0;
    switch (n->type) {
        case N_EMPTY: return 1;
        case N_CHAR: return rx_emit(rc, RX_CHAR, n->c, 0, 0) >= 0;
        case N_ANY: return rx_emit(rc, RX_ANY, 0, 0, 0) >= 0;
        case N_SET: return rx_emit(rc, RX_SET, 0, n->set, 0) >= 0;
        case N_BOL: return rx_emit(rc, RX_BOL, 0, 0, 0) >= 0;
        case N_EOL: return rx_emit(rc, RX_EOL, 0, 0, 0) >= 0;
        case N_WORDB: return rx_emit(rc, RX_WORDB, 0, 0, 0) >= 0;
        case N_NWORDB: return rx_emit(rc, RX_NWORDB, 0, 0, 0) >= 0;
        case N_CAT: return rx_gen(rc, n->a) && rx_gen(rc, n->b);
        case N_GROUP:
            return rx_emit(rc, RX_SAVE, 0, 2 * n->c, 0) >= 0 && rx_gen(rc, n->a) &&
                   rx_emit(rc, RX_SAVE, 0, 2 * n->c + 1, 0) >= 0;
        case N_ALT: {
            int split = rx_emit(rc, RX_SPLIT, 0, 0, 0);
            if (split < 0) return 0;
            rc->prog[split].x = rc->nprog;
            if (!rx_gen(rc, n->a)) return 0;
            int jmp = rx_emit(rc, RX_JMP, 0, 0, 0);
            if (jmp < 0) return 0;
            rc->prog[split].y = rc->nprog;
            if (!rx_gen(rc, n->b)) return 0;
            rc->prog[jmp].x = rc->nprog;
            return 1;
        }
        case N_REP: {
            for (int i = 0; i < n->min; i++) if (!rx_gen(rc, n->a)) return 0;
            if (n->max == -1) {
                int split = rx_emit(rc, RX_SPLIT, 0, 0, 0);
                if (split < 0) return 0;
                int body = rc->nprog;
                if (!rx_gen(rc, n->a)) return 0;
                if (rx_emit(rc, RX_JMP, 0, split, 0) < 0) return 0;
                int out = rc->nprog;
                if (n->greedy) { rc->prog[split].x = body; rc->prog[split].y = out; }
                else { rc->prog[split].x = out; rc->prog[split].y = body; }
            } else {
                int opt = n->max - n->min;
                int *splits = malloc(sizeof(int) * (size_t)(opt > 0 ? opt : 1));
                for (int i = 0; i < opt; i++) {
                    splits[i] = rx_emit(rc, RX_SPLIT, 0, 0, 0);
                    if (splits[i] < 0) { free(splits); return 0; }
                    rc->prog[splits[i]].x = rc->nprog; /* provisional: body */
                    if (!rx_gen(rc, n->a)) { free(splits); return 0; }
                }
                int out = rc->nprog;
                for (int i = 0; i < opt; i++) {
                    int body = splits[i] + 1;
                    if (n->greedy) { rc->prog[splits[i]].x = body; rc->prog[splits[i]].y = out; }
                    else { rc->prog[splits[i]].x = out; rc->prog[splits[i]].y = body; }
                }
                free(splits);
            }
            return 1;
        }
    }
    return 0;
}

/* A stack entry: a branch to try (pc >= 0, at pos), or, with pc < 0, a
   group position to restore on backtracking (slot -1 - pc, old value pos). */
typedef struct { int pc, pos; } RxState;

struct GlowRegex {
    RxInst *prog; int nprog;
    RxSet *sets;
    int ngroups;
    char **names;
    int anchored;               /* the program starts with ^ */
    int *split_id; int nsplit;  /* failure memo index of each SPLIT */
};
typedef struct GlowRegex GlowRegex;

/* Matching scratch for one regex over one text, reused by successive
   searches (findall, replace, split). */
typedef struct {
    const GlowRegex *re;
    const char *text; int len;
    uint8_t *visited;           /* (split, pos) states already explored */
    size_t *touched; size_t ntouched, cap_touched;
    RxState *stack; int stack_cap;
} RxMatcher;

/* Searches text from position `from` on; on a match returns 1 and fills
   caps[0..2*(ngroups+1)) with start/end positions (-1: group unset). */
static int rx_exec(RxMatcher *m, int from, int *caps) {
    const GlowRegex *re = m->re;
    const RxInst *prog = re->prog;
    const char *text = m->text;
    int len = m->len;
    int ncap = 2 * (re->ngroups + 1);
    /* States explored by an earlier search may lie on that search's
       successful path, so they say nothing about this one: forget them. */
    for (size_t i = 0; i < m->ntouched; i++) m->visited[m->touched[i] >> 3] = 0;
    m->ntouched = 0;
    for (int start = from; start <= len; start++) {
        if (re->anchored && start > 0) break;
        for (int i = 0; i < ncap; i++) caps[i] = -1;
        int sp = 0;
        m->stack[sp].pc = 0; m->stack[sp].pos = start; sp++;
        while (sp > 0) {
            sp--;
            int pc = m->stack[sp].pc, pos = m->stack[sp].pos;
            if (pc < 0) { caps[-1 - pc] = pos; continue; }
            for (;;) {
                const RxInst *in = &prog[pc];
                if (in->op == RX_CHAR) {
                    if (pos < len && (unsigned char)text[pos] == in->c) { pc++; pos++; continue; }
                    break;
                } else if (in->op == RX_ANY) {
                    if (pos < len) { pc++; pos++; continue; }
                    break;
                } else if (in->op == RX_SET) {
                    if (pos < len && set_has(&re->sets[in->x], (unsigned char)text[pos])) { pc++; pos++; continue; }
                    break;
                } else if (in->op == RX_BOL) {
                    if (pos == 0) { pc++; continue; }
                    break;
                } else if (in->op == RX_EOL) {
                    if (pos == len) { pc++; continue; }
                    break;
                } else if (in->op == RX_WORDB || in->op == RX_NWORDB) {
                    int before = pos > 0 && is_word_char((unsigned char)text[pos - 1]);
                    int after = pos < len && is_word_char((unsigned char)text[pos]);
                    int at = before != after;
                    if ((in->op == RX_WORDB) == at) { pc++; continue; }
                    break;
                } else if (in->op == RX_JMP) {
                    pc = in->x;
                    continue;
                } else if (in->op == RX_SAVE || in->op == RX_SPLIT) {
                    if (in->op == RX_SPLIT) {
                        size_t bit = (size_t)re->split_id[pc] * (size_t)(len + 1) + (size_t)pos;
                        if (m->visited[bit >> 3] & (1u << (bit & 7))) break;
                        m->visited[bit >> 3] |= (uint8_t)(1u << (bit & 7));
                        if (m->ntouched == m->cap_touched) {
                            m->cap_touched = m->cap_touched ? m->cap_touched * 2 : 64;
                            m->touched = realloc(m->touched, sizeof(size_t) * m->cap_touched);
                        }
                        m->touched[m->ntouched++] = bit;
                    }
                    if (sp == m->stack_cap) {
                        m->stack_cap *= 2;
                        m->stack = realloc(m->stack, sizeof(RxState) * (size_t)m->stack_cap);
                    }
                    if (in->op == RX_SPLIT) {
                        m->stack[sp].pc = in->y; m->stack[sp].pos = pos; sp++;
                        pc = in->x;
                    } else {
                        m->stack[sp].pc = -1 - in->x; m->stack[sp].pos = caps[in->x]; sp++;
                        caps[in->x] = pos;
                        pc++;
                    }
                    continue;
                } else { /* RX_MATCH */
                    caps[0] = start;
                    caps[1] = pos;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void rx_compiler_free(RxCompiler *rc, int keep_program) {
    for (int i = 0; i < rc->nnodes; i++) free(rc->nodes[i]);
    free(rc->nodes);
    if (!keep_program) {
        free(rc->sets);
        free(rc->prog);
        for (int i = 0; i < rc->ngroups; i++) free(rc->names ? rc->names[i] : NULL);
        free(rc->names);
    }
}

/* Compiles pattern; returns NULL (with a message in err) if it is invalid. */
GlowRegex *glow_regex_compile(const char *pattern, char *err, size_t errlen) {
    RxCompiler rc;
    memset(&rc, 0, sizeof(rc));
    rc.p = pattern;
    if (strncmp(rc.p, "(?i)", 4) == 0) { rc.icase = 1; rc.p += 4; }
    RxNode *root = rx_parse_alt(&rc);
    if (root && !rc.err && *rc.p == ')') rc.err = "unmatched )";
    if (root && !rc.err) {
        rx_gen(&rc, root);
        if (!rc.err) rx_emit(&rc, RX_MATCH, 0, 0, 0);
    }
    if (!root || rc.err) {
        if (err && errlen) snprintf(err, errlen, "invalid regular expression: %s", rc.err ? rc.err : "syntax error");
        rx_compiler_free(&rc, 0);
        return NULL;
    }
    GlowRegex *re = calloc(1, sizeof(GlowRegex));
    re->prog = rc.prog; re->nprog = rc.nprog;
    re->sets = rc.sets;
    re->ngroups = rc.ngroups;
    re->names = rc.names;
    re->anchored = rc.nprog > 0 && rc.prog[0].op == RX_BOL;
    re->split_id = malloc(sizeof(int) * (size_t)rc.nprog);
    for (int i = 0; i < rc.nprog; i++) re->split_id[i] = rc.prog[i].op == RX_SPLIT ? re->nsplit++ : -1;
    rx_compiler_free(&rc, 1);
    return re;
}

void glow_regex_free(GlowRegex *re) {
    if (!re) return;
    free(re->prog); free(re->sets); free(re->split_id);
    for (int i = 0; i < re->ngroups; i++) free(re->names[i]);
    free(re->names);
    free(re);
}

int glow_regex_groups(const GlowRegex *re) { return re->ngroups; }

/* Group i's name (1-based), or NULL for an unnamed group. */
const char *glow_regex_group_name(const GlowRegex *re, int i) {
    return (i >= 1 && i <= re->ngroups) ? re->names[i - 1] : NULL;
}

/* Calls on_match for each match in text, left to right, until it returns
   nonzero or `limit` matches (0: no limit) were reported. After an empty
   match the next search starts one character later, so it never loops.
   caps holds 2*(groups+1) positions as for rx_exec. Returns the number of
   matches reported. */
int glow_regex_each(const GlowRegex *re, const char *text, int len, int limit,
                    int (*on_match)(void *ctx, const int *caps), void *ctx) {
    RxMatcher m;
    memset(&m, 0, sizeof(m));
    m.re = re; m.text = text; m.len = len;
    m.visited = calloc((size_t)re->nsplit * (size_t)(len + 1) / 8 + 1, 1);
    m.stack_cap = 64;
    m.stack = malloc(sizeof(RxState) * (size_t)m.stack_cap);
    int *caps = malloc(sizeof(int) * (size_t)(2 * (re->ngroups + 1)));
    int count = 0, pos = 0;
    while (pos <= len && (limit <= 0 || count < limit)) {
        if (!rx_exec(&m, pos, caps)) break;
        count++;
        if (on_match(ctx, caps)) break;
        pos = caps[1] > caps[0] ? caps[1] : caps[1] + 1;
    }
    free(caps); free(m.stack); free(m.visited); free(m.touched);
    return count;
}

static int rx_first(void *ctx, const int *caps) {
    int *span = ctx;
    span[0] = caps[0]; span[1] = caps[1];
    return 1;
}

/* Returns 1 on a match (filling the span), 0 on no match, -1 if the pattern
   is invalid (with a message in err). */
int glow_regex_search(const char *pattern, const char *text, int *match_start, int *match_len,
                      char *err, size_t errlen) {
    GlowRegex *re = glow_regex_compile(pattern, err, errlen);
    if (!re) return -1;
    int span[2];
    int found = glow_regex_each(re, text, (int)strlen(text), 1, rx_first, span);
    glow_regex_free(re);
    if (!found) return 0;
    *match_start = span[0];
    *match_len = span[1] - span[0];
    return 1;
}
