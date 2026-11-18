%code requires {
    #include "glowlang.h"
}

%{#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#define access _access
#else
#ifndef _WIN32
#include <unistd.h>
#endif
#endif
#include "glowlang.h"
#include "lsp.h"
#include "version.h"

extern int yylex(void);
extern void lexer_cleanup(void);
extern void lexer_block_pop(void);
extern void lexer_block_reset(void);

#ifdef _WIN32
#include <windows.h>
#endif
/* Callers pass max_len one short of their buffer and NUL-terminate at the
   returned length themselves. */
static int get_self_exe_path(char *path, size_t max_len) {
    return glow_self_exe_path(path, max_len + 1);
}
/* A parameter with a default (`func f(a, b = 2)`) starts the body with
   `when b is none: b = 2`. A missing argument arrives as none, so the
   default is used both when it is left out and when none is passed. */
static Node *with_param_defaults(int count, char **names, struct Node **defaults, Node *body) {
    Node *prologue = NULL;
    for (int i = 0; i < count; i++) {
        if (!defaults || !defaults[i]) continue;
        Node *missing = newBinOp(NODE_EQ, newVarNode(names[i]), newVarNode(intern_string("none")));
        Node *set = newIfNode(missing, newAssignNode(names[i], defaults[i]), NULL);
        prologue = prologue ? newSeqNode(prologue, set) : set;
    }
    if (!prologue) return body;
    return body ? newSeqNode(prologue, body) : prologue;
}
/* A syntax error about a statement that starts on `line`. By the time a
   rule is reduced the lexer has read past the statement's newline, so
   yyerror alone would name the next line. */
static void error_at(int line, const char *msg) {
    int saved = line_num;
    if (line > 0) line_num = line;
    yyerror(msg);
    line_num = saved;
}
/* target = value. A slice target (xs[1:3] = items) calls the runtime's
   slice assignment; only names, x[i], x.name and slices can be assigned. */
static Node *assign_node(Node *target, Node *value) {
    if (target->type == NODE_VAR) return newAssignNode(target->name, value);
    if (target->type == NODE_INDEX) {
        target->type = NODE_INDEX_ASSIGN; target->third = value;
        return target;
    }
    if (target->type == NODE_SLICE) {
        Node **args = arena_alloc(sizeof(Node*) * 5);
        args[0] = target->left;
        args[1] = target->right ? target->right : newVarNode(intern_string("none"));
        args[2] = target->third ? target->third : newVarNode(intern_string("none"));
        args[3] = target->comp_expr ? target->comp_expr : newVarNode(intern_string("none"));
        args[4] = value;
        Node *call = node_new(NODE_CALL);
        call->name = intern_string("__glow_slice_set");
        call->call_args = args; call->call_arg_count = 5; call->line = target->line;
        return newExprStmtNode(call);
    }
    error_at(target->line, "Can only assign to a name, a list item or field (x[i], x.name) or a slice");
    return NULL;
}
/* Puts else_part at the end of an eif chain: the else branch of its last
   if. Returns the chain (or else_part when there is no chain). */
static Node *attach_else(Node *chain, Node *else_part) {
    if (!chain) return else_part;
    Node *last = chain;
    while (last->third) last = last->third;
    last->third = else_part;
    return chain;
}
/* Optional chains. a?.b starts one: a NODE_OPTCHAIN whose right is the
   access built on a placeholder for a's value. A following .c, (args) or
   [i] extends that inner access, so a?.b.c is none when a is none instead
   of failing on none.c. Parentheses close the chain. */
static Node *chain_base(Node *p) { return (p->type == NODE_OPTCHAIN && !p->closed) ? p->right : p; }
static Node *chain_put(Node *p, Node *n) {
    if (p->type == NODE_OPTCHAIN && !p->closed) { p->right = n; return p; }
    return n;
}
static Node *opt_start(Node *obj, Node *access) {
    Node *opt = node_new(NODE_OPTCHAIN);
    opt->left = obj; opt->right = access; opt->third = access->left; opt->line = obj->line;
    return opt;
}
/* ---- match ----
   match subject:
       case pattern, pattern when guard:
           body
       otherwise:
           body
   end
   becomes ordinary statements: the subject is computed once into a hidden
   variable, and each case is `if not matched and <pattern test>:`, which
   binds the pattern's names, checks the guard, then sets matched and runs
   its body. As in Python, a bare name captures the value (`none` is the
   value none), `_` matches anything, a list pattern [a, 0, ...rest, z]
   checks the items and captures its names, and anything else (a number,
   text, Color.RED, an expression) is compared with ==. */
static int match_count = 0;
static Node *helper_call(const char *fn, Node **args, int count, int line);
static Node *and_node(Node *a, Node *b) { return !a ? b : !b ? a : newBinOp(NODE_AND, a, b); }
static Node *index_node(Node *coll, Node *idx) {
    Node *n = node_new(NODE_INDEX); n->left = coll; n->right = idx; n->line = coll->line;
    return n;
}
static int is_wildcard(Node *p) { return p->type == NODE_VAR && strcmp(p->name, "_") == 0; }
static int is_capture(Node *p) { return p->type == NODE_VAR && strcmp(p->name, "_") != 0 && strcmp(p->name, "none") != 0; }
/* The items of a list pattern, or NULL when p is not one. */
static Node **list_pattern_items(Node *p, int *count) {
    if (p->type == NODE_LIST_LITERAL) { *count = p->element_count; return p->elements; }
    if (p->type == NODE_CALL && p->name && strcmp(p->name, "__glow_concat") == 0 && p->elements) {
        *count = p->element_count; return p->elements;
    }
    return NULL;
}
/* The test for pattern p against the value subj (NULL means always), and
   the assignments its names make (appended to *binds). */
static Node *pattern_test(Node *p, Node *subj, Node **binds) {
    int count;
    Node **items = list_pattern_items(p, &count);
    if (!items) {
        if (is_wildcard(p)) return NULL;
        if (is_capture(p)) { *binds = newSeqNode(*binds, newAssignNode(p->name, subj)); return NULL; }
        return newBinOp(NODE_EQ, subj, p);
    }
    int rest = -1;
    for (int i = 0; i < count; i++) {
        if (items[i]->type == NODE_SPREAD) {
            if (rest >= 0 || items[i]->left->type != NODE_VAR) { error_at(p->line, "A list pattern can have one ...name"); return NULL; }
            rest = i;
        }
    }
    int fixed = rest >= 0 ? count - 1 : count;
    /* __glow_list_len(x): len(x) for a list, -1 for anything else. */
    Node **la = arena_alloc(sizeof(Node*));
    la[0] = subj;
    Node *len = helper_call("__glow_list_len", la, 1, p->line);
    Node *test = newBinOp(rest >= 0 ? NODE_GTE : NODE_EQ, len, newIntNode(fixed));
    for (int i = 0; i < count; i++) {
        if (i == rest) {
            Node *name = items[i]->left;
            if (is_wildcard(name)) continue;
            int after = count - 1 - rest;
            Node *sl = node_new(NODE_SLICE);
            sl->left = subj; sl->right = newIntNode(rest);
            sl->third = after > 0 ? newIntNode(-after) : NULL;
            *binds = newSeqNode(*binds, newAssignNode(name->name, sl));
            continue;
        }
        Node *elem = index_node(subj, newIntNode(rest >= 0 && i > rest ? i - count : i));
        Node *it = items[i];
        if (is_wildcard(it)) continue;
        if (is_capture(it)) { *binds = newSeqNode(*binds, newAssignNode(it->name, elem)); continue; }
        test = and_node(test, pattern_test(it, elem, binds));
    }
    return test;
}
/* One case: its patterns (a list node of them), guard and body. */
static Node *case_node(Node **pats, int npats, Node *guard, Node *body, int line) {
    Node *c = node_new(NODE_SEQ);   /* a holder; match_node takes it apart */
    c->elements = pats; c->element_count = npats; c->comp_filter = guard; c->right = body; c->line = line;
    return c;
}
static Node *match_node(Node *subject, Node *cases, Node *otherwise_body, int line) {
    char buf[48];
    snprintf(buf, sizeof buf, "__match_%d", match_count);
    const char *sv = intern_string(buf);
    snprintf(buf, sizeof buf, "__matched_%d", match_count++);
    const char *mv = intern_string(buf);
    Node *setup = node_new(NODE_LET); setup->name = sv; setup->left = subject; setup->line = line;
    Node *flag = node_new(NODE_LET); flag->name = mv; flag->left = newBoolNode(0); flag->line = line;
    Node *out = newSeqNode(setup, flag);
    for (Node *c = cases; c; c = c->third) {
        Node *test = NULL, *binds = NULL;
        int always = 0;
        for (int i = 0; i < c->element_count; i++) {
            Node *b = NULL;
            Node *t = pattern_test(c->elements[i], newVarNode(sv), &b);
            if (b && c->element_count > 1) { error_at(c->line, "A case with several patterns cannot capture names"); return NULL; }
            binds = newSeqNode(binds, b);
            if (!t) always = 1;
            else test = test ? newBinOp(NODE_OR, test, t) : t;
        }
        if (always) test = NULL;
        Node *mark = newAssignNode(mv, newBoolNode(1));
        Node *run = newSeqNode(mark, c->right);
        Node *body = newSeqNode(binds, c->comp_filter ? newIfNode(c->comp_filter, run, NULL) : run);
        Node *not_done = newBinOp(NODE_NOT, newVarNode(mv), NULL);
        out = newSeqNode(out, newIfNode(and_node(not_done, test), body, NULL));
    }
    if (otherwise_body) out = newSeqNode(out, newIfNode(newBinOp(NODE_NOT, newVarNode(mv), NULL), otherwise_body, NULL));
    return out;
}
/* Adds a case at the end of the chain (linked through third). */
static Node *add_case(Node *chain, Node *c) {
    if (!chain) return c;
    Node *last = chain;
    while (last->third) last = last->third;
    last->third = c;
    return chain;
}

/* enum name: members (chained through third, each with its value or NULL
   in left). The names must differ. */
static Node *enum_node(const char *name, Node *members) {
    for (Node *a = members; a; a = a->third)
        for (Node *b = a->third; b; b = b->third)
            if (strcmp(a->name, b->name) == 0) {
                char msg[160];
                snprintf(msg, sizeof msg, "enum %.60s has %.60s twice", name, a->name);
                error_at(a->line, msg);
                return NULL;
            }
    Node *e = node_new(NODE_ENUM);
    e->name = intern_string(name); e->left = members; e->line = members ? members->line : line_num;
    return e;
}
/* target op= value: target = target op value. For a field or item
   (x.name op= v, x[i] op= v) x and i are worked out once, as in Python: a
   NODE_COMPOUND_INDEX computes them into registers, then reads the old
   value, computes the new one and stores it. op is a binary node type, or
   COMPOUND_FLOORDIV for //=. Errors name the statement's own line (yyerror
   would name the next one: the lexer has already read the newline). */
#define COMPOUND_FLOORDIV ((NodeType)-1)
static Node *compound_value(NodeType op, Node *old, Node *value) {
    return op == COMPOUND_FLOORDIV ? newFloorDivNode(old, value) : newBinOp(op, old, value);
}
static Node *compound_assign(Node *target, NodeType op, Node *value) {
    if (target->type == NODE_VAR) {
        Node *a = newAssignNode(target->name, compound_value(op, newVarNode(target->name), value));
        a->line = target->line;
        return a;
    }
    if (target->type != NODE_INDEX) {
        error_at(target->line, "Compound assignment target must be a variable, a field or an item (x, x.name or x[i])");
        return NULL;
    }
    Node *obj = node_new(NODE_REG), *key = target->name ? NULL : node_new(NODE_REG);
    obj->line = target->line;
    if (key) key->line = target->line;
    Node *old = node_new(NODE_INDEX);
    old->left = obj; old->right = key; old->name = target->name; old->line = target->line;
    Node *store = node_new(NODE_INDEX_ASSIGN);
    store->left = obj; store->right = key; store->name = target->name; store->line = target->line;
    store->third = compound_value(op, old, value);
    Node *c = node_new(NODE_COMPOUND_INDEX);
    c->left = target->left; c->right = target->name ? NULL : target->right;
    c->third = store; c->comp_expr = obj; c->comp_filter = key;
    c->line = target->line;
    return c;
}
/* `a, b = values` and `let a, b = values`. Each target becomes an ordinary
   assignment (or let, or a[i] / obj.x store) of a value the compiler has
   already placed in a register (NODE_REG), so names follow the usual
   local/global rules. One value must be a list of exactly that many items;
   several values (`a, b = b, a`) are all evaluated before any is stored and
   need no list at run time. */
static Node *list_node(Node **items, int count, int line);
static Node *unpack_node(Node **targets, int count, Node **values, int nvalues, int is_let, int line) {
    char msg[96];
    if (count > 255) { error_at(line, "Too many names to unpack (at most 255)"); return NULL; }
    /* One name may be ...rest: it gets the items the others leave. */
    int rest = -1;
    for (int i = 0; i < count; i++) {
        if (targets[i]->type != NODE_SPREAD) continue;
        if (rest >= 0) { error_at(line, "Only one name can be ...rest"); return NULL; }
        rest = i;
        targets[i] = targets[i]->left;
    }
    if (rest >= 0 && nvalues > 1) {
        /* a, ...b = 1, 2, 3: the values become one list (made from a copy:
           the list must not hold itself). */
        Node **items = arena_alloc(sizeof(Node*) * nvalues);
        for (int i = 0; i < nvalues; i++) items[i] = values[i];
        Node *whole = list_node(items, nvalues, line);
        if (!whole) return NULL;
        values = arena_alloc(sizeof(Node*));
        values[0] = whole;
        nvalues = 1;
    }
    if (nvalues > 1 && nvalues != count) {
        snprintf(msg, sizeof msg, "%d names but %d values", count, nvalues);
        error_at(line, msg); return NULL;
    }
    Node *u = node_new(NODE_UNPACK);
    u->line = line;
    u->elements = arena_alloc(sizeof(Node*) * count);
    u->element_count = count;
    for (int i = 0; i < count; i++) {
        Node *t = targets[i], *ph = node_new(NODE_REG), *s;
        ph->line = line;
        if (is_let || t->type == NODE_VAR) {
            s = node_new(is_let ? NODE_LET : NODE_ASSIGN);
            s->name = t->name; s->left = ph;
        } else if (t->type == NODE_INDEX) {
            s = t; s->type = NODE_INDEX_ASSIGN; s->third = ph;
        } else {
            error_at(line, "Can only unpack into names, list items and fields"); return NULL;
        }
        s->line = line;
        u->elements[i] = s;
    }
    if (nvalues == 1) u->left = values[0];
    else { u->call_args = values; u->call_arg_count = nvalues; }
    u->has_rest = rest + 1;
    return u;
}
static void list_push_node(Node ***nodes, int *count, Node *n);
/* A call of runtime helper fn with the given arguments. */
static Node *helper_call(const char *fn, Node **args, int count, int line) {
    Node *n = node_new(NODE_CALL);
    n->name = intern_string(fn);
    n->call_args = args; n->call_arg_count = count; n->line = line;
    return n;
}
/* A list literal whose items may include ...spread: [a, ...b, c] becomes
   __glow_concat([a], b, [c]), which checks that b is a list. Without a
   spread it is an ordinary list literal. */
static Node *list_node(Node **items, int count, int line) {
    int spreads = 0;
    for (int i = 0; i < count; i++) {
        if (items[i]->type == NODE_KWARG) { error_at(items[i]->line, "name = value only works in a call's arguments"); return NULL; }
        if (items[i]->type == NODE_SPREAD) spreads++;
    }
    if (!spreads) {
        Node *l = node_new(NODE_LIST_LITERAL);
        l->elements = items; l->element_count = count; l->line = line;
        return l;
    }
    Node **parts = NULL; int nparts = 0;
    for (int i = 0; i < count; ) {
        if (items[i]->type == NODE_SPREAD) { list_push_node(&parts, &nparts, items[i]->left); i++; continue; }
        Node **chunk = NULL; int nchunk = 0;
        while (i < count && items[i]->type != NODE_SPREAD) list_push_node(&chunk, &nchunk, items[i++]);
        Node *l = node_new(NODE_LIST_LITERAL);
        l->elements = chunk; l->element_count = nchunk; l->line = line;
        list_push_node(&parts, &nparts, l);
    }
    Node *c = helper_call("__glow_concat", parts, nparts, line);
    c->elements = items; c->element_count = count;   /* for a `case [a, ...rest]` pattern */
    return c;
}
/* [expr for each var in coll when filter] (filter may be NULL). */
static Node *comp_node(Node *expr, const char *var, Node *coll, Node *filter) {
    Node *c = node_new(NODE_LIST_COMP);
    c->comp_expr = expr; c->comp_var = var; c->left = coll; c->comp_filter = filter;
    return c;
}
/* {k: v for each var in coll when filter}: __glow_dict_from_pairs of the
   list comprehension of [k, v] pairs. */
static Node *dict_comp(Node *k, Node *v, const char *var, Node *coll, Node *filter) {
    Node **kv = arena_alloc(sizeof(Node*) * 2);
    kv[0] = k; kv[1] = v;
    Node *pair = node_new(NODE_LIST_LITERAL);
    pair->elements = kv; pair->element_count = 2; pair->line = k->line;
    Node **a = arena_alloc(sizeof(Node*));
    a[0] = comp_node(pair, var, coll, filter);
    return helper_call("__glow_dict_from_pairs", a, 1, k->line);
}
/* {x for each var in coll} and {a, b, ...more}: a set from a list. */
static Node *set_of(Node *list, int line) {
    if (!list) return NULL;
    Node **a = arena_alloc(sizeof(Node*));
    a[0] = list;
    return helper_call("__glow_set_from", a, 1, line);
}
/* callee(items): with a ...spread or name = value argument the call becomes
   __glow_callv(callee, kinds, arguments...), which matches names to
   parameters at run time; otherwise it is an ordinary call. kinds has a
   letter per argument: p (positional), s (...spread) or k (name = value,
   passed as two arguments: the name, then the value). Nothing is packed
   into lists, so the call allocates nothing of its own. A call with very
   many arguments uses __glow_call(callee, positional list, [names],
   [values]) instead. Positional arguments come first, and a name can be
   given only once. */
static Node *call_node(Node *callee, Node **items, int count) {
    int special = 0, seen_kw = 0, line = callee->line;
    Node **pos = NULL, **names = NULL, **vals = NULL; int npos = 0, nkw = 0, nvals = 0;
    for (int i = 0; i < count; i++) {
        Node *it = items[i];
        if (it->type == NODE_KWARG) {
            special = seen_kw = 1;
            for (int j = 0; j < nkw; j++) {
                if (strcmp(names[j]->name, it->name) == 0) {
                    char msg[160];
                    snprintf(msg, sizeof msg, "Argument '%.80s' is given twice", it->name);
                    error_at(it->line, msg); return NULL;
                }
            }
            list_push_node(&names, &nkw, newStringNode(it->name));
            list_push_node(&vals, &nvals, it->left);
            continue;
        }
        if (seen_kw) { error_at(it->line, "A positional argument cannot come after a name = value argument"); return NULL; }
        if (it->type == NODE_SPREAD) special = 1;
        list_push_node(&pos, &npos, it);
    }
    if (!special) return newCallExprNode(callee, items, count);
    if (npos + 2 * nkw + 2 <= 250) {
        int nargs = 2 + npos + 2 * nkw, a = 2;
        Node **args = arena_alloc(sizeof(Node*) * nargs);
        char *kinds = arena_alloc((size_t)(npos + nkw) + 1);
        args[0] = callee;
        for (int i = 0; i < npos; i++) {
            kinds[i] = pos[i]->type == NODE_SPREAD ? 's' : 'p';
            args[a++] = pos[i]->type == NODE_SPREAD ? pos[i]->left : pos[i];
        }
        for (int k = 0; k < nkw; k++) {
            kinds[npos + k] = 'k';
            args[a++] = names[k];
            args[a++] = vals[k];
        }
        kinds[npos + nkw] = 0;
        args[1] = newStringNode(kinds);
        return helper_call("__glow_callv", args, nargs, line);
    }
    Node **args = arena_alloc(sizeof(Node*) * 4);
    args[0] = callee;
    args[1] = list_node(pos, npos, line);
    if (!args[1]) return NULL;
    Node *nl = node_new(NODE_LIST_LITERAL); nl->elements = names; nl->element_count = nkw; nl->line = line;
    Node *vl = node_new(NODE_LIST_LITERAL); vl->elements = vals; vl->element_count = nkw; vl->line = line;
    args[2] = nl; args[3] = vl;
    return helper_call("__glow_call", args, 4, line);
}
/* for each [a, b] in items: each item is unpacked into a and b. */
/* yield from x: yield each item of x in turn (a list, a set, a string or
   another generator). */
static Node *yield_from(Node *coll) {
    static int yf_count = 0;
    char buf[32];
    snprintf(buf, sizeof buf, "__yf_%d", yf_count++);
    const char *item = intern_string(buf);
    Node *y = node_new(NODE_YIELD);
    y->left = newVarNode(item);
    y->line = y->left->line = coll->line;
    Node *loop = node_new(NODE_FOR_EACH);
    loop->name = item; loop->left = coll; loop->right = y;
    loop->line = coll->line;
    return loop;
}
static Node *unpack_loop(Node **names, int count, Node *coll, Node *body) {
    static int each_count = 0;
    char buf[32];
    snprintf(buf, sizeof buf, "__each_%d", each_count++);
    const char *item = intern_string(buf);
    Node **vals = arena_alloc(sizeof(Node*));
    vals[0] = newVarNode(item);
    vals[0]->line = coll->line;
    Node *u = unpack_node(names, count, vals, 1, 0, coll->line);
    if (!u) return NULL;
    Node *loop = node_new(NODE_FOR_EACH);
    loop->name = item; loop->left = coll;
    loop->right = body ? newSeqNode(u, body) : u;
    return loop;
}
/* A list of two or more names or places, from `a, b` or `x, y, z`. */
static void list_push_node(Node ***nodes, int *count, Node *n) {
    *nodes = arena_realloc(*nodes, sizeof(Node*) * (*count), sizeof(Node*) * (*count + 1));
    (*nodes)[(*count)++] = n;
}
%}

%union {
    int64_t int_val;
    double float_val;
    const char *str_val;
    struct Node *node;
    struct { struct Node **nodes; int count; } node_list;
    struct { struct Node **keys; struct Node **values; int count; } dict_list;
    struct { char **names; struct Node **defaults; int count; int rest; } str_list;
}

%token <str_val> INT_LIT
%token BAD_NUMBER
%token <float_val> FLOAT_LIT
%token <str_val> ID STRING FSTRING
%token YIELD
%token SHOW WRITE WHEN WHENEVER EIF OTHERWISE WHILE UNTIL REPEAT FOR EACH IN_KW FUNC LAMBDA RETURN STOP SKIP END LET TRY CATCH FINALLY THROW IMPORT CLASS TRAIT EXTENDS USE SPAWN TRUE_KW FALSE_KW TIMES_KW INPUT_KW IS IS_NOT FROM TO NEWLINE COMMA ASSIGN PLUS_ASSIGN MINUS_ASSIGN MUL_ASSIGN DIV_ASSIGN FLOORDIV_ASSIGN MOD_ASSIGN EQ NEQ LT GT LTE GTE PLUS MINUS MUL DIV FLOORDIV MOD POW COLON AND_OP OR_OP NOT_OP
%token ELLIPSIS NULLISH NULLISH_ASSIGN QDOT QDOT_INDEX MATCH CASE ENUM
%token BITAND BITOR BITXOR BITNOT SHL SHR BITAND_ASSIGN BITOR_ASSIGN BITXOR_ASSIGN SHL_ASSIGN SHR_ASSIGN

%type <node> program lines stmt expr term power postfix primary condition eif_chain otherwise_part unary
%type <node_list> expr_list arg_list target_list name_list item_list case_pats
%type <node> item target name_item match_cases case_clause enum_members enum_member case_pat enum_inline
%type <dict_list> dict_elements
%type <str_list> param_list param
%type <str_val> member_name

%right WHEN OTHERWISE
%right NULLISH
%left OR_OP
%left AND_OP
%left EQ NEQ IS IS_NOT IN_KW
%left LT GT LTE GTE
%left BITOR
%left BITXOR
%left BITAND
%left SHL SHR
%left PLUS MINUS
%left MUL DIV FLOORDIV MOD
%right POW
%nonassoc NOT_OP

/* Brackets left open by an earlier, failed parse must not swallow newlines. */
%initial-action { extern int lexer_bracket_depth, lexer_at_stmt_start; lexer_bracket_depth = 0; lexer_at_stmt_start = 1; lexer_block_reset(); }

%%

program: lines { root = $1; };
lines: lines NEWLINE stmt { $$ = newSeqNode($1, $3); } | lines NEWLINE { $$ = $1; } | stmt { $$ = $1; } | { $$ = NULL; };
stmt: postfix ASSIGN expr { $$ = assign_node($1, $3); if (!$$) YYERROR; }
    | IMPORT STRING { $$ = node_new(NODE_IMPORT); $$->name = $2; }
    | USE STRING { $$ = node_new(NODE_IMPORT); $$->name = $2; }
    | RETURN expr { $$ = newReturnNode($2); }
    | RETURN { $$ = newReturnNode(newVarNode(intern_string("none"))); }
    | YIELD expr { $$ = node_new(NODE_YIELD); $$->left = $2; $$->line = $2->line; }
    | YIELD { $$ = node_new(NODE_YIELD); }
    | YIELD FROM expr { $$ = yield_from($3); }
    | STOP { $$ = node_new(NODE_STOP); }
    | SKIP { $$ = node_new(NODE_SKIP); }
    | FOR EACH ID IN_KW expr COLON NEWLINE lines END { $$ = node_new(NODE_FOR_EACH); $$->name = $3; $$->left = $5; $$->right = $8; }
    | FOR EACH ID COMMA ID IN_KW expr COLON NEWLINE lines END { $$ = newPairLoopNode($3, $5, $7, $10); }
    | FOR ID COMMA ID IN_KW expr COLON NEWLINE lines END { $$ = newPairLoopNode($2, $4, $6, $9); }
    | FOR ID IN_KW expr COLON NEWLINE lines END { $$ = node_new(NODE_FOR_EACH); $$->name = $2; $$->left = $4; $$->right = $7; }
    | FOR ID FROM expr TO expr COLON NEWLINE lines END { $$ = node_new(NODE_FOR_RANGE); $$->name = $2; $$->left = $4; $$->right = $6; $$->third = $9; }
    | FOR ID IN_KW FROM expr TO expr COLON NEWLINE lines END { $$ = node_new(NODE_FOR_RANGE); $$->name = $2; $$->left = $5; $$->right = $7; $$->third = $10; }
    | FOR EACH ID IN_KW FROM expr TO expr COLON NEWLINE lines END { $$ = node_new(NODE_FOR_RANGE); $$->name = $3; $$->left = $6; $$->right = $8; $$->third = $11; }
    | FUNC member_name '(' param_list ')' COLON NEWLINE lines END { $$ = newFuncNode($2, $4.count, $4.names, with_param_defaults($4.count, $4.names, $4.defaults, $8)); $$->has_rest = $4.rest; }
    | FUNC member_name '(' ')' COLON NEWLINE lines END { $$ = newFuncNode($2, 0, NULL, $7); }
    | CLASS ID COLON NEWLINE lines END { $$ = node_new(NODE_CLASS); $$->name = $2; $$->left = $5; }
    | CLASS ID EXTENDS ID COLON NEWLINE lines END { $$ = node_new(NODE_CLASS); $$->name = $2; $$->parent_name = $4; $$->left = $7; }
    | TRAIT ID COLON NEWLINE lines END { $$ = node_new(NODE_TRAIT); $$->name = $2; $$->left = $5; }
    | USE ID { $$ = node_new(NODE_USE_TRAIT); $$->name = $2; }
    | WHENEVER condition COLON NEWLINE lines END { $$ = node_new(NODE_WHENEVER); $$->left = $2; $$->right = $5; }
    | WHEN condition COLON NEWLINE lines eif_chain otherwise_part END { $$ = newIfNode($2, $5, attach_else($6, $7)); }
    | WHILE condition COLON NEWLINE lines END { $$ = newWhileNode($2, $5); }
    | UNTIL condition COLON NEWLINE lines END { $$ = newUntilNode($2, $5); }
    | REPEAT expr TIMES_KW COLON NEWLINE lines END { $$ = newRepeatNode($2, $6); }
    | postfix PLUS_ASSIGN expr { if ($1->type == NODE_VAR) $$ = newCompoundAssignNode(NODE_COMPOUND_ADD, $1->name, $3); else { $$ = compound_assign($1, NODE_ADD, $3); if (!$$) YYERROR; } }
    | postfix MINUS_ASSIGN expr { if ($1->type == NODE_VAR) $$ = newCompoundAssignNode(NODE_COMPOUND_SUB, $1->name, $3); else { $$ = compound_assign($1, NODE_SUB, $3); if (!$$) YYERROR; } }
    | postfix MUL_ASSIGN expr { if ($1->type == NODE_VAR) $$ = newCompoundAssignNode(NODE_COMPOUND_MUL, $1->name, $3); else { $$ = compound_assign($1, NODE_MUL, $3); if (!$$) YYERROR; } }
    | postfix DIV_ASSIGN expr { if ($1->type == NODE_VAR) $$ = newCompoundAssignNode(NODE_COMPOUND_DIV, $1->name, $3); else { $$ = compound_assign($1, NODE_DIV, $3); if (!$$) YYERROR; } }
    | postfix FLOORDIV_ASSIGN expr { $$ = compound_assign($1, COMPOUND_FLOORDIV, $3); if (!$$) YYERROR; }
    | postfix MOD_ASSIGN expr { if ($1->type == NODE_VAR) $$ = newCompoundAssignNode(NODE_COMPOUND_MOD, $1->name, $3); else { $$ = compound_assign($1, NODE_MOD, $3); if (!$$) YYERROR; } }
    | postfix BITAND_ASSIGN expr { $$ = compound_assign($1, NODE_BITAND, $3); if (!$$) YYERROR; }
    | postfix BITOR_ASSIGN expr { $$ = compound_assign($1, NODE_BITOR, $3); if (!$$) YYERROR; }
    | postfix BITXOR_ASSIGN expr { $$ = compound_assign($1, NODE_BITXOR, $3); if (!$$) YYERROR; }
    | postfix SHL_ASSIGN expr { $$ = compound_assign($1, NODE_SHL, $3); if (!$$) YYERROR; }
    | postfix SHR_ASSIGN expr { $$ = compound_assign($1, NODE_SHR, $3); if (!$$) YYERROR; }
    | postfix NULLISH_ASSIGN expr { $$ = compound_assign($1, NODE_NULLISH, $3); if (!$$) YYERROR; }
    | target_list ASSIGN expr_list { $$ = unpack_node($1.nodes, $1.count, $3.nodes, $3.count, 0, $1.nodes[0]->line); if (!$$) YYERROR; }
    | LET name_list ASSIGN expr_list { $$ = unpack_node($2.nodes, $2.count, $4.nodes, $4.count, 1, $2.nodes[0]->line); if (!$$) YYERROR; }
    | FOR EACH '[' name_list ']' IN_KW expr COLON NEWLINE lines END { $$ = unpack_loop($4.nodes, $4.count, $7, $10); if (!$$) YYERROR; }
    | FOR '[' name_list ']' IN_KW expr COLON NEWLINE lines END { $$ = unpack_loop($3.nodes, $3.count, $6, $9); if (!$$) YYERROR; }
    | MATCH expr COLON NEWLINE opt_newlines match_cases otherwise_part END { $$ = match_node($2, $6, $7, $2->line); if (!$$) YYERROR; }
    | ENUM ID COLON NEWLINE opt_newlines enum_members END { $$ = enum_node($2, $6); if (!$$) YYERROR; }
    | ENUM ID COLON enum_inline { $$ = enum_node($2, $4); if (!$$) YYERROR; }
    | TRY COLON NEWLINE lines CATCH ID COLON NEWLINE lines END { $$ = node_new(NODE_TRY_CATCH); $$->left = $4; $$->name = $6; $$->right = $9; }
    | TRY COLON NEWLINE lines CATCH ID COLON NEWLINE lines FINALLY COLON NEWLINE lines END { $$ = node_new(NODE_TRY_CATCH); $$->left = $4; $$->name = $6; $$->right = $9; $$->third = $13; }
    | TRY COLON NEWLINE lines FINALLY COLON NEWLINE lines END { $$ = node_new(NODE_TRY_CATCH); $$->left = $4; $$->name = NULL; $$->right = NULL; $$->third = $8; }
    | THROW expr { $$ = node_new(NODE_THROW); $$->left = $2; }
    | SPAWN expr { $$ = node_new(NODE_SPAWN); $$->left = $2; }
    | LET ID ASSIGN expr { $$ = node_new(NODE_LET); $$->name = $2; $$->left = $4; }
    | SHOW expr_list { $$ = newShowNode($2.nodes, $2.count); }
    | WRITE expr_list { $$ = newWriteNode($2.nodes, $2.count); }
    | expr { $$ = newExprStmtNode($1); };

expr_list: expr { $$.count = 1; $$.nodes = arena_alloc(sizeof(Node*)); $$.nodes[0] = $1; } | expr_list COMMA expr { $$.nodes = arena_realloc($$.nodes, sizeof(Node*)*($1.count), sizeof(Node*)*($1.count+1)); $$.nodes[$1.count] = $3; $$.count = $1.count + 1; };
arg_list: item_list { $$ = $1; };
/* Call arguments and list items: an expression, ...spread, or (only in a
   call) name = value. */
item_list: item { $$.nodes = NULL; $$.count = 0; list_push_node(&$$.nodes, &$$.count, $1); }
    | item_list COMMA item { $$ = $1; list_push_node(&$$.nodes, &$$.count, $3); };
item: expr { $$ = $1; }
    | ELLIPSIS expr { $$ = node_new(NODE_SPREAD); $$->left = $2; $$->line = $2->line; }
    | ID ASSIGN expr { $$ = node_new(NODE_KWARG); $$->name = intern_string($1); $$->left = $3; $$->line = $3->line; };
target: postfix { $$ = $1; }
    | ELLIPSIS postfix { $$ = node_new(NODE_SPREAD); $$->left = $2; $$->line = $2->line; };
target_list: target COMMA target { $$.nodes = NULL; $$.count = 0; list_push_node(&$$.nodes, &$$.count, $1); list_push_node(&$$.nodes, &$$.count, $3); }
    | target_list COMMA target { $$ = $1; list_push_node(&$$.nodes, &$$.count, $3); };
name_item: ID { $$ = newVarNode($1); }
    | ELLIPSIS ID { $$ = node_new(NODE_SPREAD); $$->left = newVarNode($2); $$->line = $$->left->line; };
name_list: name_item COMMA name_item { $$.nodes = NULL; $$.count = 0; list_push_node(&$$.nodes, &$$.count, $1); list_push_node(&$$.nodes, &$$.count, $3); }
    | name_list COMMA name_item { $$ = $1; list_push_node(&$$.nodes, &$$.count, $3); };
param_list: param { $$ = $1; }
    | param_list COMMA param {
        if ($1.rest) { error_at(line_num, "The ...rest parameter must be the last one"); YYERROR; }
        int n = $1.count;
        $$.names = arena_realloc($1.names, sizeof(char*)*n, sizeof(char*)*(n+1)); $$.names[n] = $3.names[0];
        $$.defaults = arena_realloc($1.defaults, sizeof(Node*)*n, sizeof(Node*)*(n+1)); $$.defaults[n] = $3.defaults[0];
        $$.count = n + 1;
        $$.rest = $3.rest;
    };
param: ID { $$.count = 1; $$.rest = 0; $$.names = arena_alloc(sizeof(char*)); $$.names[0] = (char*)intern_string($1); $$.defaults = arena_alloc(sizeof(Node*)); $$.defaults[0] = NULL; }
    | ID ASSIGN expr { $$.count = 1; $$.rest = 0; $$.names = arena_alloc(sizeof(char*)); $$.names[0] = (char*)intern_string($1); $$.defaults = arena_alloc(sizeof(Node*)); $$.defaults[0] = $3; }
    /* ...rest: the extra arguments, as a list (empty when there are none,
       which the default gives: a call with no extras leaves it none). */
    | ELLIPSIS ID { $$.count = 1; $$.rest = 1; $$.names = arena_alloc(sizeof(char*)); $$.names[0] = (char*)intern_string($2); $$.defaults = arena_alloc(sizeof(Node*)); $$.defaults[0] = node_new(NODE_LIST_LITERAL); };
condition: expr;
opt_newlines: opt_newlines NEWLINE | ;
match_cases: case_clause { $$ = $1; } | match_cases case_clause { $$ = add_case($1, $2); };
case_clause: CASE case_pats COLON NEWLINE lines { $$ = case_node($2.nodes, $2.count, NULL, $5, $2.nodes[0]->line); }
    | CASE case_pats WHEN expr COLON NEWLINE lines { $$ = case_node($2.nodes, $2.count, $4, $7, $2.nodes[0]->line); };
case_pats: case_pat { $$.nodes = NULL; $$.count = 0; list_push_node(&$$.nodes, &$$.count, $1); }
    | case_pats COMMA case_pat { $$ = $1; list_push_node(&$$.nodes, &$$.count, $3); };
/* In a case, `when` after a pattern starts the guard, not a
   `value when condition otherwise other` expression: the reduction
   outranks WHEN. */
case_pat: expr %prec NULLISH { $$ = $1; };
/* Members, one per line or separated by commas; NAME or NAME = value. */
enum_members: enum_member { $$ = $1; }
    | enum_members NEWLINE enum_member { $$ = add_case($1, $3); }
    | enum_members COMMA enum_member { $$ = add_case($1, $3); }
    | enum_members NEWLINE { $$ = $1; };
/* enum Dir: UP, DOWN on one line */
enum_inline: enum_member { $$ = $1; } | enum_inline COMMA enum_member { $$ = add_case($1, $3); };
enum_member: ID { $$ = node_new(NODE_VAR); $$->name = intern_string($1); }
    | ID ASSIGN expr { $$ = node_new(NODE_VAR); $$->name = intern_string($1); $$->left = $3; };
/* Each eif is the else branch of the one before it, so only the first
   true branch runs (they used to run as separate ifs, and the otherwise
   block ran even when an eif matched). */
eif_chain: eif_chain EIF condition COLON NEWLINE lines { $$ = attach_else($1, newIfNode($3, $6, NULL)); } | { $$ = NULL; };
otherwise_part: OTHERWISE COLON NEWLINE lines { $$ = $4; } | { $$ = NULL; };

expr: term | expr PLUS expr { $$ = newBinOp(NODE_ADD, $1, $3); } | expr MINUS expr { $$ = newBinOp(NODE_SUB, $1, $3); } | expr EQ expr { $$ = newBinOp(NODE_EQ, $1, $3); } | expr NEQ expr { $$ = newBinOp(NODE_NEQ, $1, $3); } | expr IS expr { $$ = newBinOp(NODE_EQ, $1, $3); } | expr IS_NOT expr { $$ = newBinOp(NODE_NEQ, $1, $3); } | expr IS IN_KW expr { $$ = newBinOp(NODE_IN, $1, $4); } | expr IS_NOT IN_KW expr { $$ = newBinOp(NODE_NOT, newBinOp(NODE_IN, $1, $4), NULL); } | expr IN_KW expr { $$ = newBinOp(NODE_IN, $1, $3); } | expr NOT_OP IN_KW expr { $$ = newBinOp(NODE_NOT, newBinOp(NODE_IN, $1, $4), NULL); } | expr LT expr { $$ = newBinOp(NODE_LT, $1, $3); } | expr GT expr { $$ = newBinOp(NODE_GT, $1, $3); } | expr LTE expr { $$ = newBinOp(NODE_LTE, $1, $3); } | expr GTE expr { $$ = newBinOp(NODE_GTE, $1, $3); } | expr AND_OP expr { $$ = newBinOp(NODE_AND, $1, $3); } | expr OR_OP expr { $$ = newBinOp(NODE_OR, $1, $3); } | expr NULLISH expr { $$ = newBinOp(NODE_NULLISH, $1, $3); } | expr BITOR expr { $$ = newBinOp(NODE_BITOR, $1, $3); } | expr BITXOR expr { $$ = newBinOp(NODE_BITXOR, $1, $3); } | expr BITAND expr { $$ = newBinOp(NODE_BITAND, $1, $3); } | expr SHL expr { $$ = newBinOp(NODE_SHL, $1, $3); } | expr SHR expr { $$ = newBinOp(NODE_SHR, $1, $3); } | NOT_OP expr { $$ = newBinOp(NODE_NOT, $2, NULL); } | expr WHEN expr OTHERWISE expr %prec WHEN { $$ = node_new(NODE_COND); $$->left = $3; $$->right = $1; $$->third = $5; };
term: unary | term MUL unary { $$ = newBinOp(NODE_MUL, $1, $3); } | term DIV unary { $$ = newBinOp(NODE_DIV, $1, $3); } | term FLOORDIV unary { $$ = newFloorDivNode($1, $3); } | term MOD unary { $$ = newBinOp(NODE_MOD, $1, $3); };
unary: power | MINUS unary { $$ = newBinOp(NODE_NEG, $2, NULL); } | BITNOT unary { $$ = newBinOp(NODE_BITNOT, $2, NULL); };
power: postfix | postfix POW unary { $$ = newBinOp(NODE_POW, $1, $3); };
postfix: primary
       | postfix '.' member_name { $$ = node_new(NODE_INDEX); $$->left = chain_base($1); $$->name = $3; $$ = chain_put($1, $$); }
       | postfix '(' arg_list ')' { $$ = call_node(chain_base($1), $3.nodes, $3.count); if (!$$) YYERROR; $$ = chain_put($1, $$); }
       | postfix '(' ')' { $$ = newCallExprNode(chain_base($1), NULL, 0); $$ = chain_put($1, $$); }
       | postfix '[' expr ']' { $$ = node_new(NODE_INDEX); $$->left = chain_base($1); $$->right = $3; $$ = chain_put($1, $$); }
       | postfix QDOT member_name { Node *a = node_new(NODE_INDEX); a->left = node_new(NODE_REG); a->name = $3; $$ = opt_start($1, a); }
       /* a?.[...]: a chain whose access so far is a itself; the [i] or slice
          that follows extends it like any other. */
       | postfix QDOT_INDEX { Node *ph = node_new(NODE_REG); $$ = node_new(NODE_OPTCHAIN); $$->left = $1; $$->right = ph; $$->third = ph; $$->line = $1->line; }
       | postfix '[' expr COLON expr ']' { $$ = node_new(NODE_SLICE); $$->left = chain_base($1); $$->right = $3; $$->third = $5; $$ = chain_put($1, $$); }
       | postfix '[' expr COLON ']' { $$ = node_new(NODE_SLICE); $$->left = chain_base($1); $$->right = $3; $$->third = NULL; $$ = chain_put($1, $$); }
       | postfix '[' COLON expr ']' { $$ = node_new(NODE_SLICE); $$->left = chain_base($1); $$->right = NULL; $$->third = $4; $$ = chain_put($1, $$); }
       | postfix '[' COLON ']' { $$ = node_new(NODE_SLICE); $$->left = chain_base($1); $$->right = NULL; $$->third = NULL; $$ = chain_put($1, $$); }
       | postfix '[' expr COLON expr COLON expr ']' { $$ = node_new(NODE_SLICE); $$->left = chain_base($1); $$->right = $3; $$->third = $5; $$->comp_expr = $7; $$ = chain_put($1, $$); }
       | postfix '[' expr COLON COLON expr ']' { $$ = node_new(NODE_SLICE); $$->left = chain_base($1); $$->right = $3; $$->third = NULL; $$->comp_expr = $6; $$ = chain_put($1, $$); }
       | postfix '[' COLON expr COLON expr ']' { $$ = node_new(NODE_SLICE); $$->left = chain_base($1); $$->right = NULL; $$->third = $4; $$->comp_expr = $6; $$ = chain_put($1, $$); }
       | postfix '[' COLON COLON expr ']' { $$ = node_new(NODE_SLICE); $$->left = chain_base($1); $$->right = NULL; $$->third = NULL; $$->comp_expr = $5; $$ = chain_put($1, $$); }
       | postfix '[' expr COLON expr COLON ']' { $$ = node_new(NODE_SLICE); $$->left = chain_base($1); $$->right = $3; $$->third = $5; $$ = chain_put($1, $$); }
       | postfix '[' expr COLON COLON ']' { $$ = node_new(NODE_SLICE); $$->left = chain_base($1); $$->right = $3; $$->third = NULL; $$ = chain_put($1, $$); }
       | postfix '[' COLON expr COLON ']' { $$ = node_new(NODE_SLICE); $$->left = chain_base($1); $$->right = NULL; $$->third = $4; $$ = chain_put($1, $$); }
       | postfix '[' COLON COLON ']' { $$ = node_new(NODE_SLICE); $$->left = chain_base($1); $$->right = NULL; $$->third = NULL; $$ = chain_put($1, $$); };
member_name: ID { $$ = $1; }
           | WRITE { $$ = intern_string("write"); }
           | SHOW { $$ = intern_string("show"); }
           | STOP { $$ = intern_string("stop"); }
           | SKIP { $$ = intern_string("skip"); }
           ;
primary: ID { $$ = newVarNode($1); } | INT_LIT { $$ = newIntNodeStr($1); } | STRING { $$ = newStringNode($1); } | FSTRING { $$ = node_new(NODE_FSTRING); $$->name = $1; } | FLOAT_LIT { $$ = newFloatNode($1); } | TRUE_KW { $$ = newBoolNode(1); } | FALSE_KW { $$ = newBoolNode(0); } | '(' expr ')' { $$ = $2; if ($$->type == NODE_OPTCHAIN) $$->closed = 1; } | '[' expr FOR EACH ID IN_KW expr ']' {
    $$ = node_new(NODE_LIST_COMP);
    $$->comp_expr = $2;
    $$->comp_var = $5;
    $$->left = $7;
    $$->comp_filter = NULL;
} | '[' expr FOR EACH ID IN_KW expr WHEN expr ']' {
    $$ = node_new(NODE_LIST_COMP);
    $$->comp_expr = $2;
    $$->comp_var = $5;
    $$->left = $7;
    $$->comp_filter = $9;
} | '[' expr FOR ID IN_KW expr ']' {
    $$ = node_new(NODE_LIST_COMP);
    $$->comp_expr = $2;
    $$->comp_var = $4;
    $$->left = $6;
    $$->comp_filter = NULL;
} | '[' expr FOR ID IN_KW expr WHEN expr ']' {
    $$ = node_new(NODE_LIST_COMP);
    $$->comp_expr = $2;
    $$->comp_var = $4;
    $$->left = $6;
    $$->comp_filter = $8;
} | '[' item_list ']' { $$ = list_node($2.nodes, $2.count, line_num); if (!$$) YYERROR; } | '[' ']' { $$ = node_new(NODE_LIST_LITERAL); $$->elements = NULL; $$->element_count = 0; } | '{' item_list '}' { $$ = set_of(list_node($2.nodes, $2.count, line_num), line_num); if (!$$) YYERROR; }
    | '{' expr COLON expr FOR EACH ID IN_KW expr '}' { $$ = dict_comp($2, $4, intern_string($7), $9, NULL); }
    | '{' expr COLON expr FOR EACH ID IN_KW expr WHEN expr '}' { $$ = dict_comp($2, $4, intern_string($7), $9, $11); }
    | '{' expr COLON expr FOR ID IN_KW expr '}' { $$ = dict_comp($2, $4, intern_string($6), $8, NULL); }
    | '{' expr COLON expr FOR ID IN_KW expr WHEN expr '}' { $$ = dict_comp($2, $4, intern_string($6), $8, $10); }
    | '{' expr FOR EACH ID IN_KW expr '}' { $$ = set_of(comp_node($2, intern_string($5), $7, NULL), line_num); }
    | '{' expr FOR EACH ID IN_KW expr WHEN expr '}' { $$ = set_of(comp_node($2, intern_string($5), $7, $9), line_num); }
    | '{' expr FOR ID IN_KW expr '}' { $$ = set_of(comp_node($2, intern_string($4), $6, NULL), line_num); }
    | '{' expr FOR ID IN_KW expr WHEN expr '}' { $$ = set_of(comp_node($2, intern_string($4), $6, $8), line_num); }
    | '{' dict_elements '}' { $$ = node_new(NODE_DICT_LITERAL); $$->dict_keys = $2.keys; $$->dict_values = $2.values; $$->dict_count = $2.count; } | '{' '}' { $$ = node_new(NODE_DICT_LITERAL); $$->dict_keys = NULL; $$->dict_values = NULL; $$->dict_count = 0; } | LAMBDA '(' param_list ')' COLON expr { char buf[32]; snprintf(buf, 32, "lambda_%d", global_lambda_count++); Node *n = newFuncNode(intern_string(buf), $3.count, $3.names, with_param_defaults($3.count, $3.names, $3.defaults, newReturnNode($6))); n->type = NODE_LAMBDA; n->has_rest = $3.rest; $$ = n; } | LAMBDA '(' ')' COLON expr { char buf[32]; snprintf(buf, 32, "lambda_%d", global_lambda_count++); Node *n = newFuncNode(intern_string(buf), 0, NULL, newReturnNode($5)); n->type = NODE_LAMBDA; $$ = n; } | LAMBDA '(' param_list ')' COLON NEWLINE lines block_end END { char buf[32]; snprintf(buf, 32, "lambda_%d", global_lambda_count++); Node *n = newFuncNode(intern_string(buf), $3.count, $3.names, with_param_defaults($3.count, $3.names, $3.defaults, $7)); n->type = NODE_LAMBDA; n->has_rest = $3.rest; $$ = n; } | LAMBDA '(' ')' COLON NEWLINE lines block_end END { char buf[32]; snprintf(buf, 32, "lambda_%d", global_lambda_count++); Node *n = newFuncNode(intern_string(buf), 0, NULL, $6); n->type = NODE_LAMBDA; $$ = n; };
/* Just before a block lambda's `end`: give the lexer back the bracket depth
   it set aside at the lambda's `:` (see lexer_block_pop in glowlang.l), so
   xs.map(lambda(x): ... end) keeps counting its `(`. */
block_end: { lexer_block_pop(); };

dict_elements: expr COLON expr { $$.count = 1; $$.keys = arena_alloc(sizeof(Node*)); $$.keys[0] = $1; $$.values = arena_alloc(sizeof(Node*)); $$.values[0] = $3; } | dict_elements COMMA expr COLON expr { $$.keys = arena_realloc($$.keys, sizeof(Node*)*($1.count), sizeof(Node*)*($1.count+1)); $$.keys[$1.count] = $3; $$.values = arena_realloc($$.values, sizeof(Node*)*($1.count), sizeof(Node*)*($1.count+1)); $$.values[$1.count] = $5; $$.count = $1.count + 1; };


%%
Node *root = NULL;
/* A program file: name.glow, or any file that starts with #! (a script
   run as ./deploy through "#!/usr/bin/env glowlang"). */
static int glow_source_ok(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (ext && strcmp(ext, ".glow") == 0) return 1;
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;
    char head[2] = { 0, 0 };
    size_t n = fread(head, 1, 2, f);
    fclose(f);
    return n == 2 && head[0] == '#' && head[1] == '!';
}

/* os.args: the arguments after the program file on the command line. */
static void glow_pass_script_args(const char *filename, int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (argv[i] == filename) { glow_set_script_args(filename, argc - i - 1, argv + i + 1); return; }
    }
    glow_set_script_args(filename, 0, NULL);
}

#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
void dump_all_bc() {
    printf("Main:\n");
    if (main_fe.body) {
        print_bytecode(main_fe.body);
    }
    for (int i = 0; i < func_table_size; i++) {
        if (func_table && func_table[i].used && func_table[i].body) {
            printf("\nFunction '%s':\n", func_table[i].name);
            print_bytecode(func_table[i].body);
        }
    }
}

int main(int argc, char **argv) {
#else
int glow_main(int argc, char **argv) {
#endif
    /* The main thread owns the GIL for its whole life except while parked in a
       blocking native or yielding to a spawned thread. */
    glow_gil_acquire();
    var_table_init(); func_table_init(); vm_stack_init(); intern_table_init(); std_init();
    next_free_reg = 0;

    // 1. Check for Embedded Package
    char self_path[1024];
    int len = get_self_exe_path(self_path, sizeof(self_path)-1);
    if (len != -1) {
        self_path[len] = '\0';
        FILE *f = fopen(self_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            if (fsize >= 20) {
                fseek(f, -12, SEEK_END); // Marker is 12 bytes: "GLOW_ARCHIVE"
                char marker[13];
                if (fread(marker, 1, 12, f) == 12) {
                    marker[12] = '\0';
                    if (strcmp(marker, "GLOW_ARCHIVE") == 0) {
                        fseek(f, -20, SEEK_END); // Read 8-byte uint64_t size
                        uint64_t script_size = 0;
                        if (fread(&script_size, sizeof(uint64_t), 1, f) == 1 && script_size < (uint64_t)fsize) {
                            fseek(f, fsize - 20 - (long)script_size, SEEK_SET);
                            char *script = malloc(script_size + 1);
                            if (script && fread(script, 1, script_size, f) == (size_t)script_size) {
                                script[script_size] = '\0';
                                fclose(f);

                                compiling_file = "<embedded>";
                                root = NULL;
                                YY_BUFFER_STATE bp = yy_scan_string(script);
                                int parse_res = yyparse(); yy_delete_buffer(bp);
                                free(script);
                                if (parse_res != 0) {
                                    std_cleanup();
                                    return 1;
                                }
                                if (root) {
                                    Bytecode *main_bc = bc_new(&main_fe); compiling_bc = main_bc;
                                    main_fe.body = main_bc; compile(root);
                                    main_bc->local_count = compiler_reg_peak();
                                    bc_emit(main_bc, OP_RETURN, 0, 0);
                                    optimize_bytecode(main_bc);
                                    run_bytecode(main_bc, NULL, NULL, 0);
                                    if (has_exception) print_error_trace(pending_exception);
                                    if (runtime_error_thrown) { fflush(stdout); return 1; }
                                }
                                glow_threads_join_all();
                                std_cleanup(); return 0;
                            } else {
                                if (script) free(script);
                            }
                        }
                    }
                }
            }
            fclose(f);
        }
    }

    if (argc == 1) {
        // Default to REPL
        goto l_REPL;
    }

    const char *subcommand = argv[1];
    if (strcmp(subcommand, "--version") == 0 || strcmp(subcommand, "-v") == 0 || strcmp(subcommand, "version") == 0) {
        printf("GlowLang %s\n", GLOWLANG_VERSION);
        return 0;
    }
    if (strcmp(subcommand, "--help") == 0 || strcmp(subcommand, "-h") == 0 || strcmp(subcommand, "help") == 0) {
        printf("GlowLang %s\n", GLOWLANG_VERSION);
        printf("Usage: glowlang <subcommand> [args]\n\n");
        printf("Subcommands:\n");
        printf("  run <file.glow>   Execute a GlowLang script (default)\n");
        printf("  repl              Launch the interactive development shell\n");
        printf("  check <file.glow> Perform syntax and static analysis only\n");
        printf("  trace <file.glow> Execute with frame-by-frame VM tracing\n");
        printf("  profile <file.glow> Execute, then list the lines that ran most often\n");
        printf("  debug <file.glow> Execute with interactive step-by-step debugger\n");
        printf("  package <src>     Create a standalone executable binary\n");
        printf("  --dump-bc <file>  Dump compiled bytecode for debugging\n");
        printf("  --lsp             Start the Language Server Protocol (LSP) mode\n");
        printf("  --version         Print the GlowLang version\n\n");
        printf("Options:\n");
        printf("  -o <output>       Specify output file for 'package' subcommand\n");
        return 0;
    }

    if (strcmp(subcommand, "package") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: glow package <file.glow> [-o output]\n"); return 1; }
        const char *src_file = argv[2];
        const char *out_file = "app.bin";
        if (argc > 4 && strcmp(argv[3], "-o") == 0) out_file = argv[4];

        FILE *fsrc = fopen(src_file, "rb");
        if (!fsrc) { fprintf(stderr, "Error: Could not open source '%s'\n", src_file); return 1; }
        fseek(fsrc, 0, SEEK_END); long src_size = ftell(fsrc); fseek(fsrc, 0, SEEK_SET);
        char *src_buf = malloc(src_size);
        if (fread(src_buf, 1, src_size, fsrc) != (size_t)src_size) {
            // Silence warning and handle partial read
        }
        fclose(fsrc);

        char self_path[1024];
        int self_len = get_self_exe_path(self_path, sizeof(self_path)-1);
        if (self_len == -1) { free(src_buf); return 1; }
        self_path[self_len] = '\0';
        FILE *fself = fopen(self_path, "rb");
        if (!fself) { free(src_buf); return 1; }
        
        FILE *fout = fopen(out_file, "wb");
        if (!fout) { fclose(fself); free(src_buf); return 1; }
        
        char buf[8192]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fself)) > 0) fwrite(buf, 1, n, fout);
        fclose(fself);

        uint64_t payload_sz = (uint64_t)src_size;
        fwrite(src_buf, 1, src_size, fout);
        fwrite(&payload_sz, sizeof(uint64_t), 1, fout);
        fwrite("GLOW_ARCHIVE", 1, 12, fout);
        fclose(fout);
        free(src_buf);
        
#ifndef _WIN32
        char chmod_cmd[1024]; snprintf(chmod_cmd, 1024, "chmod +x %s", out_file);
        if (system(chmod_cmd) == -1) {
            // handle error
        }
#endif
        printf("Package created: %s\n", out_file);
        return 0;
    }

    if (strcmp(subcommand, "repl") == 0) {
        goto l_REPL;
    } else if (strcmp(subcommand, "check") == 0 || strcmp(subcommand, "run") == 0 || strcmp(subcommand, "trace") == 0 || strcmp(subcommand, "debug") == 0 || strcmp(subcommand, "profile") == 0 || strcmp(subcommand, "--dump-bc") == 0) {
        int dump_bc = 0;
        int check_only = (strcmp(subcommand, "check") == 0);
        int profile_mode = (strcmp(subcommand, "profile") == 0);
        if (strcmp(subcommand, "trace") == 0) trace_mode = 1;
        if (strcmp(subcommand, "debug") == 0) debug_mode = 1;
        const char *filename = NULL;

        if (strcmp(subcommand, "--dump-bc") == 0) {
            dump_bc = 1;
            filename = (argc > 2) ? argv[2] : NULL;
        } else {
            filename = (argc > 2) ? argv[2] : NULL;
        }

        if (!filename && !check_only) {
            // If 'run' was used without filename, maybe it was 'glow file.glow' (old style)
            filename = subcommand;
        }

        if (filename) {
            if (!glow_source_ok(filename)) {
                fprintf(stderr, "Error: GlowLang source files must have the '.glow' extension (or start with a #! line).\n");
                return 1;
            }
            glow_pass_script_args(filename, argc, argv);
            yyin = fopen(filename, "r");
            if (yyin) {
                compiling_file = filename;
                root = NULL;
                int parse_res = yyparse();
                if (parse_res != 0) {
                    if (yyin) fclose(yyin);
                    std_cleanup();
                    return 1;
                }
                if (root) {
                    if (main_fe.body) bc_free(main_fe.body);
                    Bytecode *main_bc = bc_new(&main_fe); compiling_bc = main_bc; next_free_reg = 0;
                    main_fe.body = main_bc;
                    compile(root);
                    main_bc->local_count = compiler_reg_peak();
                    bc_emit(main_bc, OP_RETURN, 0, 0);
                    optimize_bytecode(main_bc);

                    if (dump_bc) {
                        for (int i=0; i<func_table_size; i++) {
                            if (func_table[i].used && func_table[i].body) {
                                printf("Function: %s\n", func_table[i].name);
                                print_bytecode(func_table[i].body);
                            }
                        }
                        printf("Main:\n");
                        print_bytecode(main_bc);
                    }

                    if (!check_only) {
                        if (profile_mode) glow_profile_begin();
                        run_bytecode(main_bc, NULL, NULL, 0);
                        if (profile_mode) { fflush(stdout); glow_profile_report(); }
                        if (runtime_error_thrown) {
                            /* Spawned threads may still be parked on the GIL, so
                               leave the heap alone and just exit. */
                            fflush(stdout);
                            return 1;
                        }
                        glow_threads_join_all();
                    } else {
                        printf("Syntax Check: OK\n");
                    }
                }
                if (yyin) fclose(yyin);
            } else {
                fprintf(stderr, "Error: Could not open file '%s'\n", filename);
                return 1;
            }
        }
    } else if (strcmp(subcommand, "--lsp") == 0) {
        handle_client(0);
        return 0;
    } else {
        // Old style: glow file.glow
        const char *filename = subcommand;
        if (!glow_source_ok(filename)) {
            fprintf(stderr, "Error: GlowLang source files must have the '.glow' extension (or start with a #! line).\n");
            return 1;
        }
        glow_pass_script_args(filename, argc, argv);
        yyin = fopen(filename, "r");
        if (yyin) {
            compiling_file = filename;
            root = NULL;
            int parse_res = yyparse();
            if (parse_res != 0) {
                if (yyin) fclose(yyin);
                std_cleanup();
                return 1;
            }
            if (root) {
                if (main_fe.body) bc_free(main_fe.body);
                Bytecode *main_bc = bc_new(&main_fe); compiling_bc = main_bc; next_free_reg = 0;
                main_fe.body = main_bc;
                compile(root);
                main_bc->local_count = compiler_reg_peak();
                bc_emit(main_bc, OP_RETURN, 0, 0);
                optimize_bytecode(main_bc);
                run_bytecode(main_bc, NULL, NULL, 0);
                if (runtime_error_thrown) {
                    fflush(stdout);
                    return 1;
                }
                glow_threads_join_all();
            }
            if (yyin) fclose(yyin);
        } else {
            fprintf(stderr, "Usage: glow [run|repl|check] <file>\n");
            return 1;
        }
    }

    std_cleanup();
    return 0;

l_REPL:
    printf("GlowLang %s Interactive REPL\nType 'exit' to quit.\n", GLOWLANG_VERSION);
    char line_buf[4096];
    compiling_file = "<stdin>";
    while (1) {
        printf("glow> ");
        if (!fgets(line_buf, sizeof(line_buf), stdin)) break;
        if (strncmp(line_buf, "exit", 4) == 0) break;
        root = NULL; glow_clear_errors();
        YY_BUFFER_STATE bp = yy_scan_string(line_buf);
        yyparse(); yy_delete_buffer(bp);
        if (glow_error_count == 0 && root) {
            if (!main_fe.body) main_fe.body = bc_new(&main_fe);
            Bytecode *main_bc = main_fe.body; compiling_bc = main_bc;
            int old_count = main_bc->count;
            compile(root);
            if (main_bc->count > old_count) {
                main_bc->local_count = compiler_reg_peak();
                int ret_idx = bc_emit(main_bc, OP_RETURN, 0, 0);
                Instruction *start_pc = &main_bc->code[old_count];
                Value res = run_bytecode(main_bc, start_pc, NULL, 0);
                if (has_exception) { print_error_trace(pending_exception); has_exception = 0; }
                else if (res != 0 && GET_TYPE(res) != VAL_NONE) { print_value(res); printf("\n"); }
                main_bc->count = ret_idx;
            }
        }
    }
    glow_threads_join_all();
    std_cleanup();
    return 0;
}
